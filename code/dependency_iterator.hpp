
#pragma once

#include "anyfin/base.hpp"

#include "anyfin/core/meta.hpp"
#include "anyfin/core/result.hpp"
#include "anyfin/core/strings.hpp"

#include "anyfin/platform/file_system.hpp"

#include "cbuild.hpp"

struct Dependency_Iterator {
  File_Mapping  mapping;
  const char   *cursor;

  Dependency_Iterator (File_Mapping _mapping)
    : mapping { _mapping },
      cursor  { mapping.memory }
  {}
};

enum struct Parse_Error {
  Invalid_Value
};

/*
  Iterates over all user-defined #include directives in the mapped source file retrieving the provided value as-is.
  Resolution of the retrieved file path is left for the caller.
 */
Fin::Core::Result<Parse_Error, Option<String_View>> get_next_include_value (Dependency_Iterator &iterator);
