
#pragma once

#include "anyfin/arena.hpp"
#include "anyfin/base.hpp"
#include "anyfin/slice.hpp"

namespace Fin {

template <typename T>
struct Seq {
  using Value_Type = T;

  T     *values     = nullptr;
  usize  count    = 0;
  usize  capacity = 0;

  fin_forceinline constexpr Seq () = default;
  fin_forceinline constexpr Seq (T *memory, usize _capacity)
    : values { memory }, capacity { _capacity } {}

  fin_forceinline
  constexpr decltype(auto) operator [] (this auto &&self, usize offset) {
    fin_ensure(offset < self.capacity);
    return self.values[offset];
  }

  fin_forceinline
  constexpr operator Slice<T> (this auto &&self) {
    return Slice(self.values, self.count);
  }
};

template <typename T>
fin_forceinline
static void seq_push (Seq<T> &seq, typename Seq<T>::Value_Type &&value) {
  seq[seq.count] = move(value);
  seq.count     += 1;
}

template <typename T>
fin_forceinline
static void seq_push_copy (Seq<T> &seq, const typename Seq<T>::Value_Type &value) {
  seq[seq.count] = value;
  seq.count     += 1;
}

template <typename T>
static Seq<T> reserve_seq (Memory_Arena &arena, usize count, usize alignment = alignof(T)) {
  if (count == 0) return {};

  auto memory = reserve<T>(arena, count * sizeof(T), alignment);
  fin_ensure(memory);

  if (!memory) return {};

  return Seq(memory, count);
}

}
