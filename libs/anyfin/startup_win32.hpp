
#define FIN_STARTUP_HPP_IMPL

#include "anyfin/file_system.hpp"
#include "anyfin/startup.hpp"
#include "anyfin/strings.hpp"

namespace Fin {

struct Command_Line_Input {
  String program_name;
  String arguments_string;
};

static String parse_program_name (const char *input, usize length) {
  if (input[length - 1] == '"') length -= 1;

  const char *name_end    = input + length;
  const char *name_cursor = name_end;

  while (name_cursor > input) {
    if (*(name_cursor - 1) == '\\') break;
    name_cursor -= 1;
  }

  return String(name_cursor, name_end - name_cursor);
}

static Command_Line_Input get_command_line () {
  auto input = GetCommandLineA();

  auto cursor = input;
  for (; *cursor && *cursor != ' '; cursor++);

  auto program_name = parse_program_name(input, cursor - input);

  /*
    I've noticed that on Windows there might be space characters after the program's name before the terminating 0...
   */
  while (*cursor == ' ') cursor += 1;

  // No arguments were passed.
  if (!*cursor) return Command_Line_Input { program_name };

  auto args_string = cursor;
  usize args_string_length = 0;
  while (cursor[args_string_length]) args_string_length += 1;

  return Command_Line_Input {
    .program_name     = program_name,
    .arguments_string = String(args_string, args_string_length)
  };
}

static void collect_input_arguments(const String &command_line, Array<Startup_Argument> &args) {
  const auto is_whitespace = [] (char value) { return value == ' ' || value == '\t'; };
  const char *cursor = command_line.value;

  for (usize arg_index = 0; arg_index < args.count && cursor && *cursor; ++arg_index) {
    while (*cursor && is_whitespace(*cursor)) ++cursor;

    const char *start = cursor;
    while (*cursor && !is_whitespace(*cursor)) ++cursor;

    String token(start, cursor - start);
    s32 eq_offset = -1;
    for (s32 i = 0; i < s32(token.length); i++) {
      if (token[i] == '=') { eq_offset = i; break; }
    }

    if (eq_offset > 0) {
      args[arg_index] = Startup_Argument {
        .type  = Startup_Argument::Type::Pair,
        .key   = String(token.value, eq_offset),
        .value = String(token.value + eq_offset + 1, token.length - (eq_offset + 1)),
      };
    } else {
      args[arg_index] = Startup_Argument {
        .type = Startup_Argument::Type::Value,
        .key  = token
      };
    }
  }
}

static u32 count_arguments (const String &input) {
  if (is_empty(input)) return 0;

  auto cursor = input.value;

  int count = 0, in_token = 0;
  for (; cursor && *cursor; ++cursor) {
    const auto is_space_or_tab = *cursor == ' ' || *cursor == '\t';
    count += (!in_token && !is_space_or_tab);
    in_token = !is_space_or_tab;
  }

  return count;
}

static String get_program_name () {
  return get_command_line().program_name;
}

static Array<Startup_Argument> get_startup_args (Memory_Arena &arena) {
  auto args_string = get_command_line().arguments_string;
  if (!args_string) return {};

  auto args_count = count_arguments(args_string);
  if (!args_count) return {};

  auto args = reserve_array<Startup_Argument>(arena, args_count);
  collect_input_arguments(args_string, args);

  return args;
}

}
