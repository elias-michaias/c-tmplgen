// clang-format off
/* A generic function over any reflected type: everything it knows about T
 * comes from the reflection tables. A template that includes
 * gen/reflection.h gets its specializations compiled once reflection.h has
 * seen the argument types, so include reflection.h after your headers. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../gen/reflection.h"

#ifndef TMPLGEN
typedef int T;
#endif

/* Prints T's name and size, then each field with its type, offset and, for
 * scalars, its value (enums with the enumerator's name). Returns the number
 * of fields. */
// @template T
static inline uint32_t inspect(T *value, FILE *out)
{
    TypeTag tag = typetag_of(*value);
    const TypeInfo *info = typeinfo(tag);
    const unsigned char *bytes = (const unsigned char *)value;
    uint32_t count = 0, i;
    if (info->kind == TYPEKIND_STRUCT || info->kind == TYPEKIND_UNION) count = info->count;
    fprintf(out, "%s (%u bytes)%s\n", typestr(tag), (unsigned)info->size, count != 0 ? " {" : "");
    for (i = 0; i < count; i++) {
        const TypeField *field = &RuntimeTypeFieldTable[info->first + i];
        const TypeInfo *type = typeinfo((TypeTag)field->type);
        const unsigned char *at = bytes + field->offset;
        fprintf(out, "  %s: %s", reflect_str(field->name), typestr((TypeTag)field->type));
        if (field->flags & TYPEFIELD_BITFIELD) {
            fprintf(out, " : %u", (unsigned)field->bit_width);
        } else if (type->kind == TYPEKIND_BOOL) {
            fputs(*at ? " = true" : " = false", out);
        } else if (type->kind == TYPEKIND_INT || type->kind == TYPEKIND_ENUM) {
            int is_signed = (type->flags & TYPEFLAG_SIGNED) != 0;
            long long number = 0;
            uint32_t e;
            if (type->size == 1) {
                uint8_t raw; memcpy(&raw, at, 1);
                number = is_signed ? (long long)(int8_t)raw : (long long)raw;
            } else if (type->size == 2) {
                uint16_t raw; memcpy(&raw, at, 2);
                number = is_signed ? (long long)(int16_t)raw : (long long)raw;
            } else if (type->size == 4) {
                uint32_t raw; memcpy(&raw, at, 4);
                number = is_signed ? (long long)(int32_t)raw : (long long)raw;
            } else if (type->size == 8) {
                uint64_t raw; memcpy(&raw, at, 8);
                number = (long long)raw;
            }
            fprintf(out, " = %lld", number);
            for (e = 0; type->kind == TYPEKIND_ENUM && e < type->count; e++) {
                const TypeEnumerator *item = &RuntimeTypeEnumTable[type->first + e];
                if (item->value == number) fprintf(out, " (%s)", reflect_str(item->name));
            }
        } else if (type->kind == TYPEKIND_FLOAT && type->size == sizeof(float)) {
            float number;
            memcpy(&number, at, sizeof number);
            fprintf(out, " = %g", (double)number);
        } else if (type->kind == TYPEKIND_FLOAT && type->size == sizeof(double)) {
            double number;
            memcpy(&number, at, sizeof number);
            fprintf(out, " = %g", number);
        }
        fprintf(out, " @%u\n", (unsigned)field->offset);
    }
    if (count != 0) fputs("}\n", out);
    return count;
}
