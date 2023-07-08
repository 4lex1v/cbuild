
#include "generated.h"

#include "base.hpp"
#include "driver.hpp"
#include "arena.hpp"
#include "core.hpp"
#include "project_loader.hpp"
#include "cbuild_api.hpp"
#include "platform.hpp"
#include "result.hpp"
#include "runtime.hpp"
#include "toolchain.hpp"

#ifndef API_VERSION
  #error "API version must be defined at compile time"
#endif

static const u32 api_version = (API_VERSION);

extern File_Path working_directory_path;
extern File_Path cache_directory_path;
extern Platform_Info platform;

typedef bool project_func (const Arguments &args, Project &project);

Status_Code create_new_project_in_workspace (Memory_Arena *arena, bool create_c_project) {
  use(Status_Code);
  using enum Open_File_Flags;

  {
    auto local = *arena;
    auto build_c_file_path   = make_file_path(&local, working_directory_path, "project", "build.c");
    auto build_cpp_file_path = make_file_path(&local, working_directory_path, "project", "build.cpp");

    if (check_file_exists(&build_c_file_path)) {
      return {
        Resource_Already_Exists,
        format_string(&local, "It looks like this workspace already has a project configuration file at %", build_c_file_path)
      };
    }
    else if (check_file_exists(&build_cpp_file_path)) {
      return {
        Resource_Already_Exists,
        format_string(&local, "It looks like this workspace already has a project configuration file at %", build_cpp_file_path)
      };
    }
  }

  auto project_directory_path = make_file_path(arena, working_directory_path, "project");
  check_status(create_directory(&project_directory_path));

  auto code_directory_path = make_file_path(arena, working_directory_path, "code");
  check_status(create_directory(&code_directory_path));

#define generate_file(FILE_PATH, DATA) \
    do {                                          \
      auto header_file = open_file(&(FILE_PATH), Request_Write_Access | Create_File_If_Not_Exists); \
      check_status(header_file); \
      defer { close_file(&header_file); }; \
      write_buffer_to_file(&header_file, reinterpret_cast<const char *>(DATA), array_count_elements(DATA)); \
  } while(0)

  auto cbuild_h_path     = make_file_path(arena, *project_directory_path, "cbuild.h");
  auto cbuild_exp_h_path = make_file_path(arena, *project_directory_path, "cbuild_experimental.h");
  auto build_path        = make_file_path(arena, *project_directory_path, format_string(arena, "build.%", create_c_project ? "c" : "cpp"));
  auto main_path         = make_file_path(arena, *code_directory_path,    "main.cpp");

  generate_file(cbuild_h_path,     cbuild_api_content);
  generate_file(cbuild_exp_h_path, cbuild_experimental_api_content);
  generate_file(build_path,        build_template_content);
  generate_file(main_path,         main_cpp_content);

#undef generate_file

  print(arena, "Project initialized\n");

  return Success;
}

Status_Code update_cbuild_api_file (Memory_Arena *arena) {
  use(Status_Code);
  using enum Open_File_Flags;

  {
    /*
      cbuild.h
    */

    auto header_file_path = make_file_path(arena, working_directory_path, "project", "cbuild.h");
    check_status(delete_file(header_file_path));

    auto header_file = open_file(&header_file_path, Request_Write_Access | Create_File_If_Not_Exists);
    check_status(header_file);

    write_buffer_to_file(&header_file, reinterpret_cast<const char *>(cbuild_api_content), cbuild_api_content_size);

    close_file(&header_file);
  }

  {
    /*
      cbuild_experimental.h
    */

    auto header_file_path = make_file_path(arena, working_directory_path, "project", "cbuild_experimental.h");
    check_status(delete_file(header_file_path));

    auto header_file = open_file(&header_file_path, Request_Write_Access | Create_File_If_Not_Exists);
    check_status(header_file);

    write_buffer_to_file(&header_file, reinterpret_cast<const char *>(cbuild_experimental_api_content), cbuild_experimental_api_content_size);

    close_file(&header_file);
  }

  return Success;
}

