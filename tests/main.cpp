
#define FIN_EMBED_STATE

#include "anyfin/startup.hpp"
#include "anyfin/concurrent.hpp"

#include "test_suite.hpp"

File_Path working_directory; // Path to the root directory where the 'verify' program has been called
File_Path workspace;         // Path to the workspace folder where all intermediary files and folders are created
File_Path binary_path;       // Executable under test

/*
  These variables are set by the project loader and used used by the cbuild api implementation "code/cbuild_api.cpp".
 */
File_Path object_folder_path;
File_Path out_folder_path;

static void test_configuration_failure (u32 exit_code) {
  require(false);
}

static int find_arg (const char * arg, const int argc, const char * const * const argv) {
  for (auto idx = 0; idx < argc; idx++) {
    if (!_stricmp(arg, argv[idx])) return idx;
  }

  return -1;
}

static String find_arg_value (const char * arg, const int argc, const char * const * const argv) {
  auto idx = find_arg(arg, argc, argv);
  if ((idx != -1) && ((idx + 1) < argc)) return String(argv[idx + 1]);

  return {};
}

namespace Fin {

void trap (const char *msg, usize length, Callsite callsite) {
  
}

}

static Spin_Lock log_lock;
void log (Fin::String message) {
  log_lock.lock();
  write_to_stdout(message);
  log_lock.unlock();
}

int main (int argc, char **argv) {
  //set_crash_handler(test_configuration_failure);

  Memory_Arena arena { reserve_virtual_memory(megabytes(8)) };

  auto args = get_startup_args(arena);

  auto suite_runner = Test_Suite_Runner {
    .arena        = make_sub_arena(arena, megabytes(6)),
    .suite_filter = get_value(args, "suite").or_default(),
    .case_filter  = get_value(args, "case").or_default(),
  };

  auto bin_path_arg = get_value(args, "bin").or_default();
  if (!bin_path_arg) {
    write_to_stdout("ERROR: bin <path> is a required argument that should point to the cbuild binary which should be tested.\n");
    return 1;
  }

  working_directory = get_working_directory(suite_runner.arena).value;
  binary_path       = get_absolute_path(suite_runner.arena, make_file_path(suite_runner.arena, bin_path_arg)).value;
  workspace         = make_file_path(suite_runner.arena, working_directory, "tests", "verification");

  write_to_stdout(format_string(arena, "Verifying: %\n", binary_path));

#define run_suite(SUITE_NAME)                                     \
  void tokenpaste(SUITE_NAME, _test_suite)(Test_Suite_Runner &);  \
  tokenpaste(SUITE_NAME, _test_suite)(suite_runner)

  run_suite(public_api); 
  run_suite(init_command);
  run_suite(build_command);
  run_suite(clean_command);
  //run_suite(subprojects); 

  return suite_runner.report();
}
