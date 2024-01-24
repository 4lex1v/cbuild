
#include "anyfin/base.hpp"
#include "anyfin/atomics.hpp"
#include "anyfin/seq.hpp"
#include "anyfin/list.hpp"
#include "anyfin/math.hpp"
#include "anyfin/result.hpp"
#include "anyfin/string_builder.hpp"
#include "anyfin/array.hpp"
#include "anyfin/platform.hpp"
#include "anyfin/commands.hpp"
#include "anyfin/file_system.hpp"

#include "cbuild_api.hpp"
#include "dependency_iterator.hpp"
#include "driver.hpp"
#include "registry.hpp"
#include "target_builder.hpp"
#include "task_system.hpp"
#include "toolbox.hpp"

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
  const Target &target;

  Atomic<Target_Compile_Status>   compile_status  { Target_Compile_Status::Compiling };
  Atomic<Target_Link_Status>      link_status     { Target_Link_Status::Waiting };
  Atomic<Upstream_Targets_Status> upstream_status { Upstream_Targets_Status::Ignore };

  cau32 skipped_counter    { 0 };
  cas32 files_pending      { static_cast<s32>(target.files.count) };
  cas32 waiting_on_counter { static_cast<s32>(target.depends_on.count) };

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

  Target_Tracker (Target &_target)
    : target { _target }
  {
    fin_ensure(_target.build_context.tracker == nullptr);
  }

  void * operator new (usize size, Memory_Arena &arena) {
    return reserve<Target_Tracker>(arena);
  }
};

struct Build_Task {
  enum struct Type: u32 { Uninit, Compile, Link };
  using enum Type;

  /*
    REMINDER: The ordering of these fields is important to keep tasks separated on cache lines.
   */
  Type type;
  bool dependencies_updated;
  Target_Tracker *tracker;
  File file;
};

static File_Path out_folder_path;
static File_Path object_folder_path;

static Registry   registry {};
static bool       registry_enabled;
static Update_Set update_set {};

static Option<File_Path> try_resolve_include_path (Memory_Arena &arena, File_Path path, const Option<File_Path> &folder, Slice<File_Path> include_paths) {
  if (folder) {
    auto current_folder_path = make_file_path(arena, folder.value, path);
    if (auto check = check_file_exists(current_folder_path); check.is_ok() && check.value)
      return current_folder_path;
  }

  for (auto &prefix: include_paths) {
    auto full_path = make_file_path(arena, prefix, path);
    auto [error, exists] = check_file_exists(full_path);
    if (error || !exists) {
      if (error) log("WARNING: System error occured while checking file %\n", error.value);
      continue;
    }

    return full_path;
  }

  return opt_none;
}

static Chain_Status scan_dependency_chains (Memory_Arena &arena, Array<Chain_Status> &chain_status_cache, const File &source_file, Slice<File_Path> include_directories) {
  using enum Chain_Status;

  const auto &records = registry.records;

  auto file_id = unwrap(get_file_id(source_file));

  if (auto result = find_offset(get_dependencies(update_set), file_id); result) {
    return chain_status_cache[result.value];
  }

  const usize index = update_set.header->dependencies_count++;

  update_set.dependencies[index] = file_id;
  chain_status_cache[index]      = Chain_Status::Checking;

  fin_ensure(chain_status_cache[index] == Chain_Status::Checking);

  Option<File_Path> source_file_folder_path = opt_none;
  {
    auto [error, path] = get_folder_path(arena, source_file.path);
    if (!error) source_file_folder_path = Option(move(path));
    else log("ERROR: Couldn't resolve parent folder for the source file '%' due to a system error: %. "
               "Build process will continue, but this may cause issues with include files lookup.",
               source_file.path, error);
  }

  bool chain_has_updates = false;

  auto iterator = Dependency_Iterator(source_file, unwrap(map_file_into_memory(source_file)));
  while (auto include_value = get_next_include_value(iterator)) {
    auto local = arena;

    auto [is_defined, resolved_path] = try_resolve_include_path(local, include_value.value, source_file_folder_path, include_directories);
    if (!is_defined) {
      String_Builder builder { local };
      builder.add(local, Callsite(), ": Couldn't resolve the include file ", include_value, " from file ", source_file.path, " the following paths were checked:\n");
      for (auto &path: include_directories) builder.add(local, "  - ", path, "\n");      

      log(build_string(local, builder));

      continue;
    }

    auto [open_error, dependency_file] = open_file(move(resolved_path));
    if (open_error) log("Couldn't open included header file for scanning due to an error: %", open_error.value);

    defer { close_file(dependency_file); };
    
    auto chain_scan_result = scan_dependency_chains(local, chain_status_cache, dependency_file, include_directories);
    fin_ensure(chain_scan_result != Chain_Status::Unchecked);

    if (chain_scan_result == Chain_Status::Checked_Has_Updates) chain_has_updates = true;
  }

  auto timestamp = unwrap(get_last_update_timestamp(source_file));

  {
    // Attempt to find the offset for the given file_id if the chain has no updates.
    auto [offset_found, offset] = !chain_has_updates ? find_offset(get_dependencies(registry), file_id) : opt_none;
    if (offset_found) {
      auto &record = records.dependency_records[offset];
      chain_has_updates = (timestamp != record.timestamp);
    }
    else {
      /*
        If the chain already has updates or the file_id is not found in the records, it implies that this is a new dependency.
        This requires a rebuild of the source file, hence we set chain_has_updates to true.
      */
      chain_has_updates = true;
    }
  }

  auto status = chain_has_updates ? Checked_Has_Updates : Checked_No_Updates;

  update_set.dependency_records[index] = Registry::Record { .timestamp = timestamp };
  chain_status_cache[index]            = status;

  return status;
}

