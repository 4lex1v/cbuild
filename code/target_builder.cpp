
#define _CRT_SECURE_NO_WARNINGS

#include <immintrin.h>

#include "base.hpp"
#include "driver.hpp"
#include "seq.hpp"
#include "atomics.hpp"
#include "core.hpp"
#include "concurrent.hpp"
#include "dependency_iterator.hpp"
#include "platform.hpp"
#include "project_loader.hpp"
#include "cbuild_api.hpp"
#include "target_builder.hpp"
#include "result.hpp"
#include "runtime.hpp"
#include "list.hpp"
#include "toolchain.hpp"

extern File_Path     working_directory_path;
extern File_Path     cache_directory_path;
extern Platform_Info platform;

/*
  Capping the number of files the tool supports per project, largely for the purposes of
  registry update set allocation. At this point I'm not targetting large projects support,
  at this point Linux Kernel has 63k files, so this limit should be more than enough, if
  anything it could be changed later.
 */
static usize max_supported_files_count = 250'000;

enum struct Chain_Status: u32 {
  Unchecked,
  Checking,
  Checked_Has_Updates,
  Checked_No_Updates,
};

struct Registry {
  constexpr static usize Version = 1;

  struct Header {
    u16  version;
    u16  targets_count;
    u32  aligned_total_files_count;
    u32 dependencies_count;

    u32 _reserved[61];
  };

  static_assert(sizeof(Header) == sizeof(u64) * 32);

  struct Record {
    u64 timestamp;
    u64 hash;
  };

  struct Target_Info {
    char name[Target::Max_Name_Limit];

    u64  files_offset;
    au64 files_count;
    u32  aligned_max_files_count;
  };

  File registry_file;
  bool disabled;

  struct {
    File_Mapping registry_file_mapping;

    Header header;

    Target_Info *targets;

    u64    *files;
    Record *file_records;

    u64    *dependencies;
    Record *dependency_records;
  } records;

  struct {
    u8    *buffer;

    Header *header;

    Spin_Lock   *target_files_locks;
    Target_Info *targets;

    u64    *files;
    Record *file_records;

    u64    *dependencies;
    Record *dependency_records;

    Chain_Status *chain_status;
  } update_set;
};

enum struct Target_Compile_Status: u32 { Compiling, Failed, Success };
enum struct Target_Link_Status: u32    { Pending, Waiting, Linking, Failed, Success };

struct Target_Tracker {
  const Target *target;

  Atomic<Target_Compile_Status> compile_status { Target_Compile_Status::Compiling };
  Atomic<Target_Link_Status>    link_status    { Target_Link_Status::Pending };

  Spin_Lock link_lock;

  as32 files_pending   { 0 };
  au32 skipped_counter { 0 };

  Target_Tracker (Target *_target)
    : target { _target }
  {
    atomic_store(&this->files_pending, _target->files.count);
  }
};

static File_Path object_folder_path;
static File_Path out_folder_path;
static Registry  registry;

