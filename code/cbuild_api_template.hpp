
/*
  WARNING: This file is managed by the cbuild tool. Please, avoid making any manual changes to this file,
           as they are likely to be lost. 
 */

#pragma once

#if defined(CBUILD_PROJECT_CONFIGURATION) && defined(_WIN32)
  #define CBUILD_API __declspec(dllimport)
#else
  #define CBUILD_API
#endif

#ifdef CBUILD_PROJECT_CONFIGURATION

unsigned int cbuild_api_version = CBUILD_API_VERSION;

#endif // CBUILD_PROJECT_CONFIGURATION

#ifndef __cplusplus
#include <stdbool.h>
#endif

typedef struct Project Project;
typedef struct Target Target;
typedef struct Arguments Arguments;

/*
  General API Notes.

  The API provided by the "cbuild" tool follows the fail-fast approach, meaning that if an error happens, information is printed
  on the terminal and the execution terminates with an error code.

  API defined here should be considered stable and backwards compatible, any future version of "cbuild" should be able to work
  with project configurations defined using older APIs.

  Project configuration will be compiled with the following standards: C++17 and C11 depending on the language used.

 */

/*
  Toolchain type is largely defines the style of command-line arguments passed to the executable.
 */
typedef enum Toolchain_Type {
  Toolchain_Type_MSVC_X86,
  Toolchain_Type_MSVC_X64,
  Toolchain_Type_LLVM,
  Toolchain_Type_LLVM_CL,
  Toolchain_Type_GCC,
} Toolchain_Type;

typedef struct Toolchain_Configuration {
  Toolchain_Type  type;

  const char *c_compiler_path;
  const char *cpp_compiler_path;
  const char *linker_path;
  const char *archiver_path;
} Toolchain_Configuration;

