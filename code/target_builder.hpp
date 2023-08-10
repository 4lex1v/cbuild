
#pragma once

#include "base.hpp"

struct Project;
struct Memory_Arena;
struct Status_Code;
struct Arguments;
struct Build_Config;

Status_Code build_project (Memory_Arena *arena, const Project *project, Build_Config config);
