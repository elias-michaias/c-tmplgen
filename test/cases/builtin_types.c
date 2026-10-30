#include "test_types.h"
#include "../../tmpl.h"

int main(void)
{
    Vec(int) values = {0};
    push(&values, 42);
    return values.data[0] != 42;
}
