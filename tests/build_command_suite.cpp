
#include "anyfin/threads.hpp"

#include "test_suite.hpp"

extern File_Path working_directory; // Path to the root directory where the 'verify' program has been called
extern File_Path testspace_directory;         // Path to the workspace folder where all intermediary files and folders are created
extern File_Path binary_path;       // Executable under test

constexpr bool disable_piping = false;

static void setup_workspace (Memory_Arena &) {
  if (check_directory_exists(testspace_directory).or_default(true))
    delete_directory(testspace_directory);

  create_directory(testspace_directory);
  set_working_directory(testspace_directory);
}

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

static void require_path_not_exists (File_Path path, Callsite callsite = {}) {
  auto check = check_resource_exists(path);
  crequire(check, callsite);
  crequire(check.value == false, callsite);
}

static void require_path_exists (File_Path path, Callsite callsite = {}) {
  auto check = check_resource_exists(path);
  crequire(check, callsite);
  crequire(check.value, callsite);
}

static String run_command (Memory_Arena &arena, String binary_path, String extra_arguments = {}) {
  auto command = concat_string(arena, binary_path, " ", extra_arguments);
  auto result  = run_system_command(arena, command);
  require(result);
  require(result.value.status_code == 0);

  return result.value.output;
}

static String build_testsite (Memory_Arena &arena, String extra_arguments = {}) {
  auto build_command    = concat_string(arena, binary_path, " build ", extra_arguments);
  auto build_cmd_result = run_system_command(arena, build_command);
  require(build_cmd_result);

  return build_cmd_result.value.output;
}

static String build_project_testsite (Memory_Arena &arena, String project_dir = {}) {
  auto project_override = "";
  if (project_dir.length) {
    project_override = concat_string(arena, " -p=", project_dir);
  }

  auto build_command    = concat_string(arena, binary_path, project_override, " build");
  auto build_cmd_result = run_system_command(arena, build_command);
  require(build_cmd_result);

  return build_cmd_result.value.output;
}

static void validate_binary (Memory_Arena &arena, String binary_name, String expected_result, String project_name = "") {
  String project_dir = "project";
  if (project_name.length) {
    project_dir = concat_string(arena, "project_", project_name);
  }

  auto name = concat_string(arena, binary_name, ".exe");
  auto path = make_file_path(arena, ".cbuild", project_dir, "build", "out", name);

  auto output = run_command(arena, path);

  require(has_substring(output, expected_result));
}

static void require_lines_count (String output, String start_with, u32 expected_count) {
  u32 count = 0;

  split_string(output, '\n')
    .for_each([&] (auto it) { if (starts_with(it, start_with)) count += 1; });

  require(count == expected_count);
}

static void build_init_project_st_test (Memory_Arena &arena) {
  run_command(arena, binary_path, "init");
  run_command(arena, binary_path, "build builders=1");

  validate_binary(arena, "main", "Thank you for trying cbuild!");
}

static void build_init_cpp_project_tests (Memory_Arena &arena) {
  run_command(arena, binary_path, "init");
  run_command(arena, binary_path, "build");

  validate_binary(arena, "main", "Thank you for trying cbuild!");
}

static void build_init_c_project_tests (Memory_Arena &arena) {
  run_command(arena, binary_path, "init type=c");
  run_command(arena, binary_path, "build");

  validate_binary(arena, "main", "Thank you for trying cbuild!");
}

static void build_testsite_tests (Memory_Arena &arena) {
  String toolchains [] { "msvc_x86", "msvc_x64" };
  String configs    [] { "debug", "release" };

  auto cbuild_output_folder = make_file_path(arena, ".cbuild");

  for (auto toolchain: toolchains) {
    for (auto config: configs) {
      auto local = arena;

      auto output = run_command(arena, binary_path, concat_string(arena, "build toolchain=", toolchain, " config=", config));

      require(has_substring(output, concat_string(local, "Selected toolchain - ", toolchain)));
      require(has_substring(output, concat_string(local, "Selected configuration - ", config)));

      require_lines_count(output, "Building file", 10);

      require_path_exists(cbuild_output_folder);

      validate_binary(arena, "binary1", "lib1,lib2,dyn1,dyn2,bin1");
      validate_binary(arena, "binary2", "lib3,dyn3,bin2");

      delete_directory(cbuild_output_folder);
    }
  }
}

