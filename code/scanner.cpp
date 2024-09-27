
#include "anyfin/base.hpp"
#include "anyfin/console.hpp"
#include "anyfin/array_ops.hpp"

#include "scanner.hpp"

extern bool tracing_enabled_opt;

struct Dependency_Iterator {
  const File   &file;
  File_Mapping  mapping;
  const char   *cursor;
  const char   *end;

  constexpr Dependency_Iterator (const File &_file, File_Mapping _mapping)
    : file    { _file },
      mapping { _mapping },
      cursor  { mapping.memory },
      end     { mapping.memory + mapping.size }
  {}

  constexpr auto & operator += (usize by) {
    this->cursor += by;
    return *this;
  }

  constexpr auto & operator ++ (int) {
    this->cursor++;
    return *this;
  }
};

enum struct Parse_Error {
  Invalid_Value
};

/*
  Iterates over all user-defined #include directives in the mapped source file retrieving the provided value as-is.
  Resolution of the retrieved file path is left for the caller.
 */
Option<String> get_next_include_value (Dependency_Iterator &iterator);

static bool advance (Dependency_Iterator &iterator, usize by = 1) {
  if (iterator.cursor == iterator.end) return false;

  iterator.cursor += by;

  return iterator.cursor < iterator.end;
}

static const char * skip_to_next_symbol (Dependency_Iterator &iterator) {
  while (iterator.cursor < iterator.end) {
    if (*iterator.cursor == '/')  return iterator.cursor;
    if (*iterator.cursor == '\'') return iterator.cursor;
    if (*iterator.cursor == '"')  return iterator.cursor;
    if (*iterator.cursor == '#')  return iterator.cursor;

    iterator.cursor += 1;
  }

  return nullptr;
}

static const char* find_substring (const char *memory, const char *end, const char *value, size_t value_size) {
  if (memory == nullptr || end == nullptr)   return nullptr;
  if (value  == nullptr || value_size  == 0) return nullptr;

  while ((memory + value_size) <= end) {
    if (compare_bytes(memory, value, value_size)) return memory;
    memory++;
  }

  return nullptr;
}

enum Parsing_Status {
  End_Of_Parsing = 0,
  Continue       = 1,
};

