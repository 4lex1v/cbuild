
#include "anyfin/base.hpp"

#include "anyfin/core/result.hpp"

#include "dependency_iterator.hpp"

static const char* skip_to_any_symbol (const char* str, size_t size, const char* symbols) {
  for (size_t i = 0; i < size; i++) {
    const char* p = symbols;
    while (*p) {
      if (str[i] == *p) return &str[i];
      p++;
    }
  }
  return nullptr;
}

static bool compare_memory (const char *source, const char *value, const usize length) {
  for (usize idx = 0; idx < length; idx++) {
    if (source[idx] != value[idx]) return false;
  }

  return true;
}

static const char* find_substring (const char *memory, size_t memory_size, const char *value, size_t value_size) {
  if (value  == nullptr || value_size  == 0) return nullptr;
  if (memory == nullptr || memory_size == 0) return nullptr;
  if (memory_size < value_size)              return nullptr;

  auto end = memory + memory_size - value_size + 1;

  while (memory < end) {
    if (!compare_memory(memory, value, value_size)) return memory;
    memory++;
  }

  return nullptr;
}

Fin::Core::Result<Parse_Error, Option<String_View>> get_next_include_value (Dependency_Iterator &iterator) {
  auto cursor = iterator.cursor;
  auto end    = iterator.mapping.memory + iterator.mapping.size;

  auto &&None = Fin::Core::Ok(Option<String_View>());

  while (cursor < end) {
    if (*cursor == '\"') {
      if (cursor == iterator.mapping.memory || (*(cursor - 1) != 'R')) {
        while (cursor < end) {
          auto search_from_position = cursor + 1;
          if (search_from_position == end) return None;

          cursor = get_character_offset_reversed(search_from_position, end - search_from_position, '"');  
          if (cursor == nullptr) return None;

          cursor += 1;

          if (*(cursor - 2) != '\\') break;

          /*
            Have to do this for now because on the next cycle I'm looking up from cursor + 1, which may lead to 
            incorrect results in cases like \"" where we'll miss the closing quote. Should make a cleaner version
            at some point later.
          */
          cursor -= 1;
        }
        
        continue;
      }

      /*
        Raw String Handling, who on earth came up with this design :facepalm:
      */
      auto safe_word = cursor + 1;
      if (safe_word != end && *safe_word == '(') {
        auto raw_string_end = find_substring(cursor, end - cursor, ")\"", 2);
        if (raw_string_end == nullptr) return None;

        cursor = raw_string_end + 2;
          
        continue;
      }

      auto end_of_safe_word = get_character_offset_reversed(safe_word, end - safe_word, '(');
      if (end_of_safe_word == nullptr) return None;

      auto safe_word_length = end_of_safe_word - safe_word;
      char raw_string_closing_path[64] = { ')' };
      copy_memory(raw_string_closing_path + 1, safe_word, safe_word_length);

      auto raw_string_closing_path_length = safe_word_length + 1;
      auto raw_string_end_position = find_substring(end_of_safe_word, end - end_of_safe_word,
                                                    raw_string_closing_path, raw_string_closing_path_length);
      if ((raw_string_end_position == nullptr) ||
          (raw_string_end_position[raw_string_closing_path_length] != '"')) {
        return Fin::Core::Error(Parse_Error::Invalid_Value); // Unclosed string literal, TODO: report a proper error message
      }

      auto position = raw_string_end_position + safe_word_length + 1;

      cursor = position + 1; // closing " for a string literal

      continue;
    }

    if (*cursor == '\'') {
      while (cursor < end) {
        auto search_from_position = cursor + 1;
        if (search_from_position == end) return None;

        cursor = get_character_offset_reversed(search_from_position, end - search_from_position, '\'');  
        if (cursor == nullptr) return None;

        cursor += 1;

        if (*(cursor - 2) != '\\') break;

        /*
          Have to do this for now because on the next cycle I'm looking up from cursor + 1, which may lead to 
          incorrect results in cases like \"" where we'll miss the closing quote. Should make a cleaner version
          at some point later.
        */
        cursor -= 1;
      }
    }

    if (*cursor == '#') {
      if (compare_bytes(cursor, "#include", 8) != 0) {
        cursor += 1;
        continue;
      }

#define advance_by(VALUE)                         \
      do {                                        \
        if ((cursor + VALUE) >= end) return None; \
        cursor += VALUE;                          \
      } while(0)

      advance_by(8);

      while (*cursor == ' ') advance_by(1);
      if (*cursor == '<') {
        cursor = get_character_offset_reversed(cursor, end - cursor, '>');
        if (cursor == nullptr) return None;

        cursor += 1;
        continue;
      }

      assert(*cursor == '"');
      advance_by(1);

      auto file_path_start = cursor;

      while (*cursor != '"') advance_by(1);

      auto include = String_View(file_path_start, cursor - file_path_start);

      iterator.cursor = cursor + 1;

#undef advance_by

      return Fin::Core::Ok(Option(move(include)));
    }

    if (*cursor == '/') {
      if ((cursor + 1) < end) {
        if (cursor[1] == '/') {
          cursor = get_character_offset_reversed(cursor, end - cursor, '\n');
          if (cursor == nullptr) return None;
          cursor += 1;
          continue;
        }

        if (cursor[1] == '*') {
          auto position = find_substring(cursor, end - cursor, "*/", 2);
          if (position == nullptr) return None;
          cursor = position + 2;
          continue;
        }
      }

      cursor += 1;
    }
    
    cursor = skip_to_any_symbol(cursor, end - cursor, "/#'\"");
    if (cursor == nullptr) return None;
  }

  return None;
}

// static void register_file_dependencies (const char *file_path, int offset = 0) {
//   auto mapping = map_file_into_memory(file_path);
//   if (!mapping) return;

//   defer { unmap_file(&mapping); };
  
//   char                path_buffer[256] = {};
//   Dependency_Iterator iterator         = { &mapping };
//   String         include_value    = {};

//   while (retrieve_next_include_value(&iterator, &include_value)) {
//     snprintf(path_buffer, 256, "code/%.*s", (int) include_value.count, include_value.offset);
//     register_file_dependencies(path_buffer, offset + 4);
//   }
// }
