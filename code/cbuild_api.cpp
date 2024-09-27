
#include "anyfin/base.hpp"
#include "anyfin/startup.hpp"
#include "anyfin/console.hpp"

#include "cbuild.hpp"
#include "cbuild_api.hpp"
#include "workspace.hpp"
#include "toolchain.hpp"

#define require_non_null(PARAM) __require_non_null(PARAM, __FUNCTION__, #PARAM)
static void __require_non_null (const void *value, String function_name, String parameter_name) {
  if (!value) panic("Invalid '%' value passed to '%': value must NOT BE Null\n", parameter_name, function_name);
}

#define require_non_empty(PARAM) __require_non_empty(PARAM, __FUNCTION__, #PARAM)
static void __require_non_empty (const char *value, String function_name, String parameter_name) {
  if (value && !value[0]) panic("Invalid '%' value passed to '%': value must NOT BE empty\n", parameter_name, function_name);
}

const char * get_argument_or_default (const Arguments *arguments, const char *key, const char *default_value) CBUILD_NO_EXCEPT {
  if (is_empty(arguments->args))        return default_value;
  if (key == nullptr || key[0] == '\0') return default_value;

  const auto key_name = String(key);
  for (auto arg: arguments->args) {
    if (arg.key == key_name) {
      return copy_string(arguments->global_arena, (arg.is_value()) ? arg.key : arg.value);
    }
  }

  return default_value;
}

bool find_toolchain_by_type (Project *project, Toolchain_Type type, Toolchain_Configuration *out_configuration) CBUILD_NO_EXCEPT {
  require_non_null(project);
  require_non_null(out_configuration);

  auto [found, config] = lookup_toolchain_by_type(project->arena, type);
  if (!found) return false;

  *out_configuration = config;

  return true;
}

