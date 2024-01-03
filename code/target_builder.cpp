
#include "anyfin/base.hpp"

#include "anyfin/core/arrays.hpp"
#include "anyfin/core/atomics.hpp"
#include "anyfin/core/list.hpp"
#include "anyfin/core/math.hpp"
#include "anyfin/core/result.hpp"
#include "anyfin/core/seq.hpp"
#include "anyfin/core/string_builder.hpp"

#include "anyfin/platform/console.hpp"
#include "anyfin/platform/concurrent.hpp"
#include "anyfin/platform/platform.hpp"
#include "anyfin/platform/threads.hpp"
#include "anyfin/platform/commands.hpp"
#include "anyfin/platform/file_system.hpp"

#include "arrays.hpp"
#include "cbuild_api.hpp"
#include "dependency_iterator.hpp"
#include "driver.hpp"
#include "project_loader.hpp"
#include "registry.hpp"
#include "task_system.hpp"
#include "target_builder.hpp"
#include "toolchain.hpp"

extern CLI_Flags global_flags;

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

  Atomic<Target_Compile_Status>   compile_status  { Target_Compile_Status::Compiling };
  Atomic<Target_Link_Status>      link_status     { Target_Link_Status::Waiting };
  Atomic<Upstream_Targets_Status> upstream_status { Upstream_Targets_Status::Ignore };

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
  bool needs_linking { true };

  Target_Tracker (Target * const _target)
    : target { _target }
  {
    assert(_target->build_context.tracker == nullptr);
    _target->build_context.tracker = this;
  }

  void * operator new (usize size, Memory_Arena &arena) {
    return reserve_memory(arena, size, alignof(Target_Tracker));
  }
};

struct Build_Task {
  enum struct Type: u32 { Uninit, Compile, Link };
  using enum Type;

  Type type;
  bool dependencies_updated; // TODO: Check if I still need this flag?
  Target_Tracker *tracker;
  File file;
};

static File_Path out_folder_path;
static File_Path object_folder_path;

static Registry   registry {};
static bool       registry_enabled;
static Update_Set update_set {};

/*
  Massive array with per-file dependency chain statuses;
 */
static Slice<Chain_Status> chain_status_cache;

