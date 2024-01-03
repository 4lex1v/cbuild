
#pragma once

#include "anyfin/core/slice.hpp"

#include "cbuild.hpp"

struct CLI_Flags {
  bool silenced = false;
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
  s32 builders_count = -1;

  /*
    Defines the cache (i.e the registry in CBuild's parlance) behavior for the build process.
   */
  Cache_Behavior cache = Cache_Behavior::On;

  /*
    List of targets (names) requested by the user to build.
    Only these targets (and their upstream dependencies) should be built by CBuild.
   */
  Slice<String_View> selected_targets;
};
