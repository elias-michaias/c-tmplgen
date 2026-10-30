#include "test_types.h"
#include "../../gen/all.gen.h"

int main(void)
{
    TestInt value = 4;
    return chain_root(value) != 5 || recursive_value(value, 3U) != value ||
           mutual_first(value, 2U) != value ||
           (reorder_call(value, 2.5), 0) || cross_root(value) != 6;
}
