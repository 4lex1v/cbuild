
#include "cbuild.h"

#include <stdio.h>
#include <string.h>

extern "C" bool setup_project (const Arguments *args, Project *project) {
  set_toolchain(project, Toolchain_Type_LLVM);

  auto target = add_executable(project, "main");
  add_source_file(target, "code/main.cpp");
  add_linker_option(target, "libcmt.lib");

  return true;
}