Result<File_Path> discover_build_file (Memory_Arena *arena) {
  use(Status_Code);

  {
    auto build_file_path = make_file_path(arena, working_directory_path, "project", "build.cpp");
    if (check_file_exists(&build_file_path)) return build_file_path;
  }

  {
    auto build_file_path = make_file_path(arena, working_directory_path, "project", "build.c");
    if (check_file_exists(&build_file_path)) return build_file_path;
  }

  return { Resource_Missing, "No project definition found.\nPlease setup a new project manually or via the 'init' command.\n" };
}

static Status_Code load_project_from_library (const Arguments *args, Project *project, const File_Path *project_library_file_path) {
  use(Status_Code);

  Shared_Library *library = nullptr;
  check_status(load_shared_library(&library, project_library_file_path));

  auto config_api_version_value = *reinterpret_cast<unsigned int *>(load_symbol_from_library(library, "cbuild_api_version"));

  if (api_version > config_api_version_value) {
    print(&project->arena,
          "It looks like your project configuration uses an older API.\n"
          "You may update API version using `cbuild update` command.\n");
  }

  if (api_version < config_api_version_value) {
    print(&project->arena,
          "Project configuration uses a newer cbuild API (tool: %, config: %).\n"
          "While it's not a violation of the cbuild usage, compatibility is not guaranteed in this case.\n"
          "Please download a newer version at https://github.com/4lex1v/cbuild/releases\n",
          api_version,
          config_api_version_value);
  }

  auto loader = reinterpret_cast<project_func *>(load_symbol_from_library(library, "setup_project"));
  if (not loader) return { Load_Error, "Build definition load failure, couldn't resolve 'setup_project' function" };

  loader(*args, *project);

  auto arena = &project->arena;
  auto output_location = make_file_path(arena, cache_directory_path, "build", project->output_location);
  check_status(create_directory_recursive(arena, &output_location));
  project->output_location_path = output_location;

  return Success;
}

static Status_Code build_project_configuration (Memory_Arena *_arena, Project *project,
                                                File_Path project_output_folder_path,
                                                File_Path project_library_file_path,
                                                File_Path build_file_path) {
  use(Status_Code);
  using enum Open_File_Flags;

  auto toolchain = &project->toolchain;

  auto previous_env = setup_system_sdk(_arena, toolchain->type);
  defer { reset_environment(&previous_env); };

  auto project_obj_file_path = make_file_path(_arena, project_output_folder_path, format_string(_arena, "project.%", platform.is_win32() ? "obj" : "o"));

  {
    auto local = *_arena;

    String_Builder builder { &local };

    bool is_cpp = check_extension(build_file_path, "cpp");

    builder += is_cpp ? toolchain->cpp_compiler_path : toolchain->c_compiler_path;

    const char *standard_value = is_cpp ? "c++17" : "c11";

    if ((toolchain->type == Toolchain_Type_MSVC_X64) ||
        (toolchain->type == Toolchain_Type_MSVC_X86) ||
        (toolchain->type == Toolchain_Type_LLVM_CL)) {
      builder += format_string(&local, "/nologo /std:% /DCBUILD_PROJECT_CONFIGURATION /EHsc /Od /Z7 /Fo:\"%\" /c \"%\"", standard_value, project_obj_file_path, build_file_path);
    }
    else {
      builder += format_string(&local, "-std=% -DCBUILD_PROJECT_CONFIGURATION -O0 -g -gcodeview -c % -o %", standard_value, build_file_path, project_obj_file_path);
    }

    auto compilation_command = build_string_with_separator(&builder, ' ');

    auto [status, output] = run_system_command(&local, compilation_command);

    if (output.length) print(&local, "%\n", output);
    if (not status)    print(&local, "%\n", status);
    
    if (!status) return { Build_Error, "Build description file compilation failure", status.code };
  }

  {
    /*
      Linking project configuration into a shared library.
    */
    auto local = *_arena;

    String_Builder builder { &local };

    builder += toolchain->linker_path;

    switch (platform.type) {
      case Platform_Type::Win32: {
        auto cbuild_import_path = make_file_path(&local, project_output_folder_path, "cbuild.lib");

#ifdef PLATFORM_WIN32
        auto export_file = open_file(&cbuild_import_path, Request_Write_Access | Create_File_If_Not_Exists);
        check_status(export_file);

        auto file_write_result = write_buffer_to_file(&export_file, reinterpret_cast<const char *>(cbuild_lib_content), cbuild_lib_content_size);
        check_status(file_write_result);

        close_file(&export_file);
#endif

        builder += format_string(&local,
                                 "/nologo /dll /debug:full /defaultlib:libcmt /export:cbuild_api_version /export:setup_project \"%\\*.obj\" \"%\" /out:\"%\"",
                                 project_output_folder_path, cbuild_import_path, project_library_file_path);
        break;
      }
      case Platform_Type::Unix: {
        todo();
        break;
      }
      case Platform_Type::Apple: {
        todo();
        break;
      }
    }

    auto linking_command = build_string_with_separator(&builder, ' ');

    auto [status, output] = run_system_command(&local, linking_command);

    if (output.length) print(&local, "%\n", output);
    if (not status)    print(&local, "%\n", status);

    if (!status) return { Build_Error, "Build description linkage failure", status.code };
  }

  return Success;
}

