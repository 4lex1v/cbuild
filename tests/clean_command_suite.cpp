
#include "test_suite.hpp"

extern File_Path working_directory; // Path to the root directory where the 'verify' program has been called
extern File_Path testspace_directory;         // Path to the workspace folder where all intermediary files and folders are created
extern File_Path binary_path;       // Executable under test

static void setup_workspace (Memory_Arena &arena) {
  if (check_directory_exists(testspace_directory).or_default(true))
    delete_directory(testspace_directory);

  create_directory(testspace_directory);
  set_working_directory(testspace_directory);

  {
    auto init_cmd_result = run_system_command(arena, format_string(arena, "% init", binary_path));
    require(init_cmd_result);

    auto build_cmd_result = run_system_command(arena, format_string(arena, "% build", binary_path));
    require(build_cmd_result);
  }

  {
    auto init_cmd_result = run_system_command(arena, format_string(arena, "% -p=project/config.cpp init", binary_path));
    require(init_cmd_result);

    auto build_cmd_result = run_system_command(arena, format_string(arena, "% -p=project/config.cpp build", binary_path));
    require(build_cmd_result);
  }
}

static void cleanup_workspace (Memory_Arena &) {
  set_working_directory(working_directory);
  delete_directory(testspace_directory);
}

static void basic_clean_command_usage (Memory_Arena &arena) {
  auto cbuild_output_folder  = make_file_path(arena, ".cbuild", "project");
  auto output_build_folder   = make_file_path(arena, cbuild_output_folder, "build");
  auto output_project_folder = make_file_path(arena, cbuild_output_folder, "config");

  require(check_directory_exists(output_build_folder));
  require(check_directory_exists(output_project_folder));

  auto clean_command = format_string(arena, "% clean", binary_path);
  auto clean_cmd_result = run_system_command(arena, clean_command);
  require(clean_cmd_result);

  require(check_directory_exists(output_build_folder).value == false);
  require(check_directory_exists(output_project_folder).value);
}

static void complete_clean_command_usage (Memory_Arena &arena) {
  auto cbuild_output_folder  = make_file_path(arena, ".cbuild", "project");
  auto output_build_folder   = make_file_path(arena, cbuild_output_folder, "build");
  auto output_project_folder = make_file_path(arena, cbuild_output_folder, "config");

  require(check_directory_exists(output_build_folder));
  require(check_directory_exists(output_project_folder));

  auto clean_command = format_string(arena, "% clean all", binary_path);
  auto clean_cmd_result = run_system_command(arena, clean_command);
  require(clean_cmd_result);

  require(check_directory_exists(output_build_folder).value == false);
  require(check_directory_exists(output_project_folder).value == false);
}

static void cleanup_with_project_override_tests (Memory_Arena &arena) {
  {
    auto cbuild_output_folder  = make_file_path(arena, ".cbuild", "project");
    auto output_build_folder   = make_file_path(arena, cbuild_output_folder, "build");
    auto output_project_folder = make_file_path(arena, cbuild_output_folder, "config");

    require(check_directory_exists(output_build_folder));
    require(check_directory_exists(output_project_folder));

    auto clean_command = format_string(arena, "% clean all", binary_path);
    auto clean_cmd_result = run_system_command(arena, clean_command);
    require(clean_cmd_result);

    require(check_directory_exists(output_build_folder).value == false);
    require(check_directory_exists(output_project_folder).value == false);
  }

  {
    auto cbuild_output_folder  = make_file_path(arena, ".cbuild", "project_project");
    auto output_build_folder   = make_file_path(arena, cbuild_output_folder, "build");
    auto output_project_folder = make_file_path(arena, cbuild_output_folder, "config");

    require(check_directory_exists(output_build_folder));
    require(check_directory_exists(output_project_folder));

    auto clean_command = format_string(arena, "% -p=project/config.cpp clean all", binary_path);
    auto clean_cmd_result = run_system_command(arena, clean_command);
    require(clean_cmd_result);

    require(check_directory_exists(output_build_folder).value == false);
    require(check_directory_exists(output_project_folder).value == false);
  }
}

static Test_Case clean_command_tests [] {
  define_test_case_ex(basic_clean_command_usage,           setup_workspace, cleanup_workspace),
  define_test_case_ex(complete_clean_command_usage,        setup_workspace, cleanup_workspace),
  define_test_case_ex(cleanup_with_project_override_tests, setup_workspace, cleanup_workspace),
};

define_test_suite(clean_command, clean_command_tests)
