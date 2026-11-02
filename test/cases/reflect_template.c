/* Reflection inside a template: inspect(&value, out) from
 * test/tmpl/inspect.tmpl.h is one generic function; each specialization reads
 * T's layout from the reflection tables. Its specializations are compiled
 * when reflection.h is included after the headers declaring the types, here
 * including one from another template (Vec(Item)). */
#include "reflect_types.h"
#include <stdio.h>
#include <string.h>

/* After the headers that declare the types (keep it in its own block so
   include sorting does not move it up). */
#include "../gen/reflection.h"

int main(void) {
  Hero hero = {{3, 4}, 90, ROLE_MAGE, STATUS_DOWN, "Ada", 1.5f, 1};
  Vec(Item) items = {NULL, 0, 0};
  Number number;
  int count = 7;
  char text[2048];
  FILE *memory = tmpfile();
  size_t length;
  int ok = 1;

  number.whole = 42;
  if (memory == NULL)
    return 2;
  ok &= inspect(&hero, memory) == 7;
  ok &= inspect(&hero.position, memory) == 2;
  ok &= inspect(&items, memory) == 3; /* Vec(Item) */
  ok &= inspect(&number, memory) == 2;
  ok &= inspect(&count, memory) == 0;
  length = (size_t)ftell(memory);
  rewind(memory);
  if (length >= sizeof text)
    return 3;
  text[fread(text, 1, length, memory)] = '\0';
  fclose(memory);
  fputs(text, stdout);

  ok &= strstr(text, "Hero (") != NULL;
  ok &= strstr(text, "  health: int = 90 @") != NULL;
  ok &= strstr(text, "  role: Role = 1 (ROLE_MAGE) @") != NULL;
  ok &= strstr(text, "  status: Status = -1 (STATUS_DOWN) @") != NULL;
  ok &= strstr(text, "  speed: float = 1.5 @") != NULL;
  ok &= strstr(text, "  alive: _Bool = true @") != NULL;
  ok &= strstr(text, "  len: ") != NULL &&
        strstr(text, "  data: Item * @0") != NULL;
  ok &= strstr(text, "Vec(Item) (") != NULL;
  ok &= strstr(text, "int (4 bytes)\n") != NULL;
  return ok ? 0 : 1;
}
