#include "test_types.h"
#include "../gen/all.gen.h"

/* Higher-kinded templates: constructor parameters bind a template type
 * constructor by name and decompose observed specializations, including
 * value arguments and zero-arity constructors. */
int main(void)
{
  Option(TestInt) option = {0};
  Array(TestInt, 4) array = {{1, 2, 3, 0}, 3};
  Triple(TestInt, TestInt, TestInt) triple = {1, 2, 3};
  Boxed(Option) boxed = {{OPTION_SOME, {7}}};
  PlainBox(Plain) plain_box = {{9}};
  return container_unwrap(&option) != 0 || sized_unwrap(&array) != 0 ||
         triple_wrapped(&triple) != 0 || boxed.value.payload.some != 7 ||
         plain_box.value.x != 9;
}
