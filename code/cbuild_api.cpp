
#include <cstring>

#include "base.hpp"
#include "driver.hpp"
#include "cbuild_api.hpp"
#include "platform.hpp"
#include "runtime.hpp"
#include "result.hpp"
#include "toolchain.hpp"

extern Platform_Info platform;

Config_Crash_Handler crash_handler_hook;

static void __require_non_null (const void *value, const char *function_name, const char *parameter_name) {
  if (value == nullptr) [[unlikely]] {
    char temp_buffer[512];
    Memory_Arena temp_arena(temp_buffer, 512);
    print(&temp_arena, "Invalid '%' value passed to '%': value must NOT BE Null\n", parameter_name, function_name);
    crash_handler_hook(EXIT_FAILURE);
  }
}
#define require_non_null(PARAM) __require_non_null(PARAM, __FUNCTION__, #PARAM)

static void __require_non_empty (const char *value, const char *function_name, const char *parameter_name) {
  if (value[0] == '\0') [[unlikely]] {
    char temp_buffer[512];
    Memory_Arena temp_arena(temp_buffer, 512);
    print(&temp_arena, "Invalid '%' value passed to '%': value must NOT BE empty\n", parameter_name, function_name);
    crash_handler_hook(EXIT_FAILURE);
  }
}
#define require_non_empty(PARAM) __require_non_empty(PARAM, __FUNCTION__, #PARAM)

const char * get_argument_or_default (const Arguments *args, const char *key, const char *default_value) {
  if (key == nullptr)             return default_value;
  if (key[0] == '\0')             return default_value;
  if (is_empty_list(&args->args)) return default_value;

  auto key_length = strlen(key);
  for (auto arg: args->args) {
    if ((strlen(arg.key) == key_length) &&
        (strncmp(arg.key, key, key_length) == 0)) {
      return (arg.type == Argument::Type::Flag) ? key : arg.value;
    }
  }

  return default_value;
}

bool find_toolchain_by_type (Project *project, Toolchain_Type type, Toolchain_Configuration *out_configuration) {
  require_non_null(project);
  require_non_null(out_configuration);

  auto [status, result] = lookup_toolchain_by_type(&project->arena, type);
  check_status(status);

  *out_configuration = result;

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

  auto [status, result] = lookup_toolchain_by_type(&project->arena, type);
  if (!status) {
    print(&project->arena, "FATAL ERROR: Requested toolchain wasn't found on the system.\n");
    crash_handler_hook(EXIT_FAILURE);
  }
  
  overwrite_toolchain(project, result);
}

void disable_registry (Project *project) {
  require_non_null(project);

  project->registry_disabled = true;
}

void register_action (Project *project, const char *name, Action_Type action) {
  require_non_null(project);
  require_non_null(name);
  require_non_empty(name);

  add(&project->arena, &project->user_defined_commands, {
    .name = copy_string(&project->arena, String(name)),
    .proc = action
  });
}

void set_output_location (Project *project, const char *folder_path) {
  require_non_null(project);
  require_non_null(folder_path);
  require_non_empty(folder_path);

  project->output_location = copy_string(&project->arena, folder_path);
}

static Target * create_target (Project *project, const char *name) {
  require_non_null(project);
  require_non_null(name);
  require_non_empty(name);

  auto arena = &project->arena;

  auto name_length = strlen(name);
  if (name_length > Target::Max_Name_Limit) {
    print(arena, "Target's name length is limited to % symbols. If your case requires a longer target name, please submit an issue on the project's Github page\n", Target::Max_Name_Limit);
    crash_handler_hook(EXIT_FAILURE);
  }

  for (auto cursor = name; *cursor; cursor++) {
    auto value = *cursor;
    if (not ((value >= 'a' && value <= 'z') ||
             (value >= 'A' && value <= 'Z') ||
             (value >= '0' && value <= '9') ||
             (value == '_'))) {
      print(arena, "FATAL ERROR: Target name contains disallowed characters, only alphanumeric characters are allow and '_'\n");
      crash_handler_hook(EXIT_FAILURE);
    }
  }

  for (auto t: project->targets) {
    if (compare_strings(t->name, name)) {
      print(&project->arena, "FATAL ERROR: Target '%' already defined in the project. It's not allowed to have multiple targets with the same name\n", name);
      crash_handler_hook(EXIT_FAILURE);
    }
  }

  auto target = push_struct<Target>(&project->arena);
  target->project = project;
  target->name    = copy_string(&project->arena, String(name, name_length));

  add(arena, &project->targets, target);

  return target;
}

