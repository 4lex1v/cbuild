
#include "anyfin/base.hpp"

#include "anyfin/core/allocator.hpp"

#include "anyfin/platform/console.hpp"

#include "cbuild.hpp"
#include "cbuild_api.hpp"
#include "project_loader.hpp"
#include "toolchain.hpp"

#define require_non_null(PARAM) __require_non_null(PARAM, __FUNCTION__, #PARAM)
static void __require_non_null (const void *value, const String_View &function_name, const String_View &parameter_name) {
  if (!value) panic("Invalid '%' value passed to '%': value must NOT BE Null\n", parameter_name, function_name);
}

#define require_non_empty(PARAM) __require_non_empty(PARAM, __FUNCTION__, #PARAM)
static void __require_non_empty (const char *value, const String_View &function_name, const String_View &parameter_name) {
  if (value && !value[0]) panic("Invalid '%' value passed to '%': value must NOT BE empty\n", parameter_name, function_name);
}

const char * get_argument_or_default (const Arguments *_args, const char *_key, const char *default_value) {
  const auto &args = *reinterpret_cast<const Slice<Startup_Argument> *>(_args);
  const auto key = String_View(_key);

  if (is_empty(key)) return default_value;
  if (is_empty(args)) return default_value;

  for (auto arg: args) {
    if (compare_strings(arg.key, key)) {
      return ((arg.is_value()) ? key : arg.value).value;
    }
  }

  return default_value;
}

bool find_toolchain_by_type (Project *project, Toolchain_Type type, Toolchain_Configuration *out_configuration) {
  require_non_null(project);
  require_non_null(out_configuration);

  auto lookup_result = lookup_toolchain_by_type(project->arena, type);
  if (!lookup_result) return false;

  *out_configuration = *lookup_result;

  return true;
}

void overwrite_toolchain (Project *project, Toolchain_Configuration toolchain) {
  require_non_null(project);

  /*
    #REWORK:
      This is a temporary solution for 2 reasons:
        1) Support for setting the target architecture is not yet surfaced to the tool's API
        2) In case of x86 toolchain, we must change system's sdk anyway. Perhaps later it's worth
           checking which target architecture is set and which toolchain is used on Windows.
   */
  if (toolchain.type == Toolchain_Type_MSVC_X86) {
    project->target_architecture = Target_Arch_x86;
  }

  project->toolchain = toolchain;
}

void set_toolchain (Project *project, Toolchain_Type type) {
  require_non_null(project);

  auto lookup_result = lookup_toolchain_by_type(project->arena, type);
  if (!lookup_result) panic("FATAL ERROR: Requested toolchain wasn't found on the system.\n");
  
  overwrite_toolchain(project, *lookup_result);
}

void disable_registry (Project *project) {
  require_non_null(project);

  project->registry_disabled = true;
}

void register_action (Project *project, const char *name, Action_Type action) {
  require_non_null(project);
  require_non_null(name);
  require_non_empty(name);

  list_push(project->user_defined_commands, {
    .name = String::copy(project->arena, name),
    .proc = action
  });
}

void set_output_location (Project *project, const char *folder_path) {
  require_non_null(project);
  require_non_null(folder_path);
  require_non_empty(folder_path);

  project->output_location_path = make_file_path(project->arena, project->project_root, ".cbuild", "build", String_View(folder_path));
}

void add_global_compiler_option (Project *project, const char *option) {
  require_non_null(project);
  require_non_null(option);
  require_non_empty(option);

  list_push(project->project_options.compiler, String::copy(project->arena, option));
}

void add_global_archiver_option (Project *project, const char *option) {
  require_non_null(project);
  require_non_null(option);
  require_non_empty(option);

  list_push(project->project_options.archiver, String::copy(project->arena, option));
}

void add_global_linker_option (Project *project, const char *option) {
  require_non_null(project);
  require_non_null(option);
  require_non_empty(option);

  list_push(project->project_options.linker, String::copy(project->arena, option));
}

void add_global_include_search_path (Project *project, const char *path) {
  require_non_null(project);
  require_non_null(path);
  require_non_empty(path);

  const auto path_view = String_View(path);

  const auto file_path = make_file_path(project->arena, path_view);

  auto [has_failed, error, absolute_path] = get_absolute_path(project->arena, file_path);
  if (has_failed) panic("Couldn't resolve the provided path '%', error code: %, details: %\n",
                        file_path, error, retrieve_system_error_message(project->arena, error));

  list_push(project->project_options.include_paths, move(absolute_path));
}