static Status_Code load_registry (Registry *registry, Memory_Arena *arena, const File_Path *registry_file_path, const Project *project) {
  using enum Open_File_Flags;
  use(Status_Code);

  if (project->registry_disabled) {
    registry->disabled = true;
    return Success;
  }

  auto registry_file = open_file(registry_file_path, Request_Write_Access | Create_File_If_Not_Exists);
  check_status(registry_file);

  registry->registry_file = registry_file;

  auto &records    = registry->records;
  auto &update_set = registry->update_set;

  u8 *buffer_cursor = nullptr;
  auto set_field = [&buffer_cursor] <typename T> (T *field, usize count, usize align = 0) {
    if (align > 0) buffer_cursor = align_forward(buffer_cursor, align);
    auto value_size = sizeof(remove_ptr_t<T>) * count;
    *field = reinterpret_cast<T>(buffer_cursor);
    buffer_cursor += value_size;
  };

  if (auto file_size = get_file_size(&registry_file); file_size > 0) {
    auto mapping = map_file_into_memory(&registry_file);
    check_status(mapping);

    auto buffer = reinterpret_cast<u8 *>(mapping->memory);
    buffer_cursor = buffer;

    records.registry_file_mapping = mapping;

    auto set_header_field = [&buffer_cursor] <typename T> (T *field) {
      *field         = *reinterpret_cast<T *>(buffer_cursor);
      buffer_cursor += sizeof(T);
    };

    set_header_field(&records.header.version);
    set_header_field(&records.header.targets_count);
    set_header_field(&records.header.aligned_total_files_count);
    set_header_field(&records.header.dependencies_count);
    // if (records.header.version >= 2) { read_header_value(&field); }

    buffer_cursor += sizeof(records.header._reserved);

    set_field(&records.targets,            records.header.targets_count);
    set_field(&records.files,              records.header.aligned_total_files_count, 32);
    set_field(&records.file_records,       records.header.aligned_total_files_count);
    set_field(&records.dependencies,       records.header.dependencies_count, 32);
    set_field(&records.dependency_records, records.header.dependencies_count);
  }

  {
    /*
      If the number of files in the project reduced, copying old info for the new allocation will corrupt the
      memory, thus we need to allocate enough space to hold everything, we'll write the correct number of files
      into the file anyway.

      It's aligned by 4 to put the size of the allocated buffer on a 32-byte boundary, each file record is 8 bytes.
     */
    auto files_count = records.header.aligned_total_files_count;
    {
      u16 new_aligned_total = 0;
      for (auto target: project->targets)   new_aligned_total += align_forward(target->files.count, 4);
      if  (new_aligned_total > files_count) files_count = new_aligned_total;
    }

    assert(is_aligned_by(files_count, 4));

    auto dependencies_limit = max_supported_files_count - files_count;

    auto update_set_buffer = get_memory_at_current_offset<u8>(arena, 32);
    buffer_cursor          = update_set_buffer;

    set_field(&update_set.header,             1);
    set_field(&update_set.targets,            project->targets.count);
    set_field(&update_set.files,              files_count, 32);
    set_field(&update_set.file_records,       files_count);
    set_field(&update_set.dependencies,       dependencies_limit, 32);
    set_field(&update_set.dependency_records, dependencies_limit);

    /*
      The following fields should be allocated strictly after the above fields.
      This is done for a faster, easier flush operation, where I can take a linear
      chunk of memory and dump to a file.
     */

    set_field(&update_set.chain_status,       dependencies_limit);
    set_field(&update_set.target_files_locks, project->targets.count, 8);

    auto reservation_size = buffer_cursor - update_set_buffer;
    if (reserve_memory(arena, reservation_size, 32) == nullptr) {
      return { Out_Of_Memory, "Not enough memory to allocate buffer for registry update set" };
    }

    zero_memory(update_set_buffer, reservation_size);

    update_set.buffer = update_set_buffer;

    *update_set.header = Registry::Header {
      .version                   = Registry::Version,
      .targets_count             = static_cast<u16>(project->targets.count),
      .aligned_total_files_count = files_count,
      .dependencies_count        = 0,
    };

    for (usize target_index = 0, files_offset = 0;
         auto target: project->targets) {
      auto info = update_set.targets + target_index;

      target->info = info;
      copy_memory(info->name, target->name.value, target->name.length);

      for (usize idx = 0; idx < registry->records.header.targets_count; idx++) {
        auto old_info = registry->records.targets + idx;

        if (strncmp(info->name, old_info->name, Target::Max_Name_Limit) == 0) {
          target->last_info = old_info;
          break;
        }
      }

      // The boundary of each segment for target files should still be aligned on 32-bytes, that is 4 64-bit ids.
      info->aligned_max_files_count = align_forward(target->files.count, 4);
      info->files_offset            = files_offset;

      target_index += 1;
      files_offset += info->aligned_max_files_count;
    }
  }

  return Success;
}

static Status_Code flush_registry (Registry *registry) {
  use(Status_Code);

  if (registry->disabled) return Success;

  reset_file_cursor(&registry->registry_file);

  auto count = registry->update_set.header->dependencies_count;
  auto records = copy_memory(reinterpret_cast<Registry::Record *>(registry->update_set.dependencies + count), registry->update_set.dependency_records, count);

  auto flush_buffer_size = usize(records + count) - usize(registry->update_set.buffer);

  write_buffer_to_file(&registry->registry_file, (char*)registry->update_set.buffer, flush_buffer_size);

  close_file(&registry->registry_file);

  return Success;
}

static bool find_offset_intrinsic (usize *_result, const u64 *_array, usize count, u64 _value) {
  if (count == 0) return false;
  
  assert(is_aligned_by(_array, 32));

  auto value = _mm256_set1_epi64x(_value);

  s32 step  = 4;
  s32 limit = count - step;

  s32 idx = 0;
  while (idx <= limit) {
    auto array  = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(_array + idx));
    auto result = _mm256_cmpeq_epi64(array, value);

    auto match = _mm256_movemask_epi8(result);
    if (match) {
      *_result = ((__builtin_ctz(match) >> 3) + idx);
      return true;
    }

    idx += step;
  }

  for (usize i = idx; i < count; i++) {
    if (_array[i] == _value) {
      *_result = i;
      return true;
    } 
  }

  return false;
}

