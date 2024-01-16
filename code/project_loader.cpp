
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
  create_directory(project_directory_path)
    .expect([&] (auto error) -> String_View {
      return concat_string(arena, "Couldn't create directory: ", project_directory_path, ", error: ", error);
    });

  auto code_directory_path = make_file_path(arena, working_directory, "code");
  create_directory(code_directory_path)
    .expect([&] (auto error) -> String_View {
      return concat_string(arena, "Couldn't create directory: ", code_directory_path, ", error: ", error);
    });

  auto build_file_name = (config_file_type == Configuration_Type::C) ? String_View("build.c") : String_View("build.cpp");
  auto build_file_path = make_file_path(arena, project_directory_path, build_file_name);

  if (auto [has_failed, error, exists] = check_file_exists(build_file_path); has_failed)
    trap(concat_string(arena, "System error occured while checking the project's folder: ", error));
  else if (exists) {
    print("It looks like this workspace already has a project configuration file at %", build_file_path);
    return;
  }

  const auto generate_file = [&arena] <Byte_Type T> (File_Path &&path, Slice<T> data) {
    using enum File_System_Flags;

    auto path_view = String_View(path);
    auto [open_failed, error, file] = open_file(move(path), Write_Access | Create_Missing);
    if (open_failed) panic("Failed to open file '%' for writing due to an error: %", path_view, error);

    write_buffer_to_file(file, data)
      .expect(concat_string(arena, "Failed to write data into the file ", file.path));

     close_file(file).expect(concat_string(arena, "Failed to close file ", file.path));
  };

  auto cbuild_h_path     = make_file_path(arena, project_directory_path, "cbuild.h");
  auto cbuild_exp_h_path = make_file_path(arena, project_directory_path, "cbuild_experimental.h");
  auto main_path         = make_file_path(arena, code_directory_path,    "main.cpp");

  generate_file(move(cbuild_h_path),     Slice(cbuild_api_content));
  generate_file(move(cbuild_exp_h_path), Slice(cbuild_experimental_api_content));
  generate_file(move(build_file_path),   Slice(build_template_content));
  generate_file(move(main_path),         Slice(main_cpp_content));

  print("Project initialized\n");
}

void cleanup_workspace (Memory_Arena &arena, bool full_cleanup) {
  delete_directory(make_file_path(arena, ".cbuild", "build")).expect();
  if (full_cleanup) delete_directory(make_file_path(arena, ".cbuild", "project")).expect();
}

static inline void load_project_from_library (Project &project, const Slice<Startup_Argument> &args) {
  auto [load_failed, error, library] = load_shared_library(project.project_library_path);
  if (load_failed) panic("ERROR: Project % configuration file load failed due to a system error: %\n", project.name, error);

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

  loader(reinterpret_cast<const Arguments *>(&args), project);
}

static void build_project_configuration (
  Memory_Arena &arena,
  Project      &project,
  const File   &build_file,
  const Toolchain_Configuration &toolchain) {

  using enum File_System_Flags;

  auto project_obj_file_name = concat_string(arena, project.name, ".", get_object_extension());
  auto project_obj_file_path = make_file_path(arena, project.project_output_location, project_obj_file_name);

  {
    auto local = arena;
    
    String_Builder  builder { local };

    bool is_cpp = ends_with(build_file.path, "cpp");

    builder += String_View(is_cpp ? toolchain.cpp_compiler_path : toolchain.c_compiler_path);

    auto standard_value = is_cpp ? String_View("c++17") : String_View("c11");

    if ((toolchain.type == Toolchain_Type_MSVC_X64) ||
        (toolchain.type == Toolchain_Type_MSVC_X86) ||
        (toolchain.type == Toolchain_Type_LLVM_CL)) {
      builder += format_string(local, "/nologo /std:% /DCBUILD_PROJECT_CONFIGURATION /Od /Z7 /Fo:\"%\" /c \"%\"", standard_value, project_obj_file_path, build_file.path);
    }
    else {
      builder += format_string(local, "-std=% -DCBUILD_PROJECT_CONFIGURATION -O0 -g -gcodeview -c % -o %", standard_value, build_file.path, project_obj_file_path);
    }

    auto compilation_command = build_string_with_separator(local, builder, ' ');

    auto [has_failed, error, status] = run_system_command(local, compilation_command);
    if (has_failed) panic("Failed to compile configuration file due to a system error: %\n", error);

    if (status.status_code != 0) {
      if (status.output) print(concat_string(local, status.output, "\n"));
      panic("ERROR: Build file configuration compilation failed. Status: %. Command: %\nOutput: %\n",
            status.status_code, compilation_command, status.output);
    }

    if (status.output) print(concat_string(local, status.output, "\n"));
  }

  {
    /*
      Linking project configuration into a shared library.
    */
    auto local = arena;

    String_Builder builder { local };

    builder += String_View(toolchain.linker_path);

#ifdef PLATFORM_WIN32
    {
      auto cbuild_import_path = make_file_path(local, project.project_output_location,
                                               concat_string(local, "cbuild.", get_static_library_extension()));

      auto [open_failed, error, export_file] = open_file(move(cbuild_import_path), Write_Access | Create_Missing);
      if (open_failed) panic("Couldn't create export file to write data to due to an error: %.\n", error);
      defer { close_file(export_file); };

      write_buffer_to_file(export_file, Slice(cbuild_lib_content))
        .expect("Failed to write win32 export data into a file.\n");

      builder += format_string(local, "/nologo /dll /debug:full /export:cbuild_api_version /export:setup_project /subsystem:console \"%\" \"%\" /out:\"%\"",
                               project_obj_file_path, export_file.path, project.project_library_path);
    }
#endif

    auto linking_command = build_string_with_separator(local, builder, ' ');
    //print(concat_string(local, "Linking project's configuration with: ", linking_command));

    auto [has_failed, error, status] = run_system_command(local, linking_command);
    if (has_failed) panic("Failed to execute system command, details: %, command: %.\n", error, linking_command);

    if (status.status_code != 0) {
      if (status.output) print(concat_string(local, status.output, "\n"));
      panic("ERROR: Build file configuration linkage failed. Status: %. Command: %\n",
            status.status_code, linking_command);
    }

    if (status.output) print(concat_string(local, status.output, "\n"));
  }
}