static Target * create_target (Project *project, Target::Type type, const char *_name) {
  require_non_null(project);
  require_non_null(_name);
  require_non_empty(_name);

  auto name = String::copy(project->arena, _name);
  if (name.length > Target::Max_Name_Limit) 
    panic("Target's name length is limited to % symbols. If your case requires a "
          "longer target name, please submit an issue on the project's Github page\n",
          Target::Max_Name_Limit);

  for (auto value: name) {
    if (not ((value >= 'a' && value <= 'z') ||
             (value >= 'A' && value <= 'Z') ||
             (value >= '0' && value <= '9') ||
             (value == '_'))) {
      panic("FATAL ERROR: Target name contains disallowed characters, only alphanumeric characters are allow and '_'\n");
    }
  }

  for (auto t: project->targets) {
    if (compare_strings(t->name, name)) {
      panic("FATAL ERROR: Target '%' already defined in the project. "
            "It's not allowed to have multiple targets with the same name\n",
            name);
    }
  }

  auto target = new (project->arena) Target(*project, type, move(name));

  list_push_copy(project->targets, target);

  return target;
}

Target * add_static_library (Project *project, const char *name) {
  return create_target(project, Target::Type::Static_Library, name);
}

Target * add_shared_library (Project *project, const char *name) {
  return create_target(project, Target::Type::Shared_Library, name);
}

Target * add_executable (Project *project, const char *name) {
  return create_target(project, Target::Type::Executable, name);
}

void add_source_file (Target *target, const char *path) {
  require_non_null(target);
  require_non_null(path);
  require_non_empty(path);

  const auto path_view = String_View(path);
  const auto file_path = make_file_path(target->project->arena, path_view);

  auto abs_file_path = get_absolute_path(target->project->arena, file_path)
    .take(format_string(target->project->global_arena, "Couldn't resolve the absolute path for the file '%'", file_path));

  if (!check_resource_exists(abs_file_path, Resource_Type::File)) {
    panic("File '%' wasn't found, please check the correctness of the specified path and that the file exists\n", *abs_file_path);
  }

  list_push(target->files, move(abs_file_path));

  target->project->total_files_count += 1;
}

void exclude_source_file (Target *target, const char *path) {
  require_non_null(target);
  require_non_null(path);
  require_non_empty(path);

  if (is_empty(target->files)) return;

  const auto path_view = String_View(path);
  const auto file_path = make_file_path(target->project->arena, path_view);

  auto abs_file_path = get_absolute_path(target->project->arena, file_path)
    .take(format_string(target->project->global_arena, "Couldn't resolve the absolute path for the file '%'", file_path));

  auto removed = target->files.remove([&] (const File_Path &file) {
    return compare_strings(file, abs_file_path);
  });

  if (!removed) panic("File '%' not included for the target %\n", file_path, target->name);

  target->project->total_files_count -= 1;
}

void add_include_search_path (Target *target, const char *path) {
  require_non_null(target);
  require_non_null(path);
  require_non_empty(path);

  const auto path_view = String_View(path);
  const auto file_path = make_file_path(target->project->arena, path_view);

  auto include_path = get_absolute_path(target->project->arena, file_path);
  if (!include_path) panic("Couldn't resolve the path '%', error details: %", path_view, include_path.status);
  
  list_push(target->include_paths, include_path.take());
}

void add_all_sources_from_directory (Target *target, const char *_directory, const char *_extension, bool recurse) {
  require_non_null(target);
  require_non_null(_directory);
  require_non_empty(_directory);
  require_non_null(_extension);
  require_non_empty(_extension);
  
  auto &arena = target->project->arena;

  const auto directory = make_file_path(arena, String_View(_directory));
  const auto extension = make_file_path(arena, String_View(_extension));

  auto folder_path = get_absolute_path(arena, directory)
    .take(format_string(arena, "Couldn't get absolute path for '%'\n", directory));

  if (!check_resource_exists(folder_path, Resource_Type::Directory)) 
    panic("Directory '%' specified for 'add_all_sources_from_directory' wasn't found, "
          "please ensure that the path is correct and the directory exists\n",
          folder_path);
  
  auto existing_target_count = target->files.count;
  auto files = list_files(arena, folder_path, extension, recurse)
    .get(format_string(arena, "Couldn't get a list of files from directory '%'", directory));

  for (auto &file: files) list_push(target->files, move(file));

  target->project->total_files_count += (target->files.count - existing_target_count);
}

static void add_options (Memory_Arena &arena, List<String> &list, const String_View &values) {
  for (auto value: iterator::split(values, ' ')) {
    list_push(list, String::copy(arena, value));
  }
}

static void remove_option (List<String> &options, const String_View &values) {
  for (auto value: iterator::split(values, ' ')) {
    options.remove([&value] (auto &it) { return compare_strings(it, value); });
  }
}

void add_compiler_option (Target *target, const char *option) {
  require_non_null(target);
  require_non_null(option);
  require_non_empty(option);
  
  auto &arena = target->project->arena;
  add_options(arena, target->options.compiler, String_View(option));
}

void remove_compiler_option (Target *target, const char *option) {
  require_non_null(target);
  require_non_null(option);
  require_non_empty(option);

  remove_option(target->options.compiler, String_View(option));
}

void add_archiver_option (Target *target, const char *option) {
  require_non_null(target);
  require_non_null(option);
  require_non_empty(option);
  
  auto &arena = target->project->arena;
  add_options(arena, target->options.archiver, String_View(option));
}

