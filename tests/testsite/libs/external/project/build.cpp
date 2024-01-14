
#include "cbuild.h"

#include <stdio.h>
#include <string.h>

bool setup_project (const Arguments *args, Project *project) {
  auto global_but_local = add_static_library(project, "global");
  add_source_file(global_but_local, "code/global/global.c");

  // the name is intentionally clashes 
  auto the_library = add_shared_library(project, "library1"); 
  add_source_file(the_library, "code/library/library.c");
  link_with(the_library, global_but_local);

  return true;
}
