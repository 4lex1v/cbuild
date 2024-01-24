
#pragma once

#include "cbuild.hpp"

struct CLI_Flags {
  bool silenced = false;
  bool tracing  = false;
};

struct Build_Config {
  enum struct Cache_Behavior {
    // Full use of the caching system. Default behavior
    On,

    // Caching system will not be used.
    Off,

    // Existing cached information will be ignored by the builder.
    // Results of the build will overwrite currently cached information.
    Flush
  };

  /*
    Number of additional build processes to spawn with CBuild as requested by the user.
    If none is specified, the number of logical cores would be used by default.
   */
  u32 builders_count = static_cast<u32>(-1);

  /*
    Defines the cache (i.e the registry in CBuild's parlance) behavior for the build process.
   */
  Cache_Behavior cache = Cache_Behavior::On;

  /*
    List of targets (names) requested by the user to build.
    Only these targets (and their upstream dependencies) should be built by CBuild.
   */
  List<String> selected_targets;
};
