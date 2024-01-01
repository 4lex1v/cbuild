
#include "anyfin/core/arena.hpp"
#include "anyfin/core/lifecycle.hpp"
#include "anyfin/core/memory.hpp"
#include "anyfin/core/option.hpp"
#include "anyfin/core/result.hpp"
#include "anyfin/core/seq.hpp"
#include "anyfin/core/strings.hpp"

#include "anyfin/platform/console.hpp"
#include "anyfin/platform/timers.hpp"

#include "cbuild.hpp"
#include "cbuild_api.hpp"
#include "driver.hpp"
#include "project_loader.hpp"
#include "target_builder.hpp"

CLI_Flags global_flags;

enum struct CLI_Command {
  Init,
  Build,
  Clean,
  Update,
  Version,
  Help,
  Dynamic
};

static Option<String_View> find_argument_value (const Iterable<Startup_Argument> auto &args, const String_View& name) {
//static Option<String_View> find_argument_value (const Slice<Startup_Argument>& args, const String_View& name) {
  for (auto arg: args) {
    if (arg.is_value()) continue;
    if (compare_strings(arg.key, name)) return String_View(arg.value);
  }

  return {};
};

static bool find_option_flag (const Iterable<Startup_Argument> auto &args, const String_View& name) {
  for (auto arg: args) {
    if (arg.is_pair()) continue;
    if (compare_strings(arg.key, name)) return true;
  }

  return false;
};

struct Build_Command {
  Build_Config config;
  Slice<Startup_Argument> build_arguments;

  static Build_Command parse (const Slice<Startup_Argument>& command_arguments) {
    Build_Command command;

    find_argument_value(command_arguments, "builders")
      .handle_value([&command](auto &value) {
        if (value[0] == '-') panic("Invalid value for the 'builders' option, this value cannot be negative");
        if (value[0] == '0') panic("Invalid value for the 'builders' option, this value cannot be '0'");

        s32 count = 0;
        for (auto digit: value) count = (count * 10) + (digit - '0');

        command.config.builders_count = count;
      });

    find_argument_value(command_arguments, "cache")
      .handle_value([&command](auto &value) {
        if      (is_empty(value))                 command.config.cache = Build_Config::Cache_Behavior::On;
        else if (compare_strings(value, "on"))    command.config.cache = Build_Config::Cache_Behavior::On;
        else if (compare_strings(value, "off"))   command.config.cache = Build_Config::Cache_Behavior::Off;
        else if (compare_strings(value, "flush")) command.config.cache = Build_Config::Cache_Behavior::Flush;
        else panic("Invalid paramter value % for the 'cache' option", value);
      });

    find_argument_value(command_arguments, "targets")
      .handle_value([&command](auto &value) {
        //usize offset = 0;
        // while (true) {
        //   const char *value_start = targets + offset;

        //   usize length = 0;
        //   for (usize idx = offset; idx < targets.length; idx++) {
        //     if (targets[idx] == ',') break;
        //     offset += 1;
        //     length += 1;
        //   }

        //   if (offset > 0 && length > 0) {
        //     push_struct<String>(arena, value_start, length);
        //     command.build.targets_count += 1;
        //   } else if (offset == 0) {
        //     panic("Invalid 'targets' value, starting with ',': %", targets));
        //   }

        //   if (offset == targets.length) break;

        //   offset += 1; // Move past the comma
      });

    return command;
  }
};

struct Init_Command {
  Configuration_Type type;

  static Init_Command parse (const Slice<Startup_Argument>& command_arguments) {
    Init_Command command;

    find_argument_value(command_arguments, "type")
        .handle_value([&command](auto value) {
          if (compare_strings(value, "cpp")) command.type = Configuration_Type::Cpp;
          else if (compare_strings(value, "c")) command.type = Configuration_Type::C;
          else panic("Unrecognized argument value for the 'type' option: %", value);
        });

    return command;
  }
};

struct Clean_Command {
  bool all;

  static Clean_Command parse (const Slice<Startup_Argument>& command_arguments) {
    return Clean_Command{
      .all = find_option_flag(command_arguments, "all"),
    };
  }
};

static constexpr String_View help_message =
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

namespace {

  void print_usage () {
    print("%\n", help_message);
  }

