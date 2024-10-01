
#pragma once

#include "anyfin/base.hpp"
#include "anyfin/callsite.hpp"
#include "anyfin/arena.hpp"
#include "anyfin/strings.hpp"
#include "anyfin/string_builder.hpp"

namespace Fin {

static auto to_string (bool value, Memory_Arena &arena) {
  return copy_string(arena, value ? String("true") : String("false"));
}

static String to_string (char value, Memory_Arena &arena) {
  auto memory = reserve(arena, sizeof(char) + 1, alignof(char));
  fin_ensure(memory);

  if (!memory) return {};

  memory[0] = value;
  memory[1] = '\0';

  return String(memory, 1);
}

static auto to_string (Byte_Pointer auto value, Memory_Arena &arena) {
  return copy_string(arena, String(value));
}

template <usize N>
static auto to_string (Byte_Array<N> auto (&value)[N], Memory_Arena &arena) {
  return copy_string(arena, String(value, N - 1));
}

template <Integral I>
static String to_string (I value, Memory_Arena &arena) {
  char buffer[20];
  usize offset = 0;
  
  bool is_negative = false;
  if constexpr (Signed_Integral<I>) {
    if (value < 0) {
      is_negative = true;
      value       = -value; 
    }
  }

  do {
    const auto digit = value % 10;
    buffer[offset++] = '0' + digit;
    value /= 10;
  } while (value != 0);

  if constexpr (Signed_Integral<I>) {
    if (is_negative) buffer[offset++] = '-';
  }

  auto string = reserve<char>(arena, offset + 1);
  for (usize idx = 0; idx < offset; idx++) {
    string[idx] = buffer[offset - 1 - idx];
  }
  string[offset] = '\0';
  
  return String(string, offset);
}

static auto to_string (const Callsite &callsite, Memory_Arena &arena) {
  auto file_length = get_string_length(callsite.file);
  auto func_length = get_string_length(callsite.function);

  fin_ensure(file_length + func_length + 22 < get_remaining_size(arena));

  auto string = get_memory_at_current_offset(arena);

  {
    auto buffer = reserve<char>(arena, file_length);
    copy_memory(buffer, callsite.file, file_length);
  }

  {
    auto buffer = reserve<char>(arena);
    *buffer = '(';
  }
  
  {
    char temp_buffer[20];
    usize offset = 0;

    auto value = callsite.line;

    do {
      const auto digit = value % 10;
      temp_buffer[offset++] = '0' + digit;
      value /= 10;
    } while (value != 0);

    auto buffer = reserve<char>(arena, offset);
    for (usize idx = 0; idx < offset; idx++) {
      buffer[idx] = temp_buffer[offset - 1 - idx];
    }
  }

  {
    auto buffer = reserve<char>(arena, 2);
    buffer[0] = ')';
    buffer[1] = ':';
  }

  {
    auto buffer = reserve<char>(arena, func_length + 1);
    copy_memory(buffer, callsite.function, func_length);
    buffer[func_length] = '\0';
  }

  auto length = get_memory_at_current_offset(arena) - string - 1;

  return String(string, length);
}

}