#ifdef __cplusplus
extern "C" {
#endif

/*
  Retrieves the value of a specified argument from the argument set. If the argument is not found, returns a default value.

  Parameters:
  
    args - A pointer to an Arguments structure which contains the parsed arguments.
    key - A string representing the key of the argument to be retrieved.
    default_value - A string representing the default value to be returned if the argument key is not found in the Arguments structure.
  
  Returns:
  
    The function returns a pointer to a character string. If the key is found in the arguments, the function returns the corresponding value. If the key is not found, it returns the specified default value.

  Example:
  
    bool setup_project (const Arguments *args, Project *project) {
      // Retrieve the 'config' argument, or use 'debug' as a default value
      const char *config = get_argument_or_default(args, "config", "debug");

      // ... Use 'config' in your project setup code
    }

 */
CBUILD_API const char * get_argument_or_default (const Arguments *args, const char *key, const char *default_value);

/*
  Sets the toolchain of a specified type for a project.

  Attempts to look up the toolchain specified by the `type` value on the host machine and use that as a toolchain to build the project.
  If the specified toolchain wasn't found, execution terminates with an error.

  Example:

    bool setup_project (const Arguments *args, Project *project) {
      const char *tool = get_argument_or_default(args, "tool", "clang");

      if      (!strcmp(tool, "clang")) set_toolchain(project, Toolchain_Type_LLVM);
      else if (!strcmp(tool, "msvc"))  set_toolchain(project, Toolchain_Type_MSVC_X86);
      else {
        printf("Unexpected 'tool' value provided: %s\n", tool);
        return false;
      }
      return true;
    }

 */
CBUILD_API void set_toolchain (Project *project, Toolchain_Type type);

/*
  Disables artifact caching for the specified project. When the registry is disabled, the entire project will be rebuilt each time a build is triggered.
  Artifacts here refer to the output of previous build processes, such as compiled object files or linked libraries.
  
  Note that this setting does not affect the build of the project's configuration. This means that the configuration build process will continue to use cached results if available.

 */
CBUILD_API void disable_registry (Project *project);

/*
  Defines a type for user-defined actions that can be called via a command line interface.
  These actions are expected to take a set of arguments and return an integer as status code.
  '0' represents success and non-zero values represent error conditions.
 */
typedef int (*Action_Type) (const Arguments *args);

/*
  Registers a user-defined action under a specified name within a project.

  The registered action can be invoked from the command line using the registered name.
  All arguments passed on the command line are accessible within the action's implementation.

  Example:

    static int hello_world_action (const Arguments *args) {
      printf("Hello world\n");
      return 0;  // return 0 indicating success
    }

    void setup_project (const Arguments *args, Project *project) {
      // Register a user-defined action named 'hello' which when invoked, prints "Hello world"
      register_action(project, "hello", hello_world_action);
    }

  With the above setup, the action can now be called from the command line as: `cbuild hello`.
  
 */
CBUILD_API void register_action (Project *project, const char *name, Action_Type proc);

/*
  Overwrites the default output location for all produced artifacts within a project.

  Artifacts are the output of the build process, including compiled object files, linked libraries, executables, etc.

  The default output location is `<project_root>/.cbuild/out`.

  `set_output_location` specifies additional subdirectories under the default path. The main purpose of this function is to support different configuration types and to prevent clashes between object files and artifacts from different configurations or platforms.

  The specified folder path is treated as relative to the default output location. Thus, it creates a subdirectory structure under the default path where the artifacts will be stored.

  Example:

    void setup_project (const Arguments *args, Project *project) {
      const char *config   = get_argument_or_default(args, "config", "debug");
      const char *platform = get_argument_or_default(args, "platform", "win32");

      char buffer[256];
      snprintf(buffer, 256, "%s/%s", config, platform);
      set_output_location(project, buffer); // Set output location relative to '<project_root>/.cbuild/out'
    }

 */
CBUILD_API void set_output_location (Project *project, const char *folder_path);

/*
  Create a new target that will produce a static library.
 */
CBUILD_API Target * add_static_library (Project *project, const char *name);

/*
  Create a new target that will produce a shared library. 
 */
CBUILD_API Target * add_shared_library (Project *project, const char *name);

/*
  Create a new target that will produce an executable
 */
CBUILD_API Target * add_executable (Project *project, const char *name);

/*
  Add global options that will be passed to the compiler executable as-is.
  Provided `option` string may contain multiple compiler arguments separated by space.

  These options will be applied to all targets in the project.

  Example:

    void setup_project (const Arguments *args, Project *project) {
      set_toolchain(project, Toolchain_Type_MSVC_X64);
      add_global_compiler_option(project, "/nologo /std:c++20");

      Target *main = add_executable(project, "main");
    }

 */
CBUILD_API void add_global_compiler_option (Project *project, const char *option);

/*
  Add global options that will be passed to the archiver executable as-is.
  Provided `option` string may contain multiple compiler arguments separated by space.

  These options will be applied to all targets in the project.

  These configuration values will be applied to static library targets only.

  Example:

    void setup_project (const Arguments *args, Project *project) {
      set_toolchain(project, Toolchain_Type_MSVC_X64);
      add_global_compiler_option(project, "/nologo /std:c++20");

      Target *main = add_executable(project, "main");
    }

 */
CBUILD_API void add_global_archiver_option (Project *project, const char *option);

/*
  Add global options that will be passed to the linker executable as-is.
  Provided `option` string may contain multiple compiler arguments separated by space.

  These options will be applied to all targets in the project.

  These configuration values will be applied to shared library and executable targets only.

  Example:

    void setup_project (const Arguments *args, Project *project) {
      const char *config   = get_argument_or_default(args, "config", "debug");
      const char *platform = get_argument_or_default(args, "platform", "win32");

      if (!strcmp(platform, "win32")) {
        add_global_linker_option(project, "/nologo /subsystem:console");
        if (!strcmp(config, "debug")) add_global_linker_option(project, "/debug:full");
      }

      Target *main = add_executable(project, "main");
    }

 */
CBUILD_API void add_global_linker_option (Project *project, const char *option);

/*
  Additional include path to the list global list that will be passed to the compiler.
  Specified include paths must be relative to the project's root.

  These paths will be included in the search paths for all targets in the project.

  This path would also be used to resolved included header during dependency checking
  step, to find if there were any changes and which parts CBuild would rebuild.

  Example:

    void setup_project (const Arguments *args, Project *project) {
      auto tracy = add_static_library(project, "tracy_client");

      add_source_file(tracy, "libs/tracy/TracyClient.cpp");
      add_global_include_search_path(project, "libs/public");
    }

 */
CBUILD_API void add_global_include_search_path (Project *project, const char *path);

/*
  Add options that will be passed to the compiler executable as-is.
  `option` string may contain multiple compiler arguments separated by space.

  Example:

    void setup_project (const Arguments *args, Project *project) {
      const char *config = get_argument_or_default(args, "config", "debug");

      set_toolchain(project, Toolchain_Type_MSVC_X64);

      Target *main = add_executable(project, "main");
      add_compiler_option(main, "/nologo /std:c++20");
      if (!strcmp(config, "debug")) add_compiler_option(main, "/O0 /Zi");
    }

 */
CBUILD_API void add_compiler_option (Target *target, const char *option);

/*
  Remove options that were previously added to the compiler executable.
  `option` string may contain multiple compiler arguments separated by space, which are to be removed for the
  current target.

  Example:

    void setup_project (const Arguments *args, Project *project) {
      const char *config = get_argument_or_default(args, "config", "release");

      set_toolchain(project, Toolchain_Type_MSVC_X64);

      Target *main = add_executable(project, "main");
      add_compiler_option(main, "/nologo /std:c++20");
      remove_compiler_option(main, "/nologo"); // Removes the "/nologo" option from the compiler arguments
      if (!strcmp(config, "debug")) remove_compiler_option(main, "/O2 /Zi"); // Removes the "/O2 /Zi" options if in debug configuration
    }

 */
CBUILD_API void remove_compiler_option (Target *target, const char *option);

/*
  Add options that will be passed to the archiver executable as-is.
  `option` string may contain multiple compiler arguments separated by space.

  This option will be applied to static library targets only.

  Example:

    void setup_project (const Arguments *args, Project *project) {
      const char *config = get_argument_or_default(args, "config", "debug");

      set_toolchain(project, Toolchain_Type_MSVC_X64);

      Target *main = add_executable(project, "main");
      add_compiler_option(main, "/nologo /std:c++20");
      if (!strcmp(config, "debug")) add_compiler_option(main, "/O0 /Zi");
    }

 */
CBUILD_API void add_archiver_option (Target *target, const char *option);

/*
  Remove options that were previously added for the archiver executable.
  `option` string may contain multiple archiver arguments separated by space, which are to be removed for the
  current target.

  Example:

    void setup_project (const Arguments *args, Project *project) {
      const char *config = get_argument_or_default(args, "config", "release");

      set_toolchain(project, Toolchain_Type_MSVC_X64);

      Target *main = add_executable(project, "main");
      add_archiver_option(main, "/nologo /std:c++20");
      remove_archiver_option(main, "/nologo"); // Removes the "/nologo" option from the archiver arguments
      if (!strcmp(config, "debug")) remove_archiver_option(main, "/O2 /Zi"); // Removes the "/O2 /Zi" options if in debug configuration
    }

 */
CBUILD_API void remove_archiver_option (Target *target, const char *option);

/*
  Add options that will be passed to the linker executable as-is.
  `option` string may contain multiple compiler arguments separated by space.

  This option will be applied to shared library and executable target only.

  Example:

    void setup_project (const Arguments *args, Project *project) {
      const char *config   = get_argument_or_default(args, "config", "debug");
      const char *platform = get_argument_or_default(args, "platform", "win32");

      Target *main = add_executable(project, "main");

      if (!strcmp(platform, "win32")) {
        add_linker_option("/nologo /subsystem:console");
        if (!strcmp(config, "debug")) add_linker_option(main, "/debug:full");
      }
    }

 */
CBUILD_API void add_linker_option (Target *target, const char *option);

/*
  Remove options that were previously added for the linker executable.
  `option` string may contain multiple linker arguments separated by space, which are to be removed for the
  current target.

  Example:

    void setup_project (const Arguments *args, Project *project) {
      const char *config = get_argument_or_default(args, "config", "release");

      set_toolchain(project, Toolchain_Type_MSVC_X64);

      Target *main = add_executable(project, "main");
      add_linker_option(main, "/nologo /std:c++20");
      remove_linker_option(main, "/nologo"); // Removes the "/nologo" option from the linker arguments
      if (!strcmp(config, "debug")) remove_linker_option(main, "/O2 /Zi"); // Removes the "/O2 /Zi" options if in debug configuration
    }

 */
CBUILD_API void remove_linker_option (Target *target, const char *option);

/*
  Add a C/C++ source file to the target.

  Provided `file_path` value must be a relative path from the project's root.

  The source file will be appended to the list of files, preserving the order during the linkage.

  Example:

    void setup_project (const Arguments *args, Project *project) {
      Target *main = add_executable(project, "main");
      add_source_file(main, "code/main.cpp");
    }

 */
CBUILD_API void add_source_file (Target *target, const char *file_path);

/*
  Adds all C or C++ source files from the specified directory, optionally including all files from subdirectories.
  For C files you must use "c" extension, while to add C++ files "cpp" extension must be used.
  'directory' must be a relative path to the project's root.

  Example:

    void setup_project (const Arguments *args, Project *project) {
      Target *main = add_executable(project, "main");
      add_all_sources_from_directory(target, "code", "cpp", false);
    }

 */
CBUILD_API void add_all_sources_from_directory (Target *target, const char *directory, const char *extension, bool recurse);

/*
  Exclude a C/C++ source file from the target.

  Provided `file_path` value must be a relative path from the project's root.

  The source file will be removed from the list of files, preserving the order of remaining files during the linkage.

  Example:

    void setup_project (const Arguments *args, Project *project) {
      Target *main = add_executable(project, "main");
      add_source_file(main, "code/main.cpp");
      exclude_source_file(main, "code/main.cpp"); 
    }
 */
CBUILD_API void exclude_source_file (Target *target, const char *_file_path);

/*
  Specify upstream 'dependency' for the given 'target' that it should be linked with.
  This creates a link-time build dependency between two targets, i.e cbuild won't link the 'target' until 'dependency'
  wasn't linked successfully. If 'dependency', or one of it's upstream dependencies, fails to link, 'target' won't be linked.

  Example:

    void setup_project (const Arguments *args, Project *project) {
      Target *engine = add_static_library(project, "engine");
      Target *main   = add_executable(project, "main");
      
      link_with_target(main, engine);
    }

 */
CBUILD_API void link_with_target (Target *target, Target *dependency);

/*
  Specify a library for the linked to link the "target" with.
  Cbuild will only pass the value as the option to linker, without doing any extra checks or validation. 

  Example:

    void setup_project (const Arguments *args, Project *project) {
      Target *main = add_executable(project, "main");
      
      link_with_library(main, "kernel32.lib", "libcmt.lib");
    }

 */
CBUILD_API void link_with_library (Target *target, const char *library_name);

/*
  Additional include paths for the tool to check when resolving local headers.
  Specified include paths must be relative to the project's root.

  Example:

    void setup_project (const Arguments *args, Project *project) {
      auto tracy = add_static_library(project, "tracy_client");

      add_source_file(tracy, "libs/tracy/TracyClient.cpp");
      add_include_search_path(tracy, "libs/public");
    }

 */
CBUILD_API void add_include_search_path (Target *target, const char *include_path);

/*
  Returns target's name.
 */
CBUILD_API const char * get_target_name (const Target *target);

#ifdef __cplusplus
}

