
#include "arrays.hpp"
#include "base.hpp"
#include "command_line.hpp"
#include "driver.hpp"
#include "seq.hpp"
#include "atomics.hpp"
#include "registry.hpp"
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

extern CLI_Flags     global_flags;
extern File_Path     working_directory_path;
extern File_Path     cache_directory_path;
extern Platform_Info platform;

enum struct Chain_Status: u32 {
  Unchecked,
  Checking,
  Checked_Has_Updates,
  Checked_No_Updates,
};

enum struct Target_Compile_Status: u32   { Compiling, Failed, Success };
enum struct Target_Link_Status: u32      { Waiting, Linking, Failed, Success };
enum struct Upstream_Targets_Status: u32 { Ignore, Updated, Failed };

struct Target_Tracker {
  const Target *target;

  Atomic<Target_Compile_Status>   compile_status;
  Atomic<Target_Link_Status>      link_status;
  Atomic<Upstream_Targets_Status> upstream_status;

  cau32 skipped_counter    { 0 };
  cas32 files_pending      { static_cast<s32>(target->files.count) };
  cas32 waiting_on_counter { static_cast<s32>(target->depends_on.count) };

  /*
    Special flag that could be set by the compilation phase to "false", signaling
    that current target has no updates (no files were recompiled) and the existing
    artifact is legit. It's up to the linker to relink the target if upstream dependencies
    were updated.

    INVARIANT:
      This field is written once by a thread that handles the last file for the target. Any other
      thread that would try to link this target, and reads this field, will do so only after the
      compilation phase has completed (i.e compile_status != Compiling).
   */
  bool needs_linking;

  /*
    Setting this flag to true forces the current target to build fully recompiled and re-linked with all
    downstream dependencies.
   */
  bool force_build;

  Target_Tracker (Target *_target, bool _force_build = false)
    : target          { _target },
      compile_status  { Target_Compile_Status::Compiling },
      link_status     { Target_Link_Status::Waiting },
      upstream_status { Upstream_Targets_Status::Ignore },
      needs_linking   { true },
      force_build     { _force_build }
  {
    atomic_store(&this->files_pending, _target->files.count);
  }
};

struct Build_Task {
  enum struct Type: u32 { Uninit, Compile, Link };
  using enum Type;

  Type            type;
  Target_Tracker *tracker;

  File file;
  bool dependencies_updated;
};

struct Build_Queue {
  struct Node {
    Build_Task task;
    as32 sequence_number;

    char cache_line_pad[64 - sizeof(Build_Task) - sizeof(as32)];
  };

  static_assert(sizeof(Node) == 64);

  Node *tasks;
  u32 tasks_count;

  cas64 write_index;
  cas64 read_index;

  Thread    *builders;
  usize      builders_count;
  Semaphore  tasks_available;
  abool      terminating;

  cau32 tasks_submitted;
  cau32 tasks_completed;
};

static File_Path   object_folder_path;
static File_Path   out_folder_path;

static Registry   registry;
static bool       registry_enabled; // must be written only once before building, read by multiple threads
static Update_Set update_set;

static Build_Queue   build_queue;
static Chain_Status *chain_status_cache;

