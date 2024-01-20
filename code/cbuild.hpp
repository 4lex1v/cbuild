
#pragma once

#include "anyfin/base.hpp"

#include "anyfin/core/arena.hpp"
#include "anyfin/core/format.hpp"
#include "anyfin/core/trap.hpp"

namespace Fin {
  namespace Base {};
  namespace Core {};
  namespace Platform {};
}

using namespace Fin::Base;
using namespace Fin::Core;
using namespace Fin::Platform;

#define todo() panic("Not yet implemented")

#if !defined(TOOL_VERSION) || !defined(API_VERSION)
  #error "TOOL_VERSION and API_VERSION values must be defined at compile time"
#endif

template <usize MEMORY_SIZE = 2024, Fin::Core::Printable... P>
[[noreturn]] static void panic (Fin::Core::Format_String &&format, P&&... args) {
  u8 stack_memory[MEMORY_SIZE];
  auto arena = Memory_Arena(stack_memory, MEMORY_SIZE);

  auto message = Fin::Core::format_string(arena, format, forward<P>(args)...);
  Fin::Core::trap(message.value, message.length);
  __builtin_unreachable();
}