/*
  C++ helper function for `add_global_compiler_option`.
 */
template <typename... T>
static void add_global_compiler_options (Project *project, const char *option, T&&... more_options) {
  const char *options[] { option, more_options... };
  for (auto it: options) add_global_compiler_option(project, it);
}

/*
  C++ helper function for `add_global_archiver_option`.
 */
template <typename... T>
static void add_global_archiver_options (Project *project, const char *option, T&&... more_options) {
  const char *options[] { option, more_options... };
  for (auto it: options) add_global_archiver_option(project, it);
}

/*
  C++ helper function for `add_global_linker_option`.
 */
template <typename... T>
static void add_global_linker_options (Project *project, const char *option, T&&... more_options) {
  const char *options[] { option, more_options... };
  for (auto it: options) add_global_linker_option(project, it);
}

/*
  C++ helper function for `add_compiler_option`.
 */
template <typename... T>
static void add_compiler_options (Target *target, const char *option, T&&... more_options) {
  const char *options[] { option, more_options... };
  for (auto it: options) add_compiler_option(target, it);
}

/*
  C++ helper function for `remove_compiler_option`.
 */
template <typename... T>
static void remove_compiler_options (Target *target, const char *option, T&&... more_options) {
  const char *options[] { option, more_options... };
  for (auto it: options) remove_compiler_option(target, it);
}

