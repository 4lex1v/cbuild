
#pragma once

#include "anyfin/base.hpp"
#include "anyfin/arena.hpp"

#include "cbuild.hpp"

struct Project;
struct Build_Command;

enum struct Cache_Behavior {
  // Full use of the caching system. Default behavior
  On,

  // Caching system will not be used.
  Off,

  // Existing cached information will be ignored by the builder.
  // Results of the build will overwrite currently cached information.
  Flush
};

u32 build_project (
  Memory_Arena &arena,
  const Project &project,

  /*
    List of target names that should be built.
    If the list is empty (i.e user didn't provide any specific target names), all targets are built by default.
    If there are targets in the list, only these targets should be built, along with their upstream dependencies.
   */
  const List<String> &selected_targets,

  /*
    Controls the use of registry.
   */
  Cache_Behavior cache,

  /*
    How many builders to spawn for concurrent builds.
   */
  u32 builders_count);