static void build_registry_tests (Memory_Arena &arena) {
  auto executable_path = make_file_path(arena, ".cbuild", "project", "build", "out", "main.exe");

  auto output = build_testsite(arena);
  require_lines_count(output, "Building file", 10);
  validate_binary(arena, "binary1", "lib1,lib2,dyn1,dyn2,bin1");
  validate_binary(arena, "binary2", "lib3,dyn3,bin2");

  for (int idx = 0; idx < 5; idx++) {
    auto output2 = build_testsite(arena);
    require_lines_count(output2, "Building file", 0);
    validate_binary(arena, "binary1", "lib1,lib2,dyn1,dyn2,bin1");
    validate_binary(arena, "binary2", "lib3,dyn3,bin2");
  }

  for (int idx = 0; idx < 5; idx++) {
    auto output2 = build_testsite(arena, "cache=off");
    require_lines_count(output2, "Building file", 10);

    validate_binary(arena, "binary1", "lib1,lib2,dyn1,dyn2,bin1");
    validate_binary(arena, "binary2", "lib3,dyn3,bin2");
  }

  for (int idx = 0; idx < 5; idx++) {
    auto output2 = build_testsite(arena);
    require_lines_count(output2, "Building file", 0);
    validate_binary(arena, "binary1", "lib1,lib2,dyn1,dyn2,bin1");
    validate_binary(arena, "binary2", "lib3,dyn3,bin2");
  }
}

static void build_changes_tests (Memory_Arena &arena) {
  using enum File_System_Flags;

  auto output = build_testsite(arena);
  require_lines_count(output, "Building file", 10);

  validate_binary(arena, "binary1", "lib1,lib2,dyn1,dyn2,bin1");
  validate_binary(arena, "binary2", "lib3,dyn3,bin2");

  String new_lib_impl = R"lib(
#include <cstdio>

void library2 () {
  printf("lib2_updated");
  fflush(stdout);
}
)lib";

  auto old_library_path = make_file_path(arena, "code", "library2", "library2.cpp");
  auto new_library_path = make_file_path(arena, "code", "library2", "new_library2.cpp");

  delete_file(old_library_path);

  auto new_lib = open_file(new_library_path, Write_Access | Create_Missing).value;
  require(write_bytes_to_file(new_lib, new_lib_impl));
  require(close_file(new_lib));

  {
    auto output = build_testsite(arena);

    require_lines_count(output, "Building file",  1); // library2.cpp
    require_lines_count(output, "Linking target", 3); // library2, dynamic2, binary1

    validate_binary(arena, "binary1", "lib2_updated,dyn1,dyn2,bin1");
  }

  String metabase_file_content = R"(
#pragma once

#define META_BASE "new"
)";

  auto metabase_header_file = open_file(make_file_path(arena, "code", "metabase.hpp"), Write_Access | Create_Missing).value;
  require(write_bytes_to_file(metabase_header_file, metabase_file_content));
  close_file(metabase_header_file);

  String base_file_content = R"(
#pragma once

#define EXPORT_SYMBOL __declspec(dllexport)

#include "metabase.hpp"
)";

  auto base_header_file = open_file(make_file_path(arena, "code", "base.hpp"), Write_Access).value;
  require(write_bytes_to_file(base_header_file, base_file_content));
  close_file(base_header_file);
                                    
  {
    auto output = build_testsite(arena);

    require_lines_count(output, "Building file",  3); // dynamic1, dynamic2, dynamic3
    require_lines_count(output, "Linking target", 5); // dynamic1, dynamic2, dynamic3, binary1, binary2

    validate_binary(arena, "binary1", "lib2_updated,dyn1,dyn2,bin1");
    validate_binary(arena, "binary2", "lib3,dyn3,bin2");
  }
}

static void build_errors_tests (Memory_Arena &arena) {
  using enum File_System_Flags;

  auto output = build_testsite(arena);
  require_lines_count(output, "Building file", 10);

  validate_binary(arena, "binary1", "lib1,lib2,dyn1,dyn2,bin1");
  validate_binary(arena, "binary2", "lib3,dyn3,bin2");

  auto file_path = make_file_path(arena, "code", "dynamic1", "dynamic1.cpp");

  String bad_code_impl = R"lib(
#include <cstdio>

void dynamic1 () {
  printf("dyn1");
  1 + "foo"
  fflush(stdout);
}
)lib";

  auto write_file_handle = open_file(file_path, Write_Access).value;
  require(write_bytes_to_file(write_file_handle, bad_code_impl));
  close_file(write_file_handle);

  String new_lib_impl = R"lib(
#include <cstdio>

