
#include "test_suite.hpp"

extern File_Path working_directory; // Path to the root directory where the 'verify' program has been called
extern File_Path workspace;         // Path to the workspace folder where all intermediary files and folders are created
extern File_Path binary_path;       // Executable under test

static void setup_workspace (Memory_Arena &arena) {
  if (check_directory_exists(workspace).get())
    require(delete_directory(workspace));

  require(create_directory(workspace));
  require(set_working_directory(workspace));
}

static void cleanup_workspace (Memory_Arena &arena) {
  require(set_working_directory(working_directory));
  require(delete_directory(workspace));
}

static void init_project_test (Memory_Arena &arena) {
  auto command = concat_string(arena, binary_path, " init");

  auto init_cmd_result = run_system_command(arena, command);
  require(init_cmd_result);

  auto project_folder    = make_file_path(arena, "project");
  auto build_config_file = make_file_path(arena, project_folder, "build.cpp");
  auto api_header_file   = make_file_path(arena, project_folder, "cbuild.h");

  require(check_directory_exists(project_folder));
  require(check_file_exists(build_config_file));
  require(check_file_exists(api_header_file));
}

static void init_c_project_test (Memory_Arena &arena) {
  auto command = concat_string(arena, binary_path, " init type=c");

  auto init_cmd_result = run_system_command(arena, command);
  require(init_cmd_result);

  auto project_folder    = make_file_path(arena, "project");
  auto build_config_file = make_file_path(arena, project_folder, "build.c");
  auto api_header_file   = make_file_path(arena, project_folder, "cbuild.h");

  require(check_directory_exists(project_folder));
  require(check_file_exists(build_config_file));
  require(check_file_exists(api_header_file));
}

static void init_cpp_project_test (Memory_Arena &arena) {
  auto command = concat_string(arena, binary_path, " init type=cpp");

  auto init_cmd_result = run_system_command(arena, command);
  require(init_cmd_result);

  auto project_folder    = make_file_path(arena, "project");
  auto build_config_file = make_file_path(arena, project_folder, "build.cpp");
  auto api_header_file   = make_file_path(arena, project_folder, "cbuild.h");

  require(check_directory_exists(project_folder));
  require(check_file_exists(build_config_file));
  require(check_file_exists(api_header_file));
}

static void init_unknown_project_type_test (Memory_Arena &arena) {
  auto command = concat_string(arena, binary_path, " init type=rust");

  auto [init_cmd_has_failed, _, status] = run_system_command(arena, command);
  require(!init_cmd_has_failed);
  require(status.status_code != 0);

  frequire(has_substring(status.output, "ERROR: Unrecognized argument value for the 'type' option: rust"),
           concat_string(arena, "status.output = ", status.output));
}

static void init_with_unset_type_parameter_test (Memory_Arena &arena) {
  auto command = concat_string(arena, binary_path, " init type");

  auto [init_cmd_has_failed, _, status] = run_system_command(arena, command);
  require(!init_cmd_has_failed);
  require(status.status_code != 0);

  frequire(has_substring(status.output, "ERROR: Invalid option value for the key 'type', expected format: <key>=<value>"),
           concat_string(arena, "status.output = ", status.output));
}

static void init_with_unset_type_parameter_2_test (Memory_Arena &arena) {
  auto command = concat_string(arena, binary_path, " init type=");

  auto [init_cmd_has_failed, _, status] = run_system_command(arena, command);
  require(!init_cmd_has_failed);
  require(status.status_code != 0);

  frequire(has_substring(status.output, "ERROR: Unrecognized argument value for the 'type' option:"),
           concat_string(arena, "status.output = ", status.output));
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