static cb_forceinline bool contains_key (const u64 *array, usize count, u64 key) {
  usize ignored = 0;
  return find_offset_intrinsic(&ignored, array, count, key);
}

static cb_forceinline bool find_offset (usize *result, const u64 *array, usize count, u64 value) {
  return find_offset_intrinsic(result, array, count, value);
}

struct Target_Tracker;

struct Build_Queue;
struct Build_Task {
  enum struct Type: u32 { Compile, Link };
  using enum Type;

  bool dependencies_updated;

  Type            type;
  Target_Tracker *tracker;

  u64 record_index;

  File file;

  Build_Task *next;
};

struct Build_Queue {
  Atomic<Build_Task *> tasks;

  Thread    *builders;
  usize      builders_count;
  Semaphore  tasks_available;

  abool terminating;

  au32 tasks_submitted;
  au32 tasks_completed;
};

static Status_Code init_build_queue (Build_Queue *queue, Memory_Arena *arena, usize builders_count) {
  use(Status_Code);
  
  auto semaphore = create_semaphore();
  check_status(semaphore);
  queue->tasks_available = semaphore;
  queue->builders_count  = builders_count;

  if (queue->builders_count) {
    queue->builders = reserve_array<Thread>(arena, queue->builders_count);

    for (usize idx = 0; idx < queue->builders_count; idx++) {
      u32 build_queue_processor (void *);

      auto builder_thread = spawn_thread(build_queue_processor, queue);
      check_status(builder_thread);

      queue->builders[idx] = builder_thread;
    }
  }

  return Success;
}

static void destroy_build_queue (Build_Queue *queue) {
  atomic_store<Memory_Order::Release>(&queue->terminating, true);
  
  if (queue->builders_count) {
    increment_semaphore(&queue->tasks_available, queue->builders_count);

    for (usize idx = 0; idx < queue->builders_count; idx++)
      shutdown_thread(queue->builders + idx);
  }

  destroy_semaphore(&queue->tasks_available);
}

static void submit_build_command (Build_Queue *queue, Build_Task *task) {
  while (true) {
    task->next = atomic_load(&queue->tasks);
    if (atomic_compare_and_set(&queue->tasks, task->next, task)) {
      atomic_fetch_add(&queue->tasks_submitted, 1);
      increment_semaphore(&queue->tasks_available);
      return;
    }
  }
}

static Build_Task * pull_command_for_execution (Build_Queue *queue) {
  using enum Memory_Order;
  
  while (true) {
    auto task = atomic_load(&queue->tasks);
    if (task == nullptr) return nullptr;

    if (atomic_compare_and_set<Acquire_Release, Acquire>(&queue->tasks, task, task->next)) return task;
  }
}

