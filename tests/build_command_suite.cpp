
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

static void setup_testbed (Memory_Arena *arena) {
  setup_workspace(arena);

  auto testbed_path = make_file_path(arena, working_directory, "tests", "testbed");

  copy_directory_content(arena, testbed_path, workspace);
}

static void cleanup_workspace (Memory_Arena *arena) {
  set_working_directory(working_directory);
  delete_directory(workspace);
}

static void build_init_project_tests (Memory_Arena *arena) {
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

static String build_testbed (Memory_Arena *arena, const String &extra_arguments = {}) {
  auto build_command    = format_string(arena, "% build %", binary_path, extra_arguments);
  auto [status, output] = run_system_command(arena, build_command);
  if (!status) print(arena, "%\n", output);
  require(status);

  return output;
}

#define validate_binary1(ARENA_PTR)                                     \
  do {                                                                  \
    auto path             = make_file_path((ARENA_PTR), ".cbuild", "build", "out", "binary1.exe"); \
    auto [status, output] = run_system_command((ARENA_PTR), format_string((ARENA_PTR), "%", path)); \
    require(status);                                                    \
    require(strstr(output.value, "lib1,lib2,dyn1,dyn2,bin1"));          \
  } while (0)

#define validate_binary2(ARENA_PTR)                                     \
  do {                                                                  \
    auto path = make_file_path((ARENA_PTR), ".cbuild", "build", "out", "binary2.exe"); \
    require(check_file_exists(&path));                                  \
    auto [status, output] = run_system_command((ARENA_PTR), format_string((ARENA_PTR), "%", path)); \
    require(status);                                                    \
    require(strstr(output.value, "lib3,dyn3,bin2"));                    \
  } while (0)

static bool get_line(const String *input, usize *position, String *line) {
  if (*position >= input->length) return false;

  size_t start = *position;
  while (*position < input->length && input->value[*position] != '\n') {
    (*position)++;
  }

  line->value = input->value + start;
  line->length = *position - start;

  // If the line ends with a newline, exclude it from the line length
  // and advance the position to start the next line after the newline.
  if (*position < input->length && input->value[*position] == '\n') {
    line->length--;
    (*position)++;
  }

  return true;
}

static void count_lines_starting_with (String output, const String &start_with, u32 expected_count) {
  size_t position = 0;
  struct String line;
  uint32_t count = 0;

  while (get_line(&output, &position, &line)) {
    if ((line.length >= start_with.length) &&
        (strncmp(line.value, start_with.value, start_with.length) == 0)) {
      count++;
    }
  }

  require(count == expected_count);
}

static void build_testbed_tests (Memory_Arena *arena) {
  const char *toolchains [] { "msvc_x86", "msvc_x64", "llvm", "llvm_cl" };
  const char *configs    [] { "debug", "release" };

  auto cbuild_output_folder = make_file_path(arena, workspace, ".cbuild");

  for (auto toolchain: toolchains) {
    for (auto config: configs) {
      auto local = *arena;

      auto command = format_string(&local, "% build toolchain=% config=%", binary_path, toolchain, config);
      auto result = run_system_command(&local, command);

      char check_phrase[64];

      {
        auto toolchain_length = strlen(toolchain);
        char phrase[] = "Selected toolchain - ";
        auto phrase_length = array_count_elements(phrase) - 1;

        copy_memory(check_phrase, phrase, phrase_length);
        copy_memory(check_phrase + phrase_length, toolchain, toolchain_length);
        check_phrase[phrase_length + toolchain_length] = '\0';

        require(strstr(result.output, check_phrase));
      }

      {
        auto config_length = strlen(config);
        char phrase[] = "Selected configuration - ";
        auto phrase_length = array_count_elements(phrase) - 1;

        copy_memory(check_phrase, phrase, phrase_length);
        copy_memory(check_phrase + phrase_length, config, config_length);
        check_phrase[phrase_length + config_length] = '\0';

        require(strstr(result.output, check_phrase));
      }

      if (!result.status) print(&local, "%\n", result.output);
      require(result.status == Status_Code::Success);

      count_lines_starting_with(result.output, "Building file", 9);

      require(check_directory_exists(&cbuild_output_folder));

      validate_binary1(arena);
      validate_binary2(arena);

      delete_directory(cbuild_output_folder);
    }
  }
}

static void build_registry_tests (Memory_Arena *arena) {
  auto executable_path = make_file_path(arena, workspace, ".cbuild", "build", "out", "main.exe");

  auto output = build_testbed(arena);
  count_lines_starting_with(output, "Building file", 9);
  validate_binary1(arena);
  validate_binary2(arena);

  for (int idx = 0; idx < 5; idx++) {
    auto output2 = build_testbed(arena);
    count_lines_starting_with(output2, "Building file", 0);
    validate_binary1(arena);
    validate_binary2(arena);
  }

  for (int idx = 0; idx < 5; idx++) {
    auto output2 = build_testbed(arena, "cache=off");
    count_lines_starting_with(output2, "Building file", 9);

    validate_binary1(arena);
    validate_binary2(arena);
  }

  for (int idx = 0; idx < 5; idx++) {
    auto output2 = build_testbed(arena);
    count_lines_starting_with(output2, "Building file", 0);
    validate_binary1(arena);
    validate_binary2(arena);
  }
}

static void build_changes_tests (Memory_Arena *arena) {
  auto output = build_testbed(arena);
  count_lines_starting_with(output, "Building file", 9);

  validate_binary1(arena);
  validate_binary2(arena);

  auto new_lib_impl = R"lib(
#include <cstdio>

void library2 () {
  printf("lib2_updated");
  fflush(stdout);
}
)lib";

  auto old_library_path = make_file_path(arena, "code", "library2", "library2.cpp");
  auto new_library_path = make_file_path(arena, "code", "library2", "new_library2.cpp");

  delete_file(old_library_path);

  auto new_lib = open_file(&new_library_path, Open_File_Flags::Request_Write_Access | Open_File_Flags::Create_File_If_Not_Exists);
  require(write_buffer_to_file(&new_lib, new_lib_impl, strlen(new_lib_impl)));
  close_file(&new_lib);

  {
    auto output = build_testbed(arena);

    count_lines_starting_with(output, "Building file",  1); // library2.cpp
    count_lines_starting_with(output, "Linking target", 3); // library2, dynamic2, binary1

    auto result = run_system_command(arena, make_file_path(arena, ".cbuild", "build", "out", "binary1.exe")->value);
    require(result.status);
    require(strstr(result.output.value, "lib2_updated,dyn1,dyn2,bin1"));
  }

  auto metabase_file_content = R"(
#pragma once

#define META_BASE "new"
)";

  auto metabase_header_file = open_file(&make_file_path(arena, "code", "metabase.hpp"),
                                        Open_File_Flags::Request_Write_Access | Open_File_Flags::Create_File_If_Not_Exists);
  require(write_buffer_to_file(&metabase_header_file, metabase_file_content, strlen(metabase_file_content)));
  close_file(&metabase_header_file);

  auto base_file_content = R"(