void library2 () {
  printf("lib2_updated");
  fflush(stdout);
}
)lib";

  require(delete_file(make_file_path(arena, "code", "library2", "library2.cpp")));

  auto new_lib = open_file(make_file_path(arena, "code", "library2", "new_library2.cpp"), Write_Access | Create_Missing).value;
  require(write_bytes_to_file(new_lib, new_lib_impl));
  close_file(new_lib);

  {
    auto build_command    = format_string(arena, "% build", binary_path);
    auto build_result = run_system_command(arena, build_command);
    require(build_result.is_ok());
    require(build_result.value.status_code != 0);

    require_lines_count(build_result.value.output, "Building file", 2); // dynamic1, library2
    require_lines_count(build_result.value.output, "Linking target", 1); // library2
    require_lines_count(build_result.value.output, "WARNING: File compilation failed", 1);
  }

  for (int idx = 0; idx < 5; idx++) {
    auto build_command    = format_string(arena, "% build", binary_path);
    auto build_result = run_system_command(arena, build_command);
    require(build_result.is_ok());
    require(build_result.value.status_code != 0);

    require_lines_count(build_result.value.output, "Building file", 1); // dynamic1
    require_lines_count(build_result.value.output, "Linking target", 0); 
    require_lines_count(build_result.value.output, "WARNING: File compilation failed", 1);
  }

  String fixed_code_impl = R"lib(
#include <cstdio>

#include "base.hpp"

EXPORT_SYMBOL void dynamic1 () {
  printf("dyn1_updated");
  fflush(stdout);
}
)lib";

  delete_file(file_path);

  auto fixed_handle = open_file(file_path, Write_Access | Create_Missing).value;
  require(write_bytes_to_file(fixed_handle, fixed_code_impl));
  close_file(fixed_handle);

  auto output3 = build_testsite(arena);

  require_lines_count(output3, "Building file", 1); // dynamic1
  require_lines_count(output3, "Linking target", 3); // dynamic1, dynamic2, binary1

  validate_binary(arena, "binary1", "lib2_updated,dyn1_updated,dyn2,bin1");
  validate_binary(arena, "binary2", "lib3,dyn3,bin2");
}

static void test_modify_file (Memory_Arena &arena, File_Path file_path) {
  using enum File_System_Flags;

  auto file    = open_file(move(file_path), Write_Access).value;
  auto mapping = map_file_into_memory(file).value; 

  auto file_content = reserve<char>(arena, mapping.size + 2);
  copy_memory(file_content, mapping.memory, mapping.size);
  file_content[mapping.size] = ' ';
  file_content[mapping.size + 1] = '\0';
  unmap_file(mapping);

  // Short sleep in case things go too fast and the update won't be noticed?
  thread_sleep(1000);
  
  reset_file_cursor(file);
  require(write_bytes_to_file(file, file_content, mapping.size + 2));

  close_file(file);
}

static void build_project_tests (Memory_Arena &arena) {
  auto output = build_testsite(arena);
  require_lines_count(output, "Building file", 10);
  require_lines_count(output, "Linking target", 10);

  validate_binary(arena, "binary1", "lib1,lib2,dyn1,dyn2,bin1");
  validate_binary(arena, "binary2", "lib3,dyn3,bin2");

  test_modify_file(arena, make_file_path(arena, "project", "build.cpp"));
  
  auto output2 = build_testsite(arena);
  require_lines_count(output2, "Building file", 10);
  require_lines_count(output2, "Linking target", 10);

  validate_binary(arena, "binary1", "lib1,lib2,dyn1,dyn2,bin1");
  validate_binary(arena, "binary2", "lib3,dyn3,bin2");

  for (int idx = 0; idx < 5; idx++) {
    auto output3 = build_testsite(arena);
    require_lines_count(output3, "Building file", 0);
    require_lines_count(output3, "Linking target", 0);

    validate_binary(arena, "binary1", "lib1,lib2,dyn1,dyn2,bin1");
    validate_binary(arena, "binary2", "lib3,dyn3,bin2");
  }
}

static void build_cache_tests (Memory_Arena &arena) {
  auto registry_file = make_file_path(arena, testspace_directory, ".cbuild", "project", "build", "__registry");
  require_path_not_exists(registry_file);

  build_testsite(arena, "cache=off");
  validate_binary(arena, "binary1", "lib1,lib2,dyn1,dyn2,bin1");
  validate_binary(arena, "binary2", "lib3,dyn3,bin2");

  require_path_not_exists(registry_file);

  auto output = build_testsite(arena);
  require_lines_count(output, "Building file",  10); 
  require_lines_count(output, "Linking target", 10);

  require_path_exists(registry_file);

  auto output2 = build_testsite(arena, "cache=flush");
  require_lines_count(output2, "Building file",  10); 
  require_lines_count(output2, "Linking target", 10);

  require_path_exists(registry_file);

  auto output3 = build_testsite(arena);
  require_lines_count(output3, "Building file",  0); 
  require_lines_count(output3, "Linking target", 0);

  auto output4 = build_testsite(arena, "cache=off");
  require_lines_count(output4, "Building file",  10); 
  require_lines_count(output4, "Linking target", 10);

  validate_binary(arena, "binary1", "lib1,lib2,dyn1,dyn2,bin1");
  validate_binary(arena, "binary2", "lib3,dyn3,bin2");
}

