
#include "anyfin/base.hpp"

#include "anyfin/core/arena.hpp"
#include "anyfin/core/option.hpp"
#include "anyfin/core/result.hpp"
#include "anyfin/core/string_builder.hpp"
#include "anyfin/core/strings.hpp"

#include "anyfin/platform/console.hpp"
#include "anyfin/platform/file_system.hpp"
#include "anyfin/platform/shared_library.hpp"
#include "anyfin/platform/commands.hpp"

#include "generated.h"

#include "cbuild.hpp"
#include "cbuild_api.hpp"
#include "driver.hpp"
#include "project_loader.hpp"
#include "toolchain.hpp"

extern CLI_Flags global_flags;

struct Arguments;
typedef bool project_func (const Arguments *args, Project &project);

static const u32 api_version = (API_VERSION);

void init_workspace (Memory_Arena &arena, const File_Path &working_directory, Configuration_Type config_file_type) {
  auto project_directory_path = make_file_path(arena, working_directory, "project");
  create_resource(project_directory_path, Resource_Type::Directory).expect();

  auto build_file_name = (config_file_type == Configuration_Type::C) ? String_View("build.c") : String_View("build.cpp");
  auto build_file_path = make_file_path(arena, project_directory_path, build_file_name);

  if (check_resource_exists(build_file_path, Resource_Type::File)) {
    print("It looks like this workspace already has a project configuration file at %", build_file_path);
    return;
  }

  auto code_directory_path = make_file_path(arena, working_directory, "code");
  create_resource(code_directory_path, Resource_Type::Directory).expect();

  const auto generate_file = [&arena] (File_Path &&path, String_View data) {
    using enum File_System_Flags;

    auto path_view = String_View(path);
    auto [open_failed, error, file] = open_file(move(path), Write_Access | Create_Missing);
    if (open_failed) panic("Failed to open file '%' for writing due to an error: %", path_view, error);

    write_buffer_to_file(file, data)
      .expect(format_string(arena, "Failed to write data into the file '%'", file.path));

     close_file(file).expect(format_string(arena, "Failed to close file '%'", file.path));
  };

  auto cbuild_h_path     = make_file_path(arena, project_directory_path, "cbuild.h");
  auto cbuild_exp_h_path = make_file_path(arena, project_directory_path, "cbuild_experimental.h");
  auto main_path         = make_file_path(arena, code_directory_path,    "main.cpp");

  generate_file(move(cbuild_h_path),     cbuild_api_content);
  generate_file(move(cbuild_exp_h_path), cbuild_experimental_api_content);
  generate_file(move(build_file_path),   build_template_content);
  generate_file(move(main_path),         main_cpp_content);

  print("Project initialized\n");
}

void cleanup_workspace (Memory_Arena &arena, bool full_cleanup) {
  delete_directory(make_file_path(arena, ".cbuild", "build")).expect();
  if (full_cleanup) delete_directory(make_file_path(arena, ".cbuild", "project")).expect();
}

static inline void load_project_from_library (
  Project                       &project,
  const File_Path               &project_library_file_path,
  const Slice<Startup_Argument> &args) {

  auto library = load_shared_library(project_library_file_path)
    .get("Workspace configuration load failed.\n");

  /*
    If there's something wrong with the library that we load, like some expected symbols are missing, it's fine to ignore
    some of them, like 'cbuild_api_version'.
   */
  lookup_symbol<unsigned char>(*library, "cbuild_api_version")
    .handle_value([] (auto symbol) {
      if (!symbol) {
        print("Expected symbol 'cbuild_api_version' wasn't found in the loaded configuration file\n"
              "This is not expected and could be a sign of some larger issue. Please report this issue.\n");
        return;
      }

      const auto config_api_version_value = *symbol;

      if (api_version > config_api_version_value) {
        print("It looks like your project configuration uses an older API.\n"
              "You may update API version using `cbuild update` command.\n");
      }

      if (api_version < config_api_version_value) {
        print("Project configuration uses a newer cbuild API (tool: %, config: %).\n"
              "While it's not a violation of the cbuild usage, compatibility is not guaranteed in this case.\n"
              "Please download a newer version at https://github.com/4lex1v/cbuild/releases\n",
              api_version,
              config_api_version_value);
      }
    });

  auto loader = lookup_symbol<project_func>(*library, "setup_project")
    .get("Failed to load the 'setup_project' symbol from a shared library.\n");

  if (!loader) panic("Build definition load failure, couldn't resolve 'setup_project' function");

  loader(reinterpret_cast<const Arguments *>(&args), project);
}

struct Workspace {
  File_Path project_output_directory_path;
  File_Path project_tag_file_path;
};

