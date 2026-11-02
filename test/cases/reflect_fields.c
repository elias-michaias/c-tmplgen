/* Reflection: walk the generated layout tables of a struct, an enum, a
 * template specialization, and a struct with bitfields.
 * Built as C11 and as strict C99 (typetag(T) needs no _Generic). */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "reflect_types.h"
#include "../gen/reflection.h"

static int check(int ok, const char *what)
{
    if (!ok) fprintf(stderr, "reflect_fields: %s\n", what);
    return ok;
}

static const TypeField *field_named(const TypeInfo *info, const char *name)
{
    const TypeField *fields = RuntimeTypeFieldTable + info->first;
    uint32_t i;
    for (i = 0; i < info->count; i++) {
        if (strcmp(reflect_str(fields[i].name), name) == 0) return &fields[i];
    }
    return NULL;
}

int main(void)
{
    const TypeInfo *hero = typeinfo(typetag(Hero));
    const TypeInfo *role = typeinfo(typetag(Role));
    const TypeInfo *status = typeinfo(typetag(Status));
    const TypeInfo *items = typeinfo(typetag(Vec_Item));
    const TypeInfo *packed = typeinfo(typetag(struct_Packed));
    const TypeField *position = field_named(hero, "position");
    const TypeField *name = field_named(hero, "name");
    const TypeField *flag = field_named(packed, "flag");
    uint32_t i;
    int ok = 1;

    /* Print the struct the way a debugger would. */
    printf("%s (%u bytes, align %u)\n", typestr(typetag(Hero)), hero->size, hero->align);
    for (i = 0; i < hero->count; i++) {
        const TypeField *field = &RuntimeTypeFieldTable[hero->first + i];
        printf("  %-8s %-10s @%u\n", reflect_str(field->name),
               typestr((TypeTag)field->type), field->offset);
    }

    /* The numbers are libclang's; the generated header already made the
     * compiler confirm them, and they agree with offsetof here too. */
    ok &= check(hero->size == sizeof(Hero) && hero->count == 7, "Hero size or field count");
    ok &= check(position != NULL && position->type == typetag(Point) &&
                position->offset == offsetof(Hero, position), "Hero.position");
    ok &= check(name != NULL && name->offset == offsetof(Hero, name) &&
                typeinfo((TypeTag)name->type)->kind == TYPEKIND_ARRAY &&
                typeinfo((TypeTag)name->type)->count == 16, "Hero.name is char[16]");
    ok &= check((typeinfo(typetag(Point))->flags & TYPEFLAG_POINTER_FREE) != 0 &&
                (typeinfo(typetag(Point))->flags & TYPEFLAG_NO_PADDING) != 0, "Point is plain data");

    /* Enumerators with their values, including a negative one. */
    printf("%s:", typestr(typetag(Status)));
    for (i = 0; i < status->count; i++) {
        const TypeEnumerator *e = &RuntimeTypeEnumTable[status->first + i];
        printf(" %s=%lld", reflect_str(e->name), (long long)e->value);
    }
    putchar('\n');
    ok &= check(role->kind == TYPEKIND_ENUM && role->count == 3, "Role enumerators");
    ok &= check(RuntimeTypeEnumTable[status->first].value == -1 &&
                (status->flags & TYPEFLAG_SIGNED) != 0, "Status is signed");

    /* Template specializations are reflected under their pretty name. */
    printf("%s: %u fields, %s\n", typestr(typetag(Vec_Item)), items->count,
           (items->flags & TYPEFLAG_SPECIALIZED) ? "specialized" : "plain");
    ok &= check(strcmp(typestr(typetag(Vec_Item)), "Vec(Item)") == 0 &&
                items->size == sizeof(Vec_Item), "Vec(Item)");
    ok &= check(field_named(typeinfo(typetag(Inventory)), "items")->type == typetag(Vec_Item),
                "Inventory.items");

    /* Bitfields carry a bit offset and width. */
    ok &= check((packed->flags & TYPEFLAG_HAS_BITFIELDS) != 0 && flag != NULL &&
                (flag->flags & TYPEFIELD_BITFIELD) != 0 && flag->bit_width == 1,
                "struct Packed bitfields");
    ok &= check(typeinfo(typetag(Number))->kind == TYPEKIND_UNION, "Number is a union");
    return ok ? 0 : 1;
}
