
#pragma once

#include "anyfin/core/arena.hpp"
#include "anyfin/core/slice.hpp"

#include "anyfin/platform/startup.hpp"
#include "anyfin/platform/file_system.hpp"

#include "cbuild.hpp"
#include "cbuild_api.hpp"

enum struct Configuration_Type { C, Cpp };

/*
  Initialize the workspace using either language for the configuration file.
 */
void init_workspace (Memory_Arena &arena, const File_Path &working_directory, Configuration_Type type);

/*
  Cleanup current workspace build files.
 */
void cleanup_workspace (Memory_Arena &arena, bool full_cleanup);

/*
  Load main project configuration.
 */
void load_project (Memory_Arena &arena, Project &project, const Slice<Startup_Argument> &args);

/*
  Update CBuild interface files in the workspace.
 */
void update_cbuild_api_file (Memory_Arena &arena, const File_Path &working_directory);

