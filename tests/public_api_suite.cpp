
#include "anyfin/base.hpp"

#include "code/cbuild_api.hpp"
#include "code/toolchain.hpp"

#define CBUILD_API_VERSION 1
#include "code/cbuild_api_template.hpp"

#include "test_suite.hpp"

extern File_Path working_directory; // Path to the root directory where the 'verify' program has been called
extern File_Path workspace;         // Path to the workspace folder where all intermediary files and folders are created

static bool ensure_list_content (const Iterable<String> auto &list, String_View value, Convertible_To<String_View> auto&&... more_values) {
  String_View values [] { value, static_cast<String_View>(more_values)... };

  if (iterator::count(list) != Fin::Base::array_count_elements(values)) return false;

  for (usize idx = 0; auto &elem: list) {
    if (!compare_strings(elem, values[idx++])) return false;
  }

  return true;
}

static void setup_workspace (Memory_Arena &arena) {
  auto testsite_path = make_file_path(arena, working_directory, "tests", "testsite");

  require(delete_directory(workspace));
  require(copy_directory(testsite_path, workspace));

  require(set_working_directory(workspace));
}

static void cleanup_workspace (Memory_Arena &arena) {
  require(set_working_directory(working_directory));
  require(delete_directory(workspace));
}

static Project create_project (Memory_Arena &arena) {
  return Project(arena, "test_project", workspace, make_file_path(arena, workspace, ".cbuild"));
}

// TODO: Need to rewrite the arguments API for the next release
// static void arguments_test (Memory_Arena &arena) {
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

static void set_toolchain_test (Memory_Arena &arena) {
  auto project = create_project(arena);

  require(project.toolchain.type == Toolchain_Type_MSVC_X86);
  require(!project.toolchain.c_compiler_path);
  require(!project.toolchain.cpp_compiler_path);
  require(!project.toolchain.linker_path);
  require(!project.toolchain.archiver_path);

  set_toolchain(&project, Toolchain_Type_MSVC_X64);

  require(project.toolchain.type == Toolchain_Type_MSVC_X64);
  require(project.toolchain.c_compiler_path);
  require(project.toolchain.cpp_compiler_path);
  require(project.toolchain.linker_path);
  require(project.toolchain.archiver_path);

  require_crash(set_toolchain(&project, Toolchain_Type_GCC));
}

static void disable_registry_test (Memory_Arena &arena) {
  auto project = create_project(arena);
  
  disable_registry(&project);

  require(project.registry_disabled);
}

static int test_action (const Arguments *args) {
  return 0;
}

static void register_action_test (Memory_Arena &arena) {
  auto project = create_project(arena);

  require(project.user_defined_commands.count == 0);

  register_action(&project, "test", test_action);

  require(project.user_defined_commands.count == 1);

  auto &command = project.user_defined_commands.first->value;
  require(compare_strings(command.name, "test"));
  require(command.proc == test_action);
}

static void output_location_test (Memory_Arena &arena) {
  auto project = create_project(arena);

  // For now to simplify setup, .cbuild/build is the default 
  require(ends_with(project.build_location_path, "build"));

  String_View path = "somewhere/somehow/something";
  set_output_location(&project, path);

  require(project.build_location_path == make_file_path(arena, workspace, ".cbuild", "build", path));
}

static void add_static_library_test (Memory_Arena &arena) {
  auto project = create_project(arena);

  require(project.targets.count == 0);

  auto target = add_static_library(&project, "library");
  require(project.targets.count == 1);
  require(target->type == Target::Type::Static_Library);

  require_crash(add_static_library(&project, "library")); // can't have multiple targets with the same name
  require_crash(add_shared_library(&project, "library")); // the same but with different type
}

static void add_shared_library_test (Memory_Arena &arena) {
  auto project = create_project(arena);

  require(project.targets.count == 0);

  auto target = add_shared_library(&project, "library");
  require(project.targets.count == 1);
  require(target->type == Target::Type::Shared_Library);
}

static void add_executable_test (Memory_Arena &arena) {
  auto project = create_project(arena);

  require(project.targets.count == 0);

  auto target = add_executable(&project, "library");
  require(project.targets.count == 1);
  require(target->type == Target::Type::Executable);
}