static Parsing_Status skip_string_literal (Dependency_Iterator &iterator) {
  auto cursor = iterator.cursor;
  auto end    = iterator.end;

  /*
    It's expected that the iterator points the at openning string literal quote.
    We need to find a correct closing literal quote handling regular string literals as well as
    raw-string literals.
   */
  fin_ensure(*cursor == '"');

  if ((cursor == end) || (cursor + 1) == end) return End_Of_Parsing;

  const bool is_raw_string = (cursor > iterator.mapping.memory) && (*(cursor - 1) == 'R');
  
  if (is_raw_string == false) {
    /*
      Can't just search for the first ", it could be escaped with \, in which case we continue the search.
     */
    auto search_start_position  = cursor + 1;
    auto closing_quote_position = static_cast<const char *>(nullptr);

    while (search_start_position < end) {
      closing_quote_position = get_character_offset(search_start_position, end, '"');

      if (closing_quote_position == nullptr) return End_Of_Parsing; // most likely a bad source code with unclosed string literal?
      if (closing_quote_position == end)     return End_Of_Parsing; // we've reached the end of the source code

      if (closing_quote_position == (search_start_position + 1)) { // empty string literal, i.e ""
        fin_ensure(*closing_quote_position == '"');
        break;
      }

      const char previous_symbol     = *(closing_quote_position - 1);
      const bool is_escape_backslach = previous_symbol == '\\';
      if (!is_escape_backslach) break; // it's the actual closing quote of the string literal

      search_start_position = closing_quote_position + 1;
    }

    if (closing_quote_position == end) return End_Of_Parsing;

    iterator.cursor = closing_quote_position + 1;

    return Continue;
  }

  /*
    Raw String Handling, who on earth came up with this design :facepalm:

    To correctly find the closing quote we should parse the closing sequence, which may or may not
    be present in the string.
  */

  const bool has_closing_sequence = cursor[1] != '(';

  if (has_closing_sequence == false) {
    auto search_start_position = cursor + 2; // this would skip the openning (
    if (search_start_position == end) return End_Of_Parsing;

    auto raw_string_end = find_substring(search_start_position, end, ")\"", 2);
    if (raw_string_end == nullptr) return End_Of_Parsing; //most likely an unclosed string literal

    if (raw_string_end + 2 == end) return End_Of_Parsing;

    iterator.cursor = raw_string_end + 2;
    return Continue;
  }

  /*
    Otherwise let's try and pull the closing sequence out.
    The goal is to find the first ( character on the same line where the openning quote is, otherwise it's a malformed
    raw-string literal.
  */

  const char *closing_sequence_start = cursor + 1;
  if (closing_sequence_start + 1 == end) return End_Of_Parsing;

  const char *closing_sequence_end = closing_sequence_start + 1;
  while (closing_sequence_end < end) {
    auto character = closing_sequence_end[0];

    if (character == '(') break;

    /*
      This should violate the grammar raw string literals, the closing sequence with open paren ( should be fully defined
      on a single line.
    */
    if (character == '\r' || character == '\n') {
      log("WARNING: Incomplete raw-string literal closing sequence found while parsing %."
            " Invalid source code cannot be properly parsed by CBuild to check if the dependency tree "
            "(i.e files #included into the translation unit) were not updated. This file will be skipped "
            "and rebuild. If there are not issues with the file and it could be compiled, please report this bug.\n",
            iterator.file.path);
      return End_Of_Parsing;
    }

    closing_sequence_end += 1;
  }

  fin_ensure(*closing_sequence_end == '(');

  auto closing_sequence_length = closing_sequence_end - closing_sequence_start;
  if (closing_sequence_length > 64) {
    /*
      Spec limits the length of the closing sequence to 16, just in case we'll handle up to 64, but warn anyway.
     */
    log("WARNING: Raw-string literal's closing sequence '%' is bigger than the allowed limit of 16 characters (https://en.cppreference.com/w/cpp/language/string_literal) in file %",
          String(closing_sequence_start, closing_sequence_length), iterator.file.path);
    fin_ensure(false);
  }
    
  char raw_string_closing_path[64] = { ')' }; // closing sequence is limited to 16 characters.
  copy_memory(raw_string_closing_path + 1, closing_sequence_start, closing_sequence_length);

  closing_sequence_length += 1; // to include the prepended closing paren.

  auto search_start_position   = closing_sequence_end + 1; // start search after the open paren.
  auto raw_string_end_position = find_substring(search_start_position, end, raw_string_closing_path, closing_sequence_length);

  if (raw_string_end_position == nullptr) return End_Of_Parsing;
  if (raw_string_end_position == end)     return End_Of_Parsing;

  fin_ensure(raw_string_end_position[closing_sequence_length] == '"');

  if (raw_string_end_position[closing_sequence_length] != '"') {
    auto &file_path = iterator.file.path;
    panic("WARNING: Parse error occurred while checking #include files in %. "
          "This file will be recompiled and target relinked. If the compiler doesn't "
          "complain about this file and the project builds successfully, but this error "
          "keeps occuring, please report this issue. Thank you.", file_path);
    return End_Of_Parsing;
  }

  iterator.cursor = raw_string_end_position + closing_sequence_length + 1; // after the closing quote

  return Continue;
}

static Parsing_Status skip_character_literal (Dependency_Iterator &iterator) {
  auto search_from_position  = iterator.cursor + 1;
  auto literal_closing_quote = static_cast<const char *>(nullptr);

  while (iterator.cursor < iterator.end) {
    auto search_from_position = iterator.cursor + 1;
    if (search_from_position == iterator.end) return End_Of_Parsing;

    // Still doing search here, cause a char literal may have some long weird unicode sequence
    literal_closing_quote = get_character_offset(search_from_position, iterator.end, '\'');  
    if (literal_closing_quote == nullptr) return End_Of_Parsing;

    char previous_character = *(literal_closing_quote - 1);
    auto is_escaped_quote   = previous_character == '\\';
    if (!is_escaped_quote) break;
    
    literal_closing_quote += 1; // I'd think that if previous single quote was escaped, the only option that's next is the closing quote?
    break;
  }

  iterator.cursor = literal_closing_quote + 1;

  return Continue;
}

