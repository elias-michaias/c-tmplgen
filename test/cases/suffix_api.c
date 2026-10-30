#include "test_types.h"
#include "../gen/all.gen.h"

int main(void)
{
  Vec(TestInt) values = {0};
  push_TestInt(&values, max_TestInt(1, 2));
    return values.data[0] != 2 || clamp_double(3.0, 0.0, 2.0) != 2.0;
}
