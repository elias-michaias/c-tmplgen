/* reflect_demo.c: a pretty printer and a deserializer written only against
 * the tables in gen/reflection.h (tmplgen --reflect=project).
 *
 * The text format is C designated-initializer syntax, so the printer's output
 * is valid C and the parser reads it back:
 *
 *     {
 *       .pos = {
 *         .x = 1.5,
 *         .y = 2.5,
 *       },
 *       .hp = 10,
 *       .kind = KIND_MAGE,
 *       .name = "bob",
 *     }
 *
 * The file includes no project header: it works for every reflected type.
 * Compile it with the project (it needs gen/reflection.h on the include
 * path) and call reflect_print / reflect_parse with any_of(value). */
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gen/reflection.h"

void reflect_print(FILE *out, Any value);
int reflect_parse(Any destination, const char *text);

/* Scalars are moved by size and signedness, so no per-type code is needed.
 * memcpy keeps this free of aliasing problems. */
static int64_t load_int(const void *p, const TypeInfo *ti)
{
    int is_signed = (ti->flags & TYPEFLAG_SIGNED) != 0;
    switch (ti->size) {
    case 1: { int8_t v; memcpy(&v, p, 1); return is_signed ? v : (int64_t)(uint8_t)v; }
    case 2: { int16_t v; memcpy(&v, p, 2); return is_signed ? v : (int64_t)(uint16_t)v; }
    case 4: { int32_t v; memcpy(&v, p, 4); return is_signed ? v : (int64_t)(uint32_t)v; }
    default: { int64_t v; memcpy(&v, p, 8); return v; }
    }
}

static void store_int(void *p, const TypeInfo *ti, int64_t v)
{
    uint8_t b1 = (uint8_t)v;
    uint16_t b2 = (uint16_t)v;
    uint32_t b4 = (uint32_t)v;
    switch (ti->size) {
    case 1: memcpy(p, &b1, 1); break;
    case 2: memcpy(p, &b2, 2); break;
    case 4: memcpy(p, &b4, 4); break;
    default: memcpy(p, &v, 8); break;
    }
}

static long double load_float(const void *p, const TypeInfo *ti)
{
    if (ti->size == sizeof(float)) { float f; memcpy(&f, p, sizeof f); return f; }
    if (ti->size == sizeof(double)) { double d; memcpy(&d, p, sizeof d); return d; }
    { long double l; memcpy(&l, p, sizeof l); return l; }
}

static void store_float(void *p, const TypeInfo *ti, long double v)
{
    if (ti->size == sizeof(float)) { float f = (float)v; memcpy(p, &f, sizeof f); }
    else if (ti->size == sizeof(double)) { double d = (double)v; memcpy(p, &d, sizeof d); }
    else memcpy(p, &v, sizeof v);
}

static int is_char_array(const TypeInfo *ti)
{
    return ti->kind == TYPEKIND_ARRAY && ti->inner == typetag(char);
}

/* ---- 1. pretty printer --------------------------------------------------- */

static void print_value(FILE *out, Any v, int depth)
{
    const TypeInfo *ti = typeinfo(v.type);
    uint32_t i;

    switch (ti->kind) {
    case TYPEKIND_BOOL:
    case TYPEKIND_INT:
        if (ti->flags & TYPEFLAG_SIGNED) fprintf(out, "%" PRId64, load_int(v.data, ti));
        else fprintf(out, "%" PRIu64, (uint64_t)load_int(v.data, ti));
        return;
    case TYPEKIND_FLOAT:
        fprintf(out, "%.17Lg", load_float(v.data, ti));
        return;
    case TYPEKIND_ENUM: {
        int64_t value = load_int(v.data, ti);
        const TypeEnumerator *e = RuntimeTypeEnumTable + ti->first;
        for (i = 0; i < ti->count; i++) {
            if (e[i].value == value) { fputs(reflect_str(e[i].name), out); return; }
        }
        fprintf(out, "(%s)%" PRId64, typestr(v.type), value);
        return;
    }
    case TYPEKIND_POINTER: {
        void *ptr;
        memcpy(&ptr, v.data, sizeof ptr);
        if (ptr != NULL && RuntimeTypeInfoTable[ti->inner].kind == TYPEKIND_INT &&
            RuntimeTypeInfoTable[ti->inner].size == 1) {
            fprintf(out, "\"%s\"", (const char *)ptr);
        } else {
            fprintf(out, "(void *)%p", ptr);
        }
        return;
    }
    case TYPEKIND_ARRAY: {
        const TypeInfo *element = &RuntimeTypeInfoTable[ti->inner];
        if (is_char_array(ti)) {
            fprintf(out, "\"%.*s\"", (int)ti->count, (const char *)v.data);
            return;
        }
        fputs("{ ", out);
        for (i = 0; i < ti->count; i++) {
            Any item;
            item.data = (char *)v.data + (size_t)i * element->size;
            item.type = (TypeTag)ti->inner;
            print_value(out, item, depth + 1);
            fputs(i + 1 < ti->count ? ", " : " ", out);
        }
        fputs("}", out);
        return;
    }
    case TYPEKIND_STRUCT:
    case TYPEKIND_UNION:
        fputs("{\n", out);
        for (i = 0; i < ti->count; i++) {
            const TypeField *f = &RuntimeTypeFieldTable[ti->first + i];
            if (f->flags & TYPEFIELD_BITFIELD) continue;
            fprintf(out, "%*s.%s = ", (depth + 1) * 2, "", reflect_str(f->name));
            print_value(out, any_field(v, f), depth + 1);
            fputs(",\n", out);
            if (ti->kind == TYPEKIND_UNION) break;      /* first member only */
        }
        fprintf(out, "%*s}", depth * 2, "");
        return;
    default:
        fprintf(out, "/* %s */", typestr(v.type));
        return;
    }
}

