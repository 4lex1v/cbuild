
#include "code/base.hpp"
#include "code/platform.hpp"
#include "code/runtime.hpp"

#include "test_suite.hpp"

extern File_Path working_directory; // Path to the root directory where the 'verify' program has been called
extern File_Path workspace;         // Path to the workspace folder where all intermediary files and folders are created
extern File_Path binary_path;       // Executable under test

static void setup_workspace (Memory_Arena *arena) {
  if (check_directory_exists(&workspace)) delete_directory(workspace);
  create_directory(&workspace);
  set_working_directory(workspace);
}

static void cleanup_workspace (Memory_Arena *arena) {
  set_working_directory(working_directory);
  delete_directory(workspace);
}

static void build_init_project (Memory_Arena *arena) {
  auto init_command = format_string(arena, "% init", binary_path);
  auto init_cmd_result = run_system_command(arena, init_command);
  require(init_cmd_result.status == Status_Code::Success);

  auto build_command = format_string(arena, "% build", binary_path);
  auto [status, output] = run_system_command(arena, build_command);
  if (!status) print(arena, "%\n", output);

  require(status == Status_Code::Success);

  auto output_folder = make_file_path(arena, workspace, ".cbuild");
  require(check_directory_exists(&output_folder));

  auto produced_binary_path = make_file_path(arena, *output_folder, "build", "out", "main.exe");
  require(check_file_exists(&produced_binary_path));

  auto run_command = format_string(arena, "%", produced_binary_path);
  auto result = run_system_command(arena, run_command);
  if (!result.status) print(arena, "%\n", result.output);
  require(result.status == Status_Code::Success);
  require(strstr(result.output, "Thank you for trying cbuild!"));
}

static Test_Case build_command_tests [] {
  define_test_case_ex(build_init_project, setup_workspace, cleanup_workspace),
};

define_test_suite(build_command, build_command_tests)
