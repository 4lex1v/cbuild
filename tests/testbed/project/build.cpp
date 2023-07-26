
#include <string_view>

#include "cbuild.h"

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

extern "C" bool setup_project (const Arguments *args, Project *project) {
  auto toolchain = get_argument_or_default(args, "toolchain", "msvc_x64");
  auto config    = get_argument_or_default(args, "config",    "debug");
  auto cache     = get_argument_or_default(args, "cache",     "on");

  // NOTE: Test checks these printf to ensure that values are passed correctly. DON'T REMOVE
  printf("Selected toolchain - %s\n", toolchain);
  printf("Selected configuration - %s\n", config);

  setup_toolchain(project, toolchain);

  if (strcmp(cache, "off") == 0) disable_registry(project);

  auto target = add_executable(project, "main");
  add_source_file(target, "code/main.cpp");

  if (strstr(toolchain, "msvc")) {
    add_compiler_option(target, "/nologo");
    add_linker_option(target, "/nologo");
  }

  if (strncmp(toolchain, "llvm", 4) == 0) link_with(target, "kernel32.lib", "libcmt.lib");

  return true;
}
