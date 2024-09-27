
#include "test_suite.hpp"

extern File_Path working_directory; // Path to the root directory where the 'verify' program has been called
extern File_Path testspace_directory;         // Path to the workspace folder where all intermediary files and folders are created
extern File_Path binary_path;       // Executable under test

static void setup_workspace (Memory_Arena &) {
  if (check_directory_exists(testspace_directory).or_default(true))
    require(delete_directory(testspace_directory));

  require(create_directory(testspace_directory));
  require(set_working_directory(testspace_directory));
}

static void cleanup_workspace (Memory_Arena &) {
  require(set_working_directory(working_directory));
  require(delete_directory(testspace_directory));
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

  auto [init_cmd_has_failed, status] = run_system_command(arena, command);
  require(!init_cmd_has_failed);
  require(status.status_code != 0);

  frequire(has_substring(status.output, "ERROR: Unrecognized argument value for the 'type' option: 'rust'"),
           concat_string(arena, "status.output = ", status.output));
}

static void init_with_unset_type_parameter_test (Memory_Arena &arena) {
  auto command = concat_string(arena, binary_path, " init type");

  auto [init_cmd_has_failed, status] = run_system_command(arena, command);
  require(!init_cmd_has_failed);
  require(status.status_code == 1);

  require(has_substring(status.output, "ERROR: Invalid option value for the key 'type', expected format: <key>=<value>"));
}

static void init_with_unset_type_parameter_2_test (Memory_Arena &arena) {
  auto command = concat_string(arena, binary_path, " init type=");

  auto [init_cmd_has_failed, status] = run_system_command(arena, command);
  require(!init_cmd_has_failed);
  require(status.status_code == 1);

  require(has_substring(status.output, "ERROR: Unrecognized argument value for the 'type' option: ''"));
}

static void init_with_project_overwrite_test (Memory_Arena &arena) {
  {
    auto command = concat_string(arena, binary_path, " -p=alternative init");

    auto project_folder = make_file_path(arena, "alternative");
  
    require(check_directory_exists(project_folder).or_default(true) == false);

    auto [init_cmd_has_failed, status] = run_system_command(arena, command);
    require(!init_cmd_has_failed);
    require(status.status_code == 0);

    require(check_directory_exists(project_folder));
    require(check_file_exists(make_file_path(arena, "alternative", "build.cpp")));
    require(check_file_exists(make_file_path(arena, "alternative", "cbuild.h")));

    delete_directory(project_folder);
  }

  {
    auto command = concat_string(arena, binary_path, " -p=project/config.cpp init");

    auto project_folder = make_file_path(arena, "project");
  
    require(check_directory_exists(project_folder).or_default(true) == false);

    auto [init_cmd_has_failed, status] = run_system_command(arena, command);
    require(!init_cmd_has_failed);
    require(status.status_code == 0);

    require(check_directory_exists(project_folder));
    require(check_file_exists(make_file_path(arena, "project", "config.cpp")));
    require(check_file_exists(make_file_path(arena, "project", "build.cpp")).or_default(true) == false);
    require(check_file_exists(make_file_path(arena, "project", "cbuild.h")));

    delete_directory(project_folder);
  }

  {
    auto command = concat_string(arena, binary_path, " --project=project/build.cpp init");

    auto project_folder = make_file_path(arena, "project");
  
    require(check_directory_exists(project_folder).or_default(true) == false);

    auto [init_cmd_has_failed, status] = run_system_command(arena, command);
    require(!init_cmd_has_failed);
    require(status.status_code == 0);

    require(check_directory_exists(project_folder));
    require(check_file_exists(make_file_path(arena, "project", "build.cpp")));
    require(check_file_exists(make_file_path(arena, "project", "cbuild.h")));

    delete_directory(project_folder);
  }

  {
    auto command = concat_string(arena, binary_path, " --project=project/nested/build.cpp init");

    auto project_folder = make_file_path(arena, "project", "nested");
  
    require(check_directory_exists(project_folder).or_default(true) == false);

    auto [init_cmd_has_failed, status] = run_system_command(arena, command);
    require(!init_cmd_has_failed);
    require(status.status_code == 0);

    require(check_directory_exists(project_folder));
    require(check_file_exists(make_file_path(arena, "project", "nested", "build.cpp")));
    require(check_file_exists(make_file_path(arena, "project", "nested", "cbuild.h")));

    delete_directory(project_folder);
  }

  {
    {
      auto command = concat_string(arena, binary_path, " init");
      
      auto [init_cmd_has_failed, status] = run_system_command(arena, command);
      require(!init_cmd_has_failed);
      require(status.status_code == 0);
    }

    {
      auto command = concat_string(arena, binary_path, " --project=project/config.cpp init");

      auto [init_cmd_has_failed, status] = run_system_command(arena, command);
      require(!init_cmd_has_failed);
      require(status.status_code == 0);
    }

    auto project_folder = make_file_path(arena, "project");
    delete_directory(project_folder);
  }
}

static Test_Case init_command_tests [] {
    define_test_case_ex(init_project_test, setup_workspace, cleanup_workspace),
    define_test_case_ex(init_c_project_test, setup_workspace, cleanup_workspace),
    define_test_case_ex(init_cpp_project_test, setup_workspace, cleanup_workspace),
    define_test_case_ex(init_unknown_project_type_test, setup_workspace, cleanup_workspace),
    define_test_case_ex(init_with_unset_type_parameter_test, setup_workspace, cleanup_workspace),
    define_test_case_ex(init_with_unset_type_parameter_2_test, setup_workspace, cleanup_workspace),
    define_test_case_ex(init_with_project_overwrite_test, setup_workspace, cleanup_workspace),
};

define_test_suite(init_command, init_command_tests)