#pragma once

#define EXPORT_SYMBOL __declspec(dllexport)

#include "metabase.hpp"
)";

  auto base_header_file = open_file(&make_file_path(arena, "code", "base.hpp"), Open_File_Flags::Request_Write_Access);
  require(write_buffer_to_file(&base_header_file, base_file_content, strlen(base_file_content)));
  close_file(&base_header_file);
                                    
  {
    auto output = build_testbed(arena);
    count_lines_starting_with(output, "Building file",  3); // dynamic1, dynamic2, dynamic3
    count_lines_starting_with(output, "Linking target", 5); // dynamic1, dynamic2, dynamic3, binary1, binary2

    validate_binary2(arena);

    auto result = run_system_command(arena, make_file_path(arena, ".cbuild", "build", "out", "binary1.exe")->value);
    require(result.status);
    require(strstr(result.output.value, "lib2_updated,dyn1,dyn2,bin1"));
  }
}

static void build_errors_tests (Memory_Arena *arena) {
  auto output = build_testbed(arena);
  count_lines_starting_with(output, "Building file", 9);

  validate_binary1(arena);
  validate_binary2(arena);

  auto file_path = make_file_path(arena, "code", "dynamic1", "dynamic1.cpp");
  auto file      = open_file(&file_path);
  auto mapping   = map_file_into_memory(&file);

  auto correct_file_content = reserve_memory(arena, mapping->size + 1);
  copy_memory(correct_file_content, mapping->memory, mapping->size);
  correct_file_content[mapping->size] = '\0';

  unmap_file(&mapping);
  close_file(&file);

  auto bad_code_impl = R"lib(
#include <cstdio>

void dynamic1 () {
  printf("dyn1");
  1 + "foo"
  fflush(stdout);
}
)lib";

  auto write_file_handle = open_file(&file_path, Open_File_Flags::Request_Write_Access);
  require(write_buffer_to_file(&write_file_handle, bad_code_impl, strlen(bad_code_impl)));
  close_file(&write_file_handle);

  auto new_lib_impl = R"lib(
#include <cstdio>

