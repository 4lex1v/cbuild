
#pragma once

#include "arena.hpp"
#include "base.hpp"
#include "core.hpp"
#include "strings.hpp"

template <String_Convertible... Args>
static void print (Memory_Arena *arena, Format_String &&format, Args&&... args) {
  auto local = *arena;
  auto message = format_string(&local, forward<Format_String>(format), forward<Args>(args)...);

  platform_print_message(message);
}

template <typename T>
static T * copy_memory (T *destination, const T *source, usize count) {
  return reinterpret_cast<T *>(memmove(destination, source, count * sizeof(T)));
}

template <typename T>
static void zero_memory (T *region, usize count) {
  memset(region, 0, count * sizeof(T));
}
