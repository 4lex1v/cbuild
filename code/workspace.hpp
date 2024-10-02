
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

enum struct Cleanup_Type {
  /*
    Removes produced artifacts for the current project.
   */
  Build,

  /*
    Would remove all files associated with the current project.
   */
  Project,

  /*
    Removes everything for all projects.
   */
  Full
};

/*
  Cleanup current workspace build files.
 */
void cleanup_workspace (Memory_Arena &arena, const File_Path &working_directory, Cleanup_Type type = Cleanup_Type::Build);

/*
  Load main project configuration.
 */
void load_project (Memory_Arena &arena, Project &project, Slice<Startup_Argument> args);

/*
  Update CBuild interface files in the workspace.
 */
void update_cbuild_api_file (Memory_Arena &arena, File_Path working_directory);

/*
  Return the name of the folder for the current project in the cache directory.
  If no --project=<value> overwrite value were passed, this should return a default "project" value, but when an alternative
  configuration has been provided, this would help to resolve what the output directory name should be for that project, under
  the .cbuild directory.
 */
String resolve_project_output_dir_name (Memory_Arena &arena, const File_Path &work_dir);
