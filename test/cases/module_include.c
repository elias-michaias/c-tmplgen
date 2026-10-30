#include "test_types.h"
#include "../../gen/vec.gen.h"

int main(void)
{
    Vec(TestInt) values = {0};
    push(&values, 13);
    return values.data[0] != 13;
}