void library2 () {
  printf("lib2_updated");
  fflush(stdout);
}
)lib";

  auto old_library_path = make_file_path(arena, "code", "library2", "library2.cpp");
  auto new_library_path = make_file_path(arena, "code", "library2", "new_library2.cpp");

  delete_file(old_library_path);

  auto new_lib = open_file(&new_library_path, Open_File_Flags::Request_Write_Access | Open_File_Flags::Create_File_If_Not_Exists);
  require(write_buffer_to_file(&new_lib, new_lib_impl, strlen(new_lib_impl)));
  close_file(&new_lib);

  {
    auto build_command    = format_string(arena, "% build", binary_path);
    auto [status, output2] = run_system_command(arena, build_command);
    require(status != Status_Code::Success);

    count_lines_starting_with(output2, "Building file", 2); // dynamic1, library2
    count_lines_starting_with(output2, "Linking target", 1); // library2
    count_lines_starting_with(output2, "Program terminated with an error status", 1);
  }

  for (int idx = 0; idx < 5; idx++) {
    auto build_command    = format_string(arena, "% build", binary_path);
    auto [status, output2] = run_system_command(arena, build_command);
    require(status != Status_Code::Success);

    count_lines_starting_with(output2, "Building file", 1); // dynamic1
    count_lines_starting_with(output2, "Linking target", 0); 
    count_lines_starting_with(output2, "Program terminated with an error status", 1);
  }

  auto fixed_code_impl = R"lib(
#include <cstdio>

#include "base.hpp"

EXPORT_SYMBOL void dynamic1 () {
  printf("dyn1_updated");
  fflush(stdout);
}
)lib";

  delete_file(file_path);
  auto fixed_handle = open_file(&file_path, Open_File_Flags::Request_Write_Access | Open_File_Flags::Create_File_If_Not_Exists);
  require(write_buffer_to_file(&fixed_handle, fixed_code_impl, strlen(fixed_code_impl)));
  close_file(&fixed_handle);

  auto output3 = build_testbed(arena);
  count_lines_starting_with(output3, "Building file", 1); // dynamic1
  count_lines_starting_with(output3, "Linking target", 3); // dynamic1, dynamic2, binary1

  validate_binary2(arena);
  
  {
    auto result = run_system_command(arena, make_file_path(arena, ".cbuild", "build", "out", "binary1.exe")->value);
    require(result.status);
    require(strstr(result.output.value, "lib2_updated,dyn1_updated,dyn2,bin1"));
  }
}

static void test_modify_file (Memory_Arena *arena, const File_Path *file_path) {
  auto file      = open_file(file_path, Open_File_Flags::Request_Write_Access); require(file.status);
  auto mapping   = map_file_into_memory(&file); require(mapping.status);

  auto file_content = reserve_memory(arena, mapping->size + 2);
  copy_memory(file_content, mapping->memory, mapping->size);
  file_content[mapping->size] = ' ';
  file_content[mapping->size + 1] = '\0';

  unmap_file(&mapping);
  
  reset_file_cursor(&file);

  require(write_buffer_to_file(&file, file_content, strlen(file_content)));
  close_file(&file);
}

static void build_project_tests (Memory_Arena *arena) {
  auto output = build_testbed(arena);
  count_lines_starting_with(output, "Building file", 9);
  count_lines_starting_with(output, "Linking target", 9);

  validate_binary1(arena);
  validate_binary2(arena);

  auto build_file_path = make_file_path(arena, "project", "build.cpp");
  test_modify_file(arena, &build_file_path);
  
  auto output2 = build_testbed(arena);
  count_lines_starting_with(output2, "Building file", 9);
  count_lines_starting_with(output2, "Linking target", 9);

  validate_binary1(arena);
  validate_binary2(arena);

  for (int idx = 0; idx < 5; idx++) {
    auto output3 = build_testbed(arena);
    count_lines_starting_with(output3, "Building file", 0);
    count_lines_starting_with(output3, "Linking target", 0);

    validate_binary1(arena);
    validate_binary2(arena);
  }
}

static Test_Case build_command_tests [] {
  define_test_case_ex(build_init_project_tests, setup_workspace, cleanup_workspace),
  define_test_case_ex(build_testbed_tests,      setup_testbed,   cleanup_workspace),
  define_test_case_ex(build_registry_tests,     setup_testbed,   cleanup_workspace),
  define_test_case_ex(build_changes_tests,      setup_testbed,   cleanup_workspace),
  define_test_case_ex(build_errors_tests,       setup_testbed,   cleanup_workspace),
  define_test_case_ex(build_project_tests,      setup_testbed,   cleanup_workspace),
};

define_test_suite(build_command, build_command_tests)
