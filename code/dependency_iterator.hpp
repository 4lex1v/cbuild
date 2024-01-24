
#pragma once

#include "anyfin/base.hpp"

#include "anyfin/meta.hpp"
#include "anyfin/result.hpp"
#include "anyfin/strings.hpp"

#include "anyfin/file_system.hpp"

#include "cbuild.hpp"

struct Dependency_Iterator {
  const File   &file;
  File_Mapping  mapping;
  const char   *cursor;
  const char   *end;

  constexpr Dependency_Iterator (const File &_file, File_Mapping _mapping)
    : file    { _file },
      mapping { _mapping },
      cursor  { mapping.memory },
      end     { mapping.memory + mapping.size }
  {}

  constexpr auto & operator += (usize by) {
    this->cursor += by;
    return *this;
  }

  constexpr auto & operator ++ (int) {
    this->cursor++;
    return *this;
  }
};

enum struct Parse_Error {
  Invalid_Value
};

/*
  Iterates over all user-defined #include directives in the mapped source file retrieving the provided value as-is.
  Resolution of the retrieved file path is left for the caller.
 */
Option<String> get_next_include_value (Dependency_Iterator &iterator);
