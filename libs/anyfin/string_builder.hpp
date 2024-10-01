
#pragma once

#include "anyfin/base.hpp"
#include "anyfin/strings.hpp"
#include "anyfin/list.hpp"

namespace Fin {

struct String_Builder {
  List<String> sections;
  usize length = 0;

  constexpr String_Builder (Memory_Arena &arena)
    : sections { arena } {}

  constexpr auto& add (this auto &self, String value) {
    if (is_empty(value)) return self;

    list_push(self.sections, move(value));
    self.length += value.length;

    return self;
  }

  constexpr auto& operator += (this String_Builder &self, const String &value) { self.add(value); return self; }
  constexpr auto& operator += (this String_Builder &self, const char   *value) { self.add(value); return self; }

  constexpr auto& operator += (this String_Builder &self, const Iterable<String> auto &values) { for (auto &value: values) self.add(value); return self; }

  constexpr auto& add (this String_Builder &self, Memory_Arena &arena, String_Convertible auto &&... args) {
    const auto print = [&] (auto &&arg) {
      if constexpr (Convertible_To<decltype(arg), String>)
        return static_cast<String>(arg);
      else return to_string(arg, arena);
    };

    (self.add(print(args)), ...);

    return self;
  }
};

static String build_string (Memory_Arena &arena, const String_Builder &builder, bool use_separator, char separator) {
  if (!builder.length) return {};
    
  auto reservation_size = builder.length + 1;
  if (use_separator) reservation_size += builder.sections.count;

  auto buffer = reserve<char>(arena, reservation_size);

  usize offset = 0;
  for (auto &section: builder.sections) {
    copy_memory(buffer + offset, section.value, section.length);
    offset += section.length;

    if (use_separator) buffer[offset++] = separator;
  }
  
  buffer[offset] = '\0';  

  return String(buffer, offset);
}

static String build_string (Memory_Arena &arena, const String_Builder &builder) {
  return build_string(arena, builder, false, 0);
}

static String build_string_with_separator (Memory_Arena &arena, const String_Builder &builder, char separator) {
  return build_string(arena, builder, true, separator);
}

static String concat_string (Memory_Arena &arena, String_Convertible auto &&... args) {
  String_Builder builder { arena };

  const auto append = [&] (auto &&arg) {
    if constexpr (Convertible_To<decltype(arg), String>)
      builder += static_cast<String>(arg);
    else
      builder += to_string(arg, arena);
  };

  (append(args), ...);

  return build_string(arena, builder);
}

}
