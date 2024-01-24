
#include "test_suite.hpp"

extern File_Path working_directory; // Path to the root directory where the 'verify' program has been called
extern File_Path workspace;         // Path to the workspace folder where all intermediary files and folders are created
extern File_Path binary_path;       // Executable under test

constexpr bool disable_piping = false;

static void setup_workspace (Memory_Arena &arena) {
  auto [error, exists] = check_directory_exists(workspace);
  if (!error && exists) delete_directory(workspace);

  create_directory(workspace);
  set_working_directory(workspace);
}

static void setup_testsite (Memory_Arena &arena) {
  auto [error, exists] = check_directory_exists(workspace);
  if (!error && exists) delete_directory(workspace);

  create_directory(workspace);

  auto testsite_path = make_file_path(arena, working_directory, "tests", "testsite");
  copy_directory(testsite_path, workspace);

  set_working_directory(workspace);
}

static void cleanup_workspace (Memory_Arena &arena) {
  set_working_directory(working_directory);
  delete_directory(workspace);
}

static void build_init_project_st_test (Memory_Arena &arena) {
  auto init_command    = format_string(arena, "% init", binary_path);
  auto init_cmd_result = run_system_command(arena, init_command);
  require(init_cmd_result);

  auto build_command    = format_string(arena, "% build builders=1", binary_path);
  auto build_cmd_result = run_system_command(arena, build_command);
  require(build_cmd_result);

  auto output_folder = String(".cbuild");
  require(check_directory_exists(output_folder));

  auto produced_binary_path = make_file_path(arena, output_folder, "build", "out", "main.exe");
  require(check_file_exists(produced_binary_path));

  auto run_command = format_string(arena, "%", produced_binary_path);
  auto cmd_result = run_system_command(arena, run_command);
  require(cmd_result);
  require(has_substring(cmd_result.value.output, "Thank you for trying cbuild!"));
}

static void build_init_project_tests (Memory_Arena &arena) {
  auto init_command    = format_string(arena, "% init", binary_path);
  auto init_cmd_result = run_system_command(arena, init_command);
  require(init_cmd_result);

  auto build_command    = format_string(arena, "% build", binary_path);
  auto build_cmd_result = run_system_command(arena, build_command);
  require(build_cmd_result);

  auto output_folder = String(".cbuild");
  require(check_directory_exists(output_folder));

  auto produced_binary_path = make_file_path(arena, output_folder, "build", "out", "main.exe");
  require(check_file_exists(produced_binary_path));

  auto run_command = format_string(arena, "%", produced_binary_path);
  auto cmd_result = run_system_command(arena, run_command);
  require(cmd_result);
  require(has_substring(cmd_result.value.output, "Thank you for trying cbuild!"));
}

static String build_testsite (Memory_Arena &arena, String extra_arguments = {}) {
  auto build_command = format_string(arena, "% build %", binary_path, extra_arguments);
  auto build_cmd_result = run_system_command(arena, build_command);
  require(build_cmd_result);

  return move(build_cmd_result.value.output);
}

static void validate_binary (Memory_Arena &arena, String binary_name, String expected_result) {
  auto path = make_file_path(arena, ".cbuild", "build", "out", binary_name);

  auto result = run_system_command(arena, path); 
  require(result);

  require(has_substring(result.value.output, expected_result));
}

static u32 count_lines_starting_with (String output, String start_with, u32 expected_count) {
  u32 count = 0;

  split_string(output, '\n')
    .for_each([&] (auto it) { if (starts_with(it, start_with)) count += 1; });

  return count;
}

static void build_testsite_tests (Memory_Arena &arena) {
  String toolchains [] { "msvc_x86", "msvc_x64" };
  String configs    [] { "debug", "release" };

  auto cbuild_output_folder = make_file_path(arena, ".cbuild");

  for (auto toolchain: toolchains) {
    for (auto config: configs) {
      auto local = arena;

      auto command = format_string(local, "% build toolchain=% config=%", binary_path, toolchain, config);
      auto result = run_system_command(local, command);
      require(result);

      // require(has_substring(result.value.output, format_string(local, "Selected toolchain - %", toolchain)));
      // require(has_substring(result.value.output, format_string(local, "Selected configuration - %", config)));

      count_lines_starting_with(result.value.output, "Building file", 9);

      require(check_directory_exists(cbuild_output_folder));

      validate_binary(arena, "binary1.exe", "lib1,lib2,dyn1,dyn2,bin1");
      validate_binary(arena, "binary2.exe", "lib3,dyn3,bin2");

      delete_directory(cbuild_output_folder);
    }
  }
}

