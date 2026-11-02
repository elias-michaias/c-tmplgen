/* Reflection: template `// @meta Type{init}` annotations on struct fields
 * become typed metadata objects reachable from the layout tables.
 * The annotations live on Vec in test/tmpl/vec.tmpl.h.
 * Built as C11 and as strict C99 (the API needs no _Generic). */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "reflect_types.h"
#include "../gen/reflection.h"

static int check(int ok, const char *what)
{
    if (!ok) fprintf(stderr, "reflect_meta: %s\n", what);
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
    const TypeInfo *vec = typeinfo(typetag(Vec_Item));
    const TypeField *data = field_named(vec, "data");
    const TypeField *len = field_named(vec, "len");
    const TypeField *cap = field_named(vec, "cap");
    const FieldInfo *meta;
    int ok = 1;

    ok &= check(data != NULL && len != NULL && cap != NULL, "Vec(Item) fields");

    /* Annotated fields carry an index, a metadata TypeTag, and the object. */
    ok &= check(data != NULL && data->attr != 0 &&
                data->meta_type == (uint32_t)typetag(FieldInfo),
                "data carries metadata");
    meta = data ? (const FieldInfo *)reflect_meta(data) : NULL;
    ok &= check(meta != NULL && strcmp(meta->key, "data") == 0 && meta->code == 1,
                "data metadata value");
    ok &= check(len != NULL && reflect_meta_type(len) == typetag(FieldInfo),
                "len metadata type");
    meta = len ? reflect_meta(len) : NULL;
    ok &= check(meta != NULL && strcmp(meta->key, "len") == 0 && meta->code == 2,
                "len metadata value");

    /* Unannotated fields, annotated or not, return NULL / TYPETAG_INVALID. */
    ok &= check(cap != NULL && cap->attr == 0 && cap->meta_type == 0 &&
                reflect_meta(cap) == NULL &&
                reflect_meta_type(cap) == TYPETAG_INVALID, "cap has no metadata");
    {
        const TypeField *key = field_named(typeinfo(typetag(FieldInfo)), "key");
        ok &= check(key != NULL && reflect_meta(key) == NULL,
                    "FieldInfo.key has no metadata");
    }
    return ok ? 0 : 1;
}
