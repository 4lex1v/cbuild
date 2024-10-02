
#include "anyfin/arena.hpp"
#include "anyfin/memory.hpp"
#include "anyfin/option.hpp"
#include "anyfin/strings.hpp"
#include "anyfin/startup.hpp"
#include "anyfin/timers.hpp"
#include "anyfin/slice.hpp"

#include "cbuild.hpp"
#include "workspace.hpp"
#include "builder.hpp"

#include "anyfin/c_runtime_compat.hpp"

Panic_Handler panic_handler = terminate;

bool silence_logs_opt    = false;
bool tracing_enabled_opt = false;

String project_overwrite = "project";

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
  List<String> selected_targets;
  Cache_Behavior cache = Cache_Behavior::On;
  u32 builders_count   = static_cast<u32>(-1);

  constexpr Build_Command (Memory_Arena &arena)
    : selected_targets { arena } {}

  static Build_Command parse (Memory_Arena &arena, const Iterable<Startup_Argument> auto &command_arguments) {
    Build_Command command { arena };

    auto [builders_defined, builders] = find_argument_value(command_arguments, "builders");
    if (builders_defined) { 
      if (builders[0] == '-') panic("Invalid value for the 'builders' option, this value cannot be negative");
      if (builders[0] == '0') panic("Invalid value for the 'builders' option, this value cannot be '0'");

      s32 count = 0;
      for (auto digit: builders) count = (count * 10) + (digit - '0');

      command.builders_count = count;
    };

    auto [cache_defined, cache] = find_argument_value(command_arguments, "cache");
    if (cache_defined) {
      if      (is_empty(cache))  command.cache = Cache_Behavior::On;
      else if (cache == "on")    command.cache = Cache_Behavior::On;
      else if (cache == "off")   command.cache = Cache_Behavior::Off;
      else if (cache == "flush") command.cache = Cache_Behavior::Flush;
      else panic("Invalid paramter value % for the 'cache' option", cache);
    };

    auto [targets_defined, targets] = find_argument_value(command_arguments, "targets");
    if (targets_defined) {
      split_string(targets, ',').for_each([&] (auto it) {
        if (!is_empty(it)) list_push_copy(command.selected_targets, it);
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
  Cleanup_Type type;

  static Clean_Command parse (const Iterable<Startup_Argument> auto &command_arguments) {
    Cleanup_Type type = Cleanup_Type::Build;
    if      (find_option_flag(command_arguments, "project")) type = Cleanup_Type::Project;
    else if (find_option_flag(command_arguments, "all"))     type = Cleanup_Type::Full;

    return Clean_Command { .type = type };
  }
};

static constexpr String help_message =
  R"help(
Usage: cbuild [options] <command> [command_args]

Options:
  -s, --silence
    Silence cbuild's output (e.g working directory, discovered path to the configuration file, etc..), keeping only
    the output from the compiler and the linker.

  -p, --project <path>
    Specify an alternative project configuration to load. If the specified <path> value is folder, it will be used to
    load a build.(c/cpp) file, or as a folder where a new build configuration will be created. If <path> specifies a
    file name, that file will be used to setup the project.

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

    project        In addition to the default build cleanup, would also remove all files associated with user's configuration.
                   This command depends on the --project=<path> option, as it would cleanup only the current project (default or
                   overwrite, if the value was provided).

    all            Removes everything under .cbuild folder.

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
static void parse_global_options (Memory_Arena &arena, Slice<Startup_Argument> &args) {
  if (is_empty(args)) return;

  enum Option_Type { Flag, Value };
  
  struct {
    char short_name;
    String name;
    Option_Type type;
    void* slot;
    bool seen = false;
  } table [] {
    { 's', "silence", Flag,  &silence_logs_opt },
    { 'p', "project", Value, &project_overwrite },

    // Internal flags
    { '\0', "trace", Flag, &tracing_enabled_opt },
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
      for (auto& option: table) {
        if (arg.key[1] == option.short_name) {
          if (option.seen) {
            log("WARNING: Flag -%c is duplicated, latest value will be used\n", arg.key[1]);
          }
            
          switch (option.type) {
            case Flag: {
              *static_cast<bool *>(option.slot) = true;
              found = true;

              break;
            }
            case Value: {
              *static_cast<String *>(option.slot) = copy_string(arena, arg.value);
              found = true;
              break;
            }
          }

          option.seen = true;
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
          if (option.seen) log("Flag %s is duplicated and has no effect\n", arg.key);

          switch (option.type) {
            case Flag: {
              *static_cast<bool *>(option.slot) = true;
              found = true;

              break;
            }
            case Value: {
              *static_cast<String *>(option.slot) = arg.value;
              found = true;
              break;
            }
          }

          option.seen = true;
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

static bool ensure_relative_path (const File_Path &path) {
  return path[0] != '/' && (path.length >= 2 && path[1] != ':');
}

static bool is_subdirectory (Memory_Arena &arena, const File_Path &work_dir, const File_Path &path) {
  auto [error, abs_path] = get_absolute_path(arena, path);
  if (error) panic("Couldn't get absolute path for '%' due to an error: %\n", path, error.value);
  
  return has_substring(abs_path, work_dir);
}

u32 run_cbuild () { 
  // TODO: #perf check what's the impact from page faults is. How would large pages affect?
  Memory_Arena arena { reserve_virtual_memory(megabytes(64)) };

  find_executable(arena, "cbuild");
    
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

  parse_global_options(arena, args_cursor);
  auto command_type = parse_command(args_cursor);

  if (!silence_logs_opt || command_type == CLI_Command::Version) {
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
  if (!silence_logs_opt) log("Working directory: %\n", working_directory_path);

  if (!ensure_relative_path(project_overwrite) || !is_subdirectory(arena, working_directory_path, project_overwrite))
    panic("Specified --project value must be a path relative to the project's root folder.\n  Root:     %\n  Resolved: %\n",
          working_directory_path, get_absolute_path(arena, project_overwrite).value);

  if (command_type == CLI_Command::Init) {
    auto command = Init_Command::parse(args_cursor);
    init_workspace(arena, working_directory_path, command.type);
    return 0;
  }

  if (command_type == CLI_Command::Update) {
    update_cbuild_api_file(arena, working_directory_path);
    return 0;
  }

  if (command_type == CLI_Command::Help) {
    silence_report = true;
    log(help_message);
    return 0;
  }

  if (command_type == CLI_Command::Clean) {
    auto command = Clean_Command::parse(args_cursor);
    cleanup_workspace(arena, working_directory_path, command.type);
    return 0;
  }

  auto cache_dir = make_file_path(arena, working_directory_path, ".cbuild");

  /*
    This would allow having different project setups working simultaneously. Instead of building the configuration
    into .cbuild/project, name of this folder would base on the name of the path to the alternative build configuration.
    E.g calling `cbuild -p=alt/ver2 ...` would produce build files under `.cbuild/project_alt_ver2/`, if for some reason
    configuration file is in the root of the project, root's name will be added to the name, like `.cbuild/project_cbuild`
   */
  auto project_output_dir = resolve_project_output_dir_name(arena, working_directory_path);

  Project project { arena, "project", working_directory_path, cache_dir, project_output_dir };
  load_project(arena, project, args_cursor);

  if (command_type == CLI_Command::Build) {
    auto [targets, cache, builders_count] = Build_Command::parse(arena, args_cursor);
    return build_project(arena, project, targets, cache, builders_count);
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

