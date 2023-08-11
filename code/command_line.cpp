
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

    cache=<VALUE>   Specifies builder's use of the caching system.
                    <VALUE> parameter can take one of the following arguments:
                      "on":     Full use of the caching system. Default behavior
                      "off":    Caching system will not be used.
                      "flush":  Existing cached information will be ignored by the builder. Results of the build
                                will overwrite currently cached information.

    targets=<NAMES> Specifies a list of targets that should be build. CBuild will build these targets (along with their
                    upstream dependencies) only. Multiple targets name be specied, separated by ",", e.g:
                      cbuild build targets=bin1,bin2

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
      if (auto value = option[key.length]; value != '=') {
        if (value == '\0' || value == ' ')
          return {
            Invalid_Value,
            format_string(arena, "Invalid option value for the key '%', expected format: <key>=<value>", key)
          };

        continue;
      }

      auto value        = option + key.length + 1;
      auto value_length = strlen(value);

      if (value_length == 0)
        return {
          Invalid_Value,
          format_string(arena, "Invalid option value for the key '%', expected format: <key>=<value>", key)
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

  auto &command = input.command;
  if (compare_strings(command_name_arg, "init")) {
    command.type = Init;

    {
      auto type_value = find_argument_value(arena, "type", arguments_left, argv);
      check_status(type_value);

      if      (compare_strings(type_value, "cpp")) command.init.type = CLI_Command::Init::Cpp;
      else if (compare_strings(type_value, "c"))   command.init.type = CLI_Command::Init::C;
      else if (type_value->length != 0)
        return { Invalid_Value, format_string(arena, "Unrecognized argument value for the 'type' option: %", type_value) };
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

    {
      auto [status, cache] = find_argument_value(arena, "cache", arguments_left, argv);
      check_status(status);

      if      (cache.is_empty())        command.build.cache = Build_Config::Cache_Behavior::On;
      else if (!strcmp(cache, "on"))    command.build.cache = Build_Config::Cache_Behavior::On;
      else if (!strcmp(cache, "off"))   command.build.cache = Build_Config::Cache_Behavior::Off;
      else if (!strcmp(cache, "flush")) command.build.cache = Build_Config::Cache_Behavior::Flush;
      else return {
        Invalid_Value,
        format_string(arena, "Invalid paramter value % for the 'cache' option", cache)
      };
    }

    {
      auto [status, targets] = find_argument_value(arena, "targets", arguments_left, argv);
      check_status(status);

      if (targets) {
        command.build.targets = get_memory_at_current_offset<String>(arena);

        usize offset = 0;
        while (true) {
          const char *value_start = targets + offset;

          usize length = 0;
          for (usize idx = offset; idx < targets.length; idx++) {
            if (targets[idx] == ',') break;
            offset += 1;
            length += 1;
          }

          if (offset > 0 && length > 0) {
            push_struct<String>(arena, value_start, length);
            command.build.targets_count += 1;
          } else if (offset == 0) {
            return {
              Invalid_Value,
              format_string(arena, "Invalid 'targets' value, starting with ',': %", targets)
            };
          }

          if (offset == targets.length) break;

          offset += 1; // Move past the comma
        }
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
