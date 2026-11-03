#include "../gen/all.gen.h"

/* Value generics: the second constructor argument is an integer constant.
 * `array_pair_sum` spells its argument as Array(T, 1 + 1), which must fold to
 * the same Array_int_2 instance requested here. */
int main(void)
{
  Array(int, 4) values = {{1, 2, 3, 0}, 3};
  Array(int, 2) pair = {{3, 4}, 2};
  return array_capacity(&values) != 4 || array_get(&values, 1) != 2 ||
         array_pair_sum(&pair) != 7;
}
