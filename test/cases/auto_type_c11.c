#include "test_types.h"
#include "../gen/all.gen.h"

int main(void)
{
    Tag tag = 5U;
    InferredError error = InferredErrorNone;
    __auto_type result = result_make(RESULT_OK, tag, error);
    InferredLong number = 9L;
    __auto_type triple = triple_make(tag, error, number);
    return result_value(&result) != tag ||
           triple_first(&triple) != tag;
}
