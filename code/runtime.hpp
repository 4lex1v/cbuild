
#pragma once

#include "arena.hpp"
#include "base.hpp"
#include "core.hpp"
#include "strings.hpp"

template <String_Convertible... Args>
static void print (Memory_Arena *arena, Format_String &&format, Args&&... args) {
  auto local = *arena;
  auto message = format_string(&local, forward<Format_String>(format), forward<Args>(args)...);

  void platform_print_message (const String &message);
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

static bool compare_ignore_case (const char *s1, const char *s2) {
  auto toupper = [](unsigned char c) { return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c; };
  while(*s1 && (toupper(*s1) == toupper(*s2))) { s1++; s2++; }
  return (not static_cast<bool>(toupper(*s1) - toupper(*s2)));
}
