
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

  auto init_cmd_result = run_system_command(arena, format_string(arena, "% init", binary_path));
  require(init_cmd_result.status == Status_Code::Success);

  auto build_cmd_result = run_system_command(arena, format_string(arena, "% build", binary_path));
  require(build_cmd_result.status == Status_Code::Success);
}

static void cleanup_workspace (Memory_Arena *arena) {
  set_working_directory(working_directory);
  delete_directory(workspace);
}

static void basic_clean_command_usage (Memory_Arena *arena) {
  auto cbuild_output_folder  = *make_file_path(arena, ".cbuild");
  auto output_build_folder   = *make_file_path(arena, cbuild_output_folder, "build");
  auto output_project_folder = *make_file_path(arena, cbuild_output_folder, "project");

  require(check_directory_exists(&output_build_folder));
  require(check_directory_exists(&output_project_folder));

  auto clean_command = format_string(arena, "% clean", binary_path);
  auto [status, output] = run_system_command(arena, clean_command);
  if (!status) print(arena, "%\n", output);

  require(status == Status_Code::Success);

  require(check_directory_exists(&output_build_folder) == false);
  require(check_directory_exists(&output_project_folder));
}

static void complete_clean_command_usage (Memory_Arena *arena) {
  auto cbuild_output_folder  = make_file_path(arena, ".cbuild");
  auto output_build_folder   = make_file_path(arena, *cbuild_output_folder, "build");
  auto output_project_folder = make_file_path(arena, *cbuild_output_folder, "project");

  require(check_directory_exists(&output_build_folder));
  require(check_directory_exists(&output_project_folder));

  auto clean_command = format_string(arena, "% clean all", binary_path);
  auto [status, output] = run_system_command(arena, clean_command);
  if (!status) print(arena, "%\n", output);

  require(status == Status_Code::Success);

  require(check_directory_exists(&output_build_folder) == false);
  require(check_directory_exists(&output_project_folder) == false);
}

static Test_Case clean_command_tests [] {
  define_test_case_ex(basic_clean_command_usage, setup_workspace, cleanup_workspace),
  define_test_case_ex(complete_clean_command_usage, setup_workspace, cleanup_workspace),
};

define_test_suite(clean_command, clean_command_tests)