static void build_project_configuration (
  Memory_Arena      &arena,
  const Workspace   &workspace,
  const String_View &project_name,
  const File_Path   &build_file_path,
  const File_Path   &project_library_file_path,
  const Toolchain_Configuration &toolchain) {

  using enum File_System_Flags;

  auto project_obj_file_name = format_string(arena, "%.%", project_name, get_object_extension());
  auto project_obj_file_path = make_file_path(arena, workspace.project_output_directory_path, project_obj_file_name);

  {
    auto local = arena;
    
    String_Builder  builder { local };

    bool is_cpp = ends_with(build_file_path, "cpp");

    builder += String_View(is_cpp ? toolchain.cpp_compiler_path : toolchain.c_compiler_path);

    auto standard_value = is_cpp ? String_View("c++17") : String_View("c11");

    if ((toolchain.type == Toolchain_Type_MSVC_X64) ||
        (toolchain.type == Toolchain_Type_MSVC_X86) ||
        (toolchain.type == Toolchain_Type_LLVM_CL)) {
      builder += format_string(local, "/nologo /std:% /DCBUILD_PROJECT_CONFIGURATION /EHsc /Od /Z7 /Fo:\"%\" /c \"%\"", standard_value, project_obj_file_path, build_file_path);
    }
    else {
      builder += format_string(local, "-std=% -DCBUILD_PROJECT_CONFIGURATION -O0 -g -gcodeview -c % -o %", standard_value, build_file_path, project_obj_file_path);
    }

    auto compilation_command = build_string_with_separator(local, builder, ' ');

    auto [has_failed, error, status] = run_system_command(local, compilation_command);
    if (has_failed) panic("Failed to execute system command, details: %.\n", error);

    const auto &[output, return_code] = status;

    if (!return_code) panic("Command execution failed with, code: %, command: %\n", return_code, compilation_command);
    if (output) console_print_message(format_string(local, "%\n", output));
  }

  {
    /*
      Linking project configuration into a shared library.
    */
    Memory_Arena   local   { arena };
    String_Builder builder { local };

    builder += String_View(toolchain.linker_path);

#ifdef PLATFORM_WIN32
    {
      auto cbuild_import_path = make_file_path(local, workspace.project_output_directory_path,
                                               format_string(local, "cbuild.%", get_static_library_extension()));

      auto [open_failed, error, export_file] = open_file(move(cbuild_import_path), Write_Access | Create_Missing);
      if (open_failed) panic("Couldn't create export file to write data to due to an error: %.\n", error);
      defer { close_file(export_file); };

      write_buffer_to_file(export_file, cbuild_lib_content)
        .expect("Failed to write win32 export data into a file.\n");

      builder += format_string(local,
                               "/nologo /dll /debug:full /defaultlib:libcmt /export:cbuild_api_version /export:setup_project \"%\\*.obj\" \"%\" /out:\"%\"",
                               workspace.project_output_directory_path, cbuild_import_path, project_library_file_path);
    }
#endif

    auto linking_command = build_string_with_separator(arena, builder, ' ');

    auto [has_failed, error, status] = run_system_command(local, linking_command);
    if (has_failed) panic("Failed to execute system command, details: %, command: %.\n", error, linking_command);

    const auto &[output, return_code] = status;

    if (!return_code) panic("Command execution failed with, code: %, command: %\n", return_code, linking_command);
    if (output) console_print_message(format_string(local, "%\n", output));
  }
}

// Result<Project_Loader> create_project_loader (Memory_Arena *arena, File_Path cache_directory) {
//   use(Status_Code);
  
//   auto cache_project_directory = make_file_path(arena, cache_directory, "project");
//   if (!cache_project_directory_creation_status) 
//   check_status(project_output_folder);
//   check_status(create_directory(&project_output_folder));

//   auto project_tag_file = make_file_path(arena, *project_output_folder, "tag");
//   check_status(project_tag_file);

//   return Project_Loader {
//     .project_output_directory = project_output_folder,
//     .project_tag_file         = project_tag_file,
//   };
// }

static Option<File_Path> discover_build_file (Memory_Arena &arena, const File_Path &project_directory_path) {
  String_View files[] { "build.cpp", "build.c" };

  for (auto build_file_name: files) {
    auto build_file_path = make_file_path(arena, project_directory_path, build_file_name);
    if (check_resource_exists(build_file_path, Resource_Type::File)) return Option(move(build_file_path));
  }

  return {};
}

struct Project_Registry {
  constexpr static usize Version = 1;

  struct Header {
    u16 version;

    // Version 1
    u16 entries_count;
  };

  struct Record {
    char name[32];
    u64  timestamp;
    u64  hash;
  };

  File         tag_file;
  File_Mapping tag_file_mapping;
};

