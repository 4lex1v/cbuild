
#include <cstring>

#include "base.hpp"
#include "command_line.hpp"
#include "result.hpp"
#include "runtime.hpp"
#include "strings.hpp"

static const char *help_message =
  R"help(
Usage: cbuild [switches] <command> [command_args]

Switches:
  -s, --silence
    Silence cbuild's output (e.g working directory, discovered path to the configuration file, etc..), keeping only
    the output from the compiler and the linker.
    
Commands:
  init
    Initializes a new project by creating a project configuration in the current directory under the ./project folder.

    type=<c | cpp>  Specifies the type of the project's configuration file. Defaults to 'cpp'

  build
    Compiles and constructs your project based on the existing project configuration defined in ./project/build.cpp or
    ./project/build.c files.

    builders=<NUM>  Specifies the number of CPU cores to be used for building the project.
                    Accepts a value in the range [1, CORE_COUNT], allowing for parallelized builds.
                    Defaults to CORE_COUNT. "1" means that the project will be compiled on the main thread only.

    <others>        You can pass arbitrary arguments to the 'build' command. These arguments are accessible in your
                    project's configuration, via the tool's api defined in the generated ./project/cbuild.h.

   clean
     Removes all build artifacts (compiled objects, binary files, etc.) created by the 'build' command, restoring workspace to
     its pre-build state.

     all            Additionally remove artifacts associated with the project's configuration build, which are not removed by
                    default.
      
   update
     Updates the tool's API header files within your current project configuration folder (i.e ./project) to match the latest
     version of the tool.
     This operation affects only the API headers provided by the tool and doesn't modify your project source code.

   version
     Prints tool's version.

   help
     Prints the help message.
)help";

void print_usage (Memory_Arena *arena) {
  print(arena, "%\n", help_message);
}

static Result<int> parse_global_flags (Memory_Arena *arena, CLI_Flags *flags, int argc, const char **argv) {
  use(Status_Code);

  struct {
    char short_name;
    const char *name;
    bool *flag;
  } table [] {
    { 's', "silence", &flags->silenced },
  };
  
  int index = 1;
  while (index < argc) {
    auto token = argv[index];
    if (token[0] != '-') return index;

    auto token_length = strlen(token);
    if (token_length < 2) return { Invalid_Value, "Incomplete flag value passed" };

    if (token[1] != '-') {
      // Parsing a chain a single character switches, similar to how most Unix tools can group flags, e.g `tar -zcvf myfile.tgz .`
      for (int idx = 1; idx < token_length; idx++) {
        bool *flag = nullptr;
        for (auto &option: table) {
          if (option.short_name == token[idx]) {
            flag = option.flag;
            break;
          }
        }

        if (flag == nullptr) return { Invalid_Value, format_string(arena, "Flag '-%' is not supported", token[idx]) };
        if (*flag) print(arena, "Flag -% is a duplicated and has no affect\n", token[idx]);

        *flag = true;
      }
    }
    else {
      if (token_length < 3) return { Invalid_Value, "Incomplete flag value passed" };

      bool *flag = nullptr;
      for (auto &option: table) {
        if (!strcmp(token + 2, option.name)) { // offset first 2 --
          flag = option.flag;
          break;
        }
      }

      if (flag == nullptr) return { Invalid_Value, format_string(arena, "Flag '%' is not supported", token) };
      if (*flag) print(arena, "Flag % is a duplicated and has no affect\n", token);

      *flag = true;
    }

    index += 1;
  }

  return index;
}

static Result<String> find_argument_value (Memory_Arena *arena, const String &key, int argc, const char **argv) {
  use(Status_Code);

  for (int idx = 0; idx < argc; idx++) {
    auto option        = argv[idx];
    auto option_length = strlen(option);

    if (option_length < key.length) continue;

    if (!strncmp(option, key.value, key.length)) {
      // User can define the option that starts with expected key, but the rest of it is different
      if (option[key.length] != '=') continue;

      auto value        = option + key.length + 1;
      auto value_length = strlen(value);

      if (value_length == 0)
        return {
          Invalid_Value,
          format_string(arena, "Invalid option value for the key %, expected format: <key>=<value>", key)
        };

      return String(value, value_length);
    }
  }

  return String {};
}

