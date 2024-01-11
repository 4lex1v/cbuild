
#pragma once

#include "anyfin/base.hpp"

#include "anyfin/core/arena.hpp"
#include "anyfin/core/format.hpp"
#include "anyfin/core/trap.hpp"

namespace Fin {
  namespace Core {};
  namespace Platform {};
}

using namespace Fin::Core;
using namespace Fin::Platform;

#define todo() Fin::Core::trap("Not yet implemented")

#if !defined(TOOL_VERSION) || !defined(API_VERSION)
  #error "TOOL_VERSION and API_VERSION values must be defined at compile time"
#endif

template <usize MEMORY_SIZE = 1024, Fin::Core::Printable... P>
[[noreturn]] static void panic (Fin::Core::Format_String &&format, P&&... args) {
  u8 stack_memory[MEMORY_SIZE];
  auto arena = Memory_Arena(stack_memory, MEMORY_SIZE);

  Fin::Core::trap(Fin::Core::format_string(arena, forward<Fin::Core::Format_String>(format), forward<P>(args)...).value);
  __builtin_unreachable();
}
