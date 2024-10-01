
#define FIN_ATOMICS_HPP_IMPL

#include "anyfin/atomics.hpp"

namespace Fin {

template <typename T>
using Atomic_Value = typename Atomic<T>::Value_Type;

#define fin_compiler_barrier() do { asm volatile ("" ::: "memory"); } while (0)
#define fin_release_fence() do { asm volatile ("sfence" ::: "memory"); } while (0)
#define fin_acquire_fence() do { asm volatile ("lfence" ::: "memory"); } while (0)
#define fin_memory_fence() do { asm volatile ("mfence" ::: "memory"); } while (0)

template <Memory_Order order = Memory_Order::Relaxed, typename T>
static T atomic_load (const Atomic<T> &atomic) {
  using enum Memory_Order;

  static_assert(sizeof(T) <= sizeof(void*));
  static_assert((order == Relaxed) || (order == Acquire) || (order == Sequential));

  if constexpr (order == Sequential) fin_memory_fence();
  auto result = atomic.value;
  if constexpr (order != Relaxed) fin_compiler_barrier();

  return result;
}

template <Memory_Order order = Memory_Order::Relaxed, typename T>
static void atomic_store (Atomic<T> &atomic, Atomic_Value<T> value) {
  using enum Memory_Order;

  static_assert(sizeof(T) <= sizeof(void*));
  static_assert((order == Relaxed) || (order == Release) || (order == Sequential));

  if constexpr (order == Relaxed) {
    atomic.value = value;
  }
  else if constexpr (order == Release) {
    fin_compiler_barrier();
    atomic.value = value;
  }
  else {
    asm volatile (
      "lock xchg %1, %0"
      : "+r"(value), "+m"(atomic.value)
      :
      : "memory"
    );
  }
}

template <Memory_Order order = Memory_Order::Relaxed, typename T>
static T atomic_fetch_add (Atomic<T> &atomic, s32 value) {
  T last = value;
  asm volatile (
    "lock xadd %1, %0"
    : "+r"(last), "+m"(atomic.value)
    : 
    : "memory"
  );
  return last;
}

template <Memory_Order order = Memory_Order::Relaxed, typename T>
static T atomic_fetch_sub (Atomic<T> &atomic, s32 value) {
  return atomic_fetch_add<order>(atomic, -value);
}

template <Memory_Order success = Memory_Order::Acquire_Release,
          Memory_Order failure = Memory_Order::Acquire,
          typename T>
static bool atomic_compare_and_set (Atomic<T> &atomic, Atomic_Value<T> expected, Atomic_Value<T> new_value) {
  Atomic_Value<T> result;
  asm volatile (
    "lock cmpxchg %1, %3\n"
    : "=a"(result), "+m"(atomic.value)
    : "a"(expected), "r"(new_value)
    : "cc", "memory"
  );

  return result == expected;
}

}
