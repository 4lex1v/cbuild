
#include "anyfin/base.hpp"
#include "anyfin/atomics.hpp"
#include "anyfin/list.hpp"
#include "anyfin/math.hpp"
#include "anyfin/result.hpp"
#include "anyfin/string_builder.hpp"
#include "anyfin/array.hpp"
#include "anyfin/array_ops.hpp"
#include "anyfin/platform.hpp"
#include "anyfin/commands.hpp"
#include "anyfin/file_system.hpp"
#include "anyfin/threads.hpp"
#include "anyfin/concurrent.hpp"

#include "cbuild_api.hpp"
#include "scanner.hpp"
#include "registry.hpp"
#include "builder.hpp"

extern bool tracing_enabled_opt;
extern bool silence_logs_opt;

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
};

struct Build_Task {
  enum struct Type: u32 { Uninit, Compile, Link };
  using enum Type;

  /*
    REMINDER: The ordering of these fields is important to keep individual tasks separate on cache lines.
  */
  Type type;
  b32  dependencies_updated;
  Target_Tracker *tracker;
  File file;
};

struct Build_System {
  struct Node {
    Build_Task task;
    as32 sequence_number;

    char cache_line_pad[CACHE_LINE_SIZE - sizeof(Build_Task) - sizeof(as32)];
  };

  static_assert(sizeof(Node) == CACHE_LINE_SIZE);

  static constexpr usize RESERVATION_SIZE = megabytes(1);

  Array<Node> queue;
  cas64 write_index = 0;
  cas64 read_index  = 0;
  cau32 submitted   = 0;
  cau32 completed   = 0;

  Array<Thread> builders {};
  Semaphore     tasks_available {};

  Build_System (Memory_Arena &arena, const usize queue_size, const usize builders_count)
    : queue { reserve_array<Node>(arena, align_forward_to_pow_2(queue_size)) }
  {
    for (s32 idx = 0; auto &node: this->queue) node.sequence_number = idx++;

    if (builders_count) {
      tasks_available = unwrap(create_semaphore(), "Failed to create a semaphore resource for the build queue\n");
      builders        = reserve_array<Thread>(arena, builders_count);

      for (auto &builder: builders) builder = unwrap(spawn_thread(task_system_loop, this)); 
    }
  }

  static void task_system_loop (Build_System *system) {
    Memory_Arena builder_arena { reserve_virtual_memory(RESERVATION_SIZE) };

    while (true) {
      wait_for_semaphore_signal(system->tasks_available);

      reset_arena(builder_arena);
      system->execute_task(builder_arena);
    }
  }

  void execute_task (this Build_System &self, Memory_Arena &arena) {
    auto [defined, task] = self.pull_next_task_for_execution();
    if (!defined) return;

    void build_target_task (Memory_Arena&, Build_System&, Build_Task);
    build_target_task(arena, self, move(task));

    atomic_fetch_add(self.completed, 1);
  }

  bool has_unfinished_tasks (this const auto &self) {
    auto completed = atomic_load(self.completed);
    auto submitted = atomic_load(self.submitted);

    fin_ensure(completed <= submitted);

    return (submitted != completed);
  }

  Option<Build_Task> pull_next_task_for_execution (this Build_System &self) {
    using enum Memory_Order;
  
    auto index = atomic_load(self.read_index);

    const auto tasks_count = self.queue.count;
    const auto mask        = tasks_count - 1;

    Node *node = nullptr;
    while (true) {
      node = &self.queue[index & mask];

      auto sequence = atomic_load<Acquire>(node->sequence_number);
      auto diff     = sequence - (index + 1);

      if (diff == 0) {
        if (atomic_compare_and_set<Relaxed, Relaxed>(self.read_index, index, index + 1)) break;
      }
      else if (diff < 0) return {};
      else index = atomic_load(self.read_index);
    }

    auto task = move(node->task);

    atomic_store<Release>(node->sequence_number, index + tasks_count);

    return Option(move(task));
  }