static void build_registry_tests (Memory_Arena &arena) {
  auto executable_path = make_file_path(arena, ".cbuild", "build", "out", "main.exe");

  auto output = build_testsite(arena);
  count_lines_starting_with(output, "Building file", 9);
  validate_binary(arena, "binary1.exe", "lib1,lib2,dyn1,dyn2,bin1");
  validate_binary(arena, "binary2.exe", "lib3,dyn3,bin2");

  for (int idx = 0; idx < 5; idx++) {
    auto output2 = build_testsite(arena);
    count_lines_starting_with(output2, "Building file", 0);
    validate_binary(arena, "binary1.exe", "lib1,lib2,dyn1,dyn2,bin1");
    validate_binary(arena, "binary2.exe", "lib3,dyn3,bin2");
  }

  for (int idx = 0; idx < 5; idx++) {
    auto output2 = build_testsite(arena, "cache=off");
    count_lines_starting_with(output2, "Building file", 9);

    validate_binary(arena, "binary1.exe", "lib1,lib2,dyn1,dyn2,bin1");
    validate_binary(arena, "binary2.exe", "lib3,dyn3,bin2");
  }

  for (int idx = 0; idx < 5; idx++) {
    auto output2 = build_testsite(arena);
    count_lines_starting_with(output2, "Building file", 0);
    validate_binary(arena, "binary1.exe", "lib1,lib2,dyn1,dyn2,bin1");
    validate_binary(arena, "binary2.exe", "lib3,dyn3,bin2");
  }
}

static void build_changes_tests (Memory_Arena &arena) {
  using enum File_System_Flags;

  auto output = build_testsite(arena);
  count_lines_starting_with(output, "Building file", 9);

  validate_binary(arena, "binary1.exe", "lib1,lib2,dyn1,dyn2,bin1");
  validate_binary(arena, "binary2.exe", "lib3,dyn3,bin2");

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

    count_lines_starting_with(output, "Building file",  1); // library2.cpp
    count_lines_starting_with(output, "Linking target", 3); // library2, dynamic2, binary1

    validate_binary(arena, "binary1.exe", "lib2_updated,dyn1,dyn2,bin1");
  }

  String metabase_file_content = R"(
#pragma once

#define META_BASE "new"
)";

  auto metabase_header_file = open_file(make_file_path(arena, "code", "metabase.hpp"), Write_Access | Create_Missing).value;
  defer { close_file(metabase_header_file); };

  require(write_bytes_to_file(metabase_header_file, metabase_file_content));

  String base_file_content = R"(
#pragma once

#define EXPORT_SYMBOL __declspec(dllexport)

#include "metabase.hpp"
)";

  auto base_header_file = open_file(make_file_path(arena, "code", "base.hpp"), Write_Access).value;
  defer { close_file(base_header_file); };

  require(write_bytes_to_file(base_header_file, base_file_content));
                                    
  {
    auto output = build_testsite(arena);

    count_lines_starting_with(output, "Building file",  3); // dynamic1, dynamic2, dynamic3
    count_lines_starting_with(output, "Linking target", 5); // dynamic1, dynamic2, dynamic3, binary1, binary2

    validate_binary(arena, "binary1.exe", "lib2_updated,dyn1,dyn2,bin1");
    validate_binary(arena, "binary2.exe", "lib3,dyn3,bin2");
  }
}

static void build_errors_tests (Memory_Arena &arena) {
  using enum File_System_Flags;

  auto output = build_testsite(arena);
  count_lines_starting_with(output, "Building file", 9);

  validate_binary(arena, "binary1.exe", "lib1,lib2,dyn1,dyn2,bin1");
  validate_binary(arena, "binary2.exe", "lib3,dyn3,bin2");

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
    require(build_result);

    count_lines_starting_with(build_result.value.output, "Building file", 2); // dynamic1, library2
    count_lines_starting_with(build_result.value.output, "Linking target", 1); // library2
    count_lines_starting_with(build_result.value.output, "Program terminated with an error status", 1);
  }

  for (int idx = 0; idx < 5; idx++) {
    auto build_command    = format_string(arena, "% build", binary_path);
    auto build_result = run_system_command(arena, build_command);
    require(build_result);

    count_lines_starting_with(build_result.value.output, "Building file", 1); // dynamic1
    count_lines_starting_with(build_result.value.output, "Linking target", 0); 
    count_lines_starting_with(build_result.value.output, "Program terminated with an error status", 1);
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

  count_lines_starting_with(output3, "Building file", 1); // dynamic1
  count_lines_starting_with(output3, "Linking target", 3); // dynamic1, dynamic2, binary1

  validate_binary(arena, "binary1.exe", "lib2_updated,dyn1_updated,dyn2,bin1");
  validate_binary(arena, "binary2.exe", "lib3,dyn3,bin2");
}

static void test_modify_file (Memory_Arena &arena, File_Path file_path) {
  using enum File_System_Flags;

  auto file    = open_file(move(file_path), Write_Access).value;
  auto mapping = map_file_into_memory(file).value; 
  defer {
    unmap_file(mapping);
    close_file(file);
  };

  auto file_content = reserve<char>(arena, mapping.size + 2);
  copy_memory(file_content, mapping.memory, mapping.size);
  file_content[mapping.size] = ' ';
  file_content[mapping.size + 1] = '\0';
  
  reset_file_cursor(file);

  require(write_bytes_to_file(file, file_content, mapping.size + 2));
}

