
#pragma once

#include "anyfin/meta.hpp"

#include "anyfin/platform.hpp"

namespace Fin {

struct Thread {
  struct Handle;

  Handle *handle;
  u32     id;
};

static Sys_Result<Thread> spawn_thread (const Invocable<void> auto &proc);

template <typename T>
static Sys_Result<Thread> spawn_thread (const Invocable<void, T *> auto &proc, T *data);

static Sys_Result<void> shutdown_thread (Thread &thread);

static void thread_sleep (usize milliseconds);

static u32 get_current_thread_id ();

}

#ifndef FIN_THREADS_HPP_IMPL
  #ifdef PLATFORM_WIN32
    #include "anyfin/threads_win32.hpp"
  #else
    #error "Unsupported platform"
  #endif
#endif
