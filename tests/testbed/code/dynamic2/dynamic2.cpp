
#include <cstdio>

#include "base.hpp"

void library2 ();
void dynamic1 ();

EXPORT_SYMBOL void dynamic2 () {
  library2();
  printf(",");
  dynamic1();
  printf(",dyn2");
}