static Result<bool> find_option_flag (Memory_Arena *arena, const String &flag, int argc, const char **argv) {
  use(Status_Code);

  for (int idx = 0; idx < argc; idx++) {
    auto token        = argv[idx];
    auto token_length = strlen(token);

    if (token_length < flag.length) continue;

    if (!strncmp(token, flag.value, flag.length)) {
      if (token_length == flag.length) return true;
      if (token[flag.length] == '=') {
        return {
          Invalid_Value,
          format_string(arena, "Option flag '%' appears to be a key-value option", flag)
        };
      }
    }
  }

  return false;
}

Result<CLI_Input> parse_command_line (Memory_Arena *arena, int argc, char **_argv) {
  use(Status_Code);
  use(CLI_Command);

  const char **argv = const_cast<const char **>(_argv);

  CLI_Flags flags {};
  auto [flags_parse_status, index] = parse_global_flags(arena, &flags, argc, argv);
  check_status(flags_parse_status);

  auto input = CLI_Input { .flags = flags, .command = { .type = Help } };

  if (index >= argc) return input;

  auto command_name_arg = String(argv[index++]);
  if (command_name_arg.is_empty()) return input;

  auto arguments_left = argc - index;
  argv += index;

  if (!arguments_left) return input;

  auto &command = input.command;
  if (compare_strings(command_name_arg, "init")) {
    command.type = Init;

    {
      auto type_value = find_argument_value(arena, "type", arguments_left, argv);
      check_status(type_value);

      if      (compare_strings(type_value, "cpp")) command.init.type = CLI_Command::Init::Cpp;
      else if (compare_strings(type_value, "c"))   command.init.type = CLI_Command::Init::C;
      else return { Invalid_Value, format_string(arena, "Unrecognized argument value for the 'type' option: %", type_value) };
    }

    return input;
  }

  if (compare_strings(command_name_arg, "build")) {
    command.type = Build;

    {
      auto [status, builders_count] = find_argument_value(arena, "builders", arguments_left, argv);
      check_status(status);

      if (builders_count) {
        if (builders_count[0] == '-') {
          return {
            Invalid_Value,
            format_string(arena, "Invalid value for the 'builders' option, this value cannot be negative")
          };
        }

        if (builders_count[0] == '0') {
          return {
            Invalid_Value,
            format_string(arena, "Invalid value for the 'builders' option, this value cannot be '0'")
          };
        }

        u32 count = 0;
        for (auto digits = builders_count.value; *digits != '\0'; digits++) {
          count = (count * 10) + (*digits - '0');
        }

        command.build.builders_count = count;
      }
    }

    command.build.arguments = argv;
    command.build.count     = arguments_left;

    return input;
  }

  if (compare_strings(command_name_arg, "clean")) {
    command.type = Clean;

    auto all_flag = find_option_flag(arena, "all", arguments_left, argv);
    check_status(all_flag);

    command.clean.all = all_flag;

    return input;
  }

  if (compare_strings(command_name_arg, "update"))  { command.type = Update;  return input; }
  if (compare_strings(command_name_arg, "version")) { command.type = Version; return input; }
  if (compare_strings(command_name_arg, "help"))    { command.type = Help;    return input; }

  command.type = Dynamic;
  command.dynamic = {
    .command_name = command_name_arg,
    .arguments    = argv,
    .count        = arguments_left,
  };

  return input;
}

#ifdef TEST_BUILD

#include "test_suite.hpp"

