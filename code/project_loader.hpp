
#pragma once

#include "anyfin/core/arena.hpp"
#include "anyfin/core/slice.hpp"
#include "anyfin/core/lifecycle.hpp"

#include "anyfin/platform/files.hpp"

#include "cbuild.hpp"

struct Project;

enum struct Configuration_Type { C, Cpp };

/*
  Initialize the workspace using either language for the configuration file.
 */
void init_workspace (Memory_Arena &arena, const File_Path &working_directory, Configuration_Type type);

/*
  Cleanup current workspace build files.
 */
void cleanup_workspace (bool full_cleanup);

/*
  Load project configuration.
 */
Project load_project (
  Memory_Arena &arena,

  /*
    Project name is required for a chained load of dependent CBuild-based projects.
   */
  const String_View &project_name,

  /*
    Path to the main workspace directory of the main project.
   */
  const File_Path &working_directory,

  /*
    Path to the root directory of the project, i.e it's the folder where the "project" directory is, that contains
    project's configuration file - build.c[pp].

    For the main project, this is expected to be the same as the working directory path, but would be different
    for external projects.
   */
  const File_Path &workspace_directory,

  /*
    List of arguments that will be made available to the user's configuration.
    Effectively, these are all the command line values that go after the "build" command, including
    those that are used by CBuild itself (e.g cache=off and the likes).
   */
  const Slice<Startup_Argument> &args);

/*
  Update CBuild interface files in the workspace.
 */
void update_cbuild_api_file (Memory_Arena &arena, const File_Path &working_directory);

