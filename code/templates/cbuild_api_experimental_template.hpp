
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

#ifdef __cplusplus
  #define CBUILD_NO_EXCEPT noexcept
#else 
  #define CBUILD_NO_EXCEPT
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

CBUILD_EXPERIMENTAL_API Project_Ref * register_external_project (Project *project, const Arguments *args, const char *name, const char *external_project_path) CBUILD_NO_EXCEPT;

CBUILD_EXPERIMENTAL_API Target * get_external_target (Project *project, const Project_Ref *external_project, const char *target_name) CBUILD_NO_EXCEPT;

/*
  Install API
 */

CBUILD_EXPERIMENTAL_API void set_install_location (Project *project, const char *path) CBUILD_NO_EXCEPT;

CBUILD_EXPERIMENTAL_API void install_target (Target *target) CBUILD_NO_EXCEPT;

#ifdef __cplusplus
}
#endif
