#include <string.h>
#include "test_types.h"
#include "../../gen/all.gen.h"

int main(void)
{
    char *left = "left";
    char *right = "right";
    return strcmp(max(left, right), "right") != 0;
}
