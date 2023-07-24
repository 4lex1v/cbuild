
#pragma once

#include "base.hpp"

struct String;
struct File_Mapping;

template <typename T> struct Result;

struct Dependency_Iterator {
  const File_Mapping *mapping;
  const char         *cursor = nullptr;

  Dependency_Iterator (const File_Mapping *_mapping);
};

/*
  Iterates over all user-defined #include directives in the mapped source file retrieving the provided value as-is.
  Resolution of the retrieved file path is left for the caller.
 */
Result<bool> get_next_include_value (Dependency_Iterator *iterator, String *include_value);
