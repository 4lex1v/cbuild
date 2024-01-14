
#include <cstdio>

#include "base.hpp"

#include "code/library3/library3.hpp"

EXPORT_SYMBOL void dynamic3 () {
  library3();
  printf(",dyn3");
  fflush(stdout);
}