static Result<Chain_Status> scan_dependency_chains (Memory_Arena *arena, File *source_file, const List<File_Path> &extra_include_paths) {
  use(Status_Code);
  using enum Chain_Status;

  auto &records    = registry.records;
  auto &update_set = registry.update_set;

  auto file_id = get_file_id(source_file);

  usize index = 0;

  if (usize count = update_set.header->dependencies_count;
      find_offset(&index, update_set.dependencies, count, file_id)) {
    return update_set.chain_status[index];
  }
  else {
    index = update_set.header->dependencies_count++;
    update_set.dependencies[index] = file_id;
    update_set.chain_status[index] = Chain_Status::Checking;
  }

  assert(update_set.chain_status[index] == Chain_Status::Checking);

  auto mapping = map_file_into_memory(source_file);
  check_status(mapping);
  defer { unmap_file(&mapping); };
  
  auto iterator      = Dependency_Iterator(&mapping);
  auto include_value = String {};

  List<File_Path> include_directories;
  {
    add(arena, &include_directories, get_parent_folder_path(arena, source_file).value);
    for (auto path: extra_include_paths) add(arena, &include_directories, path);
    add(arena, &include_directories, working_directory_path);
  };

  bool chain_has_updates = false;
  while (get_next_include_value(&iterator, &include_value)) {
    File_Path resolved_path;
    for (auto prefix: include_directories) {
      auto full_path = make_file_path(arena, prefix, include_value);
      if (not check_file_exists(&full_path)) continue;

      resolved_path = full_path;
      break;
    }

    if (not resolved_path) {
      print(arena, "Couldn't resolve the include file: %, source: %, the following paths were checked: \n", include_value, source_file->path);
      for (auto path: include_directories) print(arena, "  - %\n", path);

      continue;
    }

    auto dependency_file = open_file(&resolved_path);
    check_status(dependency_file);
    defer { close_file(&dependency_file); };
    
    auto chain_scan_result = scan_dependency_chains(arena, &dependency_file, extra_include_paths);
    check_status(chain_scan_result);

    assert(chain_scan_result != Chain_Status::Unchecked);

    if (*chain_scan_result == Chain_Status::Checked_Has_Updates) {
      chain_has_updates = true;
    }
  }

  auto timestamp = get_last_update_timestamp(source_file);
  if (usize offset = 0, count = records.header.dependencies_count;
      /*
        If we know that this chain has updates, there's no need to spend time on checking the record.
       */
      (chain_has_updates == false) && find_offset(&offset, records.dependencies, count, file_id)) {
    auto &record = records.dependency_records[offset];
    chain_has_updates = chain_has_updates || (timestamp != record.timestamp);
  }
  else {
    /*
      In this path, it means that there's no record of this dependency file and we see this for the first time.
      This case forces a rebuild of the original source file.
     */
    chain_has_updates = true;
  }

  auto status = chain_has_updates ? Checked_Has_Updates : Checked_No_Updates;

  update_set.dependency_records[index] = Registry::Record { .timestamp = timestamp };
  update_set.chain_status[index]       = status;

  return status;
}

static Result<bool> scan_file_dependencies (Memory_Arena *_arena, File *source_file, const List<File_Path> &extra_include_paths) {
  use(Status_Code);

  /*
    When the registry is disabled we should treat that as if there's no prior information regarding dependencies.
  */
  if (registry.disabled) return true;

  auto local = *_arena;

  auto mapping = map_file_into_memory(source_file);
  check_status(mapping);
  defer { unmap_file(&mapping); };

  auto iterator      = Dependency_Iterator(&mapping);
  auto include_value = String {};

  List<File_Path> include_directories;
  {
    add(&local, &include_directories, get_parent_folder_path(&local, source_file).value);
    for (auto path: extra_include_paths) add(&local, &include_directories, path);
    add(&local, &include_directories, working_directory_path);
  };

  bool chain_has_updates = false;
  while (get_next_include_value(&iterator, &include_value)) {
    auto inner_local = local;
    
    File_Path resolved_path;
    for (auto prefix: include_directories) {
      auto full_path = make_file_path(&inner_local, prefix, include_value);
      if (not check_file_exists(&full_path)) continue;

      resolved_path = full_path;
      break;
    }

    if (not resolved_path) {
      print(&inner_local, "Couldn't resolve the include file: %, source: %, the following paths were checked: \n", include_value, source_file->path);
      for (auto path: include_directories) print(&inner_local, "  - %\n", path);

      continue;
    }

    auto dependency_file = open_file(&resolved_path);
    check_status(dependency_file);
    defer { close_file(&dependency_file); };
    
    auto chain_scan_result = scan_dependency_chains(&inner_local, &dependency_file, extra_include_paths);
    check_status(chain_scan_result);

    assert(*chain_scan_result != Chain_Status::Unchecked);

    if (*chain_scan_result == Chain_Status::Checked_Has_Updates) {
      chain_has_updates = true;
    }
  }

  return chain_has_updates;
}

enum struct Upstream_Status: u32 { Ready, In_Progress, Failed };

static Upstream_Status check_upstream_dependencies (const Target *target) {
  if (target->depends_on.count == 0) return Upstream_Status::Ready;

  bool has_in_progress = false;
  for (auto d: target->depends_on) {
    auto dtracker = d->tracker;

    auto upstream_compile_status = atomic_load(&dtracker->compile_status);
    auto upstream_link_status    = atomic_load(&dtracker->link_status);

    if ((upstream_compile_status == Target_Compile_Status::Failed) ||
        (upstream_link_status    == Target_Link_Status::Failed)) {
      return Upstream_Status::Failed;
    }

    if ((upstream_compile_status == Target_Compile_Status::Compiling) ||
        (upstream_link_status    == Target_Link_Status::Pending)      ||
        (upstream_link_status    == Target_Link_Status::Waiting)      ||
        (upstream_link_status    == Target_Link_Status::Linking)) {
      has_in_progress = true; 
    }
  }

  if (has_in_progress) return Upstream_Status::In_Progress;

  return Upstream_Status::Ready;
}

