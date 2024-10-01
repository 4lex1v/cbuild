
#pragma once

#include "anyfin/base.hpp"
#include "anyfin/arena.hpp"
#include "anyfin/slice.hpp"

namespace Fin {

template <typename T>
struct Array {
  T     *values = nullptr;
  usize  count  = 0;

  fin_forceinline constexpr Array () = default;
  fin_forceinline constexpr Array (T *_values, usize _count)
    : values { _values }, count { _count }
  {}

  fin_forceinline
  constexpr decltype(auto) operator [] (this auto &&self, usize offset) {
    return self.values[offset];
  }

  fin_forceinline constexpr decltype(auto) begin (this auto &&self) { return self.values; }
  fin_forceinline constexpr decltype(auto) end   (this auto &&self) { return self.values + self.count; }
};

template <typename T>
constexpr bool is_empty (const Array<T> &seq) {
  return seq.count == 0;
}

template <typename T = u8>
static Array<T> reserve_array (Memory_Arena &arena, usize count, usize alignment = alignof(T)) {
  fin_ensure(alignment > 0);

  if (count == 0) return Array<T>();

  auto memory = reserve<T>(arena, count * sizeof(T), alignment);
  fin_ensure(memory);
  
  if (!memory) return Array<T>();
  
  return Array(memory, count);
}

template <typename T>
fin_forceinline static Slice<T> slice (const Array<T> &array) {
  return Slice(array.values, array.count);
}

}
