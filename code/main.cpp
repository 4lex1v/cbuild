
#include <cstdlib>
#include <cstring>
#include <cstdio>

#include "base.hpp"
#include "command_line.hpp"
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

extern Config_Crash_Handler crash_handler_hook;

CLI_Flags global_flags;

File_Path working_directory_path;
File_Path cache_directory_path;

Platform_Info platform;

static void config_exit_failure (u32 exit_code) {
  exit(exit_code);
}

static Result<Arguments> parse_arguments (Memory_Arena *arena, const CLI_Command *command) {
  use(Status_Code);

  Arguments args {};

  const char **argv;
  int argc;
  switch (command->type) {
    case CLI_Command::Build: {
      argv = command->build.arguments;
      argc = command->build.count;

      break;
    }
    case CLI_Command::Dynamic: {
      argv = command->dynamic.arguments;
      argc = command->dynamic.count;

      break;
    }
    default: {
      return args;
    }
  }

  if (argc <= 0) return args;

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

  for (int idx = 0; idx < argc; idx++) {
    auto arg = parse_argument(argv[idx]);
    check_status(arg);

    add(arena, &args.args, *arg);
  }

  return args;
}

int main (int argc, char **argv) {
  crash_handler_hook = config_exit_failure;
  
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

  auto [cli_parse_status, cli_input] = parse_command_line(&arena, argc, argv);
  if (!cli_parse_status) {
    print(&arena, "ERROR: %\n", cli_parse_status.details);
    print_usage(&arena);
    exit(EXIT_FAILURE);
  }

  global_flags = cli_input.flags;
  auto cli_command = cli_input.command;

  if (!global_flags.silenced || cli_command == CLI_Command::Version) {
#ifdef DEV_BUILD
    print(&arena, "CBuild r% DEV\n", TOOL_VERSION);
#else
    print(&arena, "CBuild r%\n", TOOL_VERSION);
#endif
  }

  if (cli_command == CLI_Command::Version) { silence_report = true; return EXIT_SUCCESS; }

  working_directory_path = *get_working_directory_path(&arena);
  if (!global_flags.silenced) print(&arena, "Working directory: %\n", working_directory_path);

  /*
    We only set the path here, the actual creation of the folder is handled during
    the project loading phase. This is done to avoid the scenario when the tool is
    invoked in a wrong directory, but still creates a guarbage folder. To avoid
    littering user's system, we'll create the folder, if needed, after the configuration
    file has been discovered.
   */
  cache_directory_path = make_file_path(&arena,working_directory_path, ".cbuild");

  Status_Code exit_status = Status_Code::Success;
#define verify_status(EXPR) do { if (exit_status = capture_status(EXPR); !exit_status) goto exit_failure; } while (0)

  if (cli_command == CLI_Command::Init) {
    verify_status(create_new_project_in_workspace(&arena, cli_command.init.type == CLI_Command::Init::Type::C));
  }
  else if (cli_command == CLI_Command::Update) {
    verify_status(update_cbuild_api_file(&arena));
  }
  else if (cli_command == CLI_Command::Clean) {
    verify_status(delete_directory(make_file_path(&arena, cache_directory_path, "build")));

    if (cli_command.clean.all) {
      verify_status(delete_directory(make_file_path(&arena, cache_directory_path, "project")));
    }
  }
  else if (cli_command == CLI_Command::Help) {
    silence_report = true;
    print_usage(&arena);
  }
  else {
    auto [status, default_toolchain] = discover_toolchain(&arena);
    if (!status) {
      print(&arena, "Couldn't find any suitable C/C++ toolchain installed on the system.\n");
      return EXIT_FAILURE;
    }

    auto previous_env = setup_system_sdk(&arena, Target_Arch_x64);

    Project project {
      .arena     = Memory_Arena { reserve_memory_unsafe(&arena, megabytes(2)), megabytes(2) },
      .toolchain = default_toolchain,
    };

    auto args = parse_arguments(&arena, &cli_command);
    verify_status(args);

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

        print_usage(&arena);
        return EXIT_SUCCESS;
      }

      // Otherwise, just crash
      verify_status(load_status);
    }

    if (exit_status) {
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

      if (cli_command == CLI_Command::Build) {
        verify_status(build_project(&arena, &project, cli_command.build));
      }
      else {
        assert(cli_command == CLI_Command::Dynamic);

        exit_status = Status_Code(Status_Code::User_Command_Error,
                                  format_string(&arena, "Unrecognized cli_input '%'", argv[1]));

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