static bool scan_file_dependencies (Memory_Arena &arena, Array<Chain_Status> &chain_status_cache, const File &source_file, Slice<File_Path> include_directories) {
  bool chain_has_updates = false;

  auto file_mapping = unwrap(map_file_into_memory(source_file));
  defer { unmap_file(file_mapping); };

  Option<File_Path> source_file_folder_path = opt_none;
  {
    auto [error, path] = get_folder_path(arena, source_file.path);
    if (!error) source_file_folder_path = Option(move(path));
    else log("ERROR: Couldn't resolve parent folder for the source file '%' due to a system error: %. "
             "Build process will continue, but this may cause issues with include files lookup.",
             source_file.path, error.value);
  }

  auto iterator = Dependency_Iterator(source_file, file_mapping);
  while (auto include_value = get_next_include_value(iterator)) {
    auto local = arena;
    
    auto [is_defined, resolved_path] = try_resolve_include_path(local, include_value.value, source_file_folder_path, include_directories);
    if (!is_defined) {
      String_Builder builder { local };
      builder.add(local, "Couldn't resolve the include file ", include_value.value, " from file ", source_file.path, " the following paths were checked:\n");
      for (auto &path: include_directories) builder.add(local, "  - ", path, "\n");      

      log("%\n", build_string(local, builder));

      continue;
    }

    auto [open_error, dependency_file] = open_file(resolved_path);
    if (open_error) {
      log("WARNING: Couldn't open included header file for scanning due to a system error: %.", open_error.value);
      chain_has_updates = true;
      continue;
    }

    defer { close_file(dependency_file); };

    auto chain_scan_result = scan_dependency_chains(local, chain_status_cache, dependency_file, include_directories);
    fin_ensure(chain_scan_result != Chain_Status::Unchecked);

    if (chain_scan_result == Chain_Status::Checked_Has_Updates) chain_has_updates = true;
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

struct Target_Builder_Context {
  constexpr static auto RESERVATION_SIZE = megabytes(1);

  Memory_Arena arena;

  /*
    REMINDER:
      This constructor will be called on a dedicated builder thread, by the task system and
      owned by that thread.
   */
  Target_Builder_Context ()
    : arena { reserve_virtual_memory(RESERVATION_SIZE) }
  {}

  /*
    This version would only be used for a short period of time on the main thread,
    while it executes some of the tasks itself. In this context it's fine to use a local
    copy of the arena.
   */
  Target_Builder_Context (Memory_Arena _arena)
    : arena { _arena }
  {}
};

using Build_System = Task_System<Build_Task, Target_Builder_Context>;

static void schedule_downstream_linkage (Build_System &build_system, const Target &target, const Invocable<void, Target_Tracker &> auto &update_tracker) {
  for (auto downstream: target.required_by) {
    auto downstream_tracker = downstream->build_context.tracker;

    // In targetted builds, target may not have an associated tracker, thus we skip these
    if (downstream_tracker == nullptr) continue;

    update_tracker(*downstream_tracker);

    if ((atomic_fetch_sub(downstream_tracker->waiting_on_counter, 1) - 1) == 0) {
      build_system.add_task(Build_Task {
        .type    = Build_Task::Type::Link,
        .tracker = downstream_tracker
      });
    }
  }
}

static File_Path get_target_object_folder_path (Memory_Arena &arena, const Target &target) {
  auto external = target.flags.external ? target.project.name : "";
  return make_file_path(arena, object_folder_path, external, target.name);
}

static File_Path get_target_output_folder_path (Memory_Arena &arena, const Target &target) {
  auto external = target.flags.external ? target.project.name : "";
  return make_file_path(arena, out_folder_path, external, target.name);
}

static void link_target (Memory_Arena &arena, Build_System &build_system, Target_Tracker &tracker) {
  using TLS = Target_Link_Status;

  const u32 thread_id = get_current_thread_id();

  const auto &target  = tracker.target;
  const auto &project = target.project;

  auto target_compilation_status = atomic_load(tracker.compile_status);
  if (target_compilation_status == Target_Compile_Status::Compiling) {
    if (global_flags.tracing) log("TRACE(#%): target % is still compiling and couldn't be linked\n", thread_id, target.name);
    return;
  }

  /*
    For targets that have upstream dependencies, we must ensure that all upstreams were processed and finalized their
    statuses, before linking this target. If there are upstream that we must wait on, this link task must be rescheduled.

    `waiting_on_counter` > 0, means that when some other thread decrements this counter to zero, a linking task will be
    submitted to the queue by that thread.
  */
  if (auto counter = atomic_load<Memory_Order::Acquire>(tracker.waiting_on_counter); counter > 0) {
    if (global_flags.tracing) log("TRACE(#%): Target % is waiting on % more targets to be linked\n", thread_id, target.name, counter);
    return;
  }

  if (!atomic_compare_and_set(tracker.link_status, TLS::Waiting, TLS::Linking)) return;

  auto upstream_status = atomic_load(tracker.upstream_status);
  if ((target_compilation_status == Target_Compile_Status::Failed) ||
      (upstream_status           == Upstream_Targets_Status::Failed)) {
    atomic_store(tracker.link_status, TLS::Failed);

    schedule_downstream_linkage(build_system, target, [] (Target_Tracker &tracker) {
      atomic_store(tracker.upstream_status, Upstream_Targets_Status::Failed);
    });

    return;
  }

  enum struct Link_Result { Ignore, Success, Failed };
  auto link_result = Link_Result::Ignore;

  auto needs_linking = tracker.needs_linking || upstream_status == Upstream_Targets_Status::Updated;
  if (needs_linking) {
    if (!global_flags.silenced) log("Linking target: %\n", target.name);

    auto target_object_folder = get_target_object_folder_path(arena, target);
    auto target_output_folder = get_target_output_folder_path(arena, target);
    auto output_file_path     = get_output_file_path_for_target(arena, target);
    auto object_extension     = get_object_extension();

    ensure(create_directory(target_output_folder));

    String_Builder builder { arena };

    /*
      Clang has some stack uncertainties compiling this function, not sure why. Since each switch
      branch does pretty much the same, took some commong parts out, which aparently makes Clang
      happier.
     */
    switch (target.type) {
      case Target::Static_Library: {
        builder += String(project.toolchain.archiver_path);
        builder += project.archiver;
        builder += target.archiver;
        break;
      };
      case Target::Shared_Library: {
        builder += String(project.toolchain.linker_path);
        builder += is_win32() ? String("/dll") : String("-shared");
        builder += project.linker;
        builder += target.linker;
        break;
      };
      case Target::Executable: {
        builder += String(project.toolchain.linker_path);
        builder += project.linker;
        builder += target.linker;
        break;
      };
    }

    for (auto &path: target.files) {
      auto file_name = concat_string(arena, unwrap(get_resource_name(path)), ".", object_extension);
      builder += make_file_path(arena, target_object_folder, file_name);
    }

    for (auto upstream_target: target.depends_on) {
      fin_ensure(atomic_load(upstream_target->build_context.tracker->link_status) == Target_Link_Status::Success);
        
      String lib_extension = "lib"; // on Win32 static and import libs for dlls have the same extension
      if (!is_win32()) lib_extension = (upstream_target->type == Target::Static_Library) ? String("a") : String("so");
        
      auto file_name = concat_string(arena, upstream_target->name, ".", lib_extension);
      builder += make_file_path(arena, out_folder_path, file_name);
    }

    builder += target.link_libraries;
    builder += concat_string(arena, is_win32() ? "/OUT:" : "-o ", output_file_path);

    auto link_command = build_string_with_separator(arena, builder, ' ');
    if (global_flags.tracing) log("Linking target % with %\n", target.name, link_command);

    auto [error, status] = run_system_command(arena, link_command);
    if (error) {
      log("WARNING: Target linking failed due to a system error: %, command: %\n", error.value, link_command);
      link_result = Link_Result::Failed;
    }
    else if (status.status_code != 0) {
      log("WARNING: Target linking failed with status: %, command: %\n", status.status_code, link_command);
      if (status.output) log(concat_string(arena, status.output, "\n"));
      link_result = Link_Result::Failed;
    }
    else {
      if (status.output) log(concat_string(arena, status.output, "\n"));
      link_result = Link_Result::Success;
    }
  }

  auto target_link_status = (link_result == Link_Result::Failed) ? TLS::Failed : TLS::Success;
  atomic_store(tracker.link_status, target_link_status);

  schedule_downstream_linkage(build_system, target, [link_result] (Target_Tracker &tracker) {
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
  const auto &target    = tracker.target;
  const auto &project   = target.project;
  const auto &toolchain = project.toolchain;

  auto target_info      = reinterpret_cast<Registry::Target_Info *>(target.build_context.info);
  auto target_last_info = reinterpret_cast<Registry::Target_Info *>(target.build_context.last_info);

  auto file_id   = unwrap(get_file_id(file));
  auto timestamp = unwrap(get_last_update_timestamp(file));

  auto target_object_folder = get_target_object_folder_path(arena, target);
  auto object_file_path     = make_file_path(arena, target_object_folder, concat_string(arena, unwrap(get_resource_name(file.path)), ".", get_object_extension()));

  bool should_rebuild = true;
  if (!project.rebuild_required && !dependencies_updated && target.build_context.last_info) {
    auto &records = registry.records;

    auto section      = records.files + target_last_info->files_offset;
    auto section_size = target_last_info->files_count.value;

    auto [record_found, index] = find_offset(Array(section, section_size), file_id);
    if (record_found) {
      auto record_index     = target_last_info->files_offset + index;
      auto record_timestamp = records.file_records[record_index].timestamp;

      auto [error, exists] = check_file_exists(object_file_path);

      should_rebuild = (timestamp != record_timestamp) || (error || !exists);
    }
  }

  enum struct File_Compile_Status { Ignore, Success, Failed };
  auto file_compilation_status = File_Compile_Status::Ignore;

  if (should_rebuild) {
    if (!global_flags.silenced) log("Building file: %\n", file.path);

    auto is_cpp_file = ends_with(file.path, "cpp");
    auto _msvc       = is_msvc(toolchain);

    String_Builder builder { arena };
    builder += is_cpp_file ? project.toolchain.cpp_compiler_path : project.toolchain.c_compiler_path;
    builder += project.compiler;
    builder += target.compiler;

    project.include_paths.for_each([&] (auto &path) {
      builder += concat_string(arena, _msvc ? "/I" : "-I ", "\"", path, "\"");
    });

    target.include_paths.for_each([&] (auto &path) {
      builder += concat_string(arena, _msvc ? "/I" : "-I ", "\"", path, "\"");
    });

    builder += concat_string(arena, _msvc ? "/c " : "-c ", "\"", file.path, "\"");
    builder += concat_string(arena, _msvc ? "/Fo" : "-o ", "\"", object_file_path, "\"");

    auto compilation_command = build_string_with_separator(arena, builder, ' ');
    if (global_flags.tracing) log("Building file % with: %\n", file.path, compilation_command);

    auto [error, status] = run_system_command(arena, compilation_command);
    if (error) {
      log("WARNING: File compilation failed due to a system error: %, command: %\n", error.value, compilation_command);
      file_compilation_status = File_Compile_Status::Failed;
    }
    else if (status.status_code != 0) {
      log("WARNING: File compilation failed with status: %, command: %\n", status.status_code, compilation_command);
      if (status.output) log(concat_string(arena, status.output, "\n"));
      file_compilation_status = File_Compile_Status::Failed;
    }
    else {
      if (status.output) log(concat_string(arena, status.output, "\n"));
      file_compilation_status = File_Compile_Status::Success;
    }
  }
  else {
    atomic_fetch_add(tracker.skipped_counter, 1);
  }

  if (registry_enabled && file_compilation_status != File_Compile_Status::Failed) {
    auto index = atomic_fetch_add(target_info->files_count, 1);
    fin_ensure(index < target_info->aligned_max_files_count);

    auto update_set_index = target_info->files_offset + index;

    fin_ensure(update_set.files[update_set_index] == 0);
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
    log("Target '%' couldn't be linked because of compilation errors\n", target.name);
    return;
  }

  auto skipped_count = atomic_load(tracker.skipped_counter);
  auto needs_linking = skipped_count < target.files.count;
  if (!needs_linking) {
    // If no files were compiled, check that the binary exists
    auto output_file_path = get_output_file_path_for_target(arena, target);
    needs_linking         = unwrap(check_file_exists(output_file_path)); 
  }

  tracker.needs_linking = needs_linking;

  fin_ensure(compile_status == Target_Compile_Status::Compiling);
  atomic_store<Memory_Order::Release>(tracker.compile_status, Target_Compile_Status::Success);
}

static void build_target_task (Build_System &build_system, Target_Builder_Context &context, Build_Task &task) {
  reset_arena(context.arena);

  const u32 thread_id = get_current_thread_id();

  auto &tracker = *task.tracker;
  auto &target  = tracker.target;

  switch (task.type) {
    case Build_Task::Type::Uninit: return;
    case Build_Task::Type::Compile: {
      if (global_flags.tracing)
        log("TRACE(#%): Picking up file % for target % for compilation\n",
              thread_id, task.file.path, target.name);

      compile_file(context.arena, tracker, task.file, task.dependencies_updated);

      auto status = atomic_load(tracker.compile_status);
      if (status == Target_Compile_Status::Compiling) break;

      task.type = Build_Task::Link;
      build_system.add_task(move(task));

      break;
    }
    case Build_Task::Type::Link: {
      if (global_flags.tracing)
        log("TRACE(#%): Picking up target % for linkage\n", thread_id, target.name);

      link_target(context.arena, build_system, tracker);
      break;
    }
  }
}

static u32 number_of_extra_builders_to_spawn (const Build_Config &config) {
  // This number excludes main thread, which always exists
  auto cpu_count = get_logical_cpu_count();
  auto count     = clamp<u32>(config.builders_count, 1, cpu_count);

  // This number specifies only the count of extra threads in addition to the main thread
  return count - 1;
}

static auto create_task_system (Memory_Arena &arena, const Project &project, const Build_Config &config) {
  const auto queue_size = project.targets.count + project.total_files_count;

  const auto builders_count = number_of_extra_builders_to_spawn(config);

  return Build_System(arena, queue_size, builders_count, build_target_task);
}

struct Build_Plan {
  List<Target_Tracker> selected_targets;
  List<Target *>       skipped_targets;

  Build_Plan (Memory_Arena &arena)
    : selected_targets { arena },
      skipped_targets  { arena }
  {}
};

static Build_Plan prepare_build_plan (Memory_Arena &arena, const Project &project, const Build_Config &config) {
  Build_Plan plan(arena);
  
  if (is_empty(config.selected_targets)) {
    for (auto &it: project.targets) {
      it.build_context.tracker = &list_push(plan.selected_targets, it);
    }
      
    return plan;
  }

  List<Target *> build_list { arena };
  auto add_build_target = [] (this auto self, List<Target *> &list, Target *target) -> void {
    for (auto upstream: target->depends_on) self(list, upstream);
    for (auto it: list) if (it->name == target->name) return;

    list_push_copy(list, target);
  };

  for (auto target_name: config.selected_targets) {
    Target *selected_target = nullptr;
    for (auto &target: project.targets) {
      if (target.name == target_name) {
        selected_target = &target;
        break;
      }
    }

    if (!selected_target) panic("Target '%' not found in the project", target_name);

    add_build_target(build_list, selected_target);
  }

  for (auto target: build_list) {
    target->build_context.tracker = &list_push(plan.selected_targets, Target_Tracker(*target));
  }

  for (auto &target: project.targets) {
    if (plan.selected_targets.contains([&] (auto it) { return it.target.name == target.name; })) continue;
    list_push_copy(plan.skipped_targets, &target);
  }

  return plan;
}

static void validate_toolchain (const Project &project) {
  const auto &tc = project.toolchain;

  if (!tc.c_compiler_path)   panic("C compiler path is not set for the project\n");
  if (!tc.cpp_compiler_path) panic("C++ compiler path is not set for the project\n");
  if (!tc.linker_path)       panic("Linker path is not set for the project\n");
  if (!tc.archiver_path)     panic("Archive tool is not set for the project\n");

  if (!check_file_exists(tc.c_compiler_path).or_default(false))   panic("No C compiler found at %\n", tc.c_compiler_path);
  if (!check_file_exists(tc.cpp_compiler_path).or_default(false)) panic("No C++ compiler found at %\n", tc.cpp_compiler_path);
  if (!check_file_exists(tc.linker_path).or_default(false))       panic("No linker found at %\n", tc.linker_path);
  if (!check_file_exists(tc.archiver_path).or_default(false))     panic("No archive tool found at %\n", tc.archiver_path);
}

u32 build_project (Memory_Arena &arena, const Project &project, Build_Config config) {
  using enum File_System_Flags;

  validate_toolchain(project);

  if (is_empty(project.targets)) return 0;

  ensure(create_directory(project.build_location_path, File_System_Flags::Force));

  out_folder_path    = make_file_path(arena, project.build_location_path, "out");
  object_folder_path = make_file_path(arena, project.build_location_path, "obj");

  ensure(create_directory(out_folder_path));
  ensure(create_directory(object_folder_path));

  registry_enabled = !project.registry_disabled && config.cache != Build_Config::Cache_Behavior::Off;
  if (registry_enabled && config.cache == Build_Config::Cache_Behavior::On) {
    auto registry_file_path = make_file_path(arena, project.build_location_path, "__registry");
    registry = load_registry(arena, registry_file_path);
  }

  if (registry_enabled && config.cache != Build_Config::Cache_Behavior::Off) {
    update_set = init_update_set(arena, registry, project); 
  }

  auto task_system = create_task_system(arena, project, config);
  //defer { shutdown(task_system); };

  auto deps_cache  = reserve_array<Chain_Status>(arena, max_supported_files_count);
  auto build_plan  = prepare_build_plan(arena, project, config);

  for (auto &tracker: build_plan.selected_targets) {
    const auto &target = tracker.target;

    if (target.files.count == 0) {
      log("Target '%' doesn't have any input files and will be skipped\n", target.name);
      continue;
    }

    ensure(create_directory(get_target_object_folder_path(arena, target)));

    for (const auto &file_path: target.files) {
      Build_Task task {
        .type    = Build_Task::Compile,
        /*
          If the registry is disabled, it's expected that we do full rebuild of the project everytime, thus
          we consider that dependencies were updated (because we are not going to walk the includes tree),
          which triggers rebuild of every file. If registry is enabled, we can consider that dependencies
          were not updated and the scanner would tell us otherwise, if there were any changes.
        */
        .dependencies_updated = !registry_enabled,
        .tracker = &tracker,
        .file    = unwrap(open_file(copy_string(arena, file_path))),
      };

      if (registry_enabled) {
        auto local = arena;

        auto include_paths = reserve_seq<File_Path>(local, target.include_paths.count + project.include_paths.count);

        for (auto &path: target.include_paths)  seq_push_copy(include_paths, path);
        for (auto &path: project.include_paths) seq_push_copy(include_paths, path);

        task.dependencies_updated = scan_file_dependencies(local, deps_cache, task.file, include_paths);
      }

      task_system.add_task(move(task));
    }
  }

  /*
    For the targets that won't be build, copy whatever information is in the registry right now into the update set.
    Not doing this, would cause the registry to be overwritten with whatever was saved in the update set.
  */
  for (auto target: build_plan.skipped_targets) {
    auto last_info = reinterpret_cast<Registry::Target_Info *>(target->build_context.last_info);
    if (!last_info) continue;

    auto info = reinterpret_cast<Registry::Target_Info *>(target->build_context.info);

    copy_memory(update_set.files + last_info->files_offset,
                registry.records.files + last_info->files_offset,
                last_info->files_count.value);

    copy_memory(update_set.file_records + last_info->files_offset,
                registry.records.file_records + last_info->files_offset,
                last_info->files_count.value);

    *info = *last_info;
  }

  auto main_thread_local_context = Target_Builder_Context(make_sub_arena(arena, Target_Builder_Context::RESERVATION_SIZE));
  while (task_system.has_unfinished_tasks())
    task_system.execute_task(main_thread_local_context);

  if (registry_enabled) flush_registry(registry, update_set);

  for (auto tracker: build_plan.selected_targets) {
    fin_ensure(tracker.compile_status.value != Target_Compile_Status::Compiling);
    fin_ensure(tracker.link_status.value != Target_Link_Status::Waiting);

    if ((tracker.compile_status.value != Target_Compile_Status::Success) ||
        (tracker.link_status.value    != Target_Link_Status::Success)) {
      log("Building target '%' finished with errors\n", tracker.target.name);
    }
  }

  return 0;
}

