
#include "anyfin/base.hpp"

#include "anyfin/arena.hpp"
#include "anyfin/option.hpp"
#include "anyfin/result.hpp"
#include "anyfin/string_builder.hpp"
#include "anyfin/strings.hpp"
#include "anyfin/file_system.hpp"
#include "anyfin/shared_library.hpp"
#include "anyfin/commands.hpp"

#include "templates/generated.h"

#include "cbuild.hpp"
#include "cbuild_api.hpp"
#include "workspace.hpp"
#include "toolchain.hpp"

extern bool silence_logs_opt;
extern bool tracing_enabled_opt;

struct Arguments;
typedef bool project_func (const Arguments *args, Project &project);

static const u32 api_version = (API_VERSION);

void init_workspace (Memory_Arena &arena, File_Path working_directory, Configuration_Type config_file_type) {
  auto project_directory_path = make_file_path(arena, working_directory, "project");
  if (auto result = create_directory(project_directory_path); result.is_error()) {
    panic("Couldn't create directory: % due to an error: %\n", project_directory_path, result.error);
  }

  auto code_directory_path = make_file_path(arena, working_directory, "code");
  if (auto result = create_directory(code_directory_path); result.is_error()) {
    panic("Couldn't create directory % due to an error: %\n", code_directory_path, result.error);
  };

  auto build_file_name = (config_file_type == Configuration_Type::C) ? String("build.c") : String("build.cpp");
  auto build_file_path = make_file_path(arena, project_directory_path, build_file_name);

  if (auto [error, exists] = check_file_exists(build_file_path); error)
    panic("System error occured while checking the project's folder: %\n", error.value);
  else if (exists) {
    log("It looks like this workspace already has a project configuration file at %", build_file_path);
    return;
  }

  const auto generate_file = [&arena] <usize N> (File_Path path, const u8 (&data)[N]) {
    using enum File_System_Flags;

    auto file = unwrap(open_file(path, Write_Access | Create_Missing));
    ensure(write_bytes_to_file(file, data, N));
    ensure(close_file(file));
  };

  auto cbuild_h_path     = make_file_path(arena, project_directory_path, "cbuild.h");
  auto cbuild_exp_h_path = make_file_path(arena, project_directory_path, "cbuild_experimental.h");
  auto main_path         = make_file_path(arena, code_directory_path,    "main.cpp");

  generate_file(cbuild_h_path,     cbuild_api_content);
  generate_file(cbuild_exp_h_path, cbuild_experimental_api_content);
  generate_file(build_file_path,   build_template_content);
  generate_file(main_path,         main_cpp_content);

  log("Project initialized\n");
}

void cleanup_workspace (Memory_Arena &arena, bool full_cleanup) {
  ensure(delete_directory(make_file_path(arena, ".cbuild", "build")));
  if (full_cleanup) ensure(delete_directory(make_file_path(arena, ".cbuild", "project")));
}

static inline void load_project_from_library (Project &project, Slice<Startup_Argument> arguments) {
  auto [load_error, library] = load_shared_library(project.project_library_path);
  if (load_error) panic("ERROR: Project % configuration file load failed due to an error - %\n", project.name, load_error.value);

  /*
    If there's something wrong with the library that we load, like some expected symbols are missing, it's fine to ignore
    some of them, like 'cbuild_api_version'.
   */
  auto [version_symbol_found, symbol] = lookup_symbol<const unsigned char>(*library, "cbuild_api_version");
  if (version_symbol_found) {
    if (!symbol) {
      log("Expected symbol 'cbuild_api_version' wasn't found in the loaded configuration file\n"
          "This is not expected and could be a sign of some larger issue. Please report this issue.\n");
      return;
    }

    const auto config_api_version_value = *symbol;

    if (api_version > config_api_version_value) {
      log("It looks like your project configuration uses an older API.\n"
          "You may update API version using `cbuild update` command.\n");
    }

    if (api_version < config_api_version_value) {
      log("Project configuration uses a newer cbuild API (tool: %, config: %).\n"
          "While it's not a violation of the cbuild usage, compatibility is not guaranteed in this case.\n"
          "Please download a newer version at https://github.com/4lex1v/cbuild/releases\n",
          api_version,
          config_api_version_value);
    }
  };

  auto loader = unwrap(lookup_symbol<project_func>(*library, "setup_project"),
                       "Failed to load the 'setup_project' symbol from a shared library.\n");

  const Arguments args { project.arena, arguments };
  loader(&args, project);
}

