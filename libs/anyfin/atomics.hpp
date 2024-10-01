
#pragma once

#include "anyfin/base.hpp"

namespace Fin {

// TODO: Obviously this should be arch agnostic at some point
constexpr static usize CACHE_LINE_SIZE = 64;

enum struct Memory_Order: u32 {
  Relaxed,
  Acquire,
  Release,
  Acquire_Release,
  Sequential
};

template <typename T>
struct Atomic {
  using Value_Type = T;

  volatile T value {};

  Atomic () = default;
  Atomic (Value_Type v): value { v } {};
};

template <typename T>
struct alignas(64) Aligned_Atomic: public Atomic<T> {
  using Atomic<T>::Atomic;

  static_assert(sizeof(T) <= 64, "T is too large to fit on a single cache line");

  char padding[64 - sizeof(T)] {0};
};

using abool  = Atomic<bool>;
using au32   = Atomic<u32>;
using as32   = Atomic<s32>;
using au64   = Atomic<u64>;
using as64   = Atomic<s64>;
using ausize = Atomic<usize>;

using cabool  = Aligned_Atomic<bool>;
using cau32   = Aligned_Atomic<u32>;
using cas32   = Aligned_Atomic<s32>;
using cau64   = Aligned_Atomic<u64>;
using cas64   = Aligned_Atomic<s64>;
using causize = Aligned_Atomic<usize>;

}

#ifndef FIN_ATOMICS_HPP_IMPL
  #ifdef CPU_ARCH_X64
    #include "anyfin/atomics_x64.hpp"
  #else
    #error "Unsupported CPU architecture"
  #endif
#endif

