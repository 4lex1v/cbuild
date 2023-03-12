
#pragma once

#include "base.hpp"

enum struct Memory_Order: u32 {
  Sequential,
  Acquire,
  Release,
  Acquire_Release,
  Whatever,
};

template <typename T>
struct Atomic {
  using Value_Type = T;

  alignas(T) volatile T value {};

  Atomic () = default;
  Atomic (Value_Type v): value { v } {};
};

using abool  = Atomic<bool>;
using au32   = Atomic<u32>;
using as32   = Atomic<s32>;
using au64   = Atomic<u64>;
using ausize = Atomic<usize>;

template <typename T>
using Atomic_Value = typename Atomic<T>::Value_Type;

#ifdef PLATFORM_X64

#ifdef PLATFORM_WIN32
  #include <intrin.h>
#else
  #include <x86intrin.h>
#endif

#define compiler_barrier() do { asm volatile ("" ::: "memory"); } while(0)

template <Memory_Order order = Memory_Order::Whatever, typename T>
static T atomic_load (const Atomic<T> *atomic) {
  using enum Memory_Order;

  static_assert(sizeof(T) <= sizeof(void*));
  static_assert((order == Sequential) || (order == Acquire) || (order == Whatever));

  if constexpr (order == Sequential) compiler_barrier();
  auto result = atomic->value;
  if constexpr ((order == Sequential) || (order == Acquire)) compiler_barrier();

  return result;
}

template <Memory_Order order = Memory_Order::Whatever, typename T>
static void atomic_store (Atomic<T> *atomic, Atomic_Value<T> value) {
  using enum Memory_Order;

  static_assert(sizeof(T) <= sizeof(void*));
  static_assert((order == Sequential) || (order == Release) || (order == Whatever));
  
  if constexpr ((order == Sequential) || (order == Release)) compiler_barrier();
  reinterpret_cast<volatile T &>(atomic->value) = value;
  if constexpr (order == Sequential) compiler_barrier();
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
