#include "test_types.h"
#include "../../tmpl.h"

int main(void)
{
    TestInt first = 11;
    ErrorCode second = ErrorFailed;
    Tag third = 3U;
    Triple(TestInt, ErrorCode, Tag) value = triple_make(first, second, third);
    double other = 2.5;
    (void)triple_make(other, second, third);
    return triple_first(&value) != first || value.second != second || value.third != third;
}
