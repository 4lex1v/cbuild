
#pragma once

#include "base.hpp"

struct Memory_Arena;
struct Status_Code;
struct File_Path;
struct Arguments;
struct Project;

template <typename T> struct Result;

Status_Code create_new_project_in_workspace (Memory_Arena *arena, bool create_c_project = false);
Status_Code update_cbuild_api_file (Memory_Arena *arena);

Result<File_Path> discover_build_file (Memory_Arena *arena);

Status_Code load_project (Memory_Arena *arena, const Arguments *args, Project *project);

