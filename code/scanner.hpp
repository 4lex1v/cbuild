
#pragma once

#include "anyfin/base.hpp"
#include "anyfin/file_system.hpp"

#include "cbuild.hpp"
#include "registry.hpp"

enum struct Chain_Status: u32 {
  Unchecked,
  Checking,
  Updated,
  Unchanged,
};

struct Chain_Scanner {
  Registry   &registry;
  Update_Set &update_set;

  Array<Chain_Status> status_cache;
  
  constexpr Chain_Scanner (Memory_Arena &arena, Registry &_registry, Update_Set &_update_set)
    : registry     { _registry },
      update_set   { _update_set },
      status_cache { reserve_array<Chain_Status>(arena, max_supported_files_count) }
  {}
};

/*
  Scans the dependency chain of a translation unit, checking if it or any included header file has been changed by the user,
  which should trigger recompilation of that file.

  Returns true if the chain has any updates, false otherwise.
 */
bool scan_dependency_chain (Memory_Arena &arena, Chain_Scanner &scanner, const List<File_Path> &extra_include_directories, const File &file);
