
#include <cstdio>

#include "base.hpp"

void library3 ();

EXPORT_SYMBOL void dynamic3 () {
  library3();
  printf(",dyn3");
}
