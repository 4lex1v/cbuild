
#include <stdio.h>

void external_call();

int main () {
  printf("Making an external library call from a sub project\n");
  external_call();
  return 0;
}
