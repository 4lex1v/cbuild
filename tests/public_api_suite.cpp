
#include "code/base.hpp"
#include "code/platform.hpp"
#include "code/runtime.hpp"
#include "code/cbuild_api.hpp"
#include "code/toolchain.hpp"

#define CBUILD_API_VERSION 1
#include "code/cbuild_api_template"

#include "test_suite.hpp"

extern File_Path working_directory; // Path to the root directory where the 'verify' program has been called
extern File_Path workspace;         // Path to the workspace folder where all intermediary files and folders are created

extern Config_Crash_Handler crash_handler_hook;

static void test_configuration_failure (u32 exit_code) {
  require(false);
}

static void setup_workspace (Memory_Arena *arena) {
  crash_handler_hook = test_configuration_failure;

  if (check_directory_exists(&workspace)) delete_directory(workspace);
  create_directory(&workspace);
  set_working_directory(workspace);
  auto testbed_path = make_file_path(arena, working_directory, "tests", "testbed");
  copy_directory_content(arena, testbed_path, workspace);
}

static void cleanup_workspace (Memory_Arena *arena) {
  set_working_directory(working_directory);
  delete_directory(workspace);
}

static Project create_project (Memory_Arena *arena) {
  return {
    .arena     = Memory_Arena { reserve_memory_unsafe(arena, kilobytes(256)), kilobytes(256) },
    .toolchain = {}
  };
}

#define require_crash(EXPR)                       \
  do {                                            \
    bool exception_captured = false;              \
    try { (EXPR); }                               \
    catch (const Test_Failed_Exception &error) {  \
      exception_captured = true;                  \
    }                                             \
    require(exception_captured);                  \
  } while(0)


// TODO: Need to rewrite the arguments API for the next release
// static void arguments_test (Memory_Arena *arena) {
//   using Arg_Type = Arguments::Argument::Type;

//   Arguments args {};
//   add(arena, &args.args, Argument { .type = Arg_Type::Flag, .key = "foo" });
//   add(arena, &args.args, Argument { .type = Arg_Type::Flag, .key = "bar" });
//   add(arena, &args.args, Argument { .type = Arg_Type::Flag, .key = "bazooka" });
//   add(arena, &args.args, Argument { .type = Arg_Type::Key_Value, .key = "builders", .value = "10" });
//   add(arena, &args.args, Argument { .type = Arg_Type::Key_Value, .key = "builders", .value = "15" });
//   add(arena, &args.args, Argument { .type = Arg_Type::Key_Value, .key = "testing", .value = "20" });

//   require(get_argument_or_default(&args, "non_existing", nullptr) == nullptr);
//   require(!strcmp(get_argument_or_default(&args, "non_existing", "foo"), "foo"));

//   require(get_argument_or_default(&args, "builders", nullptr) != nullptr);
//   require(!strcmp(get_argument_or_default(&args, "builders", "20"), "10"));

//   require(!strcmp(get_argument_or_default(&args, "foo", nullptr), "foo"));
//   require(!strcmp(get_argument_or_default(&args, "bar", "baz"), "bar"));
//   require(get_argument_or_default(&args, "baz", "snoopy") == nullptr);
// }

static void set_toolchain_test (Memory_Arena *arena) {
  auto project = create_project (arena);

  set_toolchain(&project, Toolchain_Type_MSVC_X64);

  require(project.toolchain.type == Toolchain_Type_MSVC_X64);
  require(project.toolchain.c_compiler_path);
  require(project.toolchain.cpp_compiler_path);
  require(project.toolchain.linker_path);
  require(project.toolchain.archiver_path);

  require_crash(set_toolchain(&project, Toolchain_Type_GCC));
}

static void disable_registry_test (Memory_Arena *arena) {
  auto project = create_project(arena);
  
  disable_registry(&project);

  require(project.registry_disabled);
}

static int test_action (const Arguments *args) {
  return 0;
}

static void register_action_test (Memory_Arena *arena) {
  auto project = create_project(arena);

  require(project.user_defined_commands.count == 0);

  register_action(&project, "test", test_action);

  require(project.user_defined_commands.count == 1);

  auto command = project.user_defined_commands.first->value;
  require(compare_strings(command.name, "test"));
  require(command.proc == test_action);
}

static void output_location_test (Memory_Arena *arena) {
  auto project = create_project(arena);

  require(project.output_location.length == 0);

  String path = "somewhere/somehow/something";
  set_output_location(&project, path);

  require(compare_strings(project.output_location, path));
}

static void add_static_library_test (Memory_Arena *arena) {
  auto project = create_project(arena);

  require(project.targets.count == 0);

  auto target = add_static_library(&project, "library");
  require(project.targets.count == 1);
  require(target->type == Target::Type::Static_Library);

  require_crash(add_static_library(&project, "library")); // can't have multiple targets with the same name
  require_crash(add_shared_library(&project, "library")); // the same but with different type
}

static void add_shared_library_test (Memory_Arena *arena) {
  auto project = create_project(arena);

  require(project.targets.count == 0);

  auto target = add_shared_library(&project, "library");
  require(project.targets.count == 1);
  require(target->type == Target::Type::Shared_Library);
}

static void add_executable_test (Memory_Arena *arena) {
  auto project = create_project(arena);

  require(project.targets.count == 0);

  auto target = add_executable(&project, "library");
  require(project.targets.count == 1);
  require(target->type == Target::Type::Executable);
}