  /*
    Global flags are the very first argument values passed to the CBuild executable that go before all other options.
   */
  void parse_global_flags (Slice<Startup_Argument>& args) {
    struct {
      char short_name;
      const char* name;
      bool* flag;
    } table[] {
      {'s', "silence", &global_flags.silenced},
    };

    while (true) {
      auto arg = *args++;

      if (arg.is_value()) return;
      if (arg.key[0] != '-') return; // It's required that flags start with -- or -, for full- or short- name

      if (arg.key.length < 2) panic("Incomplete flag value passed");

      if (arg.key[1] != '-') {
        // Parsing a chain a single character switches, similar to how most Unix tools can group flags, e.g `tar -zcvf myfile.tgz .`
        for (int idx = 1; idx < arg.key.length; idx++) {
          bool* flag = nullptr;
          for (auto& option: table) {
            if (option.short_name == arg.key[idx]) {
              flag = option.flag;
              break;
            }
          }

          if (flag == nullptr) panic("Flag '-%' is not supported", arg.key[idx]);
          if (*flag) print("Flag -% is a duplicated and has no affect\n", arg.key[idx]);

          *flag = true;
        }
      } else {
        if (arg.key.length < 3) panic("Incomplete flag value passed");

        bool* flag = nullptr;
        for (auto& option: table) {
          if (!compare_strings(arg.key + 2, String_View(option.name))) {
            // offset first 2 --
            flag = option.flag;
            break;
          }
        }

        if (flag == nullptr) panic("Flag '%' is not supported", arg.key);
        if (*flag) print("Flag % is a duplicated and has no affect\n", arg.key);

        *flag = true;
      }
    }
  }

  CLI_Command parse_command (Slice<Startup_Argument>& args) {
    assert(args[0].is_value());

    const auto command_name = (*args++).key;

    if (compare_strings(command_name, "init"))    return CLI_Command::Init;
    if (compare_strings(command_name, "build"))   return CLI_Command::Build;
    if (compare_strings(command_name, "clean"))   return CLI_Command::Clean;
    if (compare_strings(command_name, "update"))  return CLI_Command::Update;
    if (compare_strings(command_name, "version")) return CLI_Command::Version;
    if (compare_strings(command_name, "help"))    return CLI_Command::Help;

    return CLI_Command::Dynamic;
  }

} // namespace

u32 Fin::Core::app_entry (Slice<Startup_Argument> args) {
  Memory_Arena arena { reserve_virtual_memory(megabytes(64)) };
    
  bool silence_report = false;
  auto start_stamp = get_timer_value();
  defer {
    if (!silence_report) {
      auto end_stamp = get_timer_value();
      auto elapsed = get_elapsed_millis(get_timer_frequency(), start_stamp, end_stamp);
      print("Finished in: %ms\n", elapsed);
    }
  };

  parse_global_flags(args);
  auto command_type = parse_command(args);

  if (!global_flags.silenced || command_type == CLI_Command::Version) {
#ifdef DEV_BUILD
    print("CBuild r% DEV\n", TOOL_VERSION);
#else
    print("CBuild r%\n", TOOL_VERSION);
#endif

    if (command_type == CLI_Command::Version) {
      silence_report = true;
      return 0;
    }
  }

  auto working_directory_path = *get_working_directory_path(arena);
  if (!global_flags.silenced) print("Working directory: %\n", working_directory_path);

  if (command_type == CLI_Command::Init) {
    auto command = Init_Command::parse(args);
    init_workspace(arena, working_directory_path, command.type);
    return 0;
  }

  if (command_type == CLI_Command::Update) {
    update_cbuild_api_file(arena, working_directory_path);
    return 0;
  }

  if (command_type == CLI_Command::Clean) {
    auto command = Clean_Command::parse(args);
    cleanup_workspace(command.all);
    return 0;
  }

  if (command_type == CLI_Command::Help) {
    silence_report = true;
    print_usage();
    return 0;
  }

  auto project = load_project(arena, "project", working_directory_path, working_directory_path, args);

  if (command_type == CLI_Command::Build) {
    auto command = Build_Command::parse(args);
    return build_project(arena, project, command);
  }

  assert(command_type == CLI_Command::Dynamic);

  //   exit_status = Status_Code(Status_Code::User_Command_Error,
  //                             format_string(&arena, "Unrecognized cli_input '%'", argv[1]));

  //   String command_name = argv[1];
  //   for (auto cmd: project.user_defined_commands) {
  //     if (compare_strings(cmd.name, command_name)) {
  //       auto status_code = cmd.proc(&args);
  //       if (status_code == 0) exit_status = Status_Code::Success;
  //       else {
  //         exit_status.value = Status_Code::User_Command_Error;
  //         exit_status.code  = status_code;
  //       }

  //       break;
  //     }
  //   }

  return 0;
}
