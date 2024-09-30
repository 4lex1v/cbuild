
#include "test_suite.hpp"

extern File_Path working_directory; // Path to the root directory where the 'verify' program has been called
extern File_Path testspace_directory;         // Path to the workspace folder where all intermediary files and folders are created
extern File_Path binary_path;       // Executable under test

static void setup_testsite (Memory_Arena &arena) {
  if (check_directory_exists(testspace_directory).or_default(true))
    delete_directory(testspace_directory);

  create_directory(testspace_directory);

  auto testsite_path = make_file_path(arena, working_directory, "tests", "testsite");
  copy_directory(testsite_path, testspace_directory);

  set_working_directory(testspace_directory);
}

static void cleanup_workspace (Memory_Arena &) {
  set_working_directory(working_directory);
  delete_directory(testspace_directory);
}

static void call_user_action_test (Memory_Arena &arena) {
  auto cli_command = concat_string(arena, binary_path, " test_cmd");

  auto output = run_system_command(arena, cli_command);
  require(output);
  require(output.value.status_code == 0);
  require(has_substring(output.value.output, "Calling registered command"));
}

static Test_Case user_actions_tests [] {
  define_test_case_ex(call_user_action_test, setup_testsite, cleanup_workspace),
};

define_test_suite(user_actions, user_actions_tests)