Status_Code load_project (Memory_Arena *arena, const Arguments *args, Project *project) {
  use(Status_Code);
  using enum Open_File_Flags;

  auto project_output_folder_path = make_file_path(arena, cache_directory_path, "project");
  check_status(project_output_folder_path);

  const char *platform_shared_lib_ext = nullptr;
  switch (platform.type) {
    case Platform_Type::Win32: { platform_shared_lib_ext = "dll";   break; }
    case Platform_Type::Unix:  { platform_shared_lib_ext = "so";    break; }
    case Platform_Type::Apple: { platform_shared_lib_ext = "dylib"; break; }
  }

  auto project_library_file_path = make_file_path(arena, *project_output_folder_path, format_string(arena, "project.%", platform_shared_lib_ext));

  auto tag_file_path = make_file_path(arena, *project_output_folder_path, "tag");

  auto build_file_path = discover_build_file(&project->arena);
  check_status(build_file_path);

  create_directory(&cache_directory_path);

  print(&project->arena, "Build file: %\n", build_file_path);

  if (!create_directory(&cache_directory_path)) {
    print(&project->arena, "Failed to create a cache directory at: %. This is a fatal error, terminating the program.\n", cache_directory_path);
    exit(EXIT_FAILURE);
  }

  auto build_file = open_file(&build_file_path);
  defer { close_file(&build_file); };

  auto build_file_timestamp  = get_last_update_timestamp(&build_file);

  if (!create_directory(&project_output_folder_path)) return System_Error;
  else {
    auto tag_file = open_file(&tag_file_path);
    if (tag_file.status) {
      defer { close_file(&tag_file); };

      auto file_size = get_file_size(&tag_file);
      if (file_size) {
        u64 checked_timestamp = 0ull;
        read_bytes_from_file_to_buffer(&tag_file, (char*) &checked_timestamp, sizeof(decltype(checked_timestamp)));

        if (build_file_timestamp == checked_timestamp)
          return load_project_from_library(args, project, &project_library_file_path);
      }
    }
  }

  check_status(build_project_configuration(arena, project, project_output_folder_path, project_library_file_path, build_file_path));
  
  auto tag_file = open_file(&tag_file_path, Request_Write_Access | Create_File_If_Not_Exists);
  check_status(tag_file);
  defer { close_file(&tag_file); };

  check_status(write_buffer_to_file(&tag_file, reinterpret_cast<const char *>(&build_file_timestamp), sizeof(decltype(build_file_timestamp))));
  
  return load_project_from_library(args, project, &project_library_file_path);
}


