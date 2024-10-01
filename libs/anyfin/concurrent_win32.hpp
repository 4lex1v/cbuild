
#define FIN_CONCURRENT_HPP_IMPL

#include "anyfin/math.hpp"
#include "anyfin/concurrent.hpp"

#include "anyfin/win32.hpp"

namespace Fin {

static Sys_Result<Semaphore> create_semaphore (u32 count) {
  auto clamped = clamp<s32>(count, 1, ~0x80000000);

  auto handle = CreateSemaphore(nullptr, 0, clamped, nullptr);
  if (!handle) return Error(get_system_error());
  
  return Ok(Semaphore { reinterpret_cast<Semaphore::Handle *>(handle) });
}

static Sys_Result<void> destroy (Semaphore &semaphore) {
  if (!CloseHandle(semaphore.handle))
    return Error(get_system_error());

  semaphore.handle = nullptr;

  return Ok();
}

static Sys_Result<u32> increment_semaphore (Semaphore &semaphore, u32 increment_value) {
  LONG previous;
  if (!ReleaseSemaphore(semaphore.handle, increment_value, &previous))
    return Error(get_system_error());

  return Ok<u32>(previous);
}

static Sys_Result<void> wait_for_semaphore_signal (const Semaphore &semaphore) {
  if (WaitForSingleObject(semaphore.handle, INFINITE) == WAIT_FAILED)
    return Error(get_system_error());

  return Ok();
}

}