static void build_project_tests (Memory_Arena &arena) {
  auto output = build_testsite(arena);
  count_lines_starting_with(output, "Building file", 9);
  count_lines_starting_with(output, "Linking target", 9);

  validate_binary(arena, "binary1.exe", "lib1,lib2,dyn1,dyn2,bin1");
  validate_binary(arena, "binary2.exe", "lib3,dyn3,bin2");

  test_modify_file(arena, make_file_path(arena, "project", "build.cpp"));
  
  auto output2 = build_testsite(arena);
  count_lines_starting_with(output2, "Building file", 9);
  count_lines_starting_with(output2, "Linking target", 9);

  validate_binary(arena, "binary1.exe", "lib1,lib2,dyn1,dyn2,bin1");
  validate_binary(arena, "binary2.exe", "lib3,dyn3,bin2");

  for (int idx = 0; idx < 5; idx++) {
    auto output3 = build_testsite(arena);
    count_lines_starting_with(output3, "Building file", 0);
    count_lines_starting_with(output3, "Linking target", 0);

    validate_binary(arena, "binary1.exe", "lib1,lib2,dyn1,dyn2,bin1");
    validate_binary(arena, "binary2.exe", "lib3,dyn3,bin2");
  }
}

static void build_cache_tests (Memory_Arena &arena) {
  build_testsite(arena, "cache=off");
  validate_binary(arena, "binary1.exe", "lib1,lib2,dyn1,dyn2,bin1");
  validate_binary(arena, "binary2.exe", "lib3,dyn3,bin2");

  auto registry_file = make_file_path(arena, ".cbuild", "build", "__registry");
  require(check_file_exists(registry_file).value);

  auto output = build_testsite(arena);
  count_lines_starting_with(output, "Building file",  9); 
  count_lines_starting_with(output, "Linking target", 9);

  require(check_file_exists(registry_file).value);

  auto output2 = build_testsite(arena, "cache=flush");
  count_lines_starting_with(output2, "Building file",  9); 
  count_lines_starting_with(output2, "Linking target", 9);

  require(check_file_exists(registry_file).value);

  auto output3 = build_testsite(arena);
  count_lines_starting_with(output3, "Building file",  0); 
  count_lines_starting_with(output3, "Linking target", 0);

  auto output4 = build_testsite(arena, "cache=off");
  count_lines_starting_with(output4, "Building file",  9); 
  count_lines_starting_with(output4, "Linking target", 9);

  validate_binary(arena, "binary1.exe", "lib1,lib2,dyn1,dyn2,bin1");
  validate_binary(arena, "binary2.exe", "lib3,dyn3,bin2");
}

static void build_targets_tests (Memory_Arena &arena) {
  auto output = build_testsite(arena, "targets=library1");
  count_lines_starting_with(output, "Building file",  1); // library1
  count_lines_starting_with(output, "Linking target", 1); // library1

  auto output3 = build_testsite(arena, "targets=binary2,library3");
  count_lines_starting_with(output3, "Building file",  3); // binary2, dynamic3, library3
  count_lines_starting_with(output3, "Linking target", 3);

  {
    auto output = build_testsite(arena, "targets=,library1");
    count_lines_starting_with(output, "Building file",  1); // library1
    count_lines_starting_with(output, "Linking target", 1); // library1
  }

  auto output2 = build_testsite(arena, "targets=dynamic2,");
  count_lines_starting_with(output2, "Building file",  3); // library2, dynamic1, dynamic2
  count_lines_starting_with(output2, "Linking target", 3); 

  auto output4 = build_testsite(arena);
  count_lines_starting_with(output4, "Building file",  2); // binary1, library4
  count_lines_starting_with(output4, "Linking target", 2);

  {
    auto build_command = format_string(arena, "% build targets=nonexisting", binary_path);
    auto build_result = run_system_command(arena, build_command);
    require(build_result.is_ok());
    require(build_result.value.status_code != 0);
    require(has_substring(build_result.value.output, "Target 'nonexisting' not found in the project"));
  }

}

static Test_Case build_command_tests [] {
  define_test_case_ex(build_init_project_st_test, setup_workspace,  cleanup_workspace),
  define_test_case_ex(build_init_project_tests,    setup_workspace,  cleanup_workspace),
  define_test_case_ex(build_testsite_tests,        setup_testsite,   cleanup_workspace),
  define_test_case_ex(build_registry_tests,        setup_testsite,   cleanup_workspace),
  define_test_case_ex(build_changes_tests,         setup_testsite,   cleanup_workspace),
  define_test_case_ex(build_errors_tests,          setup_testsite,   cleanup_workspace),
  define_test_case_ex(build_project_tests,         setup_testsite,   cleanup_workspace),
  define_test_case_ex(build_cache_tests,           setup_testsite,   cleanup_workspace),
  define_test_case_ex(build_targets_tests,         setup_testsite,   cleanup_workspace),
};

define_test_suite(build_command, build_command_tests)
