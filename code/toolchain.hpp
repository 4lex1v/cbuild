
#pragma once

#include "base.hpp"
#include "cbuild_api.hpp"

template <typename T> struct Result;

Result<Toolchain_Configuration> lookup_toolchain_by_type (Memory_Arena *arena, Toolchain_Type type);

Result<Toolchain_Configuration> discover_toolchain (Memory_Arena *arena);