void remove_archiver_option (Target *target, const char *option) {
  require_non_null(target);
  require_non_null(option);
  require_non_empty(option);

  remove_option(target->options.archiver, String_View(option));
}

void add_linker_option (Target *target, const char *option) {
  require_non_null(target);
  require_non_null(option);
  require_non_empty(option);

  auto &arena = target->project->arena;
  add_options(arena, target->options.linker, String_View(option));
}

void remove_linker_option (Target *target, const char *option) {
  require_non_null(target);
  require_non_null(option);
  require_non_empty(option);

  remove_option(target->options.linker, String_View(option));
}

void link_with_target (Target *target, Target *dependency) {
  require_non_null(target);
  require_non_null(dependency);

  auto &arena = target->project->arena;

  if (target == dependency)
    panic("Invalid 'dependency' value passed to 'link_with_target': "
          "the target cannot be linked with itself\n");

  list_push_copy(target->depends_on,      dependency);
  list_push_copy(dependency->required_by, target);
}

void link_with_library (Target *target, const char *library_name) {
  require_non_null(target);
  require_non_null(library_name);
  require_non_empty(library_name);
  
  auto &arena = target->project->arena;
  list_push(target->link_libraries, String::copy(arena, library_name));
}

void add_target_hook (Target *target, Hook_Type type, Hook_Func func) {
  require_non_null(target);
  
  switch (type) {
    case Hook_Type_After_Target_Linked: {
      target->hooks.on_linked = func;
      break;
    }
  }
}

const char * get_target_name (const Target *target) {
  require_non_null(target);
  return target->name.value;
}

// NOTE: CBuild interal API, not exported to the public
const String_View get_target_extension (const Target &target) {
  switch (target.type) {
    case Target::Static_Library: return get_static_library_extension();
    case Target::Shared_Library: return get_shared_library_extension();
    case Target::Executable:     return get_executable_extension();
  }
}

File_Path get_output_file_path_for_target (Memory_Arena &arena, const Target &target) {
  auto extension = get_target_extension(target);
  return make_file_path(arena, target.project->output_location_path, "out",
                        format_string(arena, "%.%", target.name, extension));
}

const char * get_generated_binary_file_path (const Target *target) {
  require_non_null(target);

  auto project = target->project;
  auto path    = get_output_file_path_for_target(project->arena, *target);

  return path.value;
}

Project_Ref * register_external_project (Project *project, const Arguments *args, const char *name, const char *external_project_path) {
  auto &arena = project->global_arena;

  auto sub_project_path = make_file_path(arena, project->project_root, String_View(external_project_path));
  if (!check_resource_exists(sub_project_path, Resource_Type::Directory))
    panic("ERROR: No valid CBuild project found under %\n", sub_project_path);

  //String external_name = name;
  // if (!external_name) {
  //   auto [resolution_status, resolved_name] = get_file_name(&sub_project_path);
  //   if (!resolution_status) {
  //     print(arena, "FATAL ERROR: Couldn't extract name (last part of the file path) from: %\n", *sub_project_path);
  //     crash_handler_hook(EXIT_FAILURE);
  //     return nullptr;
  //   }

  //   external_name = resolved_name;
  // }

  // for (auto e: project->sub_projects) {
  //   if (compare_strings(e->external_name, external_name)) {
  //     print(arena, "FATAL ERROR: External project '%' already registered\n", external_name);
  //     crash_handler_hook(EXIT_FAILURE);
  //     return nullptr;
  //   }
  // }

  // auto sub_project = push_struct<Project>(arena, arena, external_name, sub_project_path, project->toolchain);

  // auto working_directory = *get_working_directory_path(arena);

  // auto status = load_project(arena, args, working_directory, sub_project);
  // if (!status) {
  //   print(arena, "ERROR: Couldn't register an external project from %, load error: %\n", sub_project_path, status);
  //   set_working_directory(project->project_root);
  //   crash_handler_hook(EXIT_FAILURE);
  //   return nullptr;
  // }

  // add(arena, &project->sub_projects, sub_project);

  // return reinterpret_cast<Project_Ref *>(sub_project);
  return nullptr;
}

// static void add_external_target_chain (Project *project, Target *target) {
//   for (auto upstream: target->depends_on) add_external_target_chain(project, upstream);
//   add(&project->arena, &project->targets, target);
// }

Target * get_external_target (Project *project, const Project_Ref *external_project, const char *target_name) {
  auto external = reinterpret_cast<const Project *>(external_project);

  // auto external_target = external->targets.find([&] (auto target) {
  //   return compare_strings(target->name, target_name);
  // });
  
  // if (!external_target) panic("ERROR: Target '%' not found in the external project %\n", target_name, external->project_root);

  // auto value = *external_target;
  // add_external_target_chain(project, *external_target);

  //return *external_target;
  return nullptr;
}
