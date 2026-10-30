#include <string.h>
#include "test_types.h"
#include "../../tmpl.h"

int main(void)
{
    char *left = "left";
    char *right = "right";
    return strcmp(max(left, right), "right") != 0;
}