static const char * get_target_extension (const Target *target) {
  switch (target->type) {
    case Target::Type::Static_Library: return (platform.type == Platform_Type::Win32) ? "lib" : "a";
    case Target::Type::Shared_Library: return (platform.type == Platform_Type::Win32) ? "dll" : "so";
    case Target::Type::Executable:     return (platform.type == Platform_Type::Win32) ? "exe" : "";
  }
}

static File_Path get_output_file_path_for_target (Memory_Arena *arena, const Target *target) {
  switch (target->type) {
    case Target::Type::Static_Library:
    case Target::Type::Shared_Library: {
      auto library_extension = get_target_extension(target);
      return make_file_path(arena, out_folder_path, format_string(arena, "%.%", target->name, library_extension));
    }
    case Target::Type::Executable: {
      auto target_file_name = platform.type == Platform_Type::Win32 ? format_string(arena, "%.exe", target->name) : target->name;
      return make_file_path(arena, out_folder_path, target_file_name);
    }
  }
}

static void link_target (Memory_Arena *arena, Target_Tracker *tracker) {
  auto link_status = atomic_load<Memory_Order::Acquire>(&tracker->link_status);
  if (link_status != Target_Link_Status::Waiting) return;

  bool should_link = false;
  tracker->link_lock.lock();
  {
    link_status = atomic_load(&tracker->link_status);
    if (link_status == Target_Link_Status::Waiting) {
      auto upstreams_status = check_upstream_dependencies(tracker->target);
      if (upstreams_status == Upstream_Status::Failed) {
        atomic_store(&tracker->link_status, Target_Link_Status::Failed);
        print(arena, "Target '%' couldn't be linked because of an error in upstream target linkage\n", tracker->target->name);
      }
      else if (upstreams_status == Upstream_Status::Ready) {
        should_link = true;
        atomic_store(&tracker->link_status, Target_Link_Status::Linking);
      }
    }
  }
  tracker->link_lock.unlock();

  if (not should_link) return;

  auto target  = tracker->target;
  auto project = target->project;
  auto output_file_path = get_output_file_path_for_target(arena, target);

  assert(atomic_load(&tracker->compile_status) == Target_Compile_Status::Success);
  assert(atomic_load(&tracker->link_status)    == Target_Link_Status::Linking);

  auto object_file_extension = platform.type == Platform_Type::Win32 ? "obj" : "o";

  String_Builder builder { arena };

  print(arena, "Linking target: %\n", target->name);

  Status_Code status = Status_Code::Success;
  switch (target->type) {
    case Target::Type::Static_Library: {
      builder += project->toolchain.archiver_path;

      if (target->files.count > 0) {
        builder += make_file_path(arena, object_folder_path, target->name, format_string(arena, "*.%", object_file_extension));  
      }

      for (auto lib: target->depends_on) {
        assert(atomic_load(&lib->tracker->link_status) == Target_Link_Status::Success);

        if (lib->type == Target::Type::Executable) todo(); // This should be disallowed
        
        const char *lib_extension = "lib"; // on Win32 static and import libs for dlls have the same extension
        if (platform.type != Platform_Type::Win32) {
          if (lib->type == Target::Type::Static_Library) lib_extension = "a";
          else                                           lib_extension = "so";
        }
        
        builder += make_file_path(arena, out_folder_path, format_string(arena, "%.%", lib->name, lib_extension));
      }

      if (platform.type == Platform_Type::Win32) builder += format_string(arena, "/OUT:%", output_file_path);
      else                                       builder += format_string(arena, "-o %",   output_file_path);

      break;
    };
    case Target::Type::Shared_Library: {
      builder += project->toolchain.linker_path;
      builder += platform.type == Platform_Type::Win32 ? "/dll" : "-shared";
      builder += target->options.linker;

      if (target->files.count) {
        builder += make_file_path(arena, object_folder_path, target->name, format_string(arena, "*.%", object_file_extension));  
      }
      
      for (auto lib: target->depends_on) {
        assert(atomic_load(&lib->tracker->link_status) == Target_Link_Status::Success);

        if (lib->type == Target::Type::Executable) todo(); // This should be disallowed
        
        const char *lib_extension = "lib"; // on Win32 static and import libs for dlls have the same extension
        if (platform.type != Platform_Type::Win32) {
          if (lib->type == Target::Type::Static_Library) lib_extension = "a";
          else                                           lib_extension = "so";
        }
        
        builder += make_file_path(arena, out_folder_path, format_string(arena, "%.%", lib->name, lib_extension));
      }
      builder += target->link_libraries;

      if (platform.type == Platform_Type::Win32) builder += format_string(arena, "/OUT:%", output_file_path);
      else                                       builder += format_string(arena, "-o %",   output_file_path);

      break;
    };
    case Target::Type::Executable: {
      builder += project->toolchain.linker_path;
      builder += target->options.linker;
      builder += make_file_path(arena, object_folder_path, target->name, format_string(arena, "*.%", object_file_extension));

      for (auto lib: target->depends_on) {
        assert(atomic_load(&lib->tracker->link_status) == Target_Link_Status::Success);

        if (lib->type == Target::Type::Executable) todo(); // This should be disallowed
        
        const char *lib_extension = "lib"; // on Win32 static and import libs for dlls have the same extension
        if (platform.type != Platform_Type::Win32) {
          if (lib->type == Target::Type::Static_Library) lib_extension = "a";
          else                                           lib_extension = "so";
        }
        
        builder += make_file_path(arena, out_folder_path, format_string(arena, "%.%", lib->name, lib_extension));
      }

      builder += target->link_libraries;

      if (platform.type == Platform_Type::Win32) builder += format_string(arena, "/OUT:%", output_file_path);
      else                                       builder += format_string(arena, "-o %",   output_file_path);

      break;
    };
  };

  auto link_command = build_string_with_separator(&builder, ' ');

  auto [_status, output] = run_system_command(arena, link_command);
  status = _status;

  if (output.length) print(arena, "%\n", output);

  auto is_success = (status == Status_Code::Success);
  if (not is_success) {
    atomic_store(&tracker->link_status, Target_Link_Status::Failed);
  }
  else {
    atomic_store<Memory_Order::Release>(&tracker->link_status, Target_Link_Status::Success);

    if (target->hooks.on_linked) {
      target->hooks.on_linked(project, target, project->args, Hook_Type_After_Target_Linked);
    }

    for (auto downstream: target->required_by) {
      link_target(arena, downstream->tracker);
    }
  }
}

