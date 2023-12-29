
#pragma once

#include "anyfin/base.hpp"

#include "anyfin/core/arena.hpp"

#include "cbuild.hpp"

struct Project;
struct Build_Command;

u32 build_project (Memory_Arena &arena, const Project &project, Build_Command config);
