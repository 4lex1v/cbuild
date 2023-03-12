
#include "cbuild.h"

#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C"
#endif
bool setup_project (const Arguments *args, Project *project) {
  const char *toolchain = get_argument_or_default(args, "toolchain", "msvc");
  const char *config    = get_argument_or_default(args, "config",    "debug");

  /*
    This is a simple template file that could be used to bootstrap project configuration.

    Please see cbuild.h for additional documentation and the available API.

    For more information please visit project's Github page.
   */
  Target *target = add_executable(project, "main");
  add_source_file(target, "code/main.cpp");

  if (!strcmp(toolchain, "msvc")) {
    add_compiler_option(target, "/nologo");

    if (!strcmp(config, "release")) {
      add_compiler_option(target, "/O2");
    }

    add_linker_option(target, "/nologo");
    if (!strcmp(config, "release")) {
      add_linker_option(target, "/debug:full");
    }
  }

  return true;
}