static Status_Code init_build_queue (Build_Queue *queue, Memory_Arena *arena, usize queue_size, usize builders_count, Thread_Proc *builders_proc) {
  use(Status_Code);

  const auto aligned_size = align_forward_to_pow_2(queue_size);

  queue->tasks       = reserve_array<Build_Queue::Node>(arena, aligned_size);
  queue->tasks_count = aligned_size;

  for (usize idx = 0; idx < aligned_size; idx++) {
    auto node = queue->tasks + idx;

    node->task            = Build_Task {};
    node->sequence_number = idx;
  }

  auto semaphore = create_semaphore();
  check_status(semaphore);
  queue->tasks_available = semaphore;
  queue->builders_count  = builders_count;

  if (!queue->builders_count) return Success;

  queue->builders = reserve_array<Thread>(arena, queue->builders_count);

  for (usize idx = 0; idx < queue->builders_count; idx++) {
    auto builder_thread = spawn_thread(builders_proc, queue);
    check_status(builder_thread);

    queue->builders[idx] = builder_thread;
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

static bool submit_build_task (Build_Queue *queue, const Build_Task &task) {
  using enum Memory_Order;
  
  auto index = atomic_load(&queue->write_index);

  const auto tasks_count = queue->tasks_count;
  const auto mask        = tasks_count - 1;

  Build_Queue::Node *node = nullptr;
  while (true) {
    node = &queue->tasks[index & mask];

    auto sequence = atomic_load<Acquire>(&node->sequence_number);
    auto diff     = sequence - index;
    
    if (diff == 0) {
      if (atomic_compare_and_set<Whatever, Whatever>(&queue->write_index, index, index + 1)) break;
    }
    else if (diff < 0) return false;
    else index = atomic_load(&queue->write_index);
  }

  atomic_fetch_add(&queue->tasks_submitted, 1);

  node->task = task;
  atomic_store<Release>(&node->sequence_number, index + 1);

  increment_semaphore(&queue->tasks_available);

  return true;
}

static bool pull_task_for_execution (Build_Queue *queue, Build_Task *result) {
  using enum Memory_Order;
  
  auto index = atomic_load(&queue->read_index);

  const auto tasks_count = queue->tasks_count;
  const auto mask        = tasks_count - 1;

  Build_Queue::Node *node = nullptr;
  while (true) {
    node = &queue->tasks[index & mask];

    auto sequence = atomic_load<Acquire>(&node->sequence_number);
    auto diff     = sequence - (index + 1);

    if (diff == 0) {
      if (atomic_compare_and_set<Whatever, Whatever>(&queue->read_index, index, index + 1)) break;
    }
    else if (diff < 0) return false;
    else index = atomic_load(&queue->read_index);
  }

  *result = node->task;

  atomic_store<Release>(&node->sequence_number, index + tasks_count);

  return true;
}

static bool has_unfinished_tasks (const Build_Queue *queue) {
  auto submitted = atomic_load(&queue->tasks_submitted);
  auto completed = atomic_load(&queue->tasks_completed);

  return (submitted != completed);
}

static Result<Chain_Status> scan_dependency_chains (Memory_Arena *arena, File *source_file, const List<File_Path> &extra_include_paths) {
  use(Status_Code);
  using enum Chain_Status;

  assert(chain_status_cache);

  auto &records = registry.records;

  auto file_id = get_file_id(source_file);

  usize index = 0;

  if (usize count = update_set.header->dependencies_count;
      find_offset(&index, update_set.dependencies, count, file_id)) {
    return chain_status_cache[index];
  }
  else {
    index = update_set.header->dependencies_count++;

    update_set.dependencies[index] = file_id;
    chain_status_cache[index]      = Chain_Status::Checking;
  }

  assert(chain_status_cache[index] == Chain_Status::Checking);

  auto mapping = map_file_into_memory(source_file);
  check_status(mapping);
  defer { unmap_file(&mapping); };
  
  auto iterator      = Dependency_Iterator(&mapping);
  auto include_value = String {};

  List<File_Path> include_directories;
  {
    add(arena, &include_directories, get_parent_folder_path(arena, source_file).value);
    for (auto path: extra_include_paths) add(arena, &include_directories, path);
  };

  bool chain_has_updates = false;
  while (true) {
    {
      auto [status, has_more] = get_next_include_value(&iterator, &include_value);  
      check_status(status);
      if (!has_more) break;
    }

    File_Path resolved_path;
    for (auto prefix: include_directories) {
      auto full_path = make_file_path(arena, prefix, include_value);
      if (!check_file_exists(&full_path)) continue;

      resolved_path = full_path;
      break;
    }

    if (!resolved_path) {
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
  chain_status_cache[index]            = status;

  return status;
}

static Result<bool> scan_file_dependencies (Memory_Arena *_arena, File *source_file, const List<File_Path> &extra_include_paths) {
  use(Status_Code);

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
  };

  bool chain_has_updates = false;
  while (true) {
    {
      auto [status, has_more] = get_next_include_value(&iterator, &include_value);
      check_status(status);
      if (!has_more) break;
    }

    auto inner_local = local;
    
    File_Path resolved_path;
    for (auto prefix: include_directories) {
      auto full_path = make_file_path(&inner_local, prefix, include_value);
      if (!check_file_exists(&full_path)) continue;

      resolved_path = full_path;
      break;
    }

    if (!resolved_path) {
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

static bool is_msvc (Toolchain_Type type) {
  return ((type == Toolchain_Type_MSVC_X86) ||
          (type == Toolchain_Type_MSVC_X64) ||
          (type == Toolchain_Type_LLVM_CL));
}

static bool is_msvc (const Toolchain_Configuration *config) {
  return is_msvc(config->type);
}

static bool is_win32 () {
  return platform.type == Platform_Type::Win32;
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
      auto target_file_name = is_win32() ? format_string(arena, "%.exe", target->name) : target->name;
      return make_file_path(arena, out_folder_path, target_file_name);
    }
  }
}

template <typename Lambda>
static void schedule_downstream_linkage (const Target *target, Lambda update_tracker) {
  for (auto downstream: target->required_by) {
    auto downstream_tracker = downstream->tracker;

    update_tracker(downstream_tracker);

    if ((atomic_fetch_sub(&downstream_tracker->waiting_on_counter, 1) - 1) == 0) {
      submit_build_task(&build_queue, {
        .type    = Build_Task::Type::Link,
        .tracker = downstream_tracker
      });
    }
  }
}

static void link_target (Memory_Arena *arena, Target_Tracker *tracker) {
  using TLS = Target_Link_Status;

  auto target  = tracker->target;
  auto project = target->project;

  Target_Compile_Status target_compilation_status = atomic_load(&tracker->compile_status);
  if (target_compilation_status == Target_Compile_Status::Compiling) return;

  /*
    For targets that have upstream dependencies, we must ensure that all upstreams were processed and finalized their
    statuses, before linking this target. If there are upstream that we must wait on, this link task must be rescheduled.

    `waiting_on_counter` > 0, means that when some other thread decrements this counter to zero, a linking task will be
    submitted to the queue by that thread.
  */
  if (atomic_load<Memory_Order::Acquire>(&tracker->waiting_on_counter) > 0) return;

  if (!atomic_compare_and_set(&tracker->link_status, TLS::Waiting, TLS::Linking)) return;

  auto upstream_status = atomic_load(&tracker->upstream_status);
  if ((target_compilation_status == Target_Compile_Status::Failed) ||
      (upstream_status           == Upstream_Targets_Status::Failed)) {
    atomic_store(&tracker->link_status, TLS::Failed);

    schedule_downstream_linkage(target, [] (Target_Tracker *tracker) {
      atomic_store(&tracker->upstream_status, Upstream_Targets_Status::Failed);
    });

    return;
  }

  enum struct Link_Result { Ignore, Success, Failed };
  auto link_result = Link_Result::Ignore;

  auto needs_linking = tracker->needs_linking || upstream_status == Upstream_Targets_Status::Updated;
  if (needs_linking) {
    if (!global_flags.silenced) print(arena, "Linking target: %\n", target->name);

    auto output_file_path      = get_output_file_path_for_target(arena, target);
    auto object_file_extension = platform.type == Platform_Type::Win32 ? "obj" : "o";

    String_Builder builder { arena };

    switch (target->type) {
      case Target::Type::Static_Library: {
        builder += project->toolchain.archiver_path;
        builder += project->global_options.archiver;
        builder += target->options.archiver;

        for (auto &path: target->files) {
          builder += make_file_path(arena, object_folder_path, target->name,
                                    format_string(arena, "%.%", get_file_name(&path), object_file_extension));
        }

        for (auto lib: target->depends_on) {
          assert(atomic_load(&lib->tracker->link_status) == Target_Link_Status::Success);

          builder += make_file_path(arena, out_folder_path,
                                    format_string(arena, "%.%", lib->name, is_win32() ? "lib" : "a"));
        }

        if (is_win32()) builder += format_string(arena, "/OUT:%", output_file_path);
        else            builder += format_string(arena, "-o %",   output_file_path);

        break;
      };
      case Target::Type::Shared_Library: {
        builder += project->toolchain.linker_path;
        builder += is_win32() ? "/dll" : "-shared";
        builder += project->global_options.linker;
        builder += target->options.linker;

        for (auto &path: target->files) {
          builder += make_file_path(arena, object_folder_path, target->name,
                                    format_string(arena, "%.%", get_file_name(&path), object_file_extension));
        }
      
        for (auto lib: target->depends_on) {
          assert(atomic_load(&lib->tracker->link_status) == Target_Link_Status::Success);
        
          builder += make_file_path(arena, out_folder_path,
                                    format_string(arena, "%.%", lib->name, is_win32() ? "lib" : "a"));
        }

        builder += target->link_libraries;

        if (is_win32()) builder += format_string(arena, "/OUT:%", output_file_path);
        else            builder += format_string(arena, "-o %",   output_file_path);

        break;
      };
      case Target::Type::Executable: {
        builder += project->toolchain.linker_path;
        builder += project->global_options.linker;
        builder += target->options.linker;

        for (auto &path: target->files) {
          builder += make_file_path(arena, object_folder_path, target->name,
                                    format_string(arena, "%.%", get_file_name(&path), object_file_extension));
        }

        for (auto lib: target->depends_on) {
          assert(atomic_load(&lib->tracker->link_status) == Target_Link_Status::Success);
        
          const char *lib_extension = "lib"; // on Win32 static and import libs for dlls have the same extension
          if (!is_win32()) {
            if (lib->type == Target::Type::Static_Library) lib_extension = "a";
            else                                           lib_extension = "so";
          }
        
          builder += make_file_path(arena, out_folder_path,
                                    format_string(arena, "%.%", lib->name, lib_extension));
        }

        builder += target->link_libraries;

        if (is_win32()) builder += format_string(arena, "/OUT:%", output_file_path);
        else            builder += format_string(arena, "-o %",   output_file_path);

        break;
      };
    };

    auto link_command = build_string_with_separator(&builder, ' ');

    auto [status, output] = run_system_command(arena, link_command);
    link_result = (!status) ? Link_Result::Failed : Link_Result::Success;

    if (output.length) print(arena, "%\n", output);
  }

  auto target_link_status = (link_result == Link_Result::Failed) ? TLS::Failed : TLS::Success;
  atomic_store(&tracker->link_status, target_link_status);

  schedule_downstream_linkage(target, [link_result] (Target_Tracker *tracker) {
    if (link_result != Link_Result::Ignore) {
      auto new_status = Upstream_Targets_Status::Updated;
      if (link_result == Link_Result::Failed) {
        new_status = Upstream_Targets_Status::Failed;
      }
      
      // In case another thread set this to Failed, which we don't want to overwrite
      atomic_compare_and_set(&tracker->upstream_status, Upstream_Targets_Status::Ignore, new_status);  
    }
  });

  if (target->hooks.on_linked) {
    target->hooks.on_linked(project, target, project->args, Hook_Type_After_Target_Linked);
  }
}

static void compile_file (Memory_Arena *arena, Target_Tracker *tracker, File *file, bool dependencies_updated) {
  auto target    = tracker->target;
  auto project   = target->project;
  auto toolchain = &project->toolchain;

  auto target_info      = reinterpret_cast<Registry::Target_Info *>(target->info);
  auto target_last_info = reinterpret_cast<Registry::Target_Info *>(target->last_info);

  auto file_id   = get_file_id(file);
  auto timestamp = get_last_update_timestamp(file);

  auto extension        = platform.type == Platform_Type::Win32 ? "obj" : "o";
  auto object_file_name = format_string(arena, "%.%", get_file_name(&file->path), extension);
  auto object_file_path = make_file_path(arena, object_folder_path, target->name, object_file_name);

  bool should_rebuild = true;

  if (!tracker->force_build && !dependencies_updated && target->last_info) {
    auto &records = registry.records;

    auto section      = records.files + target_last_info->files_offset;
    auto section_size = target_last_info->files_count.value;

    usize index;
    bool record_found = find_offset(&index, section, section_size, file_id);

    if (record_found) {
      auto record_index     = target_last_info->files_offset + index;
      auto record_timestamp = records.file_records[record_index].timestamp;

      should_rebuild = (timestamp != record_timestamp) || (!check_file_exists(&object_file_path));
    }
  }

  enum struct File_Compile_Status { Ignore, Success, Failed };
  auto file_compilation_status = File_Compile_Status::Ignore;

  if (should_rebuild) {
    if (!global_flags.silenced) print(arena, "Building file: %\n", file->path.value);

    auto is_cpp_file = check_extension(file->path, "cpp");

    String_Builder builder { arena };
    builder += is_cpp_file ? project->toolchain.cpp_compiler_path : project->toolchain.c_compiler_path;
    builder += project->global_options.compiler;
    builder += target->options.compiler;

    for (auto &path: project->global_options.include_paths) {
      builder += is_msvc(toolchain) ? format_string(arena, R"(/I"%")", path) : format_string(arena, R"(-I "%")", path);
    }

    for (auto &path: target->include_paths) {
      builder += is_msvc(toolchain) ? format_string(arena, R"(/I"%")", path) : format_string(arena, R"(-I "%")", path);
    }

    if (is_msvc(toolchain)) builder += format_string(arena, R"(/c "%" /Fo"%")", file->path, object_file_path);
    else                    builder += format_string(arena, R"(-c "%" -o "%")",  file->path, object_file_path);

    auto compilation_command = build_string_with_separator(&builder, ' ');

    auto [status, output] = run_system_command(arena, compilation_command);

    if (output)  print(arena, "%\n", output);
    if (!status) print(arena, "%\n", status);

    file_compilation_status = (status == Status_Code::Success) ? File_Compile_Status::Success : File_Compile_Status::Failed;
  }
  else {
    atomic_fetch_add(&tracker->skipped_counter, 1);
  }

  if (registry_enabled && file_compilation_status != File_Compile_Status::Failed) {
    auto index = atomic_fetch_add(&target_info->files_count, 1);
    assert(index < target_info->aligned_max_files_count);

    auto update_set_index = target_info->files_offset + index;

    assert(update_set.files[update_set_index] == 0);
    update_set.files[update_set_index]        = file_id;
    update_set.file_records[update_set_index] = Registry::Record { .timestamp = timestamp };  
  }

  if (file_compilation_status == File_Compile_Status::Failed) {
    atomic_store(&tracker->compile_status, Target_Compile_Status::Failed);
  }

  auto pending = atomic_fetch_sub<Memory_Order::Acquire_Release>(&tracker->files_pending, 1);
  auto was_last_target_file = (pending - 1) == 0;
  
  if (!was_last_target_file) return;

  /*
    At this point it's guaranteed that no other thread would modify target's compilation status,
    since current thread processed the last file.
  */

  auto compile_status = atomic_load(&tracker->compile_status);
  if (compile_status == Target_Compile_Status::Failed) {
    print(arena, "Target '%' couldn't be linked because of compilation errors\n", target->name);
    return;
  }

  auto skipped_count = atomic_load(&tracker->skipped_counter);
  auto needs_linking = skipped_count < target->files.count;
  if (!needs_linking) {
    // If no files were compiled, check that the binary exists
    auto output_file_path = get_output_file_path_for_target(arena, target);
    needs_linking = !check_file_exists(&output_file_path); 
  }

  tracker->needs_linking = needs_linking;

  assert(compile_status == Target_Compile_Status::Compiling);
  atomic_store<Memory_Order::Release>(&tracker->compile_status, Target_Compile_Status::Success);
}

static void process_build_task (Memory_Arena *arena, Build_Queue *queue) {
  Build_Task task;
  if (!pull_task_for_execution(queue, &task)) return;

  auto tracker = task.tracker;
  auto target  = tracker->target;

  switch (task.type) {
    case Build_Task::Type::Uninit: {
      return;
    }
    case Build_Task::Type::Compile: {
      compile_file(arena, tracker, &task.file, task.dependencies_updated);

      auto status = atomic_load(&tracker->compile_status);
      if (status == Target_Compile_Status::Compiling) break;

      task.type = Build_Task::Link;
      submit_build_task(queue, task);

      break;
    }
    case Build_Task::Type::Link: {
      link_target(arena, task.tracker);
      break;
    }
  }

  atomic_fetch_add(&queue->tasks_completed, 1);
}

static u32 builder_thread_proc (void *param) {
  auto queue = reinterpret_cast<Build_Queue *>(param);

  auto virtual_memory = reserve_virtual_memory(megabytes(1));
  Memory_Arena arena { virtual_memory };
  defer { free_virtual_memory(&virtual_memory); };

  while (true) {
    wait_for_semaphore_signal(&queue->tasks_available);

    if (atomic_load<Memory_Order::Acquire>(&queue->terminating)) return 0;

    reset_arena(&arena);
    process_build_task(&arena, queue);
  }
}

static u32 number_of_extra_builders_to_spawn (Memory_Arena *arena, u32 request_builders_count) {
  use(Status_Code);

  // This number excludes main thread, which always exists
  auto cpu_count = get_logical_cpu_count();
  u32 count = request_builders_count ? request_builders_count : cpu_count;

  if (count > cpu_count) {
    print(arena, "WARNING: 'builders' value is bigger than the number of CPU cores (i.e requested - %, core count - %). Defaulting to %\n", count, cpu_count, cpu_count);
  }

  count = clamp(count, 1, cpu_count);

  // This number specifies only the count of extra threads in addition to the main thread
  return count - 1;
}

static Status_Code create_output_directories (Memory_Arena *arena, const Project *project) {
  for (auto target: project->targets) {
    auto local = *arena;
    auto target_object_folder_path = make_file_path(&local, object_folder_path, target->name);
    check_status(create_directory(&target_object_folder_path));
  }

  return Status_Code::Success;
}

static void init_trackers (Target_Tracker *trackers, const Project *project) {
  for (int idx = 0; auto target: project->targets) {
    trackers[idx]   = Target_Tracker(target, project->rebuild_required);
    target->tracker = &trackers[idx];

    idx += 1;
  }
}

Status_Code build_project (Memory_Arena *arena, const Project *project, Build_Config config) {
  using enum Open_File_Flags;
  use(Status_Code);

  if (project->targets.count == 0) return Success;

  object_folder_path = make_file_path(arena, project->output_location_path, "obj");
  check_status(create_directory(&object_folder_path));

  check_status(create_output_directories(arena, project));

  out_folder_path = make_file_path(arena, project->output_location_path, "out");
  check_status(create_directory(&out_folder_path));

  registry_enabled = !project->registry_disabled && config.cache != Build_Config::Cache_Behavior::Off;
  if (registry_enabled && config.cache == Build_Config::Cache_Behavior::On) {
    auto registry_file_path = make_file_path(arena, project->output_location_path, "__registry");
    check_status(registry_file_path);

    auto registry_file = open_file(&registry_file_path, Open_File_Flags::Request_Write_Access |
                                                        Open_File_Flags::Create_File_If_Not_Exists);
    check_status(registry_file);

    check_status(load_registry(&registry, arena, registry_file)); 
  }

  if (registry_enabled && config.cache != Build_Config::Cache_Behavior::Off) {
    check_status(init_update_set(&update_set, arena, &registry, project)); 
  }

  chain_status_cache = reserve_array<Chain_Status>(arena, max_supported_files_count);

  auto builders_count = number_of_extra_builders_to_spawn(arena, config.builders_count);
  auto queue_size = project->targets.count + project->total_files_count;
  check_status(init_build_queue(&build_queue, arena, queue_size, builders_count, builder_thread_proc));
  defer { destroy_build_queue(&build_queue); };

  auto trackers = reserve_array<Target_Tracker>(arena, project->targets.count);
  init_trackers(trackers, project);

  for (u32 idx = 0; idx < project->targets.count; idx++) {
    auto tracker = trackers + idx;
    auto target  = tracker->target;

    if (target->files.count == 0) {
      print(arena, "Target '%' doesn't have any input files and will be skipped\n", target->name);
      continue;
    }

    for (auto file_path: target->files) {
      auto file = open_file(&file_path);
      check_status(file);

      auto dependencies_updated = true;
      if (registry_enabled) {
        auto local = *arena;

        List<File_Path> include_paths;
        for (auto path: project->global_options.include_paths) add(&local, &include_paths, path);
        for (auto path: target->include_paths)                 add(&local, &include_paths, path);

        auto scan_result     = scan_file_dependencies(&local, &file, include_paths);
        dependencies_updated = !scan_result.status || scan_result.value;
      }

      submit_build_task(&build_queue, {
        .type    = Build_Task::Compile,
        .tracker = tracker,
        .file    = file,
        .dependencies_updated = dependencies_updated,
      });
    }
  }

  while (has_unfinished_tasks(&build_queue))
    process_build_task(arena, &build_queue);

  if (registry_enabled) check_status(flush_registry(&registry, &update_set));

  for (usize idx = 0; idx < project->targets.count; idx++) {
    auto tracker = trackers + idx;

    assert(tracker->compile_status.value != Target_Compile_Status::Compiling);
    assert(tracker->link_status.value    != Target_Link_Status::Waiting);

    if ((tracker->compile_status.value != Target_Compile_Status::Success) ||
        (tracker->link_status.value    != Target_Link_Status::Success)) {
      return { Build_Error, "Couldn't build one or more targets" };
    }
  }

  return Success;
}