void overwrite_toolchain (Project *project, Toolchain_Configuration toolchain) CBUILD_NO_EXCEPT {
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

void set_toolchain (Project *project, Toolchain_Type type) CBUILD_NO_EXCEPT {
  require_non_null(project);

  auto [found, toolchain] = lookup_toolchain_by_type(project->arena, type);
  if (!found) panic("FATAL ERROR: Requested toolchain wasn't found on the system.\n");
  
  overwrite_toolchain(project, toolchain);
}

void disable_registry (Project *project) CBUILD_NO_EXCEPT {
  require_non_null(project);

  project->registry_disabled = true;
}

void register_action (Project *project, const char *name, Action_Type action) CBUILD_NO_EXCEPT {
  require_non_null(project);
  require_non_null(name);
  require_non_empty(name);

  list_push(project->user_defined_commands, {
    .name = copy_string(project->arena, name),
    .proc = action
  });
}

void set_output_location (Project *project, const char *folder_path) CBUILD_NO_EXCEPT {
  require_non_null(project);
  require_non_null(folder_path);
  require_non_empty(folder_path);

  project->build_location_path = make_file_path(project->arena, project->base_build_location_path, String(folder_path));
}

static void set_absolute_path (Memory_Arena &arena, File_Path &variable, const char *path) {
  if (is_absolute_path(path)) {
    variable = copy_string(arena, path);
  }
  else {
    auto [error, result] = get_absolute_path(arena, path);
    if (error) panic("Couldn't resolve absolute path for the specified folder % due to a system error: %\n", path, error.value);

    variable = move(path);
  }
}

void set_install_location (Project *project, const char *binary_folder, const char *library_folder) CBUILD_NO_EXCEPT {
  require_non_null(project);
  require_non_null(binary_folder);
  require_non_empty(binary_folder);

  auto &arena = project->arena;

  set_absolute_path(arena, project->binary_install_location_path, binary_folder);

  if (library_folder == nullptr) {
    project->library_install_location_path = project->binary_install_location_path;
    return;
  }

  set_absolute_path(arena, project->library_install_location_path, library_folder);
}

void add_global_compiler_option (Project *project, const char *option) CBUILD_NO_EXCEPT {
  require_non_null(project);
  require_non_null(option);
  require_non_empty(option);

  list_push(project->compiler, copy_string(project->arena, option));
}

void add_global_archiver_option (Project *project, const char *option) CBUILD_NO_EXCEPT {
  require_non_null(project);
  require_non_null(option);
  require_non_empty(option);

  list_push(project->archiver, copy_string(project->arena, option));
}

void add_global_linker_option (Project *project, const char *option) CBUILD_NO_EXCEPT {
  require_non_null(project);
  require_non_null(option);
  require_non_empty(option);

  list_push(project->linker, copy_string(project->arena, option));
}

void add_global_include_search_path (Project *project, const char *path) CBUILD_NO_EXCEPT {
  require_non_null(project);
  require_non_null(path);
  require_non_empty(path);

  const auto path_view = String(path);

  const auto file_path = make_file_path(project->arena, path_view);

  auto [error, absolute_path] = get_absolute_path(project->arena, file_path);
  if (error) panic("Couldn't resolve the provided path '%', error: %\n", file_path, error.value);

  list_push(project->include_paths, Include_Path::local(move(absolute_path)));
}

void add_global_system_include_search_path (Project *project, const char *path) CBUILD_NO_EXCEPT {
  require_non_null(project);
  require_non_null(path);
  require_non_empty(path);

  const auto path_view = String(path);

  const auto file_path = make_file_path(project->arena, path_view);

  auto [error, absolute_path] = get_absolute_path(project->arena, file_path);
  if (error) panic("Couldn't resolve the provided path '%', error: %\n", file_path, error.value);

  list_push(project->include_paths, Include_Path::system(move(absolute_path)));
}

static Target * create_target (Project *project, Target::Type type, const char *_name) {
  require_non_null(project);
  require_non_null(_name);
  require_non_empty(_name);

  auto name = copy_string(project->arena, _name);
  if (name.length > Target::Max_Name_Limit) 
    panic("Target's name length is limited to % symbols. If your case requires a "
          "longer target name, please submit an issue on the project's Github page\n",
          Target::Max_Name_Limit);

  for (auto value: name) {
    if (not ((value >= 'a' && value <= 'z') ||
             (value >= 'A' && value <= 'Z') ||
             (value >= '0' && value <= '9') ||
             (value == '_') ||
             (value == ' '))) {
      panic("FATAL ERROR: Target name contains disallowed characters, only alphanumeric characters are allow and '_'\n");
    }
  }

  for (auto &target: project->targets) {
    if (target.name == name) 
      panic("FATAL ERROR: Target '%' already defined in the project. "
            "It's not allowed to have multiple targets with the same name\n", name);
  }

  return &list_push(project->targets, Target(*project, type, move(name)));
}

Target * add_static_library (Project *project, const char *name) CBUILD_NO_EXCEPT {
  return create_target(project, Target::Type::Static_Library, name);
}

Target * add_shared_library (Project *project, const char *name) CBUILD_NO_EXCEPT {
  return create_target(project, Target::Type::Shared_Library, name);
}

Target * add_executable (Project *project, const char *name) CBUILD_NO_EXCEPT {
  return create_target(project, Target::Type::Executable, name);
}

void add_source_file (Target *target, const char *path) CBUILD_NO_EXCEPT {
  require_non_null(target);
  require_non_null(path);
  require_non_empty(path);

  const auto file_path = make_file_path(target->project.arena, path);

  auto [sys_error, absolute_path] = get_absolute_path(target->project.arena, file_path);
  if (sys_error) panic("Couldn't resolve the absolute path for the file %\n", file_path);

  auto [check_error, file_exists] = check_file_exists(absolute_path);
  if (check_error)  panic("Couldn't validate file path % due to a system eror: %", absolute_path, check_error.value);
  if (!file_exists) panic("File '%' wasn't found, please check the correctness of the specified path and that the file exists\n", absolute_path);

  list_push(target->files, move(absolute_path));

  target->project.total_files_count += 1;
}

void exclude_source_file (Target *target, const char *path) CBUILD_NO_EXCEPT {
  require_non_null(target);
  require_non_null(path);
  require_non_empty(path);

  if (is_empty(target->files)) return;

  const auto path_view = String(path);
  const auto file_path = make_file_path(target->project.arena, path_view);

  auto [abs_error, abs_file_path] = get_absolute_path(target->project.arena, file_path);
  if (abs_error) panic("Couldn't resolve the absolute path for the file % due to an error: %\n", file_path, abs_error.value);

  auto removed = target->files.remove([&] (const File_Path &file) { return file == abs_file_path; });
  if (!removed) panic("File '%' not included for the target %\n", file_path, target->name);

  target->project.total_files_count -= 1;
}

void add_include_search_path (Target *target, const char *path) CBUILD_NO_EXCEPT {
  require_non_null(target);
  require_non_null(path);
  require_non_empty(path);

  const auto path_view = String(path);
  const auto file_path = make_file_path(target->project.arena, path_view);

  auto [error, include_path] = get_absolute_path(target->project.arena, file_path);
  if (error) panic("Couldn't resolve the path '%', error details: %", path_view, error.value);
  
  list_push(target->include_paths, Include_Path::local(include_path));
}

void add_system_include_search_path (Target *target, const char *path) CBUILD_NO_EXCEPT {
  require_non_null(target);
  require_non_null(path);
  require_non_empty(path);

  const auto path_view = String(path);
  const auto file_path = make_file_path(target->project.arena, path_view);

  auto [error, include_path] = get_absolute_path(target->project.arena, file_path);
  if (error) panic("Couldn't resolve the path '%', error details: %", path_view, error.value);
  
  list_push(target->include_paths, Include_Path::system(include_path));
}

void add_all_sources_from_directory (Target *target, const char *_directory, const char *extension, bool recurse) CBUILD_NO_EXCEPT {
  require_non_null(target);
  require_non_null(_directory);
  require_non_empty(_directory);
  require_non_null(extension);
  require_non_empty(extension);
  
  auto &arena = target->project.arena;

  auto directory = make_file_path(arena, _directory);
  auto [abs_error, folder_path] = get_absolute_path(arena, directory);
  if (abs_error) panic("Couldn't get absolute path for '%'\n", directory);

  {
    auto [check_error, exists] = check_directory_exists(folder_path);
    if (check_error) panic("Couldn't validate directory path % due to a system eror: %", folder_path, check_error.value);
    if (!exists)     panic("Directory '%' specified for 'add_all_sources_from_directory' wasn't found, "
                           "please ensure that the path is correct and the directory exists\n", folder_path);
  }
  
  auto existing_target_count = target->files.count;

  auto &file_list = target->files;
  auto add_file = [&] (File_Path file_path) -> bool {
    if (!file_list.contains(file_path)) list_push(file_list, copy_string(arena, file_path));
    return true;
  };

  for_each_file(folder_path, extension, recurse, add_file);

  target->project.total_files_count += (target->files.count - existing_target_count);
}

static void add_options (Memory_Arena &arena, List<String> &list, String values) {
  split_string(values, ' ').for_each([&] (auto value) {
    list_push(list, copy_string(arena, value));
  });
}

static void remove_option (List<String> &options, String values) {
  split_string(values, ' ').for_each([&] (auto value) {
    options.remove([&value] (auto &it) { return it == value; });
  });
}

void add_compiler_option (Target *target, const char *option) CBUILD_NO_EXCEPT {
  require_non_null(target);
  require_non_null(option);
  require_non_empty(option);
  
  auto &arena = target->project.arena;
  add_options(arena, target->compiler, String(option));
}

void remove_compiler_option (Target *target, const char *option) CBUILD_NO_EXCEPT {
  require_non_null(target);
  require_non_null(option);
  require_non_empty(option);

  remove_option(target->compiler, String(option));
}

void add_archiver_option (Target *target, const char *option) CBUILD_NO_EXCEPT {
  require_non_null(target);
  require_non_null(option);
  require_non_empty(option);
  
  auto &arena = target->project.arena;
  add_options(arena, target->archiver, String(option));
}

void remove_archiver_option (Target *target, const char *option) CBUILD_NO_EXCEPT {
  require_non_null(target);
  require_non_null(option);
  require_non_empty(option);

  remove_option(target->archiver, String(option));
}

void add_linker_option (Target *target, const char *option) CBUILD_NO_EXCEPT {
  require_non_null(target);
  require_non_null(option);
  require_non_empty(option);

  auto &arena = target->project.arena;
  add_options(arena, target->linker, String(option));
}

void remove_linker_option (Target *target, const char *option) CBUILD_NO_EXCEPT {
  require_non_null(target);
  require_non_null(option);
  require_non_empty(option);

  remove_option(target->linker, String(option));
}

void link_with_target (Target *target, Target *dependency) CBUILD_NO_EXCEPT {
  require_non_null(target);
  require_non_null(dependency);

  auto &arena = target->project.arena;

  if (target == dependency)
    panic("Invalid 'dependency' value passed to 'link_with_target': "
          "the target cannot be linked with itself\n");

  list_push_copy(target->depends_on,      dependency);
  list_push_copy(dependency->required_by, target);
}

void link_with_library (Target *target, const char *library_name) CBUILD_NO_EXCEPT {
  require_non_null(target);
  require_non_null(library_name);
  require_non_empty(library_name);
  
  auto &arena = target->project.arena;
  list_push(target->link_libraries, copy_string(arena, library_name));
}

void add_target_hook (Target *target, Hook_Type type, Hook_Func func) CBUILD_NO_EXCEPT {
  require_non_null(target);
  
  switch (type) {
    case Hook_Type_After_Target_Linked: {
      target->hooks.on_linked = func;
      break;
    }
  }
}

const char * get_target_name (const Target *target) CBUILD_NO_EXCEPT {
  require_non_null(target);
  return target->name.value;
}

// NOTE: CBuild interal API, not exported to the public
String get_target_extension (const Target &target) {
  switch (target.type) {
    case Target::Static_Library: return get_static_library_extension();
    case Target::Shared_Library: return get_shared_library_extension();
    case Target::Executable:     return get_executable_extension();
  }
}

File_Path get_output_file_path_for_target (Memory_Arena &arena, const Target &target) {
  auto extension = get_target_extension(target);
  auto file_name = concat_string(arena, target.name, ".", extension);
  return make_file_path(arena, target.project.build_location_path, "out", file_name);
}

const char * get_generated_binary_file_path (const Target *target) CBUILD_NO_EXCEPT {
  require_non_null(target);

  auto &project = target->project;
  auto path     = get_output_file_path_for_target(project.arena, *target);

  return path.value;
}

Project_Ref * register_external_project (Project *project, const Arguments *args, const char *name, const char *external_project_path) CBUILD_NO_EXCEPT {
  auto &arena = project->global_arena;

  auto sub_project_path = make_file_path(arena, project->project_root, String(external_project_path));
  // if (!check_resource_exists(sub_project_path, Resource_Type::Directory))
  //   panic("ERROR: No valid CBuild project found under %\n", sub_project_path);

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

  // auto working_directory = *get_working_directory(arena);

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

// Target * get_external_target (Project *project, const Project_Ref *external_project, const char *target_name) CBUILD_NO_EXCEPT {
//   auto external = reinterpret_cast<const Project *>(external_project);

//   // auto external_target = external->targets.find([&] (auto target) {
//   //   return compare_strings(target->name, target_name);
//   // });
  
//   // if (!external_target) panic("ERROR: Target '%' not found in the external project %\n", target_name, external->project_root);

//   // auto value = *external_target;
//   // add_external_target_chain(project, *external_target);

//   //return *external_target;
//   return nullptr;
// }

void install_target (Target *target, const char *install_target_overwrite) CBUILD_NO_EXCEPT {
  auto &project = target->project;

  target->flags.install = true;

  if (!install_target_overwrite) return;

  set_absolute_path(project.arena, target->install_location_overwrite, install_target_overwrite);
}
