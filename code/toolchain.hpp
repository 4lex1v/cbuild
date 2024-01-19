
#pragma once

#include "anyfin/core/option.hpp"
#include "anyfin/core/list.hpp"

#include "cbuild_api.hpp"

Option<Toolchain_Configuration> lookup_toolchain_by_type (Memory_Arena &arena, Toolchain_Type type);

Option<Toolchain_Configuration> discover_toolchain (Memory_Arena &arena);

struct Env_Var {
  String key;
  String value;
};

List<Env_Var> setup_system_sdk (Memory_Arena &arena, Target_Arch architecture);

void reset_environment (List<Env_Var> env);
