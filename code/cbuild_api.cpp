
#include <cstring>

#include "base.hpp"
#include "driver.hpp"
#include "cbuild_api.hpp"
#include "platform.hpp"
#include "runtime.hpp"
#include "result.hpp"
#include "toolchain.hpp"

extern Platform_Info platform;

const char * get_argument_or_default (const Arguments *args, const char *key, const char *default_value) {
  if (key == nullptr)             return default_value;
  if (key[0] == '\0')             return default_value;
  if (is_empty_list(&args->args)) return default_value;

  auto key_length = strlen(key);
  for (auto arg: args->args) {
    if (arg.type == Argument::Type::Flag) continue;

    if (strncmp(arg.key, key, key_length) == 0) return arg.value;
  }

  return default_value;
}

bool find_toolchain_by_type (Project *project, Toolchain_Type type, Toolchain_Configuration *out_configuration) {
  auto result = lookup_toolchain_by_type(&project->arena, type);
  if (not result) return false;

  *out_configuration = *result;

  return true;
}

void overwrite_toolchain (Project *project, Toolchain_Configuration toolchain) {
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
  auto result = lookup_toolchain_by_type(&project->arena, type);
  if (not result) {
    print(&project->arena, "FATAL ERROR: Requested toolchain wasn't found on the system.\n");
    exit(EXIT_FAILURE);
  }
  
  overwrite_toolchain(project, result);
}

void disable_registry (Project *project) {
  project->registry_disabled = true;
}

void register_action (Project *project, const char *name, Action_Type action) {
  add(&project->arena, &project->user_defined_commands, {
    .name = copy_string(&project->arena, String(name)),
    .proc = action
  });
}

void set_output_location (Project *project, const char *folder_path) {
  project->output_location = copy_string(&project->arena, folder_path);
}

static Target * create_target (Project *project, const char *name) {
  auto arena = &project->arena;

  if (name == nullptr) {
    print(arena, "FATAL ERROR: Annonymous targets (target without a name) are not allowed.");
    exit(EXIT_FAILURE);
  }

  auto name_length = strlen(name);
  if (name_length > Target::Max_Name_Limit) {
    print(arena, "Target's name length is limited to % symbols. If your case requires a longer target name, please submit an issue on the project's Github page\n", Target::Max_Name_Limit);
    exit(EXIT_FAILURE);
  }

  for (auto cursor = name; *cursor; cursor++) {
    auto value = *cursor;
    if (not ((value >= 'a' && value <= 'z') ||
             (value >= 'A' && value <= 'Z') ||
             (value >= '0' && value <= '9') ||
             (value == '_'))) {
      print(arena, "FATAL ERROR: Target name contains disallowed characters, only alphanumeric characters are allow and '_'");
      exit(EXIT_FAILURE);
    }
  }

  for (auto t: project->targets) {
    if (compare_strings(t->name, name)) {
      print(&project->arena, "FATAL ERROR: Target '%' already defined in the project. It's not allowed to have multiple targets with the same name\n", name);
      exit(EXIT_FAILURE);
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

void add_source_file (Target *target, const char *_file_path) {
  auto arena = &target->project->arena;

  auto file_path = get_absolute_path(arena, _file_path);
  add(arena, &target->files, *file_path);

  target->project->total_files_count += 1;
}

void add_include_search_path (Target *target, const char *include_path) {
  auto arena = &target->project->arena;

  auto file_path = get_absolute_path(arena, include_path);
  
  add(arena, &target->include_paths, *file_path);
}

void add_all_sources_from_directory (Target *target, const char *directory, const char *extension, bool recurse) {
  auto existing_target_count = target->files.count;
  list_files_in_directory(&target->project->arena, &target->files, directory, extension, recurse);
  target->project->total_files_count += (target->files.count - existing_target_count);
}

void add_compiler_option (Target *target, const char *option) {
  if (option == nullptr) return;
  
  auto arena = &target->project->arena;
  add(arena, &target->options.compiler, copy_string(arena, option));
}

void add_linker_option (Target *target, const char *option) {
  if (option == nullptr) return;

  auto arena = &target->project->arena;
  add(arena, &target->options.linker, copy_string(arena, option));
}

void link_with_target (Target *target, Target *dependency) {
  if (dependency == nullptr) return;

  auto arena = &target->project->arena;
  add(arena, &target->depends_on,      const_cast<const Target *>(dependency));
  add(arena, &dependency->required_by, const_cast<const Target *>(target));
}

void link_with_library (Target *target, const char *library_name) {
  if (library_name == nullptr || library_name[0] == '\0') return;
  
  auto arena  = &target->project->arena;
  auto copied = copy_string(arena, library_name);
  add(arena, &target->link_libraries, copied);
}

void add_target_hook (Target *target, Hook_Type type, Hook_Func func) {
  switch (type) {
    case Hook_Type_After_Target_Linked: {
      target->hooks.on_linked = func;
      break;
    }
  }
}

const char * get_target_name (const Target *target) {
  return target->name.value;
}
