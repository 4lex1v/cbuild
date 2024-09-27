
#pragma once

#include "anyfin/arena.hpp"
#include "anyfin/startup.hpp"
#include "anyfin/file_system.hpp"

#include "cbuild.hpp"
#include "cbuild_api.hpp"

enum struct Configuration_Type { C, Cpp };

/*
  Initialize the workspace using either language for the configuration file.
 */
void init_workspace (Memory_Arena &arena, File_Path working_directory, Configuration_Type type);

/*
  Cleanup current workspace build files.
 */
void cleanup_workspace (const Project &project, bool full_cleanup);

/*
  Load main project configuration.
 */
void load_project (Memory_Arena &arena, Project &project, Slice<Startup_Argument> args);

/*
  Update CBuild interface files in the workspace.
 */
void update_cbuild_api_file (Memory_Arena &arena, File_Path working_directory);

