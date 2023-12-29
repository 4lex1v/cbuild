
#pragma once

#include <cstring>

#include "base.hpp"
#include "core.hpp"
#include "arena.hpp"
#include "list.hpp"

struct Memory_Arena;

struct String {
  usize       length = 0;
  const char *value  = nullptr;

  constexpr String ()
    : length { 0 },
      value  { nullptr }
  {}

  constexpr String (const char *_value, usize _length)
    : length { _length },
      value  { _value }
  {}

  constexpr String (const char *_value)
    : value { _value }
  {
    if (this->value == nullptr) return;
    if (this->value[0] == '\0') return;
    
    while (this->value[++this->length]);
  }
  
  operator const char * () { return value; }

  bool is_empty () const {
    if (this->length == 0) assert(this->value == nullptr);
    
    return length == 0;
  }
};

constexpr String copy_string (Memory_Arena *arena, const String &string) {
  if (string.length == 0) return {};
  
  auto space = reserve_array<char>(arena, string.length + 1);
  if (not space) return nullptr;
  
  for (usize idx = 0; idx < string.length; idx++) space[idx] = string.value[idx];
  space[string.length] = '\0';

  return space;
}

constexpr bool compare_strings (const String &first, const String &second) {
  if (first.length != second.length) return false;
  return (strncmp(first.value, second.value, first.length) == 0);
}

constexpr bool contains_string (const String &text, const String &value) {
  if (value.length == 0)          return true;
  if (text.length < value.length) return false;

  for (size_t i = 0; i <= text.length - value.length; i++) {
    if (memcmp(text.value + i, value.value, value.length) == 0) return true;
  }

  return false;
}

template <typename T>
concept Has_To_String_Defined = requires (Memory_Arena *arena, T a) {
  { to_string(arena, a) } -> std::same_as<String>;
};

template <typename T>
concept String_Convertible = std::convertible_to<T, String> || Has_To_String_Defined<T>;

template <std::integral I>
constexpr String to_string (Memory_Arena *arena, I value) {
  if      constexpr (std::is_same_v<I, bool>) return (value) ? String("true", 4) : String("false", 5);
  else if constexpr (std::is_same_v<I, char>) {
    auto memory = reserve_memory(arena, 1, 1);
    *memory = value;

    return String(memory, 1);
  }
  else {
    if (value == 0) return String("0", 1);
      
    auto string_length = static_cast<usize>(log10(value)) + 1;
    auto buffer        = reserve_array<char>(arena, string_length + 1);

    auto copied = value;
    constexpr auto digits = "0123456789";
    for (usize idx = string_length; idx > 0; idx--) {
      buffer[idx - 1] = digits[copied % 10];
      copied /= 10; 
    }

    return String(buffer, string_length);
  }
}

template <String_Convertible S>
constexpr String make_string (Memory_Arena *arena, S &&value) {
  if constexpr (std::convertible_to<S, String>) return static_cast<String>(value);
  else                                          return to_string(arena, value);
};

struct Format_String {
  /*
    Segment is a continious sequence of characters in the given format_string value.
    A new segment is create whenever the format_string has `%` placeholder, where the
    rendered value would be emplaced, or `%%`, tearing the format string in two parts.
   */
  struct Segment {
    enum struct Type: u32 { Text, Placeholder };

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
static String format_string (Memory_Arena *arena, Format_String format, Args&&... args) {
  constexpr usize N = sizeof...(Args);
  assert(format.placeholder_count == N); // I wish this could be done at compile time :sadface:

  auto local = *arena;
  String arguments[N] { make_string(&local, args)... };

  usize reservation_size = format.reservation_size + 1;
  for (auto &a: arguments) reservation_size += a.length;

  auto buffer = reserve_array<char>(&local, reservation_size);
  if (!buffer) return String{};

  arena->offset = local.offset; // comit memory to arena if reservation was successful

  usize cursor    = 0;
  usize arg_index = 0;
  for (usize idx = 0; idx < format.segments_count; idx++) {
    auto &segment = format.segments[idx];

    switch (segment.type) {
      case Format_String::Segment::Type::Text: {
        auto length = segment.end - segment.start;

        memcpy(buffer + cursor, format.format_string + segment.start, length);
        cursor += length;
        
        break;
      }
      case Format_String::Segment::Type::Placeholder: {
        assert(arg_index < N);
        auto entry = arguments + arg_index;

        memcpy(buffer + cursor, entry->value, entry->length);
        cursor += entry->length;

        arg_index += 1;
        
        break;
      }
    }
  }

  buffer[reservation_size - 1] = '\0';

  return String { buffer, reservation_size - 1 };
}

struct String_Builder {
  Memory_Arena *arena;
  List<String> sections = {};

  usize length = 0;

  String_Builder (Memory_Arena *_arena): arena { _arena } {}

  void add (String value) {
    if (value.length == 0) return;

    ::add(this->arena, &this->sections, value);
    this->length += value.length;
  }

  void add (const List<String> &list) {
    for (auto value: list) add(value);
  }

  void operator += (const String &value)        { add(value); }
  void operator += (const List<String> &values) { add(values); }
  
  template <String_Convertible S>
  void operator += (S &&value) { add(make_string(this->arena, value)); }
};

