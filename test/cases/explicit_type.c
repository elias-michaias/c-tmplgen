#include "test_types.h"
#include "../gen/all.gen.h"

/* Explicit type pseudo-arguments select specializations that cannot be
 * inferred from ordinary arguments (the type occurs only in the return). */
int main(void)
{
  TestInt zero = zero_value(TestInt);
  Dog dog = {7};
  Dog copy = read_as(Dog, &dog);
  return zero != 0 || copy.snacks != 7;
}
