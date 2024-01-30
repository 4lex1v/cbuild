
#include "anyfin/arena.hpp"
#include "anyfin/memory.hpp"
#include "anyfin/option.hpp"
#include "anyfin/strings.hpp"
#include "anyfin/startup.hpp"
#include "anyfin/timers.hpp"
#include "anyfin/slice.hpp"

#include "cbuild.hpp"
#include "driver.hpp"
#include "project_loader.hpp"
#include "target_builder.hpp"

CLI_Flags global_flags;
Panic_Handler panic_handler = terminate;

enum struct CLI_Command {
  Init,
  Build,
  Clean,
  Update,
  Version,
  Help,
  Dynamic
};

static Option<String> find_argument_value (const Iterable<Startup_Argument> auto &args, String name) {
  for (auto arg: args) {
    /*
      Calling find_argument_value implicitly presumes the fact that we need a key-value setting. If it's a plain value
      instead, that's deemed as invalid input.
    */
    if (arg.key == name) {
      if (arg.is_value())  panic("ERROR: Invalid option value for the key 'type', expected format: <key>=<value>");
      return String(arg.value);
    }
  }

  return opt_none;
};

static bool find_option_flag (const Iterable<Startup_Argument> auto &args, String name) {
  for (auto arg: args) {
    /*
      Calling find_option_flag implicitly presumes the fact that we need a singular key (flag) setting.
      If it's a key-value pair instead, it's considered an input error.
    */
    if (arg.key == name) {
      if (arg.is_pair()) panic("ERROR: Unexpected input type of the '%' flag", name);
      return true;
    }
  }

  return false;
};

struct Build_Command {
  Build_Config config;

  constexpr Build_Command (Memory_Arena &arena)
    : config { arena } {}

  static Build_Command parse (Memory_Arena &arena, const Iterable<Startup_Argument> auto &command_arguments) {
    Build_Command command { arena };

    auto [builders_defined, builders] = find_argument_value(command_arguments, "builders");
    if (builders_defined) { 
      if (builders[0] == '-') panic("Invalid value for the 'builders' option, this value cannot be negative");
      if (builders[0] == '0') panic("Invalid value for the 'builders' option, this value cannot be '0'");

      s32 count = 0;
      for (auto digit: builders) count = (count * 10) + (digit - '0');

      command.config.builders_count = count;
    };

    auto [cache_defined, cache] = find_argument_value(command_arguments, "cache");
    if (cache_defined) {
      if      (is_empty(cache))  command.config.cache = Build_Config::Cache_Behavior::On;
      else if (cache == "on")    command.config.cache = Build_Config::Cache_Behavior::On;
      else if (cache == "off")   command.config.cache = Build_Config::Cache_Behavior::Off;
      else if (cache == "flush") command.config.cache = Build_Config::Cache_Behavior::Flush;
      else panic("Invalid paramter value % for the 'cache' option", cache);
    };

    auto [targets_defined, targets] = find_argument_value(command_arguments, "targets");
    if (targets_defined) {
      split_string(targets, ',').for_each([&] (auto it) {
        if (!is_empty(it)) list_push_copy(command.config.selected_targets, it);
      });
    };

    return command;
  }
};

struct Init_Command {
  Configuration_Type type = Configuration_Type::Cpp;

  static Init_Command parse (const Iterable<Startup_Argument> auto &command_arguments) {
    Init_Command command;

    auto [type_defined, type] = find_argument_value(command_arguments, "type");
    if (type_defined) {
      if      (type == "cpp") command.type = Configuration_Type::Cpp;
      else if (type == "c")   command.type = Configuration_Type::C;
      else panic("ERROR: Unrecognized argument value for the 'type' option: '%'\n", type);
    }

    return command;
  }
};

struct Clean_Command {
  bool all;

  static Clean_Command parse (const Iterable<Startup_Argument> auto &command_arguments) {
    return Clean_Command {
      .all = find_option_flag(command_arguments, "all"),
    };
  }
};

static constexpr String help_message =
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