static void add_compiler_option_test (Memory_Arena &arena) {
  auto project = create_project(arena);

  auto target = add_static_library(&project, "library");
  add_compiler_option(target, "/nologo");
  add_compiler_option(target, "  ");
  add_compiler_option(target, "  /W4274");
  add_compiler_option(target, "/foo   /bar /baz  ");
  require_crash(add_compiler_option(target, ""));

  require(target->compiler.count == 5);
  require(ensure_list_content(target->compiler, "/nologo", "/W4274", "/foo", "/bar", "/baz"));
}

static void remove_compiler_option_test (Memory_Arena &arena) {
  auto project = create_project(arena);

  auto target = add_static_library(&project, "test_lib");
  add_compiler_options(target, "--test", "--test2", "--multiple --options --passed", "--final_one");

  require(target->compiler.count == 6);
  require(ensure_list_content(target->compiler, "--test", "--test2", "--multiple", "--options", "--passed", "--final_one"));

  remove_compiler_option(target, "--test2");
  require(ensure_list_content(target->compiler, "--test", "--multiple", "--options", "--passed", "--final_one"));

  remove_compiler_option(target, "--options");
  require(ensure_list_content(target->compiler, "--test", "--multiple", "--passed", "--final_one"));

  remove_compiler_option(target, "--multiple");
  require(ensure_list_content(target->compiler, "--test", "--passed", "--final_one"));

  remove_compiler_option(target, "--non_existing");
  require(ensure_list_content(target->compiler, "--test", "--passed", "--final_one"));

  remove_compiler_option(target, "--test --final_one");
  require(ensure_list_content(target->compiler, "--passed"));
}

static void add_archiver_option_test (Memory_Arena &arena) {
  auto project = create_project(arena);

  auto target = add_static_library(&project, "library");
  add_archiver_option(target, "/nologo");
  add_archiver_option(target, "/W4274");
  add_archiver_option(target, "/foo /bar /baz");

  require(target->archiver.count == 5);
  require(ensure_list_content(target->archiver, "/nologo", "/W4274", "/foo", "/bar", "/baz"));
}

static void remove_archiver_option_test (Memory_Arena &arena) {
  auto project = create_project(arena);

  auto target = add_static_library(&project, "test_lib");
  add_archiver_options(target, "--test", "--test2", "--multiple --options --passed", "--final_one");

  require(target->archiver.count == 6);
  require(ensure_list_content(target->archiver, "--test", "--test2", "--multiple", "--options", "--passed", "--final_one"));

  remove_archiver_option(target, "--test2");
  require(ensure_list_content(target->archiver, "--test", "--multiple", "--options", "--passed", "--final_one"));

  remove_archiver_option(target, "--options");
  require(ensure_list_content(target->archiver, "--test", "--multiple", "--passed", "--final_one"));

  remove_archiver_option(target, "--multiple");
  require(ensure_list_content(target->archiver, "--test", "--passed", "--final_one"));

  remove_archiver_option(target, "--non_existing");
  require(ensure_list_content(target->archiver, "--test", "--passed", "--final_one"));

  remove_archiver_option(target, "--test --final_one");
  require(ensure_list_content(target->archiver, "--passed"));
}

static void add_linker_option_test (Memory_Arena &arena) {
  auto project = create_project(arena);

  auto target = add_static_library(&project, "library");
  add_linker_option(target, "/nologo");
  add_linker_option(target, "/O4");
  add_linker_option(target, "/W4274");

  require(target->linker.count == 3);
}

static void remove_linker_option_test (Memory_Arena &arena) {
  auto project = create_project(arena);

  auto target = add_static_library(&project, "test_lib");
  add_linker_options(target, "--test", "--test2", "--multiple --options --passed", "--final_one");

  require(target->linker.count == 6);
  require(ensure_list_content(target->linker, "--test", "--test2", "--multiple", "--options", "--passed", "--final_one"));

  remove_linker_option(target, "--test2");
  require(ensure_list_content(target->linker, "--test", "--multiple", "--options", "--passed", "--final_one"));

  remove_linker_option(target, "--options");
  require(ensure_list_content(target->linker, "--test", "--multiple", "--passed", "--final_one"));

  remove_linker_option(target, "--multiple");
  require(ensure_list_content(target->linker, "--test", "--passed", "--final_one"));

  remove_linker_option(target, "--non_existing");
  require(ensure_list_content(target->linker, "--test", "--passed", "--final_one"));

  remove_linker_option(target, "--test --final_one");
  require(ensure_list_content(target->linker, "--passed"));
}

