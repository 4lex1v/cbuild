
#include <cstdlib>
#include <cstring>
#include <cstdio>

#include "base.hpp"
#include "driver.hpp"
#include "arena.hpp"
#include "list.hpp"
#include "core.hpp"
#include "toolchain.hpp"
#include "target_builder.hpp"
#include "project_loader.hpp"
#include "cbuild_api.hpp"
#include "platform.hpp"
#include "result.hpp"
#include "runtime.hpp"

#if !defined(TOOL_VERSION) || !defined(API_VERSION)
  #error "TOOL_VERSION and API_VERSION values must be defined at compile time"
#endif

File_Path working_directory_path;
File_Path cache_directory_path;

Platform_Info platform;

enum struct CLI_Command {
  Init,
  Build,
  Run,
  Clean,
  Update,
  Version,
  Help,
  Dynamic
};

struct CLI_Command_Help_Info {
  struct Info {
    const char *name;
    std::initializer_list<const char *> description_lines;
  };

  Info command_info;
  std::initializer_list<Info> arguments_info;
};

static CLI_Command_Help_Info help_info[] = {
  {
    {
      "init",
      {
        "Initializes a new project by creating a project configuration in the current directory under the ./project folder."
      }
    },
    {
      {
        "type=<c | cpp>",
        {
          "Specifies the type of the project's configuration file.",
          "Defaults to 'cpp'"
        }
      }
    }
  },
  {
    {
      "build",
      {
        "Compiles and constructs your project based on the existing project configuration defined in ./project/build.cpp or ./project/build.c files."
      }
    },
    {
      {
        "builders=<NUM>",
        {
          "Specifies the number of CPU cores to be used for building the project.",
          "Accepts a value in the range [1, CORE_COUNT], allowing for parallelized builds.",
          "Defaults to CORE_COUNT. 1 means that the project will be compiled on the main thread only."
        }
      },
      {
        "<others>",
        {
          "You can pass arbitrary arguments to the 'build' command. These arguments are accessible in your project's configuration,",
          "via the tool's api defined in the generated ./project/cbuild.h."
        }
      }
    },
  },
  {
    {
      "run",
      {
        "Runs the executable target by name."
      },
    },
    {
      {
        "<name> [args]",
        {
          "Specifies the name of the target that should be executed.",
          "Additional arguments that come right after <name> are passed on to the target's executable.",
          "INTERACTIVE USER INPUT IS NOT SUPPORTED AT THIS POINT."
        }
      }
    }
  },
  {
    {
      "clean",
      {
        "Removes all build artifacts (compiled objects, binary files, etc.) created by the 'build' command, restoring workspace to its pre-build state."
      }
    },
    {
      {
        "all",
        {
         "Additionally remove artifacts associated with the project's configuration build, which are not removed by default."
        } 
      }
    }
  },
  {
    {
      "update",
      {
        "Updates the tool's API header files within your current project configuration folder (i.e ./project) to match the latest version of the tool",
        "This operation affects only the API headers provided by the tool and doesn't modify your project source code."
      }
    },
  },
  {
    {
      "version",
      {
        "Prints tool's version."
      }
    },
  },
  {
    {
      "help",
      {
        "Prints the help message."
      }
    }
  }
};

static Result<Arguments> parse_arguments (Memory_Arena *arena, int cli_args_count, char ** cli_args) {
  use(Status_Code);

  Arguments args {};

  if (cli_args_count <= 0) return args;

  auto parse_argument = [&arena] (const char *cli_arg) -> Result<Argument> {
    auto value_offset = strchr(cli_arg, '=');
    if (not value_offset)            return Argument { Argument::Type::Flag, cli_arg };
    if (*(value_offset + 1) == '\0') return { Invalid_Value, format_string(arena, "Argument value after '=' cannot be empty, invalid value: %", cli_arg) };

    usize key_length = value_offset - cli_arg;
    if (key_length == 0) return { Invalid_Value, format_string(arena, "Argument cannot start with '=', invalid value: %", cli_arg) };

    return Argument {
      .type  = Argument::Type::Key_Value,
      .key   = copy_string(arena, { cli_arg, key_length }),
      .value = value_offset + 1,
    };
  };

  for (int idx = 0; idx < cli_args_count; idx++) {
    auto arg = parse_argument(cli_args[idx]);
    check_status(arg);

    add(arena, &args.args, *arg);
  }

  return args;
}

