// clang-format off
#include "test_types.h"
#include "../gen/all.gen.h"


void foo(Vec(TestInt)* v, TestInt t) {
  push(v, t);
}

/* GNU dispatch is a convenience macro; use suffix_api.c for direct
 * implementation navigation. */
int main(void) {
  Vec(TestInt) values = {0};
  TestInt low = 1;
  TestInt high = 2;
  double first = 1.0;
  double second = 2.0;
  push(&values, max(low, high));
  return values.data[0] != 2 || max(first, second) != 2.0;
}