void reflect_print(FILE *out, Any value)
{
    print_value(out, value, 0);
    fputc('\n', out);
}

/* ---- 2. deserializer (reads what reflect_print writes) ------------------- */

static void skip_ws(const char **s) { while (isspace((unsigned char)**s)) ++*s; }
static int eat(const char **s, char c) { skip_ws(s); if (**s != c) return 0; ++*s; return 1; }

static size_t identifier(const char **s)
{
    const char *start;
    skip_ws(s);
    start = *s;
    while (isalnum((unsigned char)**s) || **s == '_') ++*s;
    return (size_t)(*s - start);
}

static int name_is(uint32_t name, const char *text, size_t length)
{
    const char *n = reflect_str(name);
    return strncmp(n, text, length) == 0 && n[length] == '\0';
}

static int parse_value(const char **s, Any dst)
{
    const TypeInfo *ti = typeinfo(dst.type);
    char *end;
    uint32_t i;

    skip_ws(s);
    switch (ti->kind) {
    case TYPEKIND_BOOL:
    case TYPEKIND_INT: {
        int64_t v = (ti->flags & TYPEFLAG_SIGNED) ? (int64_t)strtoll(*s, &end, 0)
                                                  : (int64_t)strtoull(*s, &end, 0);
        if (end == *s) return 0;
        *s = end;
        store_int(dst.data, ti, ti->kind == TYPEKIND_BOOL ? v != 0 : v);
        return 1;
    }
    case TYPEKIND_FLOAT: {
        long double v = strtold(*s, &end);
        if (end == *s) return 0;
        *s = end;
        store_float(dst.data, ti, v);
        return 1;
    }
    case TYPEKIND_ENUM: {
        const char *name = *s;
        size_t length = identifier(s);
        const TypeEnumerator *e = RuntimeTypeEnumTable + ti->first;
        for (i = 0; i < ti->count; i++) {
            if (name_is(e[i].name, name, length)) {
                store_int(dst.data, ti, e[i].value);
                return 1;
            }
        }
        return 0;
    }
    case TYPEKIND_ARRAY: {
        const TypeInfo *element = &RuntimeTypeInfoTable[ti->inner];
        if (is_char_array(ti)) {                     /* "text", no escapes */
            const char *q;
            size_t n;
            if (!eat(s, '"') || (q = strchr(*s, '"')) == NULL) return 0;
            n = (size_t)(q - *s) < ti->count ? (size_t)(q - *s) : ti->count - 1;
            memcpy(dst.data, *s, n);
            ((char *)dst.data)[n] = '\0';
            *s = q + 1;
            return 1;
        }
        if (!eat(s, '{')) return 0;
        for (i = 0; !eat(s, '}'); i++) {
            Any item;
            item.data = (char *)dst.data + (size_t)i * element->size;
            item.type = (TypeTag)ti->inner;
            if (i >= ti->count || !parse_value(s, item)) return 0;
            (void)eat(s, ',');
        }
        return 1;
    }
    case TYPEKIND_STRUCT:
    case TYPEKIND_UNION:
        if (!eat(s, '{')) return 0;
        while (!eat(s, '}')) {                       /* fields by name, any order */
            const char *name;
            size_t length;
            const TypeField *field = NULL;
            if (!eat(s, '.')) return 0;
            name = *s;
            length = identifier(s);
            for (i = 0; i < ti->count; i++) {
                const TypeField *f = &RuntimeTypeFieldTable[ti->first + i];
                if (name_is(f->name, name, length)) { field = f; break; }
            }
            if (field == NULL || (field->flags & TYPEFIELD_BITFIELD) || !eat(s, '=')) return 0;
            if (!parse_value(s, any_field(dst, field))) return 0;
            (void)eat(s, ',');
        }
        return 1;
    default:
        return 0;                                    /* pointers are not read back */
    }
}

int reflect_parse(Any destination, const char *text)
{
    const char *s = text;
    if (!parse_value(&s, destination)) return 0;
    skip_ws(&s);
    return *s == '\0';
}
