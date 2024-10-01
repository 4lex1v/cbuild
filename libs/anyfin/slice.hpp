
#pragma once

#include "anyfin/base.hpp"

namespace Fin {

template <typename T>
struct Slice {
  T *values = nullptr;
  usize count = 0;

  fin_forceinline constexpr Slice () = default;

  template <usize N>
  fin_forceinline constexpr Slice (T (&data)[N])
    : Slice(data, N) {}
  
  fin_forceinline constexpr Slice (T *_value, usize _count)
    : values { _value }, count { _count } {}

  fin_forceinline constexpr operator bool (this auto self) { return self.values && self.count; }

  fin_forceinline constexpr decltype(auto) operator [] (this auto &&self, usize offset) { return self.value[offset]; }
  fin_forceinline constexpr decltype(auto) operator *  (this auto &&self)               { return *self.values; }

  fin_forceinline
  constexpr Slice<T> operator + (this auto self, usize offset) {
    assert(offset <= self.count);
    return Slice(self.values + offset, self.count - offset);
  }

  fin_forceinline
  constexpr Slice<T>& operator += (this Slice<T> &self, usize offset) {
    fin_ensure(offset <= self.count);

    self.values += offset;
    self.count  -= offset;

    return self;
  }

  fin_forceinline constexpr Slice<T> operator ++ (this Slice<T> &self, int) { return (self += 1); }

  fin_forceinline constexpr decltype(auto) begin (this auto self) { return self.values; }
  fin_forceinline constexpr decltype(auto) end   (this auto self) { return self.values + self.count; }
};

template <typename T>
constexpr bool is_empty (Slice<T> args) {
  return args.count == 0;
}

}
