/* Reflection: typetag_of / any_of select a TypeTag from the static type, so
 * values of different types can travel as Any and be told apart with a
 * plain switch; enum values go through enumtag_of. Built as C11
 * (_Generic) and GNU C99 (__builtin_choose_expr). */
#include <stdio.h>
#include <string.h>
#include "reflect_types.h"
#include "../gen/reflection.h"

static void describe(Any value)
{
    const TypeInfo *info = typeinfo(value.type);
    switch (value.type) {
    case typetag(Hero):
        printf("hero %s with %d health\n", ((Hero *)value.data)->name,
               ((Hero *)value.data)->health);
        break;
    case typetag(int):
        printf("int %d\n", *(int *)value.data);
        break;
    case typetag(const_char_ptr):
        printf("string \"%s\"\n", *(const char **)value.data);
        break;
    default:
        printf("%s (%u bytes)\n", typestr(value.type), info->size);
        break;
    }
}

int main(void)
{
    Hero hero = { { 3, 4 }, 90, ROLE_MAGE, STATUS_BUFFED, "Ada", 1.5f, 1 };
    Hero *leader = &hero;
    Inventory inventory = { { NULL, 0, 0 }, 25 };
    Item potion = { "potion", 3 };
    const char *motto = "onward";
    int count = 2;
    double score = 9.5;
    Any values[6];
    size_t i;
    int ok = 1;

    push(&inventory.items, potion);
    values[0] = any_of(hero);
    values[1] = any_of(count);
    values[2] = any_of(motto);
    values[3] = any_of(score);
    values[4] = any_of(inventory.items);
    values[5] = any_of(leader);
    for (i = 0; i < sizeof values / sizeof values[0]; i++) describe(values[i]);

    ok &= values[0].type == typetag(Hero) && values[1].type == typetag(int) &&
          values[2].type == typetag(const_char_ptr) && values[3].type == typetag(double) &&
          values[4].type == typetag(Vec_Item) && values[5].type == typetag(Hero_ptr);
    ok &= typetag_of(hero.position) == typetag(Point);
    ok &= typeinfo(typetag_of(inventory)) == typeinfo(typetag(Inventory));
    /* An enum value is compatible with its integer type, so typetag_of gives
     * that type's id; enumtag_of picks the enum itself. */
    ok &= enumtag_of(hero.role) == typetag(Role) && enumtag_of(hero.status) == typetag(Status);
    switch (enumtag_of(hero.status)) {
    case typetag(Role): ok = 0; break;
    case typetag(Status): break;
    default: ok = 0; break;
    }
    /* Fields of an Any are Anys too. */
    {
        const TypeInfo *info = typeinfo(values[0].type);
        uint32_t f;
        for (f = 0; f < info->count; f++) {
            const TypeField *field = &RuntimeTypeFieldTable[info->first + f];
            Any part = any_field(values[0], field);
            if (strcmp(reflect_str(field->name), "health") == 0) {
                ok &= part.type == typetag(int) && *(int *)part.data == 90;
            }
        }
    }
    free(inventory.items.data);
    return ok ? 0 : 1;
}
