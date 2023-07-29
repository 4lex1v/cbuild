
#include <cstdio>

#include "base.hpp"
#include "library/library.hpp"
#include "dynamic.hpp"

void test_phrase () {
  printf("Calling from dynamic\n");
  printf("Thank you for trying %s!\n", control_phrase());
}

