
#include <cstdio>

void library1 ();

void library2 () {
  library1();
  printf(",lib2");
  fflush(stdout);
}
