/* Reflection: examples/reflect_demo.c prints any value as C designated
 * initializers and parses that text back, using only the generated tables.
 * Build with examples/reflect_demo.c and -I test. */
#include <stdio.h>
#include <string.h>
#include "reflect_types.h"
#include "../gen/reflection.h"

void reflect_print(FILE *out, Any value);
int reflect_parse(Any destination, const char *text);

int main(void)
{
    Hero original, copy;
    char text[2048];
    FILE *memory = tmpfile();
    long length;

    memset(&original, 0, sizeof original);
    memset(&copy, 0, sizeof copy);
    original.position.x = -7;
    original.position.y = 12;
    original.health = 90;
    original.role = ROLE_ROGUE;
    original.status = STATUS_DOWN;
    strcpy(original.name, "Grace");
    original.speed = 2.25f;
    original.alive = 1;

    if (memory == NULL) return 2;
    reflect_print(memory, any_of(original));
    length = ftell(memory);
    rewind(memory);
    if (length <= 0 || (size_t)length >= sizeof text) return 3;
    text[fread(text, 1, (size_t)length, memory)] = '\0';
    fclose(memory);
    fputs(text, stdout);

    /* Fields may come in any order and be omitted. */
    if (!reflect_parse(any_of(copy), text)) return 4;
    if (memcmp(&original, &copy, sizeof original) != 0) return 5;
    {
        Hero partial;
        memset(&partial, 0, sizeof partial);
        if (!reflect_parse(any_of(partial), "{ .role = ROLE_MAGE, .health = 5 }") ||
            partial.role != ROLE_MAGE || partial.health != 5) return 6;
    }
    return strstr(text, ".role = ROLE_ROGUE") != NULL &&
           strstr(text, ".status = STATUS_DOWN") != NULL ? 0 : 7;
}
