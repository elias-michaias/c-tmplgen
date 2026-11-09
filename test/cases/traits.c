#include "../gen/all.gen.h"

int main(void) {
  if (trait_max_int(3, 7) != 7)
    return 1;
  if (trait_max_double(0.5, 2.5) < 2.0)
    return 2;
  if (Ord_compare(4, 4) != 0)
    return 3;
  if (!trait_between_open_int(5, 1, 9))
    return 4;
  if (trait_between_open_int(1, 1, 9))
    return 5;
  if (!Eq_equal(2, 2))
    return 6;
  if (Eq_equal(2, 3))
    return 7;
  return 0;
}
