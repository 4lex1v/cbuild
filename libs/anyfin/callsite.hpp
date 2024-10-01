
#pragma once

#include "anyfin/base.hpp"

namespace Fin {

struct Callsite {
  u32         line;
  u32         column;
  const char *file;
  const char *function;

  consteval Callsite (const u32 _l = __builtin_LINE(), const u32 _c = __builtin_COLUMN(),
                      const char *_fl = __builtin_FILE(), const char *_fn = __builtin_FUNCTION())
    : line { _l }, column { _c }, file { _fl }, function { _fn }
  {}
};

}
