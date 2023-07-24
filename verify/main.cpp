
#include "code/base.hpp"
#include "code/platform.hpp"
#include "code/runtime.hpp"

#include "code/test_suite.hpp"

File_Path working_directory; // Path to the root directory where the 'verify' program has been called
File_Path workspace;         // Path to the workspace folder where all intermediary files and folders are created
File_Path binary_path;       // Executable under test

int main (int argc, char **argv) {
  enum { buffer_size = megabytes(1) };
  auto buffer = new char[buffer_size];
  defer { delete[] buffer; };

  auto suite_runner = Test_Suite_Runner { .arena = Memory_Arena { buffer, buffer_size } };

  working_directory = *get_working_directory_path(&suite_runner.arena);
  binary_path       = get_absolute_path(&suite_runner.arena, argv[1]);
  workspace         = make_file_path(&suite_runner.arena, working_directory, "out", "verification");

  print(&suite_runner.arena, "Verifying: %\n", binary_path);

#define run_suite(SUITE_NAME) \
    void tokenpaste(SUITE_NAME, _test_suite)(const Test_Suite_Runner &); \
    tokenpaste(SUITE_NAME, _test_suite)(suite_runner)

  run_suite(init_command);
}
