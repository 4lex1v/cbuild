
#pragma once

#include "base.hpp"
#include "cbuild_api.hpp"

struct String;

template <typename T> struct List;
template <typename T> struct Result;

Result<Toolchain_Configuration> lookup_toolchain_by_type (Memory_Arena *arena, Toolchain_Type type);

Result<Toolchain_Configuration> discover_toolchain (Memory_Arena *arena);

List<Pair<String, String>> setup_system_sdk (Memory_Arena *arena, Target_Arch architecture);
void reset_environment (const List<Pair<String, String>> *env);