Project load_project (
  Memory_Arena                  &arena,
  const String_View             &project_name,
  const File_Path               &working_directory,
  const File_Path               &workspace_directory,
  const Slice<Startup_Argument> &args) {

  using enum File_System_Flags;

  auto previous_env = setup_system_sdk(arena, Target_Arch_x64);

  auto cache_directory_path = make_file_path(arena, working_directory, ".cbuild");

    /*
      Previous setup_system_sdk call configures env to build the the project's configuration for the host machine,
      while this call should setup CBuild to build the project for the specific target, where, at least in the case of
      Windows, different dll libs should be used.

      CBuild itself targets x64 machine only, while it allows the user to build for x86. Since the default toolchain must
      be x64, current env must already be configured for that and there's no need to do this again.
    */
    // if (project.target_architecture == Target_Arch_x86) {
    //   reset_environment(previous_env);
    //   setup_system_sdk(arena, project.target_architecture);
    // }

  auto build_file_path = discover_build_file(arena, workspace_directory)
    .take(format_string(arena, "No project configuration at: %\n", workspace_directory));

  if (!global_flags.silenced) print("Configuration file: %\n", build_file_path);

  /*
    There are two parts to loading the project:
      - Check if there's corresponding shared object
        - If not, proceed with building the configuraiton
      - Check the build file's timestamp
        - If it's out of sync -> rebuild the configuration

     If the project was rebuilt, update configuration's timestamp in the tags file.
   */

  auto [open_failed, error, build_file] = open_file(move(build_file_path));
  if (open_failed) panic("Failed to open project's configuration file due to an error: %", error);
  defer { close_file(build_file); };

  auto build_file_timestamp = get_last_update_timestamp(build_file)
    .get("Failed to retrieve configuration's file timestamp.");

  //auto must_rebuild_configuration = /* load the tag file content and read stuff from it */;

  Workspace workspace;
  {
    auto project_output_directory = make_file_path(arena, cache_directory_path, "project");
    auto project_tag_file         = make_file_path(arena, project_output_directory, "tag");

    workspace = Workspace { move(project_output_directory), move(project_tag_file) };
  }

  auto shared_library_file_name  = format_string(arena, "%.%", project_name, get_shared_library_extension());
  auto project_library_file_path = make_file_path(arena, workspace.project_output_directory_path, shared_library_file_name);
  if (!check_resource_exists(project_library_file_path, Resource_Type::File)) {
    auto toolchain = discover_toolchain(arena)
      .get("Failed to find any suitable toolchain on the host machine to "
           "build & load the project's configuration file.\n");

    build_project_configuration(arena, workspace, project_name, build_file.path, project_library_file_path, toolchain);
  }

  Project project {
    arena,
    project_name,
    String::copy(arena, workspace_directory),
    String::copy(arena, workspace.project_output_directory_path)
  };

  // load_project_from_library(user_arguments, &project, project_library_file_path);

  create_resource(project.output_location_path, Resource_Type::Directory).expect();

  return project;
  //check_status(project_library_file_path);

  // if (!create_directory(&cache_directory_path)) {
  //   print(arena, "Failed to create a cache directory at: %. This is a fatal error, terminating the program.\n", cache_directory_path);
  //   exit(EXIT_FAILURE);
  // }

  // if (!create_directory(&project_output_folder_path)) return System_Error;
  // else {
  //   auto tag_file = open_file(&tag_file_path);
  //   if (tag_file.status) {
  //     defer { close_file(&tag_file); };

  //     auto file_size = get_file_size(&tag_file);
  //     if (file_size) {
  //       u64 checked_timestamp = 0ull;
  //       read_bytes_from_file_to_buffer(&tag_file, (char*) &checked_timestamp, sizeof(decltype(checked_timestamp)));

  //       if (build_file_timestamp == checked_timestamp)
  //         return load_project_from_library(args, project, &project_library_file_path);
  //     }
  //   }
  // }

  // check_status(build_project_configuration(arena, project, project_output_folder_path, project_library_file_path, build_file_path));
  
  // auto tag_file = open_file(&tag_file_path, Request_Write_Access | Create_File_If_Not_Exists);
  // check_status(tag_file);
  // defer { close_file(&tag_file); };

  // check_status(write_buffer_to_file(&tag_file, reinterpret_cast<const char *>(&build_file_timestamp), sizeof(decltype(build_file_timestamp))));

  // project->rebuild_required = true;
  
  // return load_project_from_library(args, project, &project_library_file_path);
}

void update_cbuild_api_file (Memory_Arena &arena, const File_Path &working_directory) {
  struct { String_View file_name; String_View data; } input[] {
    { "cbuild.h",              cbuild_api_content },
    { "cbuild_experimental.h", cbuild_experimental_api_content }
  };

  for (auto &[file_name, data]: input) {
    using enum File_System_Flags;

    auto file_path      = make_file_path(arena, working_directory, "project", file_name);
    auto file_path_view = String_View(file_path);

    auto [open_failed, error, file] = open_file(move(file_path), Write_Access | Create_Missing);
    if (open_failed) panic("Couldn't open file % due to an error: %.\n", file_path_view, error);

    write_buffer_to_file(file, data)
      .expect("Failed to write data to the generated header file.\n");

    close_file(file).expect("Failed to close the generate header file's handle.\n");
  }
}