static void build_targets_tests (Memory_Arena &arena) {
  auto output = build_testsite(arena, "targets=library1");
  require_lines_count(output, "Building file",  1); // library1
  require_lines_count(output, "Linking target", 1); // library1

  auto output3 = build_testsite(arena, "targets=binary2,library3");
  require_lines_count(output3, "Building file",  3); // binary2, dynamic3, library3
  require_lines_count(output3, "Linking target", 3);

  {
    // Thi was already build, we should not build it second time.
    auto output = build_testsite(arena, "targets=library1");
    require_lines_count(output, "Building file",  0);
    require_lines_count(output, "Linking target", 0);
  }

  auto output2 = build_testsite(arena, "targets=dynamic2,");
  require_lines_count(output2, "Building file",  3); // library2, dynamic1, dynamic2
  require_lines_count(output2, "Linking target", 3); 

  auto output4 = build_testsite(arena);
  require_lines_count(output4, "Building file",  3); // binary1, library4, binary3
  require_lines_count(output4, "Linking target", 3);

  {
    auto build_command = format_string(arena, "% build targets=nonexisting", binary_path);
    auto build_result = run_system_command(arena, build_command);
    require(build_result.is_ok());
    require(build_result.value.status_code != 0);
    require(has_substring(build_result.value.output, "Target 'nonexisting' not found in the project"));
  }

  {
    auto build_command = format_string(arena, "% build targets=library1,nonexisting", binary_path);
    auto build_result = run_system_command(arena, build_command);
    require(build_result.is_ok());
    require(build_result.value.status_code != 0);
    require(has_substring(build_result.value.output, "Target 'nonexisting' not found in the project"));
  }
}

static void build_with_project_overwrite_tests (Memory_Arena &arena) {
  auto output1 = build_project_testsite(arena);
  require_lines_count(output1, "Building file", 10);
  validate_binary(arena, "binary1", "lib1,lib2,dyn1,dyn2,bin1");
  validate_binary(arena, "binary2", "lib3,dyn3,bin2");

  auto output2 = build_project_testsite(arena);
  require_lines_count(output2, "Building file", 0);

  auto output3 = build_project_testsite(arena, "projectv2");
  require_lines_count(output3, "Building file", 2);
  validate_binary(arena, "binary4", "lib1,bin4", "projectv2");

  require_crash(validate_binary(arena, "binary1", "lib1,lib2,dyn1,dyn2,bin1", "projectv2"));
  require_crash(validate_binary(arena, "binary2", "lib3,dyn3,bin2",           "projectv2"));
  require_crash(validate_binary(arena, "binary4", "lib1,bin4"));

  auto output4 = build_project_testsite(arena, "projectv2");
  require_lines_count(output4, "Building file", 0);
}

static Test_Case build_command_tests [] {
  /*
    Unlike other tests these just build the project generated with init to ensure that the basic
    functionality works as expected. We don't need to prepare the testsite for these, just a dedicated
    workspace that we can cleanup later.
   */
  define_test_case_ex(build_init_project_st_test,   setup_workspace, cleanup_workspace),
  define_test_case_ex(build_init_cpp_project_tests, setup_workspace, cleanup_workspace),
  define_test_case_ex(build_init_c_project_tests,   setup_workspace, cleanup_workspace),

  define_test_case_ex(build_testsite_tests,        setup_testsite, cleanup_workspace),
  define_test_case_ex(build_registry_tests,        setup_testsite, cleanup_workspace),
  define_test_case_ex(build_changes_tests,         setup_testsite, cleanup_workspace),
  define_test_case_ex(build_errors_tests,          setup_testsite, cleanup_workspace),
  define_test_case_ex(build_project_tests,         setup_testsite, cleanup_workspace),
  define_test_case_ex(build_cache_tests,           setup_testsite, cleanup_workspace),
  define_test_case_ex(build_targets_tests,         setup_testsite, cleanup_workspace),

  define_test_case_ex(build_with_project_overwrite_tests, setup_testsite, cleanup_workspace),
};

define_test_suite(build_command, build_command_tests)