static void compile_file (Memory_Arena *arena, const Build_Task *task) {
  assert(task->type == Build_Task::Compile);

  auto file      = task->file;
  auto tracker   = task->tracker;
  auto target    = tracker->target;
  auto project   = target->project;
  auto toolchain = &project->toolchain;

  auto target_info      = reinterpret_cast<Registry::Target_Info *>(target->info);
  auto target_last_info = reinterpret_cast<Registry::Target_Info *>(target->last_info);

  auto file_id   = get_file_id(&file);
  auto timestamp = get_last_update_timestamp(&file);

  auto extension        = platform.type == Platform_Type::Win32 ? "obj" : "o";
  auto object_file_name = format_string(arena, "%.%", get_file_name(&file.path), extension);
  auto object_file_path = make_file_path(arena, object_folder_path, target->name, object_file_name);

  bool should_rebuild = true;
  if (!task->dependencies_updated && target->last_info) {
    auto &records = registry.records;

    auto section      = records.files + target_last_info->files_offset;
    auto section_size = target_last_info->files_count.value;

    usize index;
    bool record_found = find_offset(&index, section, section_size, file_id);

    if (record_found) {
      auto record_index     = target_last_info->files_offset + index;
      auto record_timestamp = records.file_records[record_index].timestamp;

      should_rebuild = (timestamp != record_timestamp) || (not check_file_exists(&object_file_path));
    }
  }

  bool is_success = true;
  if (should_rebuild) {
    print(arena, "Building file: %\n", file.path.value);

    auto is_cpp_file = check_extension(file.path, "cpp");

    String_Builder builder { arena };
    builder += is_cpp_file ? project->toolchain.cpp_compiler_path : project->toolchain.c_compiler_path;
    builder += target->options.compiler;

    if (toolchain->type == Toolchain_Type_MSVC_X64) builder += format_string(arena, "/c % /Fo:%", file.path, object_file_path);
    else                                            builder += format_string(arena, "-c % -o %",  file.path, object_file_path);

    auto compilation_command = build_string_with_separator(&builder, ' ');

    auto [status, output] = run_system_command(arena, compilation_command);

    if (output)     print(arena, "%\n", output);
    if (not status) print(arena, "%\n", status);

    is_success = (status == Status_Code::Success);
  }
  else {
    atomic_fetch_add(&tracker->skipped_counter, 1);
  }
  
  if (not is_success) {
    atomic_store(&tracker->compile_status, Target_Compile_Status::Failed);
    atomic_fetch_sub<Memory_Order::Release>(&tracker->files_pending, 1);
  }
  else {
    auto last = atomic_fetch_sub<Memory_Order::Acquire>(&tracker->files_pending, 1);

    if (not registry.disabled) {
      auto index = atomic_fetch_add(&target_info->files_count, 1);
      assert(index < target_info->aligned_max_files_count);

      auto update_set_index = target_info->files_offset + index;

      assert(registry.update_set.files[update_set_index] == 0);
      registry.update_set.files[update_set_index]        = file_id;
      registry.update_set.file_records[update_set_index] = Registry::Record { .timestamp = timestamp };
    }

    if ((last - 1) == 0) {
      /*
        At this point it's guaranteed that no other thread would modify target's compilation status,
        since current thread processed the last file.
       */

      auto compile_status = atomic_load(&tracker->compile_status);
      if (compile_status == Target_Compile_Status::Failed) {
        print(arena, "Target '%' couldn't be linked because of compilation errors\n", target->name);
        return;
      }

      assert(compile_status == Target_Compile_Status::Compiling);
      atomic_store<Memory_Order::Release>(&tracker->compile_status, Target_Compile_Status::Success);

      /*
        If no files were recompiled for the given target and the output target already exists, no need to
        call the linker.
       */
      auto output_file_path = get_output_file_path_for_target(arena, target);
      auto skipped = atomic_load(&tracker->skipped_counter);
      if (skipped < tracker->target->files.count || (not check_file_exists(&output_file_path))) {
        assert(atomic_load(&tracker->link_status) == Target_Link_Status::Pending);
        atomic_store<Memory_Order::Release>(&tracker->link_status, Target_Link_Status::Waiting);

        link_target(arena, tracker);
      }
      else {
        atomic_store<Memory_Order::Release>(&tracker->link_status, Target_Link_Status::Success);
        for (auto downstream: target->required_by) {
          link_target(arena, downstream->tracker);
        }
      }
    }
  }
}

