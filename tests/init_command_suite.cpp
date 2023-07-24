
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

static void init_project_test (Memory_Arena *arena) {
  auto command = format_string(arena, "% init", binary_path);

  auto [status, output] = run_system_command(arena, command);
  if (!status) print(arena, "%\n%\n", output, status);

  require(status == Status_Code::Success);

  auto project_folder    = make_file_path(arena, "project");
  auto build_config_file = make_file_path(arena, *project_folder, "build.cpp");
  auto api_header_file   = make_file_path(arena, *project_folder, "cbuild.h");

  require(check_directory_exists(&project_folder));
  require(check_file_exists(&build_config_file));
  require(check_file_exists(&api_header_file));
}

static void init_c_project_test (Memory_Arena *arena) {
  auto command = format_string(arena, "% init type=c", binary_path);

  auto [status, output] = run_system_command(arena, command);
  if (!status) print(arena, "%\n", output);

  require(status == Status_Code::Success);

  auto project_folder    = make_file_path(arena, "project");
  auto build_config_file = make_file_path(arena, *project_folder, "build.c");
  auto api_header_file   = make_file_path(arena, *project_folder, "cbuild.h");

  require(check_directory_exists(&project_folder));
  require(check_file_exists(&build_config_file));
  require(check_file_exists(&api_header_file));
}

static void init_cpp_project_test (Memory_Arena *arena) {
  auto command = format_string(arena, "% init type=cpp", binary_path);

  auto [status, output] = run_system_command(arena, command);
  if (!status) print(arena, "%\n", output);

  require(status == Status_Code::Success);

  auto project_folder    = make_file_path(arena, "project");
  auto build_config_file = make_file_path(arena, *project_folder, "build.cpp");
  auto api_header_file   = make_file_path(arena, *project_folder, "cbuild.h");

  require(check_directory_exists(&project_folder));
  require(check_file_exists(&build_config_file));
  require(check_file_exists(&api_header_file));
}

static void init_unknown_project_type_test (Memory_Arena *arena) {
  auto command = format_string(arena, "% init type=rust", binary_path);

  auto [status, output] = run_system_command(arena, command);

  require(status == Status_Code::System_Command_Error);

  require(strstr(output.value, "ERROR: Unrecognized argument value for the 'type' option: rust"));
  require(strstr(output.value, "Usage:"));
}

static void init_with_unset_type_parameter_test (Memory_Arena *arena) {
  auto command = format_string(arena, "% init type", binary_path);

  auto [status, output] = run_system_command(arena, command);

  require(status == Status_Code::System_Command_Error);

  require(strstr(output.value, "ERROR: Invalid option value for the key 'type', expected format: <key>=<value>"));
}

static void init_with_unset_type_parameter_2_test (Memory_Arena *arena) {
  auto command = format_string(arena, "% init type=", binary_path);

  auto [status, output] = run_system_command(arena, command);

  require(status == Status_Code::System_Command_Error);

  require(strstr(output.value, "ERROR: Invalid option value for the key 'type', expected format: <key>=<value>"));
}

static Test_Case init_command_tests [] {
    define_test_case_ex(init_project_test, setup_workspace, cleanup_workspace),
    define_test_case_ex(init_c_project_test, setup_workspace, cleanup_workspace),
    define_test_case_ex(init_cpp_project_test, setup_workspace, cleanup_workspace),
    define_test_case_ex(init_unknown_project_type_test, setup_workspace, cleanup_workspace),
    define_test_case_ex(init_with_unset_type_parameter_test, setup_workspace, cleanup_workspace),
    define_test_case_ex(init_with_unset_type_parameter_2_test, setup_workspace, cleanup_workspace),
};

define_test_suite(init_command, init_command_tests)
