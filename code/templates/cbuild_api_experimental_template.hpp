
/*
  WARNING 1: This file is managed by the cbuild tool. Please, avoid making any manual changes to this file,
             as they are likely to be lost.

  WARNING 2: This file contains the experimental API.
 */

#pragma once

#if defined(CBUILD_PROJECT_CONFIGURATION) && defined(_WIN32)
#define CBUILD_EXPERIMENTAL_API __declspec(dllimport)
#else
#define CBUILD_EXPERIMENTAL_API
#endif

#ifndef CBUILD_NO_EXCEPT
#if defined(__cplusplus) && !defined(CBUILD_ENABLE_EXCEPTIONS)
  #define CBUILD_NO_EXCEPT noexcept
#else 
  #define CBUILD_NO_EXCEPT
#endif
#endif

typedef struct Project Project;
typedef struct Project_Ref Project_Ref;
typedef struct Target Target;
typedef struct Arguments Arguments;

enum Hook_Type {
  Hook_Type_After_Target_Linked,
};

typedef void (*Hook_Func) (const Project *project, const Target *target, const Arguments *args, Hook_Type type) CBUILD_NO_EXCEPT;

#ifdef __cplusplus
extern "C" {
#endif

CBUILD_EXPERIMENTAL_API void add_target_hook (Target *target, Hook_Type type, Hook_Func func) CBUILD_NO_EXCEPT;

CBUILD_EXPERIMENTAL_API const char * get_generated_binary_file_path (const Target *target) CBUILD_NO_EXCEPT;

CBUILD_EXPERIMENTAL_API void set_install_location (Project *project, const char *binary_folder, const char *library_folder) CBUILD_NO_EXCEPT;
CBUILD_EXPERIMENTAL_API void install_target (Target *target, const char *install_target_overwrite) CBUILD_NO_EXCEPT;

CBUILD_EXPERIMENTAL_API void add_global_system_include_search_path (Target *target, const char *include_path) CBUILD_NO_EXCEPT;
CBUILD_EXPERIMENTAL_API void add_system_include_search_path (Target *target, const char *include_path) CBUILD_NO_EXCEPT;

CBUILD_EXPERIMENTAL_API const char * find_executable (Project *project, const char *name) CBUILD_NO_EXCEPT;

CBUILD_EXPERIMENTAL_API int run_system_command (Project *project, const char *command_name, char *buffer, unsigned int buffer_size, unsigned int *written_size) CBUILD_NO_EXCEPT;

#ifdef __cplusplus
}
#endif
