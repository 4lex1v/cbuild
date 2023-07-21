
#include "platform.hpp"
#include "driver.hpp"
#include "test_suite.hpp"
#include "command_line.hpp"

File_Path working_directory_path;
File_Path cache_directory_path;

Platform_Info platform;
CLI_Flags global_flags;

static int find_arg(const char * arg, const int argc, const char * const * const argv) {
    for (int i = 0; i < argc; ++i) {
        if (!_stricmp(arg, argv[i])) {
            return i;
        }
    }

    return -1;
}

String find_arg_value(const char * arg, const int argc, const char * const * const argv) {
    const int i = find_arg(arg, argc, argv);
    if ((i != -1) && ((i + 1) < argc)) {
        return String(argv[i + 1]);
    }

    return {};
}

int main (int argc, char **argv) {
  enum { buffer_size = 2046 };
  auto buffer = new char[buffer_size];
  defer { delete[] buffer; };

  const Test_Suite_Runner suite_runner {
    .arena        = Memory_Arena { buffer, buffer_size },
    .suite_filter = find_arg_value("--suite", argc, argv),
    .case_filter  = find_arg_value("--case", argc, argv),
  };

#define run_suite(SUITE_NAME) \
  void tokenpaste(SUITE_NAME, _test_suite)(const Test_Suite_Runner &); \
  tokenpaste(SUITE_NAME, _test_suite)(suite_runner)

  run_suite(command_line);

  return 0;
}
