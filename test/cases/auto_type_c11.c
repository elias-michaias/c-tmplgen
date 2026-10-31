#include "test_types.h"
#include "../gen/all.gen.h"

int main(void)
{
    Tag tag = 5U;
    InferredError error = InferredErrorNone;
    __auto_type result = result_make(RESULT_OK, tag, error);
    __auto_type boxed = box_make(tag);
    __auto_type pair = pair_make(tag, 2.5);
    __auto_type flipped_pair = pair_flip(pair);
    InferredLong number = 9L;
    __auto_type triple = triple_make(tag, error, number);
    return result_value(&result) != tag ||
           box_get(&boxed) != tag ||
           flipped_pair.first != 2.5 || flipped_pair.second != tag ||
           triple_first(&triple) != tag;
}
