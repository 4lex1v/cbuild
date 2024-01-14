
#include <stdio.h>

void external_global_but_local (void);

__declspec(dllexport) void external_call (void) {
  printf("Making a chain of external calls\n");
  external_global_but_local();
  printf("Done\n");
}
