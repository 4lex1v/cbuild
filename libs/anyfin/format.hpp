
#pragma once

#include "anyfin/base.hpp"
#include "anyfin/strings.hpp"

namespace Fin {

struct Format_String {
  /*
    Segment is a continious sequence of characters in the given format_string value.
    A new segment is create whenever the format_string has `%` placeholder, where the
    rendered value would be emplaced, or `%%`, tearing the format string in two parts.
   */
  struct Segment {
    enum Type: u32 { Text, Placeholder };

    Type type;

    // These fields are valid for Text segment types
    u16  start;
    u16  end;
  };

  constexpr static usize Segments_Count_Limit = 16;

  const char *format_string;
  usize       format_string_count;

  Segment segments[Segments_Count_Limit] {};

  usize   segments_count    = 0;
  usize   placeholder_count = 0;
  usize   reservation_size  = 0;

  template <usize STRING_LITERAL_LENGTH>
  consteval Format_String (const char (&format)[STRING_LITERAL_LENGTH])
    : format_string       { format },
      format_string_count { STRING_LITERAL_LENGTH - 1 }
  {
    static_assert(STRING_LITERAL_LENGTH > 1, "Empty string in formatter is not allowed");

    usize last = 0;
    for (usize idx = 0; idx < format_string_count; idx++) {
      if (format[idx] == '%') {
        u16 start  = last;
        u16 end    = idx;
        u16 length = end - start;

        auto text = Segment { .type = Segment::Type::Text, .start = start, .end = end };

        if (format[idx + 1] == '%') {
          text.end += 1;

          segments[segments_count]  = text;
          segments_count           += 1;
          reservation_size         += text.end - text.start;

          idx  += 1;
          last  = (idx + 1);

          continue;
        }

        // This is a valid scenario if a placeholder '%' is at the beginning of the string.
        if (length > 0) {
          segments[segments_count]  = text;
          segments_count           += 1;
          reservation_size         += text.end - text.start;
        }

        auto placeholder = Segment { .type = Segment::Type::Placeholder };
        
        segments[segments_count] = placeholder;
        segments_count    += 1;
        placeholder_count += 1;

        last = (idx + 1);
      }
    }

    if (last != format_string_count) {
      u16 start = static_cast<u16>(last);
      u16 end   = format_string_count;

      auto segment = Segment {.type = Segment::Type::Text, .start = start, .end = end };

      segments[segments_count]  = segment;
      segments_count           += 1;
      reservation_size         += end - start;
    }
  }
};

template <String_Convertible... Args>
static void process_segment (Memory_Arena &arena, usize index, Format_String &format, Args&&... args);

template <String_Convertible... Args>
static void print_text (Memory_Arena &arena, usize index, Format_String &format, Args&&... args) {
  fin_ensure(index < format.segments_count);

  auto &segment = format.segments[index];
  fin_ensure(segment.type == Format_String::Segment::Text);

  auto length = segment.end - segment.start;
  auto buffer = reserve<char>(arena, length);

  copy_memory(buffer, format.format_string + segment.start, length);
  
  process_segment(arena, index + 1, format, args...);
}

template <String_Convertible T, String_Convertible... Args>
static void print_argument (Memory_Arena &arena, usize index, Format_String &format, T &&arg, Args&&... rest) {
  fin_ensure(index < format.segments_count);
  
  auto &segment = format.segments[index];
  fin_ensure(segment.type == Format_String::Segment::Placeholder);

  if constexpr (Convertible_To<T, String>) {
    auto str = static_cast<String>(arg);
    if (!is_empty(str)) {
      auto buffer = reserve<char>(arena, str.length);
      copy_memory(buffer, str.value, str.length);
    }
  }
  else {
    auto str = to_string(forward<T>(arg), arena);
    fin_ensure(!is_empty(str));

    /*
      To main proper C strings interface with null terminators to_string would place a \0 after the renderered string.
      For the formatting purposes we don't need that and adjust the arena manually to "offset" the terminator.
     */
    if (str.length) [[likely]] {
      fin_ensure(str.value[str.length] == '\0');
      arena.offset -= 1;
    }
  }

  process_segment(arena, index + 1, format, rest...);
}

template <String_Convertible... Args>
static void process_segment (Memory_Arena &arena, usize index, Format_String &format, Args&&... args) {
  if (index == format.segments_count) return;
  
  auto &segment = format.segments[index];
  switch (segment.type) {
    case Format_String::Segment::Text:        { print_text(arena, index, format, args...);     break; }
    case Format_String::Segment::Placeholder: { print_argument(arena, index, format, args...); break; }
  }
}

template <>
void process_segment (Memory_Arena &arena, usize index, Format_String &format) {
  if (index == format.segments_count) return;

  auto &segment = format.segments[index];
  fin_ensure(segment.type == Format_String::Segment::Text);

  print_text(arena, index, format);
}

template <String_Convertible... Args>
static String format_string (Memory_Arena &arena, Format_String &&format, Args&&... args) {
  constexpr usize N = sizeof...(Args);
  fin_ensure(N == format.placeholder_count);

  if constexpr (N == 0) {
    return copy_string(arena, format.format_string, format.format_string_count);
  }
  else {
    auto buffer = get_memory_at_current_offset<char>(arena);
    process_segment(arena, 0, format, args...);

    auto end = reserve<char>(arena);
    fin_ensure(end);
    end[0] = '\0';

    auto length = end - buffer;
    fin_ensure(length > 0);

    fin_ensure(get_memory_at_current_offset<char>(arena) == (end + 1));

    return String(buffer, length);
  }
}

}
