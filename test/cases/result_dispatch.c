#include "test_types.h"
#include "../../gen/all.gen.h"

int main(void)
{
    TestInt value = 37;
    ErrorCode error = ErrorFailed;
    Result(TestInt, ErrorCode) result =
        result_make(RESULT_OK, value, error);
    Result(TestInt, ErrorCode) failed =
        result_make(RESULT_ERROR, value, error);
    Option(TestInt) option = option_some(value);
    int switched_value = 0;
    switch (result.tag) {
    case RESULT_OK:
        switched_value = result.payload.ok;
        break;
    case RESULT_ERROR:
        switched_value = -1;
        break;
    }
    return !result_is_ok(&result) || result_is_error(&result) ||
           switched_value != value || result_value(&result) != value ||
           !result_is_error(&failed) || result_error(&failed) != error ||
           result_identity(value) != value || result_pointer(&value) != value ||
           !option_is_some(&option) || option_value(&option) != value ||
           result_version() != 1;
}
