
#include <string_view>

#include "cbuild.h"
#include "cbuild_experimental.h"

#include <cstdio>
#include <cstring>

static void setup_toolchain (Project *project, const std::string_view toolchain) {
  if      (toolchain == "msvc_x86") set_toolchain(project, Toolchain_Type_MSVC_X86);
  else if (toolchain == "msvc_x64") set_toolchain(project, Toolchain_Type_MSVC_X64);
  else if (toolchain == "llvm")     set_toolchain(project, Toolchain_Type_LLVM);
  else if (toolchain == "llvm_cl")  set_toolchain(project, Toolchain_Type_LLVM_CL);
  else {
    printf("Unrecognized toolchain value: '%s'", toolchain.data());
    exit(EXIT_FAILURE);
  }
}

static int test_command (const Arguments *) noexcept {
  printf("Calling registered command\n");
  fflush(stdout); // This is needed for tests to properly capture the stdout output
  return 0;
}

extern "C" bool setup_project (const Arguments *args, Project *project) {
  auto toolchain = get_argument_or_default(args, "toolchain", "msvc_x64");
  auto config    = get_argument_or_default(args, "config",    "debug");
  auto cache     = get_argument_or_default(args, "cache",     "on");

  register_action(project, "test_cmd", test_command);

  // NOTE: Test checks these printf to ensure that values are passed correctly. DON'T REMOVE
  fprintf(stdout, "Selected toolchain - %s\n", toolchain);
  fprintf(stdout, "Selected configuration - %s\n", config);
  fprintf(stdout, "Cache - %s\n", cache);

  setup_toolchain(project, toolchain);

  // auto external = register_external_project(project, args, "external", "libs/external");
  // auto ext_lib  = get_external_target(project, external, "library1");

  if (strcmp(cache, "off") == 0) disable_registry(project);

  if (strstr(toolchain, "msvc")) {
    add_global_compiler_option(project, "/nologo");  
    add_global_archiver_option(project, "/nologo");  
    add_global_linker_option(project, "/nologo");  
  }

  auto apply_common_settings = [&] (Target *target) {
    add_include_search_path(target, ".");
    add_include_search_path(target, "code");

    if (strstr(toolchain, "llvm")) link_with(target, "libcmt.lib");
  };

  auto lib1 = add_static_library(project, "library1");
  {
    apply_common_settings(lib1);
    add_all_sources_from_directory(lib1, "code/library1", "cpp", false);
  }

  auto lib2 = add_static_library(project, "library2");
  {
    apply_common_settings(lib2);
    add_all_sources_from_directory(lib2, "code/library2", "cpp", false);
    link_with(lib2, lib1);
  }

  auto lib3 = add_static_library(project, "library3");
  {
    apply_common_settings(lib3);
    add_all_sources_from_directory(lib3, "code/library3", "cpp", false);
  }

  auto lib4 = add_static_library(project, "library4");
  {
    apply_common_settings(lib4);
    add_all_sources_from_directory(lib4, "code/library4", "cpp", false);
  }

  auto dyn1 = add_shared_library(project, "dynamic1");
  {
    apply_common_settings(dyn1);
    add_all_sources_from_directory(dyn1, "code/dynamic1", "cpp", false);
  }

  auto dyn2 = add_shared_library(project, "dynamic2");
  {
    apply_common_settings(dyn2);
    add_all_sources_from_directory(dyn2, "code/dynamic2", "cpp", false);
    link_with(dyn2, lib2, dyn1);
    remove_linker_option(dyn2, "/nologo");
  }

  auto dyn3 = add_shared_library(project, "dynamic3");
  {
    apply_common_settings(dyn3);
    add_all_sources_from_directory(dyn3, "code/dynamic3", "cpp", false);
    link_with(dyn3, lib3);
  }

  auto bin1 = add_executable(project, "binary1");
  {
    apply_common_settings(bin1);
    add_all_sources_from_directory(bin1, "code/binary1", "cpp", false);
    link_with(bin1, dyn2, lib4);
  }

  auto bin2 = add_executable(project, "binary2");
  {
    apply_common_settings(bin2);
    add_all_sources_from_directory(bin2, "code/binary2", "cpp", false);
    link_with(bin2, dyn3);
  }

  auto bin3 = add_executable(project, "binary3");
  {
    apply_common_settings(bin3);
    add_all_sources_from_directory(bin3, "code/binary3", "c", false);
    //link_with(bin3, ext_lib);
  }

  fflush(stdout); // This is needed for tests to properly capture the stdout output

  return true;
}
    
