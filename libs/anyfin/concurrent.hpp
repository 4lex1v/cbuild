
#pragma once

#include "anyfin/atomics.hpp"
#include "anyfin/platform.hpp"

namespace Fin {

struct Spin_Lock {
  enum struct Status: u64 { Available = 0, Locked = 1 };

  Atomic<Status> _lock { Status::Available };

  Spin_Lock () = default;

  void lock () {
    using enum Status;
    using enum Memory_Order;

    while (!atomic_compare_and_set<Acquire_Release, Acquire>(this->_lock, Available, Locked));
  }

  void unlock () {
    atomic_store<Memory_Order::Release>(this->_lock, Status::Available);
  }
};

struct Semaphore {
  struct Handle;
  Handle *handle;
};

static Sys_Result<Semaphore> create_semaphore (u32 count = static_cast<u32>(-1));
static Sys_Result<void> destroy (Semaphore &semaphore);

static Sys_Result<u32> increment_semaphore (Semaphore &semaphore, u32 increment_value = 1);

static Sys_Result<void> wait_for_semaphore_signal (const Semaphore &sempahore);

}

#ifndef FIN_CONCURRENT_HPP_IMPL
  #ifdef PLATFORM_WIN32
    #include "anyfin/concurrent_win32.hpp"
  #else
    #error "Unsupported platform"
  #endif
#endif