static void add_compiler_option_test (Memory_Arena *arena) {
  auto project = create_project(arena);

  auto target = add_static_library(&project, "library");
  add_compiler_option(target, "/nologo");
  add_compiler_option(target, "/O4");
  add_compiler_option(target, "/W4274");

  require(target->options.compiler.count == 3);
}

static void add_linker_option_test (Memory_Arena *arena) {
  auto project = create_project(arena);

  auto target = add_static_library(&project, "library");
  add_linker_option(target, "/nologo");
  add_linker_option(target, "/O4");
  add_linker_option(target, "/W4274");

  require(target->options.linker.count == 3);
}

static void add_source_file_test (Memory_Arena *arena) {
  auto project = create_project(arena);

  auto target = add_static_library(&project, "library");
  add_source_file(target, "code/library1/library1.cpp");
  add_source_file(target, "code/library2/library2.cpp");

  require(target->files.count == 2);
  require(project.total_files_count == 2);

  require_crash(add_source_file(target, "non_existing.cpp"));
}

static void add_all_sources_from_directory_test (Memory_Arena *arena) {
  auto project = create_project(arena);

  auto target = add_static_library(&project, "library");
  add_source_file(target, "code/library1/library1.cpp");
  add_all_sources_from_directory(target, "code", "cpp", true);

  require(target->files.count == 9); // should dedup files
  require(project.total_files_count == 9);

  add_all_sources_from_directory(target, "code", "c", true);
  require(target->files.count == 9); 
  require(project.total_files_count == 9);

  require_crash(add_all_sources_from_directory(target, "non_existing_dir", "c", false));
  require_crash(add_all_sources_from_directory(target, "dir/file.cpp", "cpp", false));
  require_crash(add_all_sources_from_directory(target, nullptr, "cpp", false));
  require_crash(add_all_sources_from_directory(target, "", "cpp", false));
  require_crash(add_all_sources_from_directory(target, "dir/file.cpp", nullptr, false));
  require_crash(add_all_sources_from_directory(target, "dir/file.cpp", "", false));
}

static void link_with_target_test (Memory_Arena *arena) {
  auto project = create_project(arena);

  auto target1 = add_static_library(&project, "lib");
  auto target2 = add_static_library(&project, "lib2");
  auto target3 = add_static_library(&project, "lib3");

  link_with_target(target2, target1);
  require(target2->depends_on.count == 1);
  require(target1->required_by.count == 1);

  link_with_target(target3, target2);
  link_with_target(target3, target1);
  require(target3->depends_on.count == 2);
  require(target1->required_by.count == 2);

  require_crash(link_with_target(target1, target1));
  require_crash(link_with_target(target3, nullptr));
}

static void link_with_library_test (Memory_Arena *arena) {
  auto project = create_project(arena);

  auto target = add_static_library(&project, "lib");
  link_with_library(target, "foo.lib");

  require(target->link_libraries.count == 1);

  require_crash(link_with_library(target, nullptr));
  require_crash(link_with_library(target, ""));
}

static void add_include_search_path_test (Memory_Arena *arena) {
  auto project = create_project(arena);

  auto target = add_static_library(&project, "lib");
  add_include_search_path(target, "C:\\Users\\SomeUser\\libs");
  add_include_search_path(target, "includes");

  require(target->include_paths.count == 2);
}

static void get_target_name_test (Memory_Arena *arena) {
  auto project = create_project(arena);

  auto target = add_static_library(&project, "lib");
  require(strcmp(get_target_name(target), "lib") == 0);
}

static void cpp_wrappers_test (Memory_Arena *arena) {
  auto project = create_project(arena);

  auto target = add_static_library(&project, "library");
  add_compiler_options(target, "/nologo", "/O4", "/W4274");
  require(target->options.compiler.count == 3);

  add_linker_options(target, "/nologo", "/O4", "/W4274", "/something");
  require(target->options.linker.count == 4);

  // TODO: Missing impl, should fix with the next API release
  //add_source_files(target, "code/library1/library1.cpp", "code/library2/library2.cpp");

  auto lib2 = add_static_library(&project, "lib2");
  link_with(lib2, "something.lib", target, "foo.lib");
  require(lib2->depends_on.count == 1);
  require(lib2->link_libraries.count == 2);
}

static Test_Case public_api_tests [] {
  //define_test_case(arguments_test),
  //define_test_case(),
  define_test_case_ex(set_toolchain_test, setup_workspace, cleanup_workspace),
  define_test_case(disable_registry_test),
  define_test_case(register_action_test),
  define_test_case(output_location_test),
  define_test_case(add_static_library_test),
  define_test_case(add_shared_library_test),
  define_test_case(add_executable_test),
  define_test_case(add_compiler_option_test),
  define_test_case(add_linker_option_test),
  define_test_case_ex(add_source_file_test, setup_workspace, cleanup_workspace),
  define_test_case_ex(add_all_sources_from_directory_test, setup_workspace, cleanup_workspace),
  define_test_case(link_with_target_test),
  define_test_case(link_with_library_test),
  define_test_case_ex(add_include_search_path_test, setup_workspace, cleanup_workspace),
  define_test_case(get_target_name_test),
  define_test_case_ex(cpp_wrappers_test, setup_workspace, cleanup_workspace),
};

define_test_suite(public_api, public_api_tests)