static void add_source_file_test (Memory_Arena &arena) {
  auto project = create_project(arena);

  auto target = add_static_library(&project, "library");
  add_source_file(target, "code/library1/library1.cpp");
  add_source_file(target, "code/library2/library2.cpp");

  require(target->files.count == 2);
  require(project.total_files_count == 2);

  require_crash(add_source_file(target, "non_existing.cpp"));
}

static void add_all_sources_from_directory_test (Memory_Arena &arena) {
  auto project = create_project(arena);

  auto target = add_static_library(&project, "library");
  add_source_file(target, "code/library1/library1.cpp");
  add_all_sources_from_directory(target, "code", "cpp", true);

  require(target->files.count == 9); // should dedup files
  require(project.total_files_count == 9);

  add_all_sources_from_directory(target, "code", "c", true);
  require(target->files.count == 10); 
  require(project.total_files_count == 10);

  require_crash(add_all_sources_from_directory(target, "non_existing_dir", "c", false));
  require_crash(add_all_sources_from_directory(target, "dir/file.cpp", "cpp", false));
  require_crash(add_all_sources_from_directory(target, nullptr, "cpp", false));
  require_crash(add_all_sources_from_directory(target, "", "cpp", false));
  require_crash(add_all_sources_from_directory(target, "dir/file.cpp", nullptr, false));
  require_crash(add_all_sources_from_directory(target, "dir/file.cpp", "", false));
}

static void exclude_source_file_test (Memory_Arena &arena) {
  auto project = create_project(arena);

  auto target = add_static_library(&project, "library");
  add_source_file(target, "code/library1/library1.cpp");
  add_source_file(target, "code/library2/library2.cpp");

  require(target->files.count == 2);
  require(project.total_files_count == 2);

  exclude_source_file(target, "code/library1/library1.cpp");
  require(target->files.count == 1);
  require(project.total_files_count == 1);

  require(ensure_list_content(target->files, *get_absolute_path(arena, "code/library2/library2.cpp")));

  for (int idx = 0; idx < 5; idx++) exclude_source_file(target, "code/library2/library2.cpp");
  require(target->files.count == 0);
  require(project.total_files_count == 0);
}