static u32 build_queue_processor (void *param) {
  auto queue = reinterpret_cast<Build_Queue *>(param);

  auto virtual_memory = reserve_virtual_memory(megabytes(1));
  Memory_Arena arena { virtual_memory };
  defer { free_virtual_memory(&virtual_memory); };

  while (true) {
    wait_for_semaphore_signal(&queue->tasks_available);

    if (atomic_load<Memory_Order::Acquire>(&queue->terminating)) return 0;

    auto task = pull_command_for_execution(queue);  
    if (task == nullptr) continue;

    reset_arena(&arena);

    switch (task->type) {
      case Build_Task::Type::Compile: {
        compile_file(&arena, task);
        break;
      }
      case Build_Task::Type::Link: {
        link_target(&arena, task->tracker);
        break;
      }
    }

    atomic_fetch_add(&queue->tasks_completed, 1);
  }
  
  return 0;
}

static void wait_for_all_tasks_to_complete (Build_Queue *queue) {
  do {
    auto completed = atomic_load(&queue->tasks_completed);
    auto submitted = atomic_load(&queue->tasks_submitted);

    if (completed == submitted) return;
  } while (true);
}

static Result<u32> number_of_extra_builders_to_spawn (Memory_Arena *arena, const Arguments *args) {
  use(Status_Code);

  /*
    From a user's perspective the builders count is the number of threads that will be used to build the project.
    Logically, we should have at least 1 builder, which is the main thread. Internally, The build queue creates a
    pool of additional builders. Thus, from the user's perspective builders count is in [1, LOGICAL CPUs], but how
    many threads are in the pool is that number - 1, because we have to respect the main thread to avoid oversubscription.
  */
  auto requested_builders_count = get_argument_or_default(args, "builders", nullptr);

  // This number excludes main thread, which always exists
  auto cpu_count            = get_logical_cpu_count();
  auto extra_builders_limit = cpu_count - 1;

  if (requested_builders_count == nullptr) return extra_builders_limit;

  if (requested_builders_count[0] == '-') {
    print(arena, "WARNING: 'builders' value cannot be a negative number. Valid value should be in the range of [1, %]. Defaulting to 1, i.e a single-threaded execution mode\n", cpu_count);
    return 0;
  }

  if (requested_builders_count[0] == '0') {
    print(arena, "WARNING: 'builders' value cannot be 0. Valid value should be in the range of [1, %]. Defaulting to 1, i.e a single-threaded execution mode\n", cpu_count);
    return 0;
  }

  u32 count = 0;
  while (*requested_builders_count != '\0') {
    count = (count * 10) + (*requested_builders_count - '0');
    requested_builders_count++;
  }

  if (count > cpu_count) {
    print(arena, "WARNING: 'builders' value is bigger than the number of CPU core (i.e requested - %, core count - %). Defaulting to %\n", count, cpu_count, cpu_count);
  }

  count = clamp(count, 1, cpu_count);

  return count - 1;
}