static CLI_Command parse_command (int argc, char **argv) {
  using enum CLI_Command;
  
  if (argc <= 1) return Help;

  if      (strcmp(argv[1], "init")    == 0) return Init;
  else if (strcmp(argv[1], "build")   == 0) return Build;
  else if (strcmp(argv[1], "clean")   == 0) return Clean;
  else if (strcmp(argv[1], "update")  == 0) return Update;
  else if (strcmp(argv[1], "version") == 0) return Version;
  else if (strcmp(argv[1], "help")    == 0) return Help;
  else if (strcmp(argv[1], "run")     == 0) return Run;

  return CLI_Command::Dynamic;
}

static void print_usage (Memory_Arena &arena) {
  print(&arena, "\nUsage: cbuild <cli_command> [arguments]\n\nCommands:\n");

  for (auto &info: help_info) {
    auto &command = info.command_info;
    print(&arena, "  %\n", command.name);
    for (auto &line: command.description_lines) {
      print(&arena, "    %\n", line);
    }

    for (auto &arg_info: info.arguments_info) {
      printf("\n    %-15s", arg_info.name);
      for (usize idx = 0; auto &line: arg_info.description_lines) {
        if (idx == 0) print(&arena, "%\n", line);
        else          printf("%19s%s\n", " ", line);

        idx += 1;
      }
    }

    print(&arena, "\n");
  }
}