/*
  Global flags are the very first argument values passed to the CBuild executable that go before all other options.
*/
static void parse_global_flags (Slice<Startup_Argument> &args) {
  if (is_empty(args)) return;
  
  struct {
    char short_name;
    String name;
    bool* flag;
  } table [] {
    { 's',  "silence", &global_flags.silenced },

    // Internal flags
    { '\0', "trace",   &global_flags.tracing },
  };

  usize parsed_flags_count = 0;
  for (auto &arg: args) {
    /*
      Global flags should always come before other input arguments. If we see a token that doesn't start with a dash,
      that's not a global flag and should be parsed at a later stage.
    */
    if (arg.key[0] != '-') break;

    /*
      Flags should always be prefixed with a single or double dash, thus in the shortest case it should at least be of
      length 2, if it's not there's something wrong.
    */
    if (arg.key.length < 2) panic("Incomplete flag value passed");

    if (arg.key[1] != '-') {
      // Parsing single character switches
      bool found = false;
      for (auto& option : table) {
        if (arg.key[1] == option.short_name) {
          if (*option.flag) log("Flag -%c is duplicated and has no effect\n", arg.key[1]);

          *option.flag = true;
          found        = true;

          break;
        }
      }

      if (!found) panic("Flag '-%c' is not supported", arg.key[1]);
    } else {
      // Parsing long name flags
      if (arg.key.length < 3) panic("Incomplete flag value passed");

      const auto key_name = arg.key + 2;

      bool found = false;
      for (auto& option : table) {
        if (key_name == option.name) {
          if (*option.flag) log("Flag %s is duplicated and has no effect\n", arg.key);

          *option.flag = true;
          found        = true;

          break;
        }
      }

      if (!found) panic("Flag '%s' is not supported", arg.key);
    }

    parsed_flags_count += 1;
  }

  args += parsed_flags_count;
}

static CLI_Command parse_command (Slice<Startup_Argument> &args) {
  if (is_empty(args)) return CLI_Command::Help;

  auto arg = *args;
  if (!arg.is_value()) {
    log("Command name is expected as the first argument, a %=% pair is found instead\n", arg.key, arg.value);
    log(help_message);
    terminate(1);
  }

  args += 1;

  const auto command_name = arg.key;

  if (command_name == "init")    return CLI_Command::Init;
  if (command_name == "build")   return CLI_Command::Build;
  if (command_name == "clean")   return CLI_Command::Clean;
  if (command_name == "update")  return CLI_Command::Update;
  if (command_name == "version") return CLI_Command::Version;
  if (command_name == "help")    return CLI_Command::Help;

  return CLI_Command::Dynamic;
}

u32 run_cbuild () { 
  // TODO: #perf check what's the impact from page faults is. How would large pages affect?
  Memory_Arena arena { reserve_virtual_memory(megabytes(64)) };
    
  auto args        = get_startup_args(arena);
  auto args_cursor = slice(args);

  bool silence_report = false;
  auto start_stamp = get_timer_value();
  defer {
    if (!silence_report) {
      auto end_stamp = get_timer_value();
      auto elapsed   = get_elapsed_millis(get_timer_frequency(), start_stamp, end_stamp);

      log("Finished in: %ms\n", elapsed);
    }
  };

  parse_global_flags(args_cursor);
  auto command_type = parse_command(args_cursor);

  if (!global_flags.silenced || command_type == CLI_Command::Version) {
#ifdef DEV_BUILD
    log("CBuild r% DEV\n", TOOL_VERSION);
#else
    log("CBuild r%\n", TOOL_VERSION);
#endif

    if (command_type == CLI_Command::Version) {
      silence_report = true;
      return 0;
    }
  }

  auto working_directory_path = unwrap(get_working_directory(arena));
  if (!global_flags.silenced) log("Working directory: %\n", working_directory_path);

  if (command_type == CLI_Command::Init) {
    auto command = Init_Command::parse(args_cursor);
    init_workspace(arena, working_directory_path, command.type);
    return 0;
  }

  if (command_type == CLI_Command::Update) {
    update_cbuild_api_file(arena, working_directory_path);
    return 0;
  }

  if (command_type == CLI_Command::Clean) {
    auto command = Clean_Command::parse(args_cursor);
    cleanup_workspace(arena, command.all);
    return 0;
  }

  if (command_type == CLI_Command::Help) {
    silence_report = true;
    log(help_message);
    return 0;
  }

  Project project { arena, "project", working_directory_path, make_file_path(arena, working_directory_path, ".cbuild") };
  load_project(arena, project, args_cursor);

  if (command_type == CLI_Command::Build) {
    auto command = Build_Command::parse(arena, args_cursor);
    return build_project(arena, project, move(command.config));
  }

  fin_ensure(command_type == CLI_Command::Dynamic);

  auto command_name = args[0].key;
  for (auto cmd: project.user_defined_commands) {
    if (cmd.name == command_name) {
      const Arguments arguments { arena, args_cursor };
      return cmd.proc(&arguments);
    }
  }

  log("Unknown command passed: %\n", command_name);
  log(help_message);

  return 1;
}

int mainCRTStartup () {
  terminate(run_cbuild());
}