static Parsing_Status skip_comment_section (Dependency_Iterator &iterator) {
  if (iterator.cursor < iterator.end) {
    if (iterator.cursor[1] == '/') {
      iterator.cursor = get_character_offset(iterator.cursor, iterator.end, '\n');
      if (iterator.cursor == nullptr) return End_Of_Parsing;

      iterator.cursor += 1;

      return Continue;
    }

    if (iterator.cursor[1] == '*') {
      auto position = find_substring(iterator.cursor, iterator.end, "*/", 2);
      if (position == nullptr) return End_Of_Parsing;

      iterator.cursor = position + 2;

      return Continue;
    }
  }

  iterator.cursor += 1;

  return Continue;
}

static bool is_include_directive (Dependency_Iterator &iterator) {
  fin_ensure(iterator.cursor[0] == '#');

  const char directive[] = "#include";
  auto directive_length   = array_count_elements(directive) - 1;

  if (directive_length > (iterator.end - iterator.cursor)) return false;

  return compare_bytes(iterator.cursor, directive, directive_length);
}

Option<String> get_next_include_value (Dependency_Iterator &iterator) {
  while (skip_to_next_symbol(iterator)) {
    /*
      It's possible to something like #include directive be in the string literal, thus we should detect string literals
      and skip them altogether to avoid incorrectly parsing the included path.
     */
    if (*iterator.cursor == '"') {
      if (skip_string_literal(iterator) == End_Of_Parsing) return opt_none;
      continue;
    }

    /*
      Character literals must be skipped since those may contain quotes or hash characters that may confuse the parser into thinking
      that it's a string literal openning quote.
     */
    if (*iterator.cursor == '\'') {
      if (skip_character_literal(iterator) == End_Of_Parsing) return opt_none;
      continue;
    }

    /*
      Skipping comment section as those may also contain #include directives that we shouldn't parse.
     */
    if (*iterator.cursor == '/') {
      if (skip_comment_section(iterator) == End_Of_Parsing) return opt_none;
      continue;
    }
    
    if (*iterator.cursor == '#' && is_include_directive(iterator)) {
      if (!advance(iterator, 8)) return opt_none;

      while (*iterator.cursor == ' ')
        if (!advance(iterator)) return opt_none;

      /*
        Skipping system includes for now.
       */
      if (*iterator.cursor == '<') {
        iterator.cursor = get_character_offset(iterator.cursor, iterator.end, '>');
        if (iterator.cursor == nullptr) return opt_none;
        continue;
      }

      fin_ensure(*iterator.cursor == '"');
      if (!advance(iterator)) return opt_none;

      auto file_path_start = iterator.cursor;

      iterator.cursor = get_character_offset(iterator.cursor, iterator.end, '"');
      if (iterator.cursor == nullptr) return opt_none;

      auto include = String(file_path_start, iterator.cursor - file_path_start);

      advance(iterator);

      return include;
    }

    iterator += 1;
  }

  return opt_none;
}

