
#pragma once

#include "anyfin/base.hpp"
#include "anyfin/arena.hpp"
#include "anyfin/memory.hpp"
#include "anyfin/meta.hpp"
#include "anyfin/prelude.hpp"

namespace Fin {

struct String;

template <typename T>
concept String_Ref_Convertible = requires (const T &value, Memory_Arena &arena) {
  { to_string(value, arena) } -> Same_Types<String>;
};

template <typename T>
concept String_Value_Convertible = requires (T value, Memory_Arena &arena) {
  { to_string(value, arena) } -> Same_Types<String>;
};

template <typename T>
concept String_Convertible = Convertible_To<T, String> || String_Ref_Convertible<T> || String_Value_Convertible<T>;

  // NOTE: Won't include the terminating null into the length
constexpr usize get_string_length (const char *value) {
  if ((value == nullptr) || (value[0] == '\0')) return 0;

  usize length = 0;
  while (value[length]) length += 1;

  return length;
}

struct String {
  const char *value  = nullptr;
  usize       length = 0;

  fin_forceinline
  constexpr String () = default;

  fin_forceinline
  constexpr String (Byte_Pointer auto _value, usize _length)
    : value { cast_bytes(_value) }, length { _length }
  {
    if (length) fin_ensure(value[length - 1] != '\0');
  }

  template <usize N>
  fin_forceinline
  constexpr String (const Byte_Array<N> auto (&literal)[N])
    : String(cast_bytes(literal), N - 1) {}

  fin_forceinline
  constexpr String (Byte_Pointer auto _value)
    : String(_value, get_string_length(_value)) {}

  fin_forceinline
  constexpr String& operator = (String other) {
    this->value  = other.value;
    this->length = other.length;
    
    return *this;
  }

  constexpr operator bool         (this auto self) { return self.value && self.length; }
  constexpr operator const char * (this auto self) { return self.value; }
  
  constexpr auto operator [] (this auto self, usize idx) { return self.value[idx]; }

  constexpr auto operator + (this auto self, Integral auto offset) {
    return String(self.value + offset, self.length - offset);
  }

  constexpr bool operator == (String other) const {
    if (this->length != other.length) return false;
    if (!this->value || !other.value) return this->value == other.value;

    return compare_bytes<char>(this->value, other.value, this->length);
  }

  template <usize N>
  constexpr bool operator == (const char (&value)[N]) const {
    return *this == String(value);
  }

  constexpr const char * begin (this auto self) { return self.value; }
  constexpr const char * end   (this auto self) { return self.value + self.length; }
};

static_assert(sizeof(String) == 16);

constexpr String copy_string (Memory_Arena &arena, String other) {
  auto memory = reserve<char>(arena, other.length + 1);
  fin_ensure(memory);

  if (!memory) return {};

  copy_memory(memory, other.value, other.length);
  memory[other.length] = '\0';

  return { memory, other.length };
}

constexpr String copy_string (Memory_Arena &arena, Byte_Type auto *bytes, usize count) {
  return copy_string(arena, String(bytes, count));
}

constexpr String copy_string (Memory_Arena &arena, Byte_Type auto *bytes) {
  return copy_string(arena, String(bytes));
}

constexpr String string_replace (Memory_Arena &arena, const String original, const String find, const String replace) {
  if (!find.length || !original.length || find.length > original.length) return String(original.value, original.length);

  usize max_possible_length = original.length + (replace.length > find.length ? (replace.length - find.length) * original.length / find.length : 0);
  auto memory = reserve<char>(arena, max_possible_length + 1);
  fin_ensure(memory);

  if (!memory) return {};

  usize output_index = 0;

  usize i = 0;
  while (i < original.length) {
    if (i + find.length <= original.length && String(original.value + i, find.length) == find) {
      for (usize j = 0; j < replace.length; ++j) {
        memory[output_index++] = replace[j];
      }
      i += find.length;
    } else {
      memory[output_index++] = original[i++];
    }
  }

  memory[output_index] = '\0';

  return String(memory, output_index);
}

constexpr bool is_empty (String view) {
  return view.length == 0;
}

constexpr bool starts_with (String view, String start) {
  if (start.length > view.length) return false;

  for (size_t i = 0; i < start.length; ++i) {
    if (view.value[i] != start.value[i]) return false;
  }

  return true;
}

constexpr bool ends_with (String view, String end) {
  if (end.length > view.length) return false;

  for (size_t i = 0; i < end.length; ++i) {
    if (view.value[view.length - end.length + i] != end.value[i]) {
      return false;
    }
  }

  return true; 
}

constexpr bool contains (const String original, const String substring) {
  if (!substring.length || !original.length || substring.length > original.length) return false;

  for (usize i = 0; i <= original.length - substring.length; ++i) {
    if (String(original.value + i, substring.length) == substring) {
      return true;
    }
  }

  return false;
}

constexpr bool has_substring (String text, String value) {
  if (value.length == 0)          return true;
  if (text.length < value.length) return false;

  for (size_t i = 0; i <= text.length - value.length; i++) {
    if (compare_bytes(text.value + i, value.value, value.length)) return true;
  }

  return false;
}

struct split_string {
  const char *cursor = nullptr;
  const char *end    = nullptr;

  char separator;
  
  constexpr split_string (String _string, char _separator)
    : separator { _separator }
  {
    if (is_empty(_string)) return;
      
    cursor = _string.value;
    end    = cursor + _string.length;
  }

  constexpr void for_each (const Invocable<void, String> auto &func) {
    while (end_reached() == false) {
      if (skip_consequtive_separators()) return;

      auto next_position = get_character_offset(cursor, end - cursor, separator);
      fin_ensure(next_position != cursor); // We've just skipped all separators, this wouldn't make any sense.

      if (next_position) {
        func(String(cursor, next_position - cursor));

        cursor = next_position;
        continue;
      }
      
      func(String(cursor, end - cursor));
      
      cursor = end;
    }
  }

  constexpr bool skip_consequtive_separators () {
    while (!end_reached()) {
      if (*cursor != separator) return false;
      cursor += 1;
    }

    return true;
  }

  constexpr bool end_reached () { return cursor == end; }
};

}
  
