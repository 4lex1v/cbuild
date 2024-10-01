
#define FIN_THREADS_HPP_IMPL

#include "anyfin/threads.hpp"

namespace Fin {

template <typename T>
static Sys_Result<Thread> spawn_thread (const Invocable<void, T *> auto &proc, T *data) {
  DWORD thread_id;
  auto handle = CreateThread(nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(proc), data, 0, &thread_id);
  if (!handle) return Error(get_system_error());

  return Ok(Thread { reinterpret_cast<Thread::Handle *>(handle), thread_id });
}

static Sys_Result<Thread> spawn_thread (const Invocable<void> auto &proc) {
  return spawn_thread(nullptr, proc);
}

static Sys_Result<void> shutdown_thread (Thread &thread);

static u32 get_current_thread_id () {
  return GetCurrentThreadId();
}

static void thread_sleep (usize milliseconds) {
  Sleep(milliseconds);
}

}