int main (int argc, char **argv) {
  auto arena = Memory_Arena { reserve_virtual_memory(megabytes(64)) };

  bool silence_report = false;
  auto counter     = create_performance_counter(&arena);
  auto start_stamp = get_clock_timestamp(counter);
  defer {
    if (!silence_report) {
      auto end_stamp = get_clock_timestamp(counter);
      auto elapsed   = get_ellapsed_millis(counter, start_stamp, end_stamp);

      print(&arena, "Finished in: %ms\n", elapsed);
    }
  };

#ifdef DEV_BUILD
  print(&arena, "CBuild r% DEV\n", TOOL_VERSION);
#else
  print(&arena, "CBuild r%\n", TOOL_VERSION);
#endif

  auto cli_command = parse_command(argc, argv);

  if (cli_command == CLI_Command::Version) { silence_report = true; return EXIT_SUCCESS; }

  working_directory_path = *get_working_directory_path(&arena);
  print(&arena, "Working directory: %\n", working_directory_path);

  /*
    We only set the path here, the actual creation of the folder is handled during
    the project loading phase. This is done to avoid the scenario when the tool is
    invoked in a wrong directory, but still creates a guarbage folder. To avoid
    littering user's system, we'll create the folder, if needed, after the configuration
    file has been discovered.
   */
  cache_directory_path = make_file_path(&arena, working_directory_path, ".cbuild");


  Status_Code exit_status = Status_Code::Success;
#define verify_status(EXPR) do { if (exit_status = capture_status(EXPR); !exit_status) goto exit_failure; } while (0)

  auto args = parse_arguments(&arena, argc - 1, argv + 1);
  verify_status(args);

  if (cli_command == CLI_Command::Init) {
    auto type_arg = get_argument_or_default(&args, "type", "cpp");
    bool create_c_project = strcmp(type_arg, "c") == 0;

    verify_status(create_new_project_in_workspace(&arena, create_c_project));
  }
  else if (cli_command == CLI_Command::Update) {
    verify_status(update_cbuild_api_file(&arena));
  }
  else if (cli_command == CLI_Command::Clean) {
    verify_status(delete_directory(make_file_path(&arena, cache_directory_path, "build")));

    if (argc == 3 && (strcmp(argv[2], "all") == 0)) {
      verify_status(delete_directory(make_file_path(&arena, cache_directory_path, "project")));
    }
  }
  else if (cli_command == CLI_Command::Help) {
    silence_report = true;
    print_usage(arena);
  }
  else {
    auto default_toolchain = discover_toolchain(&arena);
    if (not default_toolchain) {
      print(&arena, "Couldn't find any suitable C/C++ toolchain installed on the system.");
      return EXIT_FAILURE;
    }

    auto previous_env = setup_system_sdk(&arena, Target_Arch_x64);

    Project project {
      .arena     = Memory_Arena { reserve_memory_unsafe(&arena, megabytes(2)), megabytes(2) },
      .toolchain = default_toolchain,
    };

    auto load_status = load_project(&arena, &args, &project);
    if (not load_status) {
      if (cli_command == CLI_Command::Dynamic) {
        /*
          If we can't load the project, there's no way to resolve user-defined commands.
          I think it's still a good idea to report usage here, because of a different style and 
          possible input mistakes, like `cbuild --version` instead of `cbuild version`. In the 
          first case, we'll try to look up the command in the project, if it's called from a
          non-project directory, it'll terminate with project not found error.
         */

        print(&arena, "\nNo such command: %\n", argv[1]);

        print_usage(arena);
        return EXIT_SUCCESS;
      }

      // Otherwise, just crash
      verify_status(load_status);
    }

    if (exit_status) {
      if (cli_command == CLI_Command::Build) {
        /*
          Previous setup_system_sdk call configures env to build the the project's configuration for the host machine,
          while this call should setup CBuild to build the project for the specific target, where, at least in the case of
          Windows, different dll libs should be used.

          CBuild itself targets x64 machine only, while it allows the user to build for x86. Since the default toolchain must
          be x64, current env must already be configured for that and there's no need to do this again.
         */
        if (project.target_architecture == Target_Arch_x86) {
          reset_environment(&previous_env);
          setup_system_sdk(&arena, project.target_architecture);
        }

        verify_status(build_project(&arena, &project, &args));
      }
      else if (cli_command == CLI_Command::Run) {
        use(Status_Code);

#ifndef DEV_BUILD
        silence_report = true;
#endif
        
        verify_status(build_project(&arena, &project, &args));

        if (argc < 3) verify_status(Status_Code(User_Command_Error, "Target name required but not provided"));

        const char *target_name = argv[2];
        for (auto target: project.targets) {
          if (compare_strings(target->name, target_name)) {
            if (target->type != Target::Type::Executable) {
              auto message = format_string(&arena, "Target '%' is not executable\n", target->name);
              verify_status(Status_Code(User_Command_Error, message));
            }

            String_Builder command_builder { &arena };

            command_builder += make_file_path(&arena, project.output_location_path, "out",
                                              format_string(&arena, "main.%", platform.is_win32() ? "exe" : ""));
            for (int idx = 3; idx < argc; idx++) command_builder += argv[idx];

            auto command = build_string_with_separator(&command_builder, ' ');
            auto [status, output] = run_system_command(&arena, command);

            if (output) print(&arena, "%\n", output);
            verify_status(status);
            
            break;
          }
        }
        
      }
      else {
        assert(cli_command == CLI_Command::Dynamic);

        exit_status = Status_Code(Status_Code::User_Command_Error,
                                  format_string(&arena, "Unrecognized cli_command '%'", argv[1]));

        String command_name = argv[1];
        for (auto cmd: project.user_defined_commands) {
          if (compare_strings(cmd.name, command_name)) {
            auto status_code = cmd.proc(&args);
            if (status_code == 0) exit_status = Status_Code::Success;
            else {
              exit_status.value = Status_Code::User_Command_Error;
              exit_status.code  = status_code;
            }

            break;
          }
        }
      }
    }

  }

  if (exit_status) return EXIT_SUCCESS;

exit_failure:
  print(&arena, "Program terminated with an error status: %\n", exit_status);
  return EXIT_FAILURE;
}