/*
  C++ helper function for `add_archiver_option`.
 */
template <typename... T>
static void add_archiver_options (Target *target, const char *option, T&&... more_options) {
  const char *options[] { option, more_options... };
  for (auto it: options) add_archiver_option(target, it);
}

/*
  C++ helper function for `remove_archiver_option`.
 */
template <typename... T>
static void remove_archiver_options (Target *target, const char *option, T&&... more_options) {
  const char *options[] { option, more_options... };
  for (auto it: options) remove_archiver_option(target, it);
}

/*
  C++ helper function for `add_linker_option`.
 */
template <typename... T>
static void add_linker_options (Target *target, const char *option, T&&... more_options) {
  const char *options[] { option, more_options... };
  for (auto it: options) add_linker_option(target, it);
}

/*
  C++ helper function for `remove_linker_option`.
 */
template <typename... T>
static void remove_linker_options (Target *target, const char *option, T&&... more_options) {
  const char *options[] { option, more_options... };
  for (auto it: options) remove_linker_option(target, it);
}

/*
  C++ helper function for `add_source_file`.
 */
template <typename... T>
static void add_source_files (Target *target, const char *file_path, T&&... more_paths) {
  const char *paths[] { file_path, more_paths... };
  for (auto it: paths) add_source_file(target, it);
}

/*
  C++ helper function for `exclude_source_file`.
 */
template <typename... T>
static void exclude_source_files (Target *target, const char *file_path, T&&... more_paths) {
  const char *paths[] { file_path, more_paths... };
  for (auto it: paths) exclude_source_file(target, it);
}

/*
  C++ helper function for `link_with_target` and `link_with_library`.
 */
template <typename T1, typename... T>
static void link_with (Target *target, T1 dependency, T&&... more_dependencies) {
  struct Link_Target {
    enum Kind { Kind_Target, Kind_String };

    Kind kind;
    union {
      Target *target;
      const char *name;
    };

    Link_Target(Target *_t): kind(Kind_Target), target(_t){}
    Link_Target(const char *n): kind(Kind_String), name(n){}
  };

  Link_Target targets[] { Link_Target(dependency), Link_Target(more_dependencies)... };

  for (auto t: targets) {
    switch (t.kind) {
      case Link_Target::Kind_Target: { link_with_target(target, t.target); break; }
      case Link_Target::Kind_String: { link_with_library(target, t.name);  break; }
    }
  }
}

#endif