  void submit_task (this Build_System &self, Build_Task &&task) {
    using enum Memory_Order;
  
    auto index = atomic_load(self.write_index);

    const auto tasks_count = self.queue.count;
    const auto mask        = tasks_count - 1;

    Node *node = nullptr;
    while (true) {
      node = &self.queue[index & mask];

      auto sequence = atomic_load<Acquire>(node->sequence_number);
      auto diff     = sequence - index;
    
      if (diff == 0) {
        if (atomic_compare_and_set(self.write_index, index, index + 1)) break;
      }
      else if (diff < 0) continue;
      else index = atomic_load(self.write_index);
    }

    /*
      The submitted count is only checked to see if there are unfinished tasks in the queue or not,
      so we want to increment it as early as possible.
    */
    atomic_fetch_add(self.submitted, 1);

    zero_memory(&node->task);
    node->task = move(task);

    atomic_store<Release>(node->sequence_number, index + 1);
    increment_semaphore(self.tasks_available);
  }
};

static File_Path out_folder_path;
static File_Path object_folder_path;

static Registry   registry {};
static bool       registry_enabled;
static Update_Set update_set {};

static bool is_msvc (Toolchain_Type type) {
  return ((type == Toolchain_Type_MSVC_X86) ||
          (type == Toolchain_Type_MSVC_X64) ||
          (type == Toolchain_Type_LLVM_CL));
}

static bool is_msvc (const Toolchain_Configuration &config) {
  return is_msvc(config.type);
}

static void schedule_downstream_linkage (Build_System &build_system, const Target &target, const Invocable<void, Target_Tracker &> auto &update_tracker) {
  for (auto downstream: target.required_by) {
    auto downstream_tracker = downstream->build_context.tracker;

    // In targetted builds, target may not have an associated tracker, thus we skip these
    if (downstream_tracker == nullptr) continue;

    update_tracker(*downstream_tracker);

    if ((atomic_fetch_sub(downstream_tracker->waiting_on_counter, 1) - 1) == 0) {
      build_system.submit_task(Build_Task {
        .type    = Build_Task::Type::Link,
        .tracker = downstream_tracker
      });
    }
  }
}