static Chain_Status scan_dependency_chains (Memory_Arena &arena, const File &source_file, const List<File_Path> &extra_include_paths) {
  using enum Chain_Status;

  assert(chain_status_cache);

  auto &records = registry.records;

  auto file_id = *get_file_id(source_file);

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

  auto iterator = Dependency_Iterator(*map_file_into_memory(source_file));

  List<File_Path> include_directories(arena);
  {
    auto [tag, error, parent_folder] = get_parent_folder_path(arena, source_file.path);
    if (tag) [[likely]] {
      if (parent_folder) list_push(include_directories, parent_folder.take());  
      else {
        print("ERROR: Couldn't resolve parent folder for the source file '%'. "
              "Build process will continue, but this may cause issues with include files lookup.",
              source_file.path);
      }
    }
    else {
      print("ERROR: Couldn't resolve parent folder for the source file '%' due to a system error: %. "
            "Build process will continue, but this may cause issues with include files lookup.",
            source_file.path, error);
    }
    
    for (auto &path: extra_include_paths)
      list_push(include_directories, String::copy(arena, path));
  };

  bool chain_has_updates = false;
  while (true) {
    auto [tag, parse_error, next_value] = get_next_include_value(iterator);

    /*
      TODO: Perhaps this shouldn't trap the entire process and it would be better to keep building other targets to make
      overall progress?
    */
    if (!tag) panic("Parse error occurred while resolving included files");
    if (next_value.is_none()) break;

    auto include_value = next_value.take();

    File_Path resolved_path;
    for (auto &prefix: include_directories) {
      auto full_path = make_file_path(arena, prefix, include_value);
      if (auto [tag, _, result] = check_resource_exists(full_path, Resource_Type::File); !tag || result) continue;

      resolved_path = move(full_path);

      break;
    }

    if (!resolved_path) {
      /*
        TODO:
          For now this is fine, but running this on multiple threads the message maybe interspersed with
          other message and broken into multiple parts. I could compose a string with a builder and print
          with a single call instead, which would work better in multi-threaded env.
       */
      print("Couldn't resolve the include file: %, source: %, the following paths were checked: \n", include_value, source_file.path);
      for (auto &path: include_directories) print("  - %\n", path);

      continue;
    }

    auto dependency_file = open_file(arena, resolved_path).take("Couldn't open included header file for scanning.");
    defer { close_file(dependency_file); };
    
    auto chain_scan_result = scan_dependency_chains(arena, dependency_file, extra_include_paths);
    assert(chain_scan_result != Chain_Status::Unchecked);

    if (chain_scan_result == Chain_Status::Checked_Has_Updates) {
      chain_has_updates = true;
    }
  }

  auto timestamp = *get_last_update_timestamp(source_file);
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

static bool scan_file_dependencies (Memory_Arena &_arena, const File &source_file, const List<File_Path> &extra_include_paths) {
  auto local = _arena;

  auto iterator = Dependency_Iterator(*map_file_into_memory(source_file));

  List<File_Path> include_directories;
  {
    auto [tag, error, parent_folder] = get_parent_folder_path(local, source_file.path);
    if (tag) [[likely]] {
      if (parent_folder) list_push(include_directories, parent_folder.take());  
      else {
        print("ERROR: Couldn't resolve parent folder for the source file '%'. "
              "Build process will continue, but this may cause issues with include files lookup.",
              source_file.path);
      }
    }
    else {
      print("ERROR: Couldn't resolve parent folder for the source file '%' due to a system error: %. "
            "Build process will continue, but this may cause issues with include files lookup.",
            source_file.path, error);
    }
    
    for (auto &path: extra_include_paths)
      list_push(include_directories, String::copy(local, path));
  };

  bool chain_has_updates = false;
  while (true) {
    auto [tag, parse_error, include_value] = get_next_include_value(iterator);

    /*
      TODO: Perhaps this shouldn't trap the entire process and it would be better to keep building other targets to make
      overall progress?
    */
    if (!tag) panic("Parse error occurred while resolving included files");
    if (!include_value) break;

    auto inner_local = local;
    
    File_Path resolved_path;
    for (auto &prefix: include_directories) {
      auto full_path = make_file_path(inner_local, prefix, include_value.get());
      if (auto [tag, _, result] = check_resource_exists(full_path, Resource_Type::File); !tag || result) continue;

      resolved_path = move(full_path);

      break;
    }

    if (!resolved_path) {
      /*
        TODO:
          For now this is fine, but running this on multiple threads the message maybe interspersed with
          other message and broken into multiple parts. I could compose a string with a builder and print
          with a single call instead, which would work better in multi-threaded env.
       */
      print("Couldn't resolve the include file: %, source: %, the following paths were checked: \n",
            include_value, source_file.path);
      for (auto &path: include_directories) print("  - %\n", path);

      continue;
    }

    auto dependency_file = open_file(inner_local, resolved_path).take("Couldn't open included header file for scanning.");
    defer { close_file(dependency_file); };
    
    auto chain_scan_result = scan_dependency_chains(inner_local, dependency_file, extra_include_paths);
    assert(chain_scan_result != Chain_Status::Unchecked);

    if (chain_scan_result == Chain_Status::Checked_Has_Updates) {
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

static bool is_msvc (const Toolchain_Configuration &config) {
  return is_msvc(config.type);
}

static void schedule_downstream_linkage (const Target &target, const Invocable<void, Target_Tracker &> auto &update_tracker) {
  for (auto downstream: target.required_by) {
    auto downstream_tracker = downstream->build_context.tracker;

    // In targetted builds, target may not have an associated tracker, thus we skip these
    if (downstream_tracker == nullptr) continue;

    update_tracker(*downstream_tracker);

    if ((atomic_fetch_sub(downstream_tracker->waiting_on_counter, 1) - 1) == 0) {
      // submit_build_task(&build_queue, {
      //   .type    = Build_Task::Type::Link,
      //   .tracker = downstream_tracker
      // });
      todo();
    }
  }
}

static File_Path get_target_object_folder_path (Memory_Arena &arena, const Target &target) {
  if (!target.flags.external) [[likely]]
    return make_file_path(arena, object_folder_path, target.name);
  
  return make_file_path(arena, object_folder_path, target.project->external_name, target.name);
}

static void link_target (Memory_Arena &arena, Target_Tracker &tracker) {
  using TLS = Target_Link_Status;

  const auto &target  = *tracker.target;
  const auto &project = *target.project;

  Target_Compile_Status target_compilation_status = atomic_load(tracker.compile_status);
  if (target_compilation_status == Target_Compile_Status::Compiling) return;

  /*
    For targets that have upstream dependencies, we must ensure that all upstreams were processed and finalized their
    statuses, before linking this target. If there are upstream that we must wait on, this link task must be rescheduled.

    `waiting_on_counter` > 0, means that when some other thread decrements this counter to zero, a linking task will be
    submitted to the queue by that thread.
  */
  if (atomic_load<Memory_Order::Acquire>(tracker.waiting_on_counter) > 0) return;

  if (!atomic_compare_and_set(tracker.link_status, TLS::Waiting, TLS::Linking)) return;

  auto upstream_status = atomic_load(tracker.upstream_status);
  if ((target_compilation_status == Target_Compile_Status::Failed) ||
      (upstream_status           == Upstream_Targets_Status::Failed)) {
    atomic_store(tracker.link_status, TLS::Failed);

    schedule_downstream_linkage(target, [] (Target_Tracker &tracker) {
      atomic_store(tracker.upstream_status, Upstream_Targets_Status::Failed);
    });

    return;
  }

  enum struct Link_Result { Ignore, Success, Failed };
  auto link_result = Link_Result::Ignore;

  auto needs_linking = tracker.needs_linking || upstream_status == Upstream_Targets_Status::Updated;
  if (needs_linking) {
    if (!global_flags.silenced) print("Linking target: %\n", target.name);

    auto target_object_folder = get_target_object_folder_path(arena, target);
    auto output_file_path     = get_output_file_path_for_target(arena, target);

    String_Builder builder { arena };

    switch (target.type) {
      case Target::Type::Static_Library: {
        builder += String_View(project.toolchain.archiver_path);
        builder += project.project_options.archiver;
        builder += target.options.archiver;

        for (auto &path: target.files) {
          builder += make_file_path(arena, target_object_folder,
                                    format_string(arena, "%.%", *get_resource_name(arena, path), get_object_extension()));
        }

        for (auto lib: target.depends_on) {
          assert(atomic_load(lib->build_context.tracker->link_status) == Target_Link_Status::Success);

          builder += make_file_path(arena, out_folder_path,
                                    format_string(arena, "%.%", lib->name, get_static_library_extension()));
        }

        if (is_win32()) builder += format_string(arena, "/OUT:%", output_file_path);
        else            builder += format_string(arena, "-o %",   output_file_path);

        break;
      };
      case Target::Type::Shared_Library: {
        builder += String_View(project.toolchain.linker_path);
        builder += is_win32() ? String_View("/dll") : String_View("-shared");
        builder += project.project_options.linker;
        builder += target.options.linker;

        for (auto &path: target.files) {
          builder += make_file_path(arena, target_object_folder,
                                    format_string(arena, "%.%", *get_resource_name(arena, path), get_object_extension()));
        }
      
        for (auto lib: target.depends_on) {
          assert(atomic_load(lib->build_context.tracker->link_status) == Target_Link_Status::Success);
        
          builder += make_file_path(arena, out_folder_path,
                                    format_string(arena, "%.%", lib->name, get_static_library_extension()));
        }

        builder += target.link_libraries;

        if (is_win32()) builder += format_string(arena, "/OUT:%", output_file_path);
        else            builder += format_string(arena, "-o %",   output_file_path);

        break;
      };
      case Target::Type::Executable: {
        builder += String_View(project.toolchain.linker_path);
        builder += project.project_options.linker;
        builder += target.options.linker;

        for (auto &path: target.files) {
          builder += make_file_path(arena, target_object_folder,
                                    format_string(arena, "%.%", *get_resource_name(arena, path), get_object_extension()));
        }

        for (auto lib: target.depends_on) {
          assert(atomic_load(lib->build_context.tracker->link_status) == Target_Link_Status::Success);
        
          String_View lib_extension = "lib"; // on Win32 static and import libs for dlls have the same extension
          if (!is_win32()) {
            if (lib->type == Target::Type::Static_Library) lib_extension = "a";
            else                                           lib_extension = "so";
          }
        
          builder += make_file_path(arena, out_folder_path,
                                    format_string(arena, "%.%", lib->name, lib_extension));
        }

        builder += target.link_libraries;

        if (is_win32()) builder += format_string(arena, "/OUT:%", output_file_path);
        else            builder += format_string(arena, "-o %",   output_file_path);

        break;
      };
    };

    auto link_command = build_string_with_separator(arena, builder, ' ');

    auto [tag, error, _return] = run_system_command(arena, link_command);
    link_result = (!tag || _return.status_code != 0) ? Link_Result::Failed : Link_Result::Success;

    if (_return.output) print("%\n", _return.output);
  }

  auto target_link_status = (link_result == Link_Result::Failed) ? TLS::Failed : TLS::Success;
  atomic_store(tracker.link_status, target_link_status);

  schedule_downstream_linkage(target, [link_result] (Target_Tracker &tracker) {
    if (link_result != Link_Result::Ignore) {
      auto new_status = Upstream_Targets_Status::Updated;
      if (link_result == Link_Result::Failed) {
        new_status = Upstream_Targets_Status::Failed;
      }
      
      // In case another thread set this to Failed, which we don't want to overwrite
      atomic_compare_and_set(tracker.upstream_status, Upstream_Targets_Status::Ignore, new_status);  
    }
  });

  if (target.hooks.on_linked) {
    target.hooks.on_linked(&project, &target, project.args, Hook_Type_After_Target_Linked);
  }
}

static void compile_file (Memory_Arena &arena, Target_Tracker &tracker, const File &file, const bool dependencies_updated) {
  const auto &target    = *tracker.target;
  const auto &project   = *target.project;
  const auto &toolchain = project.toolchain;

  auto target_info      = reinterpret_cast<Registry::Target_Info *>(target.build_context.info);
  auto target_last_info = reinterpret_cast<Registry::Target_Info *>(target.build_context.last_info);

  auto file_id   = *get_file_id(file);
  auto timestamp = *get_last_update_timestamp(file);

  auto target_object_folder = get_target_object_folder_path(arena, target);
  auto object_file_path     = make_file_path(arena, target_object_folder,
                                             format_string(arena, "%.%", *get_resource_name(arena, file.path), get_object_extension()));

  bool should_rebuild = true;
  if (!project.rebuild_required && !dependencies_updated && target.build_context.last_info) {
    auto &records = registry.records;

    auto section      = records.files + target_last_info->files_offset;
    auto section_size = target_last_info->files_count.value;

    usize index;
    bool record_found = find_offset(&index, section, section_size, file_id);

    if (record_found) {
      auto record_index     = target_last_info->files_offset + index;
      auto record_timestamp = records.file_records[record_index].timestamp;

      auto [tag, _, exists] = check_resource_exists(object_file_path, Resource_Type::File);

      should_rebuild = (timestamp != record_timestamp) || (!tag || !exists);
    }
  }

  enum struct File_Compile_Status { Ignore, Success, Failed };
  auto file_compilation_status = File_Compile_Status::Ignore;

  if (should_rebuild) {
    if (!global_flags.silenced) print("Building file: %\n", file.path);

    auto is_cpp_file = ends_with(file.path, "cpp");

    String_Builder builder { arena };
    builder += String_View(is_cpp_file ? project.toolchain.cpp_compiler_path : project.toolchain.c_compiler_path);
    builder += project.project_options.compiler;
    builder += target.options.compiler;

    for (auto &path: project.project_options.include_paths) {
      builder += is_msvc(toolchain) ? format_string(arena, R"(/I"%")", path) : format_string(arena, R"(-I "%")", path);
    }

    for (auto &path: target.include_paths) {
      builder += is_msvc(toolchain) ? format_string(arena, R"(/I"%")", path) : format_string(arena, R"(-I "%")", path);
    }

    if (is_msvc(toolchain)) builder += format_string(arena, R"(/c "%" /Fo"%")",  file.path, object_file_path);
    else                    builder += format_string(arena, R"(-c "%" -o "%")",  file.path, object_file_path);

    auto compilation_command = build_string_with_separator(arena, builder, ' ');

    auto [has_failed, error, status] = run_system_command(arena, compilation_command);
    if (has_failed) panic("File compilation failed with a system error: %, command: %", error, compilation_command);

    auto &[output, return_code] = status;

    if (!return_code) panic("File compilation failed with status: %, command: %\n", return_code, compilation_command);
    if (output)       print("%\n", output);

    file_compilation_status = (!return_code) ? File_Compile_Status::Success : File_Compile_Status::Failed;
  }
  else {
    atomic_fetch_add(tracker.skipped_counter, 1);
  }

  if (registry_enabled && file_compilation_status != File_Compile_Status::Failed) {
    auto index = atomic_fetch_add(target_info->files_count, 1);
    assert(index < target_info->aligned_max_files_count);

    auto update_set_index = target_info->files_offset + index;

    assert(update_set.files[update_set_index] == 0);
    update_set.files[update_set_index]        = file_id;
    update_set.file_records[update_set_index] = Registry::Record { .timestamp = timestamp };  
  }

  if (file_compilation_status == File_Compile_Status::Failed) {
    atomic_store(tracker.compile_status, Target_Compile_Status::Failed);
  }

  auto pending = atomic_fetch_sub<Memory_Order::Acquire_Release>(tracker.files_pending, 1);
  auto was_last_target_file = (pending - 1) == 0;
  
  if (!was_last_target_file) return;

  /*
    At this point it's guaranteed that no other thread would modify target's compilation status,
    since current thread processed the last file.
  */

  auto compile_status = atomic_load(tracker.compile_status);
  if (compile_status == Target_Compile_Status::Failed) {
    print("Target '%' couldn't be linked because of compilation errors\n", target.name);
    return;
  }

  auto skipped_count = atomic_load(tracker.skipped_counter);
  auto needs_linking = skipped_count < target.files.count;
  if (!needs_linking) {
    // If no files were compiled, check that the binary exists
    auto output_file_path = get_output_file_path_for_target(arena, target);
    needs_linking = !check_resource_exists(output_file_path, Resource_Type::File); 
  }

  tracker.needs_linking = needs_linking;

  assert(compile_status == Target_Compile_Status::Compiling);
  atomic_store<Memory_Order::Release>(tracker.compile_status, Target_Compile_Status::Success);
}

struct Target_Builder_Context {
  Memory_Region region;
  Memory_Arena  arena;

  /*
    NOTE:
      This constructor will be called on a dedicated thread
   */
  Target_Builder_Context ()
    : region { reserve_virtual_memory(megabytes(1)) },
      arena  { region.memory, region.size }
  {}

  ~Target_Builder_Context () {
    free_virtual_memory(this->region);
  }
};

using Build_System = Task_System<Build_Task, Target_Builder_Context>;

static void build_target_task (Build_System &build_system, Target_Builder_Context &context, Build_Task &task) {
  reset_arena(context.arena);

  auto &tracker = *task.tracker;
  auto &target  = *tracker.target;

  switch (task.type) {
    case Build_Task::Type::Uninit: return;
    case Build_Task::Type::Compile: {
      compile_file(context.arena, tracker, task.file, task.dependencies_updated);

      auto status = atomic_load(tracker.compile_status);
      if (status == Target_Compile_Status::Compiling) break;

      task.type = Build_Task::Link;
      build_system.add_task(move(task));

      break;
    }
    case Build_Task::Type::Link: {
      link_target(context.arena, tracker);
      break;
    }
  }
}

static u32 number_of_extra_builders_to_spawn (const Build_Config &config) {
  if (config.builders_count <= 0) return 0;
  
  // This number excludes main thread, which always exists
  auto cpu_count = get_logical_cpu_count();
  auto count     = static_cast<u32>(config.builders_count);

  if (count > cpu_count) {
    print("WARNING: 'builders' value is bigger than the number of CPU cores "
          "(i.e requested - %, core count - %). Defaulting to %\n",
          count, cpu_count, cpu_count);
  }

  count = clamp(count, 1, cpu_count);

  // This number specifies only the count of extra threads in addition to the main thread
  return count - 1;
}

static auto create_task_system (Memory_Arena &arena, const Project &project, const Build_Config &config) {
  const auto queue_size = project.targets.count + project.total_files_count;

  const auto builders_count = number_of_extra_builders_to_spawn(config);

  return Build_System(arena, queue_size, builders_count, build_target_task);
}

struct Build_Plan {
  using Targets_List = List<Target_Tracker *>;

  Targets_List selected_targets;
  Targets_List ignored_targets;

  Build_Plan (Memory_Arena &arena)
    : selected_targets { arena },
      ignored_targets  { arena }
  {}
};

static Build_Plan prepare_build_plan (Memory_Arena &arena, const Project &project, const Build_Config &config) {
  Build_Plan plan(arena);
  
  if (is_empty(config.selected_targets)) {
    for (int idx = 0; auto target: project.targets) {
      list_push_copy(plan.selected_targets, new(arena) Target_Tracker(target));
    }

    return plan;
  }

  List<Target *> build_list(arena);
  auto add_build_target = [&] (Target *target) {
    auto fixpoint = [&] (Target *target, auto &thunk) -> void {
      for (auto upstream: target->depends_on)
        thunk(const_cast<Target *>(upstream), thunk);

      for (auto it: build_list) {
        if (compare_strings(it->name, target->name)) return;
      }

      list_push_copy(build_list, target);
    };
    
    fixpoint(target, fixpoint);
  };

  for (auto &target_name: config.selected_targets) {
    Target *target = nullptr;
    for (auto it: project.targets) {
      if (compare_strings(it->name, target_name)) {
        target = it;
        break;
      }
    }

    if (target == nullptr) panic("Target '%' not found in the project", target_name);

    add_build_target(target);
  }

  auto trackers = reserve_memory<Target_Tracker *>(arena, build_list.count);
  for (usize idx = 0; auto target: build_list) {
    trackers[idx++] = new (arena) Target_Tracker(target);
  }

  return plan;
}

u32 build_project (Memory_Arena &arena, const Project &project, Build_Config config) {
  using enum File_System_Flags;

  if (is_empty(project.targets)) return 0;

  out_folder_path    = make_file_path(arena, project.output_location_path, "out");
  object_folder_path = make_file_path(arena, project.output_location_path, "obj");

  create_resource(out_folder_path,    Resource_Type::Directory).expect();
  create_resource(object_folder_path, Resource_Type::Directory).expect();

  registry_enabled = !project.registry_disabled && config.cache != Build_Config::Cache_Behavior::Off;
  if (registry_enabled && config.cache == Build_Config::Cache_Behavior::On) {
    auto registry_file_path = make_file_path(arena, project.output_location_path, "__registry");
    registry = load_registry(arena, registry_file_path);
  }

  if (registry_enabled && config.cache != Build_Config::Cache_Behavior::Off) {
    update_set = init_update_set(arena, registry, project); 
  }

  auto task_system = create_task_system(arena, project, config);

  chain_status_cache = Slice(reserve_memory<Chain_Status>(arena, max_supported_files_count), max_supported_files_count);

  auto build_plan = prepare_build_plan(arena, project, config);

  for (auto tracker: build_plan.selected_targets) { // TODO: This should follow a build plan
    auto target = tracker->target;

    if (target->files.count == 0) {
      print("Target '%' doesn't have any input files and will be skipped\n", target->name);

      continue;
    }

    create_resource(get_target_object_folder_path(arena, *target), Resource_Type::Directory)
      .expect(format_string(arena, "Couldn't create a build output directory for the target '%'", target->name));

    /*
    bool should_build_target = (target->tracker != nullptr);

    for (auto file_path: target->files) {
      auto file = open_file(file_path)
        .take("Couldn't open target's file");

      auto dependencies_updated = true;
      if (context.registry_enabled) {
        auto local = arena;

        List<File_Path> include_paths(local);
        for (auto path: project.project_options.include_paths) list_push_copy(include_paths, path);
        for (auto path: target->include_paths)                list_push_copy(include_paths, path);

        auto scan_result     = scan_file_dependencies(local, file, include_paths);
        dependencies_updated = !scan_result.status || scan_result.value;
      }

      if (should_build_target) {
        task_system.add_task({
          .type    = Build_Task::Compile,
          .tracker = target->tracker,
          .file    = file,
          .dependencies_updated = dependencies_updated,
        });
      }
    }
    */
  }


  /*
    For the targets that won't be build, copy whatever information is in the registry right now into the update set.
    Not doing this, would cause the registry to be overwritten with whatever was saved in the update set.
  */
  /*
  for (auto target: build_plan.skipped_targets) {
    auto last_info = reinterpret_cast<Registry::Target_Info *>(target->last_info);
    if (last_info == nullptr) continue;

    auto info = reinterpret_cast<Registry::Target_Info *>(target->info);

    copy_memory(update_set.files + last_info->files_offset,
                registry.records.files + last_info->files_offset,
                last_info->files_count.value);

    copy_memory(update_set.file_records + last_info->files_offset,
                registry.records.file_records + last_info->files_offset,
                last_info->files_count.value);

    *info = *last_info;
  }
  */

  /*
  while (build_queue.has_unfinished_tasks()) builders.execute_task(arena);

  if (registry_enabled) flush_registry(&registry, &update_set);

  for (auto tracker: build_plan) {
    fassert(tracker->compile_status.value != Target_Compile_Status::Compiling,
            "INVALID STATE: Target '%' is still waiting for one or more files to be built",
            tracker->target->name);

    fassert(tracker->link_status.value != Target_Link_Status::Waiting,
            "INVALID STATE: Target '%' is still waiting on one or more of its dependencies",
            tracker->target->name);

    if ((tracker->compile_status.value != Target_Compile_Status::Success) ||
        (tracker->link_status.value    != Target_Link_Status::Success)) {
      trap("Building target '%' finished with errors", tracker->target->name);
    }
  }
  */

  return 0;
}