static Chain_Status scan_dependency_chain (Memory_Arena &arena, Chain_Scanner &scanner, const List<Include_Path> &extra_include_directories, const File &file, bool is_included_file) {
  if (tracing_enabled_opt && !is_included_file) log("Scanning file: %\n", file.path);
  
  auto file_id = unwrap(get_file_id(file));

  usize dependency_file_index = 0; // Only for included files
  if (is_included_file) {
    /*
      Protection from recursive includes.

      If we see this file for the first time, add it into the cache as 'Checking'. If the scanner sees this file included
      in the upstream chain of this file, i.e recursive include, we prevent infinite looping by returning the 'Checking'
      status.
     */

    /*
      For targeted builds we pre-load update set with dependency records from the existing registry. In this case we may
      find a record that doesn't have a corresponding entry in the cache. For non-targeted builds, the number of entries in
      the cache should reflect the number of dependencies in the update_set and this situation shouldn't arise.
    */
    auto [found, index] = find_offset(get_dependencies(scanner.update_set), file_id);
    if (found) {
      auto status = scanner.status_cache[index];
      if (status != Chain_Status::Unchecked) return status;
    }

    /*
      See the previous note regarding targeted builds. In a targeted build, we should update the
      cache entry that corresponds to the dependecy file, which may be in the update_set, but whose
      cache entry is Unchecked.
    */
    dependency_file_index = found ? index : scanner.update_set.header->dependencies_count++;  

    scanner.update_set.dependencies[dependency_file_index] = file_id;
    scanner.status_cache[dependency_file_index]      = Chain_Status::Checking;
  }

  List<Include_Path> include_directories(arena, extra_include_directories);

  auto [error, path] = get_folder_path(arena, file.path);
  if (!error) list_push_front(include_directories, Include_Path::local(path));
  else log("WARNING: Couldn't resolve parent folder for the source file '%' due to a system error: %. "
           "Build process will continue, but this may cause issues with include files lookup.",
           file.path, error);

  Chain_Status chain_status = Chain_Status::Unchanged;

  auto mapping = unwrap(map_file_into_memory(file));
  defer { unmap_file(mapping); };

  const auto try_resolve_include_path = [&] (Memory_Arena &arena, File_Path path) {
    for (auto &prefix: include_directories) {
      /*
        Perhaps at some point later checking system paths for changes would be helpful, but it this point it could
        be a really deep rabbit hole with lots of issues (e.g perf, any form of macros that this tool doesn't support
        at this point).
       */
      if (prefix.kind == Include_Path::System) continue;
      
      auto full_path = make_file_path(arena, prefix.value, path);

      auto [error, exists] = check_file_exists(full_path);
      if (!exists) continue;
      else if (error) {
        log("WARNING: System error occured while checking file %\n", error.value);
        continue;
      }

      return full_path;
    }

    return File_Path {};
  };

  auto iterator = Dependency_Iterator(file, mapping);
  while (auto include_value = get_next_include_value(iterator)) {
    auto local = arena;

    auto resolved_path = try_resolve_include_path(local, include_value.value);
    if (!resolved_path) {
      String_Builder builder { local };
      builder.add(local, "Couldn't resolve the include file ", include_value.value, " from file ", file.path, " the following paths were checked:\n");
      for (auto &path: include_directories)
        if (path.kind == Include_Path::Local)
          builder.add(local, "  - ", path.value, "\n");      

      log("\n%\n", build_string(local, builder));
      chain_status = Chain_Status::Updated;
      continue;
    }

    auto [open_error, dependency_file] = open_file(resolved_path);
    if (open_error) {
      log("WARNING: Couldn't open included header file for scanning due to a system error: %.", open_error.value);
      chain_status = Chain_Status::Updated;
      continue;
    }

    defer { close_file(dependency_file); };

    auto chain_scan_result = scan_dependency_chain(local, scanner, extra_include_directories, dependency_file, true);
    fin_ensure(chain_scan_result != Chain_Status::Unchecked);

    if (chain_scan_result == Chain_Status::Updated) chain_status = Chain_Status::Updated;
  }

  if (!is_included_file) return chain_status; // That's all that we need to do for a translation unit

  auto timestamp = unwrap(get_last_update_timestamp(file));

  /*
    If the upstream chain hasn't been updated, we must also consider the current file for any changes.
   */
  if (chain_status != Chain_Status::Updated) {
    fin_ensure((chain_status == Chain_Status::Checking) || (chain_status == Chain_Status::Unchanged));

    auto registry_dependencies = get_dependencies(scanner.registry);

    auto [existing_entry_found, index] = find_offset(registry_dependencies, file_id);
    if (existing_entry_found) {
      auto record_timestamp = scanner.registry.records.dependency_records[index].timestamp;
      if (timestamp != record_timestamp) {
        if (tracing_enabled_opt) log("Included file '%' has newer timestamp\n", file.path);
        chain_status = Chain_Status::Updated;
      }
    }
    else {
      /*
        If the file_id is not found in the records, this must be a new dependency (or a bug?).
      */
      chain_status = Chain_Status::Updated;
    }
  }

  scanner.update_set.dependency_records[dependency_file_index] = Registry::Record { .timestamp = timestamp };
  scanner.status_cache[dependency_file_index] = chain_status;

  return chain_status;
}

bool scan_dependency_chain (Memory_Arena &arena, Chain_Scanner &scanner, const List<Include_Path> &extra_include_directories, const File &file) {
  return scan_dependency_chain (arena, scanner, extra_include_directories, file, false) == Chain_Status::Updated;
}
