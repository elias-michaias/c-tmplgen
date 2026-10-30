#include "test_types.h"
#include "../../tmpl.h"

int main(void)
{
    TestInt value = 37;
    ErrorCode error = ErrorFailed;
    Result(TestInt, ErrorCode) result = result_make(value, error);
    Option(TestInt) option = { value };
    return result_value(&result) != value || result_error(&result) != error ||
           result_identity(value) != value || result_pointer(&value) != value ||
           option_value(&option) != value || result_version() != 1;
}