Status_Code build_project (Memory_Arena *arena, const Project *project, const Arguments *args) {  
  using enum Open_File_Flags;
  use(Status_Code);

  if (project->targets.count == 0) return Success;

  object_folder_path = make_file_path(arena, project->output_location_path, "obj");
  check_status(create_directory(&object_folder_path));

  for (auto target: project->targets) {
    auto local = *arena;
    auto target_object_folder_path = make_file_path(&local, object_folder_path, target->name);
    check_status(create_directory(&target_object_folder_path));
  }

  out_folder_path = make_file_path(arena, project->output_location_path, "out");
  check_status(create_directory(&out_folder_path));

  auto registry_file_path = make_file_path(arena, project->output_location_path, "__registry");
  check_status(load_registry(&registry, arena, &registry_file_path, project));

  Build_Queue build_queue {};
  auto builders_count = number_of_extra_builders_to_spawn(arena, args);
  check_status(init_build_queue(&build_queue, arena, builders_count));
  defer { destroy_build_queue(&build_queue); };

  List<Target_Tracker *> trackers;
  for (auto *target: project->targets) {
    auto tracker = push_struct<Target_Tracker>(arena, target);
    add(arena, &trackers, tracker);

    target->tracker = tracker;
  }

  auto previous_env = setup_system_sdk(arena, project->toolchain.type);
  defer { reset_environment(&previous_env); };

  for (auto tracker: trackers) {
    auto target = tracker->target;
    
    usize files_submitted = 0;

    /*
      I would like to support cases when there's a target sole purpose of which to combine multiple upstream
      targets, for example a static library, that has no source input files, but, which composes multiple static
      libraries into a single one. Another case would be packing static library into a dynamic library.
     */
    if (target->files.count == 0) [[unlikely]] {
      if (target->depends_on.count > 0) {
        atomic_store(&tracker->compile_status, Target_Compile_Status::Success);
        atomic_store(&tracker->link_status,    Target_Link_Status::Waiting);

        auto task = push_struct<Build_Task>(arena);
        task->type    = Build_Task::Link;
        task->tracker = tracker;

        submit_build_command(&build_queue, task);
        continue;
      }

      atomic_store(&tracker->compile_status, Target_Compile_Status::Success);
      /*
        I have some uncertainties about this setting the failed status, it's rather cancelled than failed?
       */
      atomic_store<Memory_Order::Release>(&tracker->link_status, Target_Link_Status::Failed);

      print(arena,
            "Target '%' doesn't have any input files and no upstream dependencies registered. "
            "This target will be skipped. If this is intentional, please report your case\n",
            target->name);

      continue;
    }

    for (auto file_path: target->files) {
      auto file = open_file(&file_path);
      check_status(file);

      /*
        Dependencies should be checked regardless every time. If we don't do this, then we 
        can end up with forced rebuild of the project because we miss some information.

        So it's the question of how to make this process as fast as possible, since most dependency
        chain would repeat.
      */
      auto scan_result = scan_file_dependencies(arena, &file, target->include_paths);
      check_status(scan_result);

      auto task     = push_struct<Build_Task>(arena);
      task->type    = Build_Task::Compile;
      task->file    = file;
      task->tracker = tracker;
      task->dependencies_updated = scan_result;

      submit_build_command(&build_queue, task);
    }
  }

  while (true) {
    auto task = pull_command_for_execution(&build_queue);
    if (not task) break;

    switch (task->type) {
      case Build_Task::Compile: { compile_file(arena, task);         break; }
      case Build_Task::Link:    { link_target(arena, task->tracker); break; }
    }

    atomic_fetch_add(&build_queue.tasks_completed, 1);
  }

  if (builders_count) wait_for_all_tasks_to_complete(&build_queue);

  flush_registry(&registry);

  return Success;
}
