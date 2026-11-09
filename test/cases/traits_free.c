#include "../gen/all.gen.h"

int main(void) {
  Crate(int) boxed_int = {5};
  Crate(double) boxed_double = {2.0};
  Duo(int, double) duo = duo_make(5, 2.5);
  Duo(int, double) left = duo_make(1, 7.5);
  Duo(int, double) right = duo_make(2, 0.25);
  Duo(int, double) sum = Add_add(left, right);
  if (Label_label(boxed_int) != 105)
    return 1;
  if (Label_label(boxed_double) != 12)
    return 2;
  if (duo.first != 5 || duo.second != 2.5)
    return 3;
  if (Add_add(1, 2) != 3 || Add_add(1.5, 2.25) != 3.75)
    return 4;
  if (sum.first != 3 || sum.second != 7.75)
    return 5;
  return 0;
}
