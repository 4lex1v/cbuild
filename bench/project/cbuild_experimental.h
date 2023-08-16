
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

struct Project;
struct Target;
struct Arguments;

enum Hook_Type {
  Hook_Type_After_Target_Linked,
};

typedef void (*Hook_Func) (const Project *project, const Target *target, const Arguments *args, Hook_Type type);

extern "C" {
CBUILD_EXPERIMENTAL_API void add_target_hook (Target *target, Hook_Type type, Hook_Func func);

CBUILD_EXPERIMENTAL_API void exclude_source_file (Target *target, const char *_file_path);
}

#ifdef __cplusplus

#endif
