
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

static void build_testbed (Memory_Arena *arena) {
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

      require(check_directory_exists(&cbuild_output_folder).value);

      delete_directory(cbuild_output_folder);
    }
  }
}

static void build_registry_on_test (Memory_Arena *arena) {
  auto executable_path = make_file_path(arena, workspace, ".cbuild", "build", "out", "main.exe");
    
  auto command = format_string(arena, "% build", binary_path);

  {
    auto result  = run_system_command(arena, command);

    if (!result.status) print(arena, "%\n", result.output);
    require(result.status);
    require(strstr(result.output.value, "Building file"));
  }

  {
    require(check_file_exists(&executable_path).value);

    auto [status, output] = run_system_command(arena, executable_path.value.value);
    require(status);
    require(strstr(output.value, "Thank you for trying cbuild!"));
  }

  {
    auto result = run_system_command(arena, command);
    require(result.status);
    require(strstr(result.output.value, "Building file") == nullptr);
  }
}

static void build_registry_off_test (Memory_Arena *arena) {
  auto executable_path = make_file_path(arena, workspace, ".cbuild", "build", "out", "main.exe");
    
  auto command = format_string(arena, "% build cache=off", binary_path);

  {
    auto result  = run_system_command(arena, command);

    if (!result.status) print(arena, "%\n", result.output);
    require(result.status);
    require(strstr(result.output.value, "Building file"));
  }

  {
    require(check_file_exists(&executable_path).value);

    auto [status, output] = run_system_command(arena, executable_path.value.value);
    require(status);
    require(strstr(output.value, "Thank you for trying cbuild!"));
  }

  {
    auto result = run_system_command(arena, command);
    require(result.status);
    require(strstr(result.output.value, "Building file"));
  }
}

static void build_replaced_file (Memory_Arena *arena) {
  auto produced_binary_path = make_file_path(arena, ".cbuild", "build", "out", "main.exe");

  {
    auto build_command = format_string(arena, "% build", binary_path);
    auto [status, output] = run_system_command(arena, build_command);
    if (!status) print(arena, "%\n", output);
    require(status == Status_Code::Success);

    require(check_file_exists(&produced_binary_path));

    auto run_command = format_string(arena, "%", produced_binary_path);
    auto result = run_system_command(arena, run_command);
    if (!result.status) print(arena, "%\n", result.output);
    require(result.status == Status_Code::Success);
    require(strstr(result.output, "Thank you for trying cbuild!"));
  }

  auto new_lib_impl = R"lib(
#include "library.hpp"

const char* control_phrase () {
  return "testbed";
}
)lib";

  auto old_library_path = make_file_path(arena, "code", "library", "library.cpp");
  auto new_library_path = make_file_path(arena, "code", "library", "new_library.cpp");

  delete_file(old_library_path);

  auto new_lib = open_file(&new_library_path, Open_File_Flags::Request_Write_Access | Open_File_Flags::Create_File_If_Not_Exists);
  require(write_buffer_to_file(&new_lib, new_lib_impl, strlen(new_lib_impl)));
  close_file(&new_lib);

  {
    auto build_command = format_string(arena, "% build", binary_path);
    auto [status, output] = run_system_command(arena, build_command);
    if (!status) print(arena, "%\n", output);
    require(status == Status_Code::Success);
    
    require(check_file_exists(&produced_binary_path));
    require(strstr(output, "Building file"));

    auto run_command = format_string(arena, "%", produced_binary_path);
    auto result = run_system_command(arena, run_command);
    if (!result.status) print(arena, "%\n", result.output);
    require(result.status == Status_Code::Success);
    require(strstr(result.output, "Thank you for trying testbed!"));
  }

}

static Test_Case build_command_tests [] {
  define_test_case_ex(build_init_project, setup_workspace, cleanup_workspace),
  define_test_case_ex(build_testbed, setup_testbed, cleanup_workspace),
  define_test_case_ex(build_registry_on_test, setup_testbed, cleanup_workspace),
  define_test_case_ex(build_registry_off_test, setup_testbed, cleanup_workspace),
  define_test_case_ex(build_replaced_file, setup_testbed, cleanup_workspace),
};

define_test_suite(build_command, build_command_tests)
