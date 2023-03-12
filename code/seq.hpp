
#pragma once

#include "base.hpp"

template <typename T>
struct Seq {
  T *    values = nullptr;
  size_t count  = 0;

  Seq () = default;
  Seq (T *_values, const size_t _count): values(_values), count(_count) {};

        T * begin ()       { return values; }
  const T * begin () const { return values; }

        T * end ()       { return values + count; }
  const T * end () const { return values + count; }

  T *       operator + (usize offset)       { return values + offset; }
  T const * operator + (usize offset) const { return values + offset; }

  T & operator [] (usize offset) { return values[offset]; }

  bool is_empty () const { return this->count == 0; }
};