static void build_project_configuration (Memory_Arena &arena, Project &project, const File &build_file) {
  using enum File_System_Flags;

  auto &toolchain = project.toolchain;

  auto project_obj_file_name = concat_string(arena, project.name, ".", get_object_extension());
  auto project_obj_file_path = make_file_path(arena, project.project_output_location, project_obj_file_name);

  {
    auto local = arena;
    
    String_Builder  builder { local };

    bool is_cpp = ends_with(build_file.path, "cpp");

    builder += String(is_cpp ? toolchain.cpp_compiler_path : toolchain.c_compiler_path);

    auto standard_value = is_cpp ? String("c++17") : String("c11");

    if ((toolchain.type == Toolchain_Type_MSVC_X64) ||
        (toolchain.type == Toolchain_Type_MSVC_X86) ||
        (toolchain.type == Toolchain_Type_LLVM_CL)) {
      builder += format_string(local, "/nologo /std:% /DCBUILD_PROJECT_CONFIGURATION /EHsc /Od /Z7 /Fo:\"%\" /c \"%\"", standard_value, project_obj_file_path, build_file.path);
    }
    else {
      builder += format_string(local, "-std=% -DCBUILD_PROJECT_CONFIGURATION -O0 -g -gcodeview -c % -o %", standard_value, build_file.path, project_obj_file_path);
    }

    auto compilation_command = build_string_with_separator(local, builder, ' ');
    if (tracing_enabled_opt) log("Project build configuration compile command: %\n", compilation_command);

    auto [cmd_error, status] = run_system_command(local, compilation_command);
    if (cmd_error) panic("Failed to compile configuration file due to a system error: %\n", cmd_error.value);

    if (status.status_code != 0) {
      if (status.output) log(concat_string(local, status.output, "\n"));
      panic("ERROR: Build file configuration compilation failed. Status: %. Command: %\nOutput: %\n",
            status.status_code, compilation_command, status.output);
    }

    if (status.output) log(concat_string(local, status.output, "\n"));
  }

  {
    /*
      Linking project configuration into a shared library.
    */
    auto local = arena;

    String_Builder builder { local };

    builder += String(toolchain.linker_path);

#ifdef PLATFORM_WIN32
    {
      auto cbuild_export_module_path = make_file_path(local, project.project_output_location, "cbuild.def");
      auto cbuild_import_path        = make_file_path(local, project.project_output_location, "cbuild.lib");

      auto [open_error, export_module] = open_file(cbuild_export_module_path, Write_Access | Always_New);
      if (open_error) panic("Couldn't create export file to write data to due to an error: %.\n", open_error.value);

      auto program_name = get_program_name();
      
      ensure(write_bytes_to_file(export_module, concat_string(local, "LIBRARY \"", program_name, "\"\n")));
      ensure(write_bytes_to_file(export_module, cbuild_def_content), "Failed to write win32 export data into a file");

      ensure(close_file(export_module));

      auto [cbuild_lib_cmd_error, cbuild_lib_cmd_result] = 
        run_system_command(local, concat_string(local, "lib.exe /nologo /machine:x64 ",
                                                "/DEF:\"", cbuild_export_module_path, "\" ",
                                                "/OUT:\"", cbuild_import_path, "\""));
      if (cbuild_lib_cmd_error) panic("Couldn't generate export library for the executable % due a %\n", program_name, cbuild_lib_cmd_error.value);
      if (cbuild_lib_cmd_result.status_code != 0) panic("Couldn't generate export library for the executable %:\n%\n", program_name, cbuild_lib_cmd_result.output);

      builder += format_string(local, "/nologo /dll /debug:full /export:cbuild_api_version /export:setup_project /subsystem:console "
                               "\"%\" \"%\" /out:\"%\"", project_obj_file_path, cbuild_import_path, project.project_library_path);
    }
#else
#error "Unsupported platform"
#endif

    auto linking_command = build_string_with_separator(local, builder, ' ');
    if (tracing_enabled_opt) log("Project build configuration link command: %\n", linking_command);

    auto [cmd_error, status] = run_system_command(local, linking_command);
    if (cmd_error) panic("Failed to execute system command, details: %, command: %.\n", cmd_error.value, linking_command);

    if (status.status_code != 0) {
      if (status.output) log(concat_string(local, status.output, "\n"));
      panic("ERROR: Build file configuration linkage failed. Status: %. Command: %\n",
            status.status_code, linking_command);
    }

    if (status.output) log(concat_string(local, status.output, "\n"));
  }
}

