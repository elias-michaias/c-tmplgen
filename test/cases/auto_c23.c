#include "test_types.h"
#include "../../gen/all.gen.h"

int main(void)
{
    InferredLong long_value = 41L;
    InferredError inferred_error = InferredErrorFailed;
    auto first = result_make(RESULT_OK, long_value, inferred_error);

    InferredDouble double_value = 2.75;
    ErrorCode other_error = ErrorFailed;
    auto second = result_make(RESULT_ERROR, double_value, other_error);

    return result_value(&first) != long_value ||
           result_error(&second) != other_error;
}