static Option<File_Path> discover_build_file (Memory_Arena &arena, const File_Path &workspace_directory_path) {
  const String_View files [] { "project/build.cpp", "projectbuild.c" };

  for (auto build_file_name: files) {
    auto build_file_path = make_file_path(arena, workspace_directory_path, build_file_name);
    auto [has_failed, error, exists] = check_file_exists(build_file_path);
    if (!has_failed && exists) return move(build_file_path);
  }

  return opt_none;
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

void update_cbuild_api_file (Memory_Arena &arena, const File_Path &working_directory) {
  struct { String_View file_name; Slice<const u8> data; } input[] {
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

void load_project (Memory_Arena &arena, Project &project, const Slice<Startup_Argument> &args) {
  using enum File_System_Flags;

  create_directory(project.cache_root);
  create_directory(project.project_output_location);

  auto previous_env = setup_system_sdk(arena, Target_Arch_x64);

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

  auto build_file_path = discover_build_file(arena, project.project_root)
    .take(concat_string(arena, "No project configuration at: ", project.project_root, "\n"));

  if (!global_flags.silenced) print("Configuration file: %\n", build_file_path);

  /*
    There are two parts to loading the project:
      - Check if there's corresponding shared object
        - If not, proceed with building the configuraiton
      - Check the build file's timestamp
        - If it's out of sync -> rebuild the configuration

     If the project was rebuilt, update configuration's timestamp in the tags file.
   */

  auto build_file = open_file(arena, move(build_file_path));
  defer { close_file(build_file); };

  auto tag_file_path = make_file_path(arena, project.project_output_location, "tag");
  auto tag_file = open_file(arena, move(tag_file_path), Write_Access | Create_Missing);
  defer { close_file(tag_file); };

  auto build_file_timestamp = get_last_update_timestamp(build_file)
    .get("Failed to retrieve configuration's file timestamp.");

  if (get_file_size(tag_file).or_default(0) &&
      /*
        Ensure that there's a built configuration library that could be loaded, otherwise it should be built anyway.
        We cannot fully rely on the tag's file existence and its validity.
       */
      check_file_exists(project.project_library_path).or_default(false)) {
    u64 checked_timestamp = 0ull;
    if (read_bytes_into_buffer(tag_file, (u8*) &checked_timestamp, sizeof(checked_timestamp)).is_ok()) {
      if (build_file_timestamp == checked_timestamp) return load_project_from_library(project, args);
    }
  }

  auto toolchain = discover_toolchain(arena)
    .get("Failed to find any suitable toolchain on the host machine to "
         "build & load the project's configuration file.\n");

  build_project_configuration(arena, project, build_file, toolchain);
  
  reset_file_cursor(tag_file).expect("Failed to reset tag's file pointer");
  if (auto bytes = Slice(reinterpret_cast<u8*>(&build_file_timestamp), sizeof(build_file_timestamp));
      write_buffer_to_file(tag_file, bytes).is_error()) {
    if (auto [has_failed, delete_error] = delete_file(tag_file.path); has_failed) {
      print(concat_string(arena,
                          "ERROR: Something went wrong and CBuild couldn't update the tag's at ", tag_file.path,
                          " file properly. Attempt to delete it also ended up with a system error: ", delete_error,
                          ". If this behaviour persists please try 'cbuild clean all' and if this doesn't help, "
                          "report the issue."));
    }
  }

  return load_project_from_library(project, args);
}