Target * add_static_library (Project *project, const char *name) {
  auto target = create_target(project, name);

  target->type = Target::Type::Static_Library;

  return target;
}

Target * add_shared_library (Project *project, const char *name) {
  auto target = create_target(project, name);

  target->type = Target::Type::Shared_Library;

  return target;
}

Target * add_executable (Project *project, const char *name) {
  auto target = create_target(project, name);

  target->type = Target::Type::Executable;

  return target;
}

void add_source_file (Target *target, const char *file_path) {
  require_non_null(target);
  require_non_null(file_path);
  require_non_empty(file_path);

  auto arena = &target->project->arena;

  auto abs_file_path = get_absolute_path(arena, file_path);
  if (!check_file_exists(&abs_file_path)) {
    print(arena, "File '%' wasn't found, please check the correctness of the specified path and that the file exists\n", *abs_file_path);
    crash_handler_hook(EXIT_FAILURE);
  }

  add(arena, &target->files, *abs_file_path);

  target->project->total_files_count += 1;
}

void exclude_source_file (Target *target, const char *file_path) {
  require_non_null(target);
  require_non_null(file_path);
  require_non_empty(file_path);

  auto arena = &target->project->arena;

  if (is_empty_list(&target->files)) return;

  auto [status, abs_file_path] = get_absolute_path(arena, file_path);
  if (!check_file_exists(&abs_file_path)) {
    print(arena, "File '%' not found, please check the correctness of the specified path\n", file_path);
    crash_handler_hook(EXIT_FAILURE);
  }

  auto [found, position] =
    find_position(&target->files, [&] (const File_Path *node) {
      return compare_strings(*node, abs_file_path);
    });

  if (!found) {
    print(arena, "File '%' not included for the target %\n", file_path, target->name);
    return;
  }

  if (!remove_at(&target->files, position)) {
    print(arena, "Couldn't remove file '%' from the target due to an internal error, please report this case.\n", file_path);
    return;
  }
}

void add_include_search_path (Target *target, const char *include_path) {
  require_non_null(target);
  require_non_null(include_path);
  require_non_empty(include_path);

  auto arena = &target->project->arena;

  auto file_path = get_absolute_path(arena, include_path);
  
  add(arena, &target->include_paths, *file_path);
}

void add_all_sources_from_directory (Target *target, const char *directory, const char *extension, bool recurse) {
  require_non_null(target);
  require_non_null(directory);
  require_non_empty(directory);
  require_non_null(extension);
  require_non_empty(extension);
  
  auto arena = &target->project->arena;

  auto folder_path = get_absolute_path(arena, directory);
  if (!folder_path.status) {
    print(arena, "Couldn't get absolute path for '%'\n", directory);
    crash_handler_hook(EXIT_FAILURE);
  }

  if (!check_directory_exists(&folder_path)) {
    print(arena, "Directory '%' specified for 'add_all_sources_from_directory' wasn't found, please ensure that the path is correct and the directory exists\n", folder_path);
    crash_handler_hook(EXIT_FAILURE); 
  }
  
  auto existing_target_count = target->files.count;
  list_files_in_directory(&target->project->arena, &target->files, directory, extension, recurse);
  target->project->total_files_count += (target->files.count - existing_target_count);
}

void add_compiler_option (Target *target, const char *option) {
  require_non_null(target);
  require_non_null(option);
  require_non_empty(option);
  
  auto arena = &target->project->arena;
  add(arena, &target->options.compiler, copy_string(arena, option));
}

void add_linker_option (Target *target, const char *option) {
  require_non_null(target);
  require_non_null(option);
  require_non_empty(option);

  auto arena = &target->project->arena;
  add(arena, &target->options.linker, copy_string(arena, option));
}

void link_with_target (Target *target, Target *dependency) {
  require_non_null(target);
  require_non_null(dependency);

  auto arena = &target->project->arena;

  if (target == dependency) {
    print(arena, "Invalid 'dependency' value passed to 'link_with_target': the target cannot be linked with itself\n");
    crash_handler_hook(EXIT_FAILURE);
  }

  add(arena, &target->depends_on,      const_cast<const Target *>(dependency));
  add(arena, &dependency->required_by, const_cast<const Target *>(target));
}

void link_with_library (Target *target, const char *library_name) {
  require_non_null(target);
  require_non_null(library_name);
  require_non_empty(library_name);
  
  auto arena  = &target->project->arena;
  auto copied = copy_string(arena, library_name);
  add(arena, &target->link_libraries, copied);
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