static void link_target (Memory_Arena &arena, Build_System &build_system, Target_Tracker &tracker) {
  using TLS = Target_Link_Status;

  const u32 thread_id = get_current_thread_id();

  const auto &target  = tracker.target;
  const auto &project = target.project;

  auto target_compilation_status = atomic_load(tracker.compile_status);
  if (target_compilation_status == Target_Compile_Status::Compiling) {
    if (tracing_enabled_opt) log("TRACE(#%): target % is still compiling and couldn't be linked\n", thread_id, target.name);
    return;
  }

  /*
    For targets that have upstream dependencies, we must ensure that all upstreams were processed and finalized their
    statuses, before linking this target. If there are upstream that we must wait on, this link task must be rescheduled.

    `waiting_on_counter` > 0, means that when some other thread decrements this counter to zero, a linking task will be
    submitted to the queue by that thread.
  */
  if (auto counter = atomic_load<Memory_Order::Acquire>(tracker.waiting_on_counter); counter > 0) {
    if (tracing_enabled_opt) log("TRACE(#%): Target % is waiting on % more targets to be linked\n", thread_id, target.name, counter);
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
  if (!needs_linking) {
    if (tracing_enabled_opt) log("Target '%' linking cancelled, linking is not required\n", target.name);
  }
  else {
    if (!silence_logs_opt) log("Linking target: %\n", target.name);

    auto output_file_name     = concat_string(arena, target.name, ".", get_target_extension(target));
    auto target_object_folder = make_file_path(arena, object_folder_path, target.name);
    auto output_file_path     = make_file_path(arena, out_folder_path, output_file_name);

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

    for (auto ext = get_object_extension(); auto &path: target.files) {
      auto file_name = concat_string(arena, unwrap(get_resource_name(path)), ".", ext);
      builder += concat_string(arena, "\"", make_file_path(arena, target_object_folder, file_name), "\"");
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
    if (tracing_enabled_opt) log("Linking target % with %\n", target.name, link_command);

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

  auto target_object_folder = make_file_path(arena, object_folder_path, target.name);
  auto object_file_path     = make_file_path(arena, target_object_folder, concat_string(arena, unwrap(get_resource_name(file.path)), ".", get_object_extension()));

  bool should_rebuild = true;

  /*
    If there are existing records for the given target, we should check if this file was
    compiled previously and if there were any changes from the last time.

    - If registry has been disabled, we should always build all files.
    - If any of the included files was updated in the chain, we should also rebuild this file (unless we can get
      smarter about checking for semantic changes at some point).
    - If no previous information is available, which should be the case if we build this target for the first time,
      also rebuild.
  */
  if (!project.rebuild_required && !dependencies_updated && target_last_info) {
    auto &records = registry.records;

    auto section      = records.files + target_last_info->files_offset;
    auto section_size = target_last_info->aligned_max_files_count;//target_last_info->files_count.value;

    auto [record_found, index] = find_offset(Array(section, section_size), file_id);
    if (record_found) {
      auto record_index     = target_last_info->files_offset + index;
      auto record_timestamp = records.file_records[record_index].timestamp;

      auto [error, exists] = check_file_exists(object_file_path);

      should_rebuild = (timestamp != record_timestamp) || (error || !exists);
      if (tracing_enabled_opt && !should_rebuild) log("No changes in file %, skipping compilation\n", file.path);
    }
  }

  enum struct File_Compile_Status { Ignore, Success, Failed };
  auto file_compilation_status = File_Compile_Status::Ignore;

  if (should_rebuild) {
    if (!silence_logs_opt) log("Building file: %\n", file.path);

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
    if (tracing_enabled_opt) log("Building file % with: %\n", file.path, compilation_command);

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

    auto [error, exists] = check_file_exists(output_file_path);
    if (error) log("WARNING: Couldn't verify target output file at % due to an error: %\n", file.path, error.value);

    needs_linking = error || !exists; // force linking in case of a system error check (unlikely) or if doesn't exist
  }

  tracker.needs_linking = needs_linking;

  fin_ensure(compile_status == Target_Compile_Status::Compiling);
  atomic_store<Memory_Order::Release>(tracker.compile_status, Target_Compile_Status::Success);
}

static void build_target_task (Memory_Arena &arena, Build_System &build_system, Build_Task task) {
  const u32 thread_id = get_current_thread_id();

  auto &tracker = *task.tracker;
  auto &target  = tracker.target;

  switch (task.type) {
    case Build_Task::Type::Uninit: return;
    case Build_Task::Type::Compile: {
      if (tracing_enabled_opt)
        log("TRACE(#%): Picking up file % for target % for compilation\n",
              thread_id, task.file.path, target.name);

      compile_file(arena, tracker, task.file, task.dependencies_updated);

      auto status = atomic_load(tracker.compile_status);
      if (status == Target_Compile_Status::Compiling) break;

      task.type = Build_Task::Link;
      build_system.submit_task(move(task));

      break;
    }
    case Build_Task::Type::Link: {
      if (tracing_enabled_opt)
        log("TRACE(#%): Picking up target % for linkage\n", thread_id, target.name);

      link_target(arena, build_system, tracker);
      break;
    }
  }
}

static u32 number_of_extra_builders_to_spawn (u32 builders_count) {
  // This number excludes main thread, which always exists
  auto cpu_count = get_logical_cpu_count();
  auto count     = clamp<u32>(builders_count, 1, cpu_count);

  // This number specifies only the count of extra threads in addition to the main thread
  return count - 1;
}

static auto create_task_system (Memory_Arena &arena, const Project &project, u32 builders_count) {
  const auto queue_size = project.targets.count + project.total_files_count;
  return Build_System(arena, queue_size, number_of_extra_builders_to_spawn(builders_count));
}

struct Build_Plan {
  List<Target_Tracker> selected_targets;
  List<Target *>       skipped_targets;

  Build_Plan (Memory_Arena &arena)
    : selected_targets { arena },
      skipped_targets  { arena }
  {}
};

static Build_Plan prepare_build_plan (Memory_Arena &arena, const Project &project, const List<String> &selected_targets) {
  Build_Plan plan(arena);
  
  if (is_empty(selected_targets)) {
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

  for (auto target_name: selected_targets) {
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

u32 build_project (Memory_Arena &arena, const Project &project, const List<String> &selected_targets, Cache_Behavior cache, u32 builders_count) {
  using enum File_System_Flags;

  const bool is_targeted_build = !is_empty(selected_targets);

  validate_toolchain(project);

  if (is_empty(project.targets)) return 0;

  ensure(create_directory(project.build_location_path, File_System_Flags::Force));

  out_folder_path    = make_file_path(arena, project.build_location_path, "out");
  object_folder_path = make_file_path(arena, project.build_location_path, "obj");

  ensure(create_directory(out_folder_path));
  ensure(create_directory(object_folder_path));

  registry_enabled = !project.registry_disabled && cache != Cache_Behavior::Off;
  if (registry_enabled) {
    auto registry_file_path = make_file_path(arena, project.build_location_path, "__registry");
    registry = create_registry(registry_file_path);

    if (cache == Cache_Behavior::On) load_registry(arena, registry);

    update_set = init_update_set(arena, project, registry, is_targeted_build); 
  }

  auto task_system = create_task_system(arena, project, builders_count);
  auto build_plan  = prepare_build_plan(arena, project, selected_targets);

  Chain_Scanner scanner(arena, registry, update_set);

  List<File_Path> project_include_paths { arena };
  for (auto &path: project.include_paths) list_push_copy(project_include_paths, path);

  for (auto &tracker: build_plan.selected_targets) {
    const auto &target = tracker.target;

    if (target.files.count == 0) {
      log("Target '%' doesn't have any input files and will be skipped\n", target.name);
      continue;
    }

    ensure(create_directory(make_file_path(arena, object_folder_path, target.name)));

    for (const auto &file_path: target.files) {
      Build_Task task {
        .type = Build_Task::Compile,
        /*
          If the registry is disabled, it's expected that we do full rebuild of the project everytime, thus
          we consider that dependencies were updated (because we are not going to walk the includes tree),
          which triggers rebuild of every file. If registry is enabled, we can consider that dependencies
          were not updated and the scanner would tell us otherwise, if there were any changes.
        */
        .dependencies_updated = !registry_enabled,
        .tracker = &tracker,
        .file    = unwrap(open_file(file_path)),
      };

      if (registry_enabled) {
        auto local = arena;

        List<File_Path> include_paths(local, project_include_paths);
        for (auto &path: target.include_paths) list_push_front_copy(include_paths, path);

        task.dependencies_updated = scan_dependency_chain(local, scanner, include_paths, task.file);
      }

      task_system.submit_task(move(task));
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

    copy_memory(update_set.files        + last_info->files_offset, registry.records.files        + last_info->files_offset, last_info->files_count.value);
    copy_memory(update_set.file_records + last_info->files_offset, registry.records.file_records + last_info->files_offset, last_info->files_count.value);

    *info = *last_info;
  }

  auto main_thread_local_context = make_sub_arena(arena, Build_System::RESERVATION_SIZE);
  while (task_system.has_unfinished_tasks()) task_system.execute_task(main_thread_local_context);

  if (registry_enabled) flush_registry(registry, update_set);

  u32 exit_code = 0;
  for (auto tracker: build_plan.selected_targets) {
    fin_ensure(tracker.compile_status.value != Target_Compile_Status::Compiling);
    fin_ensure(tracker.link_status.value    != Target_Link_Status::Waiting);

    if ((tracker.compile_status.value != Target_Compile_Status::Success) ||
        (tracker.link_status.value    != Target_Link_Status::Success)) {
      log("Building target '%' finished with errors\n", tracker.target.name);
      exit_code = 1;
    }
  }

  return exit_code;
}