static void link_with_target_test (Memory_Arena &arena) {
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

static void link_with_library_test (Memory_Arena &arena) {
  auto project = create_project(arena);

  auto target = add_static_library(&project, "lib");
  link_with_library(target, "foo.lib");

  require(target->link_libraries.count == 1);

  require_crash(link_with_library(target, nullptr));
  require_crash(link_with_library(target, ""));
}

static void add_include_search_path_test (Memory_Arena &arena) {
  auto project = create_project(arena);

  auto target = add_static_library(&project, "lib");
  add_include_search_path(target, "C:\\Users\\SomeUser\\libs");
  add_include_search_path(target, "includes");

  require(target->include_paths.count == 2);
}

static void get_target_name_test (Memory_Arena &arena) {
  auto project = create_project(arena);

  auto target = add_static_library(&project, "lib");
  require(has_substring(get_target_name(target), "lib"));
}

static void cpp_wrappers_test (Memory_Arena &arena) {
  auto project = create_project(arena);

  add_global_compiler_options(&project, "/nologo", "/std:c++20", "-O3");
  add_global_archiver_options(&project, "/nologo");
  add_global_linker_options(&project, "/nologo", "/debug:full", "/incremental:no");

  require(ensure_list_content(project.compiler, "/nologo", "/std:c++20", "-O3"));
  require(ensure_list_content(project.archiver, "/nologo"));
  require(ensure_list_content(project.linker, "/nologo", "/debug:full", "/incremental:no"));

  add_global_include_search_path(&project, "./includes");
  require(project.include_paths.count == 1);

  auto target = add_static_library(&project, "library");
  add_compiler_options(target, "/nologo", "/O4 /W4274", "/verbose", "/foo /bar /bar");
  require(target->compiler.count == 7);

  remove_compiler_options(target, "/nologo /bar", "/verbose");
  require(ensure_list_content(target->compiler, "/O4", "/W4274", "/foo", "/bar"));

  add_linker_options(target, "/nologo", "/O4", "/W4274 /something");
  require(target->linker.count == 4);

  remove_linker_options(target, "/nologo /something", "/O4 /W4274");
  require(target->linker.count == 0);

  add_archiver_options(target, "/test", "/foo");
  remove_archiver_option(target, "/foo");
  require(target->archiver.count == 1);

  add_source_files(target, "code/library1/library1.cpp", "code/library2/library2.cpp", "code/library3/library3.cpp");
  require(target->files.count == 3);
  require(project.total_files_count == 3);

  exclude_source_files(target, "code/library1/library1.cpp", "code/library3/library3.cpp");
  require(target->files.count == 1);
  require(project.total_files_count == 1);
  require(ensure_list_content(target->files, *get_absolute_path(arena, "code/library2/library2.cpp")));

  auto lib2 = add_static_library(&project, "lib2");
  link_with(lib2, "something.lib", target, "foo.lib");
  require(lib2->depends_on.count == 1);
  require(lib2->link_libraries.count == 2);
}

static void add_global_compiler_option_test (Memory_Arena &arena) {
  auto project = create_project(arena);

  require(project.compiler.count == 0);

  add_global_compiler_option(&project, "/nologo");
  add_global_compiler_option(&project, "/std:c++20");
  
  require(project.compiler.count == 2);
  require(ensure_list_content(project.compiler, "/nologo", "/std:c++20"));
}

static void add_global_archiver_option_test (Memory_Arena &arena) {
  auto project = create_project(arena);

  require(project.archiver.count == 0);

  add_global_archiver_option(&project, "/nologo");
  add_global_archiver_option(&project, "/std:c++20");
  
  require(project.archiver.count == 2);
  require(ensure_list_content(project.archiver, "/nologo", "/std:c++20"));
}

static void add_global_linker_option_test (Memory_Arena &arena) {
  auto project = create_project(arena);

  require(project.linker.count == 0);

  add_global_linker_option(&project, "/nologo");
  add_global_linker_option(&project, "/std:c++20");
  
  require(project.linker.count == 2);
  require(ensure_list_content(project.linker, "/nologo", "/std:c++20"));
}

static void add_global_include_search_path_test (Memory_Arena &arena) {
  auto project = create_project(arena);

  require(project.include_paths.count == 0);

  add_global_include_search_path(&project, "./includes");
  add_global_include_search_path(&project, "./libs");

  require(project.include_paths.count == 2);
  require(ensure_list_content(project.include_paths,
                              *get_absolute_path(arena, "./includes"),
                              *get_absolute_path(arena, "./libs")));
}

static Test_Case public_api_tests [] {
  define_test_case(set_toolchain_test),
  define_test_case(disable_registry_test),
  define_test_case(register_action_test),
  define_test_case(output_location_test),
  define_test_case(add_static_library_test),
  define_test_case(add_shared_library_test),
  define_test_case(add_executable_test),
  define_test_case(add_compiler_option_test),
  define_test_case(remove_compiler_option_test),
  define_test_case(add_archiver_option_test),
  define_test_case(remove_archiver_option_test),
  define_test_case(add_linker_option_test),
  define_test_case(remove_linker_option_test),
  define_test_case(remove_archiver_option_test),
  define_test_case_ex(add_source_file_test, setup_workspace, cleanup_workspace),
  define_test_case_ex(add_all_sources_from_directory_test, setup_workspace, cleanup_workspace),
  define_test_case_ex(exclude_source_file_test, setup_workspace, cleanup_workspace),
  define_test_case(link_with_target_test),
  define_test_case(link_with_library_test),
  define_test_case_ex(add_include_search_path_test, setup_workspace, cleanup_workspace),
  define_test_case(get_target_name_test),
  define_test_case_ex(cpp_wrappers_test, setup_workspace, cleanup_workspace),
  define_test_case(add_global_compiler_option_test),
  define_test_case(add_global_archiver_option_test),
  define_test_case(add_global_linker_option_test),
  define_test_case(add_global_include_search_path_test)
};

define_test_suite(public_api, public_api_tests)
