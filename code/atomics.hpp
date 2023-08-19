
#pragma once

#include "base.hpp"

enum struct Memory_Order: u32 {
  Whatever,
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

template <typename T>
using Atomic_Value = typename Atomic<T>::Value_Type;

#ifdef PLATFORM_X64

#ifdef PLATFORM_WIN32
  #include <intrin.h>
#else
  #include <x86intrin.h>
#endif

#define compiler_barrier() do { asm volatile ("" ::: "memory"); } while (0)

#define release_fence() do { asm volatile ("sfence" ::: "memory"); } while (0)
#define acquire_fence() do { asm volatile ("lfence" ::: "memory"); } while (0)
#define memory_fence() do { asm volatile ("mfence" ::: "memory"); } while (0)

template <Memory_Order order = Memory_Order::Whatever, typename T>
static T atomic_load (const Atomic<T> *atomic) {
  using enum Memory_Order;

  static_assert(sizeof(T) <= sizeof(void*));
  static_assert((order == Whatever) || (order == Acquire) || (order == Sequential));

  if constexpr (order == Sequential) memory_fence();
  auto result = atomic->value;
  if constexpr (order != Whatever) compiler_barrier();

  return result;
}

template <Memory_Order order = Memory_Order::Whatever, typename T>
static void atomic_store (Atomic<T> *atomic, Atomic_Value<T> value) {
  using enum Memory_Order;

  static_assert(sizeof(T) <= sizeof(void*));
  static_assert((order == Whatever) || (order == Release) || (order == Sequential));

  if constexpr (order == Whatever) {
    atomic->value = value;
  }
  else if constexpr (order == Release) {
    compiler_barrier();
    atomic->value = value;
  }
  else {
    asm volatile (
      "lock xchg %1, %0"
      : "+r"(value), "+m"(atomic->value)
      :
      : "memory"
    );
  }
}

template <Memory_Order order = Memory_Order::Whatever, typename T>
static T atomic_fetch_add (Atomic<T> *atomic, s32 value) {
  T last = value;
  asm volatile (
    "lock xadd %1, %0"
    : "+r"(last), "+m"(atomic->value)
    : 
    : "memory"
  );
  return last;
}

template <Memory_Order order = Memory_Order::Whatever, typename T>
static T atomic_fetch_sub (Atomic<T> *atomic, s32 value) {
  return atomic_fetch_add(atomic, -value);
}

template <Memory_Order success = Memory_Order::Acquire_Release,
          Memory_Order failure = Memory_Order::Acquire,
          typename T>
static bool atomic_compare_and_set (Atomic<T> *atomic, Atomic_Value<T> expected, Atomic_Value<T> new_value) {
  char result;
  asm volatile (
    "lock cmpxchg %1, %3\n"
    "setz %0"
    : "=r"(result), "+m"(atomic->value)
    : "a"(expected), "r"(new_value)
    : "cc", "memory"
  );
  return static_cast<bool>(result);
}

#else
  #error "Unsupported Platform"
#endif