static String to_string (Memory_Arena *arena, CLI_Command::Value type) {
  switch (type) {
    case CLI_Command::Init:    return "CLI_Command::Init"; 
    case CLI_Command::Build:   return "CLI_Command::Build"; 
    case CLI_Command::Clean:   return "CLI_Command::Clean"; 
    case CLI_Command::Update:  return "CLI_Command::Update"; 
    case CLI_Command::Version: return "CLI_Command::Version"; 
    case CLI_Command::Help:    return "CLI_Command::Help"; 
    case CLI_Command::Dynamic: return "CLI_Command::Dynamic"; 
  }
}

static void empty_command_line_tests (Memory_Arena *arena) {
  char *argv[] = { "cbuild" };
  auto result = parse_command_line(arena, array_count_elements(argv), argv);
  require_eq(result.status, Status_Code::Success);

  CLI_Flags empty_flags {};
  require(!memcmp(&result->flags, &empty_flags, sizeof(CLI_Flags)));

  require_eq(result->command.type, CLI_Command::Help);
}

static void parse_global_flags_tests (Memory_Arena *arena) {
  {
    char *argv [] = { "cbuild", "-s" };
    auto [status, input] = parse_command_line(arena, array_count_elements(argv), argv);
    require_eq(status, Status_Code::Success);

    require(input.flags.silenced);
  }

  {
    char *argv [] = { "cbuild", "-ss" };
    auto [status, input] = parse_command_line(arena, array_count_elements(argv), argv);
    require_eq(status, Status_Code::Success);

    require(input.flags.silenced);
  }

  {
    char *argv [] { "cbuild", "--silence" };
    auto result = parse_command_line(arena, array_count_elements(argv), argv);
    require_eq(result.status, Status_Code::Success);
    require(result->flags.silenced);
  }

  {
    char *argv [] { "cbuild", "--garbage" };
    auto result = parse_command_line(arena, array_count_elements(argv), argv);
    require_eq(result.status, Status_Code::Invalid_Value);
  }
}

static void parse_commands (Memory_Arena *arena) {
  {
    char *argv [] { "cbuild", "version" };
    auto result = parse_command_line(arena, array_count_elements(argv), argv);
    require_eq(result.status, Status_Code::Success);
    require_eq(result->command.type, CLI_Command::Version);
  }

  {
    char *argv [] { "cbuild", "clean" };
    auto result = parse_command_line(arena, array_count_elements(argv), argv);
    require_eq(result.status, Status_Code::Success);
    require_eq(result->command.type, CLI_Command::Clean);
    require(!result->command.clean.all);
  }

  {
    char *argv [] { "cbuild", "clean", "all" };
    auto result = parse_command_line(arena, array_count_elements(argv), argv);
    require_eq(result.status, Status_Code::Success);
    require_eq(result->command.type, CLI_Command::Clean);
    require(result->command.clean.all);
  }

  {
    char *argv [] { "cbuild", "build", "foo", "builders=3", "baz" };
    auto result = parse_command_line(arena, array_count_elements(argv), argv);
    require_eq(result.status, Status_Code::Success);
    require_eq(result->command.type, CLI_Command::Build);

    auto &build = result->command.build;

    require_eq(build.builders_count, 1);
    require_eq(build.count, 3);
    require(build.arguments == (argv + 2));
  }

  {
    char *argv [] { "cbuild", "-s", "foo", "bar", "baz" };
    auto result = parse_command_line(arena, array_count_elements(argv), argv);
    require_eq(result.status, Status_Code::Success);
    require_eq(result->command.type, CLI_Command::Dynamic);

    auto &dyn = result->command.dynamic;

    require(!strcmp(dyn.command_name, "foo"));
    require_eq(dyn.count, 2);
    require(dyn.arguments == (argv + 3));
  }

}

static Test_Case tests [] {
  define_test_case(empty_command_line_tests),
  define_test_case(parse_global_flags_tests)
};

define_test_suite(command_line, tests)

#endif