static Option<File_Path> discover_build_file (Memory_Arena &arena, const File_Path &workspace_directory_path) {
  const String files [] { "project/build.cpp", "project/build.c",  };

  for (auto build_file_name: files) {
    auto build_file_path = make_file_path(arena, workspace_directory_path, build_file_name);
    auto [error, exists] = check_file_exists(build_file_path);
    if (!error && exists) return move(build_file_path);
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

void update_cbuild_api_file (Memory_Arena &arena, File_Path working_directory) {
  struct { String file_name; String data; } input [] {
    { "cbuild.h",              cbuild_api_content },
    { "cbuild_experimental.h", cbuild_experimental_api_content }
  };

  for (auto &[file_name, data]: input) {
    using enum File_System_Flags;

    auto file_path      = make_file_path(arena, working_directory, "project", file_name);
    auto file_path_view = String(file_path);

    auto [open_error, file] = open_file(move(file_path), Write_Access | Create_Missing);
    if (open_error) panic("Couldn't open file % due to an error: %.\n", file_path_view, open_error.value);

    ensure(write_bytes_to_file(file, data), "Failed to write data to the generated header file");
    ensure(close_file(file), "Failed to close the generate header file's handle");
  }
}

void load_project (Memory_Arena &arena, Project &project, Slice<Startup_Argument> args) {
  using enum File_System_Flags;

  create_directory(project.cache_root);
  create_directory(project.project_output_location);

  auto previous_env = setup_system_sdk(arena, Target_Arch_x64);
  defer {
    /*
      Previous setup_system_sdk call configures env to build the configuration for the host machine. After the project
      was loaded, it's entirely possible for the user to overwrite the toolset and target architecture. In which case
      the env should be restored and we need to try and setup the environment for the new target.
    */
    if (project.target_architecture == Target_Arch_x86) {
      reset_environment(previous_env);
      setup_system_sdk(arena, project.target_architecture);
    }
  };

  project.toolchain = unwrap(discover_toolchain(arena),
                             "Failed to find any suitable toolchain on the host machine to "
                             "build & load the project's configuration file");

  auto build_file_path =
    unwrap(discover_build_file(arena, project.project_root),
           concat_string(arena, "No project configuration at: ", project.project_root, "\n"));

  if (!silence_logs_opt) log("Configuration file: %\n", build_file_path);

  /*
    There are two parts to loading the project:
      - Check if there's corresponding shared object
        - If not, proceed with building the configuraiton
      - Check the build file's timestamp
        - If it's out of sync -> rebuild the configuration

     If the project was rebuilt, update configuration's timestamp in the tags file.
   */

  auto build_file = unwrap(open_file(build_file_path));
  defer { close_file(build_file); };

  auto tag_file_path = make_file_path(arena, project.project_output_location, "tag");
  auto tag_file = unwrap(open_file(tag_file_path, Write_Access | Create_Missing));
  defer { close_file(tag_file); };

  auto build_file_timestamp =
    unwrap(get_last_update_timestamp(build_file),
           "Failed to retrieve configuration's file timestamp.");

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

  build_project_configuration(arena, project, build_file);

  /*
    Force rebuild of all targets if the project has been updated, since we don't know what kind of changes were made.
   */
  project.rebuild_required = true;
  
  // TODO: Perhaps it's worth to add overwrite option to the write function instead?
  ensure(reset_file_cursor(tag_file), "Failed to reset tag's file pointer");

  if (auto result = write_bytes_to_file(tag_file, reinterpret_cast<u8*>(&build_file_timestamp), sizeof(build_file_timestamp));
      result.is_error()) {
    log("WARNING: CBuild couldn't update the project's tag file due to an error: %. Full rebuild will happen on the next run.\n",
        result.error.value);

    /*
      If something went wrong and we couldn't update the project's tag file, try delete it so that next time CBuild is called
      we just rebuild everything.
     */
    if (auto result = delete_file(tag_file.path); result.is_error()) {
      log("WARNING: Something went wrong and CBuild couldn't update the tag's at % file properly. Attempt to delete it also ended up with an error: %. "
          "If this behaviour persists please try 'cbuild clean all' and if this doesn't help, report the issue.",
          tag_file.path, result.error.value);
    }
  }

  return load_project_from_library(project, args);
}

