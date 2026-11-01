# Reflection for tmplgen — design sketch and implementation plan

> Status: **proposal / not implemented.** This document sketches a runtime
> reflection layer that `tmplgen` generates as `gen/reflection.h`, integrated
> into the standalone executable and both plugins (Clang and clangd).
> Line references (`tmplgen.c:NNNN`) point at the current code the plan builds on.
>
> **Revised after PROBLEM.md was fixed.** Generated headers now follow one
> convention: code that names a project type sits in a section guarded by the
> include guard of the header that declares the type, and dispatch picks those
> sections up through hook macros (`doc/usage.md`, "Include generated headers
> after your types"). Reflection adopts the same convention, which removes its
> biggest open problem (§6.4 / old Q3: `reflection.h` had to include every
> project header). The key change is §6.1: **the tables hold layout numbers
> computed by libclang, and the consumer's compiler verifies them** inside the
> guarded sections. The tables then name no project type, so they are complete
> in every translation unit, header-only, with no project includes.

---

## 1. Goals

**North star:** reflection in C that is robust and first-class. You add a struct
to a header, save the file, and in the editor `typeinfo(x)` can already list its
fields, `switch (any.type)` already has a `TYPEID_MyStruct` case, and a generic
`debug_print(any_of(x))` works. No registry, no X-macros, no hand-written
descriptors.

Concretely:

1. **`enum TypeId`.** Every reflected concrete type gets a dense numeric id
   (`TYPEID_int`, `TYPEID_Point`, `TYPEID_Vec_int`, `TYPEID_char_ptr`, …), so
   programs can `switch` on types with `-Wswitch` checking.
2. **`RuntimeTypeInfoTable`.** One `static const` (or `extern const`) table
   indexed by `TypeId`. It holds kind, size, alignment, flags, names,
   fields, enumerators, element and pointee links, and stable hashes. It lives
   entirely in `.rodata`: no relocations and no startup code.
3. **Compile-time operators:**
   - `typeinfo(x)` and `typeid_of(x)` use C11 `_Generic` or GNU builtins,
     reusing the existing dispatch backends.
   - `typeinfo_of(T)` / `TYPEID(T)` work for type names. They paste tokens,
     cost nothing, and also work in plain C99.
4. **`Any`.** `{ void *data; TypeId type; }`, plus `any_of(x)`, field access,
   and iteration helpers.
5. **Speed throughout:**
   - **Generation:** no extra libclang parses in the common path, hash-consed
     type graph, flat arrays, linear merges, cached per-file facts.
   - **Consumer compile time:** small dispatch lists, a single string pool,
     plain integer tables that need no project declarations.
   - **Runtime:** flat tables in `.rodata` with no startup cost; `typeinfo(x)`
     is a constant address.
6. **Works with partial includes.** A translation unit that never includes
   `player.h` can still print a `Player` it receives as an `Any`: the tables
   describe every reflected type in every translation unit. Only the
   operations that *name* a type in source (`typeid_of(p)`, `any_of(p)`)
   need its header, exactly like the rest of C.

tmplgen generates **data plus a handful of inline accessors**, not a runtime
library. Printing, serialization, and so on are user code written against the
tables. Two demo functions in §2.3 (a pretty printer and a deserializer) show
how little code that takes. They ship as an example, not as generated output.

### Non-goals (v1)

- Reflection of function *declarations* (call-by-name or RPC). The table can
  grow a `FunctionInfo` section later.
- Types that exist only at runtime (dlopen'd plugins). This is the same
  boundary as the rest of tmplgen.
- Mutating type metadata at runtime.
- Precise reflection of types declared *only* inside a `.c` file. Those are
  TU-private; see §9.4.

---

## 2. What the user writes

### 2.1 Using the generated API directly

```c
#include <stdio.h>
#include "types.h"
#include "gen/all.gen.h"
#include "gen/reflection.h"

/* types.h:
 *   typedef int Count;                     a name for int: reflects as int
 *   typedef struct { int value; } Score;   a distinct type: reflects as Score
 */

int main(void)
{
    Point p = { 1.5f, 2.5f };
    Count c = 3;
    Score s = { 120 };

    const TypeInfo *ti = typeinfo(p);           /* &RuntimeTypeInfoTable[TYPEID_Point] */
    printf("%s: size=%u align=%u\n", type_name(TYPEID_Point), ti->size, ti->align);

    TYPEINFO_FOR_EACH_FIELD(ti, f)              /* x (float) @0, y (float) @4 */
        printf("  %s (%s) @%u\n", type_string(f->name), type_name((TypeId)f->type), f->offset);

    Any items[] = { any_of(p), any_of(c), any_of(s) };
    for (size_t i = 0; i < sizeof items / sizeof items[0]; i++) {
        switch (items[i].type) {                /* exact type switch, -Wswitch friendly */
        case TYPEID_int:   printf("int %d\n", *(int *)items[i].data); break;      /* c lands here */
        case TYPEID_Score: printf("score %d\n", ((Score *)items[i].data)->value); break;
        default:           printf("<%s>\n", type_name(items[i].type)); break;
        }
    }
}
```

### 2.2 Using a type name directly (C99, zero cost)

```c
const TypeInfo *vi = typeinfo_of(Vec_int);      /* token paste, no _Generic */
TypeId id = TYPEID(Point);                      /* == TYPEID_Point */
TypeId c  = TYPEID(enum_Color);                 /* enum identity even where _Generic can't see it (§10) */
```

### 2.3 Demo: a pretty printer and a deserializer

These are the only "runtime" code in the plan. They live in
`examples/reflect_demo.c`, not in generated output, and they read only the
generated tables. The text format is C designated-initializer syntax, so the
printer's output is valid C and the parser reads it back:

```c
{
  .pos = {
    .x = 1.5,
    .y = 2.5,
  },
  .hp = 10,
  .kind = KIND_WARRIOR,
  .name = "bob",
}
```

```c
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gen/reflection.h"

/* Scalars are moved by size and signedness, so no per-type code is needed.
 * memcpy keeps this free of aliasing problems. */
static int64_t load_int(const void *p, const TypeInfo *ti)
{
    int signed_ = (ti->flags & TYPEFLAG_SIGNED) != 0;
    switch (ti->size) {
    case 1: { int8_t  v; memcpy(&v, p, 1); return signed_ ? v : (int64_t)(uint8_t)v; }
    case 2: { int16_t v; memcpy(&v, p, 2); return signed_ ? v : (int64_t)(uint16_t)v; }
    case 4: { int32_t v; memcpy(&v, p, 4); return signed_ ? v : (int64_t)(uint32_t)v; }
    default: { int64_t v; memcpy(&v, p, 8); return v; }
    }
}

static void store_int(void *p, const TypeInfo *ti, int64_t v)
{
    uint8_t b1 = (uint8_t)v; uint16_t b2 = (uint16_t)v; uint32_t b4 = (uint32_t)v;
    switch (ti->size) {
    case 1: memcpy(p, &b1, 1); break;
    case 2: memcpy(p, &b2, 2); break;
    case 4: memcpy(p, &b4, 4); break;
    default: memcpy(p, &v, 8); break;
    }
}

static long double load_float(const void *p, const TypeInfo *ti)
{
    if (ti->size == sizeof(float))  { float f;  memcpy(&f, p, sizeof f); return f; }
    if (ti->size == sizeof(double)) { double d; memcpy(&d, p, sizeof d); return d; }
    { long double l; memcpy(&l, p, sizeof l); return l; }
}

static void store_float(void *p, const TypeInfo *ti, long double v)
{
    if (ti->size == sizeof(float))       { float f = (float)v;   memcpy(p, &f, sizeof f); }
    else if (ti->size == sizeof(double)) { double d = (double)v; memcpy(p, &d, sizeof d); }
    else                                 memcpy(p, &v, sizeof v);
}

static int is_char_array(const TypeInfo *ti)
{
    return ti->kind == TYPEKIND_ARRAY && ti->inner == TYPEID_char;
}

/* ---- 1. pretty printer --------------------------------------------------- */

static void print_value(FILE *out, Any v, int depth)
{
    const TypeInfo *ti = typeinfo_any(v);

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
        for (uint32_t i = 0; i < ti->count; i++)
            if (e[i].value == value) { fputs(type_string(e[i].name), out); return; }
        fprintf(out, "(%s)%" PRId64, type_name(v.type), value);
        return;
    }
    case TYPEKIND_POINTER: {
        void *ptr;
        memcpy(&ptr, v.data, sizeof ptr);
        if (ti->inner == TYPEID_char && ptr) fprintf(out, "\"%s\"", (const char *)ptr);
        else fprintf(out, "(void *)%p", ptr);
        return;
    }
    case TYPEKIND_ARRAY: {
        const TypeInfo *elem = &RuntimeTypeInfoTable[ti->inner];
        if (is_char_array(ti)) { fprintf(out, "\"%.*s\"", (int)ti->count, (const char *)v.data); return; }
        fputs("{ ", out);
        for (uint32_t i = 0; i < ti->count; i++) {
            Any item = { (char *)v.data + (size_t)i * elem->size, (TypeId)ti->inner };
            print_value(out, item, depth + 1);
            fputs(i + 1 < ti->count ? ", " : " ", out);
        }
        fputs("}", out);
        return;
    }
    case TYPEKIND_STRUCT:
    case TYPEKIND_UNION:
        fputs("{\n", out);
        TYPEINFO_FOR_EACH_FIELD(ti, f) {
            if (f->flags & (TYPEFIELD_BITFIELD | TYPEFIELD_IGNORED)) continue;
            fprintf(out, "%*s.%s = ", (depth + 1) * 2, "", type_string(f->name));
            print_value(out, any_field(v, f), depth + 1);
            fputs(",\n", out);
            if (ti->kind == TYPEKIND_UNION) break;      /* first member only */
        }
        fprintf(out, "%*s}", depth * 2, "");
        return;
    default:
        fprintf(out, "/* %s */", type_name(v.type));
        return;
    }
}

void reflect_print(FILE *out, Any v)
{
    print_value(out, v, 0);
    fputc('\n', out);
}

/* ---- 2. deserializer (reads what reflect_print writes) ------------------- */

static void skip_ws(const char **s) { while (isspace((unsigned char)**s)) ++*s; }
static int  eat(const char **s, char c) { skip_ws(s); if (**s != c) return 0; ++*s; return 1; }

static size_t ident(const char **s)
{
    const char *start;
    skip_ws(s);
    start = *s;
    while (isalnum((unsigned char)**s) || **s == '_') ++*s;
    return (size_t)(*s - start);
}

static int name_is(uint32_t name, const char *text, size_t len)
{
    const char *n = type_string(name);
    return strncmp(n, text, len) == 0 && n[len] == '\0';
}

static int parse_value(const char **s, Any dst)
{
    const TypeInfo *ti = typeinfo_any(dst);
    char *end;

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
        size_t len = ident(s);
        const TypeEnumerator *e = RuntimeTypeEnumTable + ti->first;
        for (uint32_t i = 0; i < ti->count; i++)
            if (name_is(e[i].name, name, len)) { store_int(dst.data, ti, e[i].value); return 1; }
        return 0;
    }
    case TYPEKIND_ARRAY: {
        const TypeInfo *elem = &RuntimeTypeInfoTable[ti->inner];
        if (is_char_array(ti)) {                        /* "text", no escapes in the demo */
            const char *q;
            size_t n;
            if (!eat(s, '"') || !(q = strchr(*s, '"'))) return 0;
            n = (size_t)(q - *s) < ti->count ? (size_t)(q - *s) : ti->count - 1;
            memcpy(dst.data, *s, n);
            ((char *)dst.data)[n] = '\0';
            *s = q + 1;
            return 1;
        }
        if (!eat(s, '{')) return 0;
        for (uint32_t i = 0; !eat(s, '}'); i++) {
            Any item = { (char *)dst.data + (size_t)i * elem->size, (TypeId)ti->inner };
            if (i >= ti->count || !parse_value(s, item)) return 0;
            eat(s, ',');
        }
        return 1;
    }
    case TYPEKIND_STRUCT:
    case TYPEKIND_UNION:
        if (!eat(s, '{')) return 0;
        while (!eat(s, '}')) {                          /* fields by name, any order */
            const char *name;
            size_t len;
            const TypeField *field = NULL;
            if (!eat(s, '.')) return 0;
            name = *s;
            len = ident(s);
            TYPEINFO_FOR_EACH_FIELD(ti, f)
                if (name_is(f->name, name, len)) { field = f; break; }
            if (!field || (field->flags & TYPEFIELD_BITFIELD) || !eat(s, '=')) return 0;
            if (!parse_value(s, any_field(dst, field))) return 0;
            eat(s, ',');
        }
        return 1;
    default:
        return 0;                                       /* pointers etc. are not read back */
    }
}

int reflect_parse(Any dst, const char *text)
{
    const char *s = text;
    if (!parse_value(&s, dst)) return 0;
    skip_ws(&s);
    return *s == '\0';
}
```

Usage:

```c
/* types.h */
typedef enum { KIND_WARRIOR, KIND_MAGE } Kind;
typedef struct { Point pos; int hp; Kind kind; char name[16]; } Player;

/* main.c */
Player p = { { 1.5f, 2.5f }, 10, KIND_WARRIOR, "bob" };
reflect_print(stdout, any_of(p));

Player q = {0};
if (!reflect_parse(any_of(q), "{ .name = \"alice\", .hp = 7, .kind = KIND_MAGE }"))
    fputs("parse error\n", stderr);
```

These two functions use every part of the table: kinds, sizes, the signed
flag, field names, offsets, element and pointee links, enumerators, and names.
That makes them the end-to-end test of the generator (§12). The code above was
compiled warning-free with GCC 13 and Clang 18 (`-std=c11 -Wall -Wextra
-pedantic -Wconversion -Wshadow`) against a hand-written mock of the §3 tables,
and a print → parse round trip reproduced the original struct byte for byte.

### 2.4 Annotations (optional, same `// @` style as templates)

```c
// @reflect                         (only needed in --reflect=annotated mode)
typedef struct {
    // @reflect-name "hp"           name stored in the table instead of "health"
    int health;
    // @reflect-ignore              TYPEFIELD_IGNORED: user code can skip it
    void *cache;
    // @reflect-count len           data points at len elements (used for Vec(T))
    Item *data;
    size_t len;
} Inventory;
```

---

## 3. Generated output: `gen/reflection.h`

Below is a trimmed sketch of what would be generated for the
`c-tmplgen-playground` project (`Point`, `Score`, `Vec(int)`, `Vec(Point)`,
`Pair(Score, double)`). It has two parts, like every generated header:

- a **core**, guarded once, that names **no project type**: ids, schema types,
  the tables (plain integers), `Any`, helpers, and the dispatch macro with its
  hooks;
- **sections**, one per declaring header (keyed on its include guard), that
  verify the table's layout numbers against the consumer's compiler and add
  the header's types to `typeid_of(x)`.

A hand-written mock of this layout (two headers, a printer TU that includes
neither, a TU that includes the generated header before its types and again
after) compiles warning-free with GCC and Clang (`-std=c11/c17 -Wall -Wextra
-pedantic -Wconversion -Wshadow -Werror`). The printer prints a nested
`Player` it never saw declared, and compiling with a different `Point` layout
(`-DBIG_POINT`) fails the generated `_Static_assert`.

```c
/* gen/reflection.h — generated by tmplgen. Do not edit. */

/* Generated, so safe to include; outside the guard so that including
 * reflection.h again also activates newly available template sections. */
#include "all.gen.h"     /* only if template specializations are reflected */

/* ======== core: guarded once, names no project type ======================= */
#ifndef TMPLGEN_REFLECTION_H
#define TMPLGEN_REFLECTION_H

#include <stddef.h>
#include <stdint.h>
/* No project headers: nothing below needs a project declaration. */

/* ---- 1. ids ------------------------------------------------------------ */
typedef enum TypeId {
    TYPEID_INVALID = 0,
    /* builtins: fixed order, identical across projects */
    TYPEID_void, TYPEID__Bool, TYPEID_char, TYPEID_signed_char, TYPEID_unsigned_char,
    TYPEID_short, TYPEID_unsigned_short, TYPEID_int, TYPEID_unsigned_int,
    TYPEID_long, TYPEID_unsigned_long, TYPEID_long_long, TYPEID_unsigned_long_long,
    TYPEID_float, TYPEID_double, TYPEID_long_double,
    /* project types, sorted by stable key */
    TYPEID_Pair_Score_double,  /* a real struct; its name comes from tmplgen */
    TYPEID_Point,
    TYPEID_Score,
    TYPEID_Vec_Point,
    TYPEID_Vec_int,
    /* derived types reached from fields or uses */
    TYPEID_Point_ptr,
    TYPEID_int_ptr,
    TYPEID_COUNT
} TypeId;

/* ---- 2. schema types (fixed text, emitted once) ------------------------- */
typedef enum TypeKind {
    TYPEKIND_INVALID, TYPEKIND_VOID, TYPEKIND_BOOL, TYPEKIND_INT, TYPEKIND_FLOAT,
    TYPEKIND_POINTER, TYPEKIND_ARRAY, TYPEKIND_STRUCT, TYPEKIND_UNION,
    TYPEKIND_ENUM, TYPEKIND_FUNCTION, TYPEKIND_OPAQUE
} TypeKind;

enum {
    TYPEFLAG_SIGNED        = 1u << 0,
    TYPEFLAG_POINTER_FREE  = 1u << 1,  /* transitively no pointers: memcpy-serializable */
    TYPEFLAG_NO_PADDING    = 1u << 2,  /* from libclang's layout, verified in §3 sections */
    TYPEFLAG_HAS_BITFIELDS = 1u << 3,
    TYPEFLAG_FLEXIBLE      = 1u << 4,  /* ends in a flexible array member */
    TYPEFLAG_ANONYMOUS     = 1u << 5,
    TYPEFLAG_SPECIALIZED   = 1u << 6,  /* tmplgen template instance */
    TYPEFLAG_ENUM_DENSE    = 1u << 7   /* enumerators form [min, min+count) */
};

enum { TYPEFIELD_BITFIELD = 1u << 0, TYPEFIELD_IGNORED = 1u << 1, TYPEFIELD_COUNTED = 1u << 2 };

typedef struct TypeInfo {       /* 32 bytes, 2 per cache line */
    uint32_t size;
    uint16_t align;
    uint8_t  kind;              /* TypeKind */
    uint8_t  reserved;
    uint32_t flags;
    uint32_t name;              /* offset into RuntimeTypeStrings */
    uint32_t first;             /* first field / enumerator / param index */
    uint32_t count;             /* fields, enumerators, params, or array length */
    uint32_t inner;             /* TypeId: pointee / element / enum underlying / return */
    uint32_t display;           /* pretty name, e.g. "Vec(int)", or == name */
} TypeInfo;

typedef struct TypeField {      /* 20 bytes */
    uint32_t name;              /* string offset */
    uint32_t type;              /* TypeId */
    uint32_t offset;            /* bytes; bit offset if TYPEFIELD_BITFIELD */
    uint16_t bit_width;
    uint8_t  quals;             /* const / volatile / restrict / _Atomic */
    uint8_t  flags;             /* TYPEFIELD_BITFIELD | _IGNORED | _COUNTED */
    uint32_t attr;              /* count-field index for TYPEFIELD_COUNTED */
} TypeField;

typedef struct TypeEnumerator { int64_t value; uint32_t name; uint32_t pad; } TypeEnumerator;

/* ---- 3. tables: integers only (§6.1) ---------------------------------------
 * Header-only mode (default): TMPLGEN_REFLECT_STORAGE = static.
 * --compiled mode: these are extern here and defined in gen/reflection.gen.c. */
TMPLGEN_REFLECT_STORAGE const char RuntimeTypeStrings[] =
    "\0" "int\0" "float\0" "double\0" "Point\0" "x\0" "y\0"
    "Vec_int\0" "Vec(int)\0" "data\0" "len\0" "cap\0" /* ... */;

TMPLGEN_REFLECT_STORAGE const TypeInfo RuntimeTypeInfoTable[TYPEID_COUNT] = {
    [TYPEID_int]     = {  4, 4, TYPEKIND_INT, 0,
                          TYPEFLAG_SIGNED | TYPEFLAG_POINTER_FREE | TYPEFLAG_NO_PADDING,
                          1, 0, 0, 0, 1 },
    [TYPEID_Point]   = {  8, 4, TYPEKIND_STRUCT, 0,
                          TYPEFLAG_POINTER_FREE | TYPEFLAG_NO_PADDING,
                          19, /*first*/0, /*count*/2, 0, 19 },
    [TYPEID_Vec_int] = { 24, 8, TYPEKIND_STRUCT, 0, TYPEFLAG_SPECIALIZED,
                          29, 2, 3, 0, /*display*/37 },
    /* ... */
};

TMPLGEN_REFLECT_STORAGE const TypeField RuntimeTypeFieldTable[] = {
    /* Point */
    { 25, TYPEID_float, 0, 0, 0, 0, 0 },
    { 27, TYPEID_float, 4, 0, 0, 0, 0 },
    /* Vec_int: size_t fields are whatever size_t is (unsigned long here) */
    { 46, TYPEID_int_ptr,        0, 0, 0, TYPEFIELD_COUNTED, /*count field*/4 },
    { 51, TYPEID_unsigned_long,  8, 0, 0, 0, 0 },
    { 55, TYPEID_unsigned_long, 16, 0, 0, 0, 0 },
};

/* enumerators of every reflected enum, grouped per enum, sorted by value */
TMPLGEN_REFLECT_STORAGE const TypeEnumerator RuntimeTypeEnumTable[] = {
    /* Kind */
    { 0, 74, 0 },
    { 1, 87, 0 },
};

/* ---- 4. compile-time operators ------------------------------------------ */
#define TYPEID(T)        TYPEID_##T                    /* no declaration needed */
#define typeinfo_of(T)   (&RuntimeTypeInfoTable[TYPEID_##T])

/* Project types join the dispatch through hooks that their sections define;
 * macros expand at the call, so a later section is still picked up. */
#ifndef TMPLGEN_TYPEID_TYPES_H
#define TMPLGEN_TYPEID_TYPES_H(...)
#endif
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define typeid_of(x) _Generic((x),             \
    int: TYPEID_int,                            \
    double: TYPEID_double,                      \
    Vec_int: TYPEID_Vec_int,    /* core: needs only builtins */ \
    TMPLGEN_TYPEID_TYPES_H(x)                   \
    default: TMPLGEN_REFLECT_UNKNOWN(x))
#elif defined(__GNUC__) || defined(__clang__)
#define typeid_of(x) /* balanced __builtin_choose_expr tree, see §6.4 */
#else
#error "typeid_of(x) needs C11 _Generic or GNU builtins; use TYPEID(T)"
#endif

#define typeinfo(x)      (&RuntimeTypeInfoTable[typeid_of(x)])

/* ---- 5. Any -------------------------------------------------------------- */
typedef struct Any { void *data; TypeId type; } Any;
#define any_of(x)        ((Any){ (void *)&(x), typeid_of(x) })        /* lvalues */
#define any_val(x)       ((Any){ (void *)&(__typeof__(x)){ (x) }, typeid_of(x) }) /* C23 / GNU */
#define typeinfo_any(a)  (&RuntimeTypeInfoTable[(a).type])

/* ---- 6. inline helpers (static inline, no linkage) ----------------------- */
static inline const char *type_string(uint32_t off) { return RuntimeTypeStrings + off; }
static inline const char *type_name(TypeId id) { return type_string(RuntimeTypeInfoTable[id].display); }
static inline Any any_field(Any v, const TypeField *f)
{ Any r = { (char *)v.data + f->offset, (TypeId)f->type }; return r; }
#define TYPEINFO_FOR_EACH_FIELD(ti, f) \
    for (const TypeField *f = RuntimeTypeFieldTable + (ti)->first, \
         *f##_end = f + (ti)->count; f < f##_end; ++f)

#endif /* TMPLGEN_REFLECTION_H */

/* ======== sections: one per requirement key ================================ */
/* Same keys as the template modules: types.h declares Point and Score, so its
 * section also covers Vec_Point and Pair_Score_double, whose own template
 * sections were activated by the all.gen.h include above. */
#if defined(TYPES_H) && !defined(TMPLGEN_REFLECT_HAVE_TYPES_H)
#define TMPLGEN_REFLECT_HAVE_TYPES_H
/* The consumer's compiler checks libclang's numbers wherever the type is
 * visible; a mismatch means tmplgen ran with different target flags. */
_Static_assert(sizeof(Point) == 8 && _Alignof(Point) == 4 &&
               offsetof(Point, x) == 0 && offsetof(Point, y) == 4,
               "tmplgen: reflected layout of Point differs from this compiler/target");
_Static_assert(sizeof(Score) == 4 && offsetof(Score, value) == 0,
               "tmplgen: reflected layout of Score differs from this compiler/target");
_Static_assert(sizeof(Vec_Point) == 24 && offsetof(Vec_Point, len) == 8, "...");
#undef  TMPLGEN_TYPEID_TYPES_H
#define TMPLGEN_TYPEID_TYPES_H(...) Point: TYPEID_Point, Score: TYPEID_Score, \
    Vec_Point: TYPEID_Vec_Point, Pair_Score_double: TYPEID_Pair_Score_double,
#endif
```

There is no generated runtime library. The core is the whole API; anything
more (printing, serialization) is user code like the demo in §2.3, and that
code needs no project headers.

---

## 4. Integration architecture

### 4.1 Where it plugs into the existing pipeline

```
run_once (tmplgen.c:11405)
 └ generate (10313)
    ├ parse/analyze templates ─────────────── + R0: record template struct field *type expressions*
    ├ discover_source_usages (10139)
    │   ├ pass 1 per file: collect_clang_source_cursor (7951)
    │   │                     already records TypeOrigin (name → declaring file)
    │   │                     + R1: reflect_collect_decls(cursor)   ← same traversal, no extra parse,
    │   │                           including libclang layout numbers (§6.1)
    │   │                     + R2: typeinfo/typeid_of/any_of are registered as built-in generic
    │   │                           uses with dispatch argument 0 → existing ClangUse machinery
    │   ├ expand_dependencies_to_fixpoint (9824)
    │   ├ compute_instance_requirements (5685)  ← requirement keys for the bootstrap
    │   └ pass 2 per file ───────────────────── R2 resolves operator argument types (expression types)
    ├ validate_structural_constraints (7143)
    ├ compute_instance_requirements (5685)      ← final keys; reports unguarded headers etc.
    ├ emit modules + emit_aggregate_header (6626)
    └ + R3: reflect_finalize   (merge facts, close graph, specializations, assign ids, keys)
      + R4: reflect_emit       (gen/reflection.h [+ reflection.gen.c])
```

Reflection is **opt-in** (`--reflect=…`, §8). With it off, the pipeline is
unchanged and outputs are byte-identical.

### 4.2 Reusing existing machinery

The PROBLEM.md fix built most of what reflection needs to stay out of the
include-order trap:

| Need | Existing piece | Change |
| --- | --- | --- |
| Which header declares a type | `TypeOrigin` facts recorded in `collect_clang_source_cursor` (7951), `find_type_origin` (305) | None; reflection keys use the same origins |
| A header's guard macro, error reporting | `header_guard_for` (5397), `parse_include_guard` (5351), `generated_include_before` (5453) | None |
| Requirement keys for an instance's arguments | `compute_instance_requirements` (5685), `add_spelling_requirements` (5592) | Generalize from instance arguments to any reflected node (fields, pointees) |
| Guarded sections | `buf_put_section_open` (6489), `collect_module_requirements` (6393) | Reuse for reflection sections |
| Dispatch hooks for `_Generic`/GNU | `build_dispatch_plan` (6010), `emit_hook_default` (6092), `emit_hook_definition` (6102), `emit_dispatch_section` (6223) | Generalize "association target" from function name to an integer constant (`TYPEID_*`) |
| Find expression type at `typeinfo(x)` | `ClangUse` argument-range + `collect_clang_expression_type` (8168) | Register the 3 operators as pseudo generic functions |
| No-match error | `emit_no_match_declaration` (5802) | Same pattern for `TMPLGEN_REFLECT_UNKNOWN` in strict mode |
| Field enumeration | `register_structural_schema` (7083), names only | Replace with the richer `reflect_collect_record`; schemas become a *view* over the reflection graph |
| Name mangling | `type_suffix` (368) | Reused for `TYPEID_*`, with a collision pass (§5.4) |
| Per-file caching | `SourceFacts` + `.tmplgen-facts-*.cache` (now `TMPLGEN-FACTS-2`, with origins) | New binary `REFL` section (§7.6) |
| Write-if-changed | `write_file_if_changed` (1800) | Unchanged |
| `#line` for go-to-definition | `emit_line_directive` (4509) | Optional `#line` before each `TYPEID_` enumerator |

### 4.3 New-output checklist

An output must be registered in several places, plus two kinds of state
hygiene. The plan first folds these into **one `OutputRegistry`** so
reflection (and future outputs) cannot miss one:

1. `cache_expected_outputs` (10654)
2. `ensure_bootstrap_placeholders` (11346): libclang only applies an
   in-memory override to a file that exists, so `reflection.h` needs a
   placeholder before the first parse, like the template outputs
3. unsaved-file overrides in `parse_source_with_clang` (8299–8340), so
   `#include "gen/reflection.h"` sees the bootstrap text and not stale disk
   contents
4. the generated-file list in `generate` (10392–10407)
5. config hashing at 10496 and 10800 (`--reflect*` flags)
6. `clear_discovery_state` (10222) / `clear_requirement_state` /
   `reset_invocation_state` (11288), because the plugins call the core
   repeatedly

`is_generated_output_file` (1346) must also recognize `reflection.h` and
`reflection.gen.c`. Alternatively the generator can stamp every output with a
marker line and recognize that.

### 4.4 Bootstrap text during parsing

Like `emit_bootstrap_header` (8608), `gen/reflection.h` gets bootstrap
overrides while libclang parses sources, with the same core-plus-sections
layout as the real output (the template bootstrap already works this way):

- **Pass 1:** `TypeId` is `int`, `Any` is a struct, and the
  `typeid_of(x)`/`typeinfo(x)`/`any_of(x)` macros expand so the argument
  expression is visible: `((void)(x), 0)`. `TYPEID_*` names are emitted from
  the *previous* run's id list, with a `TYPEID__tmplgen_any` catch-all macro, so
  `case TYPEID_Point:` does not stop the parse with errors (`KeepGoing` already
  tolerates them).
- **Pass 2:** the real enum and the dispatch with hooks, defined in sections
  keyed like the output. A translation unit that lacks a type therefore parses
  cleanly instead of failing on an unknown association type. That failure mode
  produced spurious "cannot infer" warnings in the template bootstrap before it
  was changed the same way.

---

## 5. The type model

### 5.1 Which types are reflected (`--reflect=` modes)

| Mode | Root set | Typical size |
| --- | --- | --- |
| `none` (default) | nothing, no output | — |
| `used` | types observed in `typeinfo`/`typeid_of`/`any_of` + closure over fields | smallest |
| `annotated` | `// @reflect` declarations + `used` + closure | small |
| `project` | every record and enum **declared in a scanned file** + closure | medium |
| `all` | `project` + every complete type visible from system headers | large; debugging only |

*Closure* means: follow field types, pointee and element types, enum
underlying types, typedef targets, and function-pointer signatures until the
set stops growing. Types reached from outside the source root are reflected as
**external** nodes:

- `--reflect-external=opaque` (default): name, size, and align only. `FILE`,
  `pthread_mutex_t` and similar stay opaque and safe.
- `--reflect-external=full`: fields as well.

The builtin scalar set is always present and always has the same ids, so
small-integer `TypeId`s mean the same thing in every project.

### 5.2 Type identity: hash-consed nodes, not strings

The core today identifies types by strings (spelling and canonical spelling),
found by linear `str_eq` scans. Reflection needs a real type graph, so it uses
**structural hash-consing**:

```
node key = (kind, a, b, c)               all u32
  builtin        (BUILTIN, scalar, 0, 0)
  pointer        (POINTER, pointee_node, quals, 0)
  array          (ARRAY,   elem_node,  length_lo, length_hi)
  function       (FUNC,    ret_node,   param_list_id, variadic)
  record/enum    (RECORD|UNION|ENUM, usr_string_id, 0, 0)
  specialization (RECORD,  template_index, arg_tuple_id, 0)
```

Typedefs never become nodes: collection resolves them to their target first
(`clang_getCanonicalType`), as §5.3 explains.

- Records and enums are identified by **USR** (`clang_getCursorUSR`). USRs are
  stable across TUs (`c:@S@Point`, `c:@SA@Point` for typedef'd anonymous
  structs) and handle tag-namespace corner cases. For example, `struct node`
  and an unrelated `typedef struct {…} node` stay distinct.
- Derived types are never spelled as strings to find them; they are looked up
  in the node hash table using child node ids. Display strings are generated
  lazily from the graph, only for emitted nodes.
- One open-addressing hash table (`u64 key hash → u32 node`) handles inserts
  and lookups in O(1).

### 5.3 Typedefs are names, not types

Reflection follows C's own rule: **one `TypeId` per actual C type, and a
typedef is just a name.**

- `typedef int Count;` adds no id. A `Count` value, a `Count` field, and a
  `Count *` all reflect as `int`, `int`, and `int *`.
- `size_t`, `int64_t`, and similar typedefs are the same. They reflect as
  whatever type they are on the target (`unsigned long`, …).
- A typedef that names a record or enum is that type's name:
  `typedef struct { … } Point;` and `typedef struct Point Point;` give
  `TYPEID_Point`. A second name for the same record (`typedef Point Vec2;`)
  adds nothing.
- There is nothing extra to learn: what `_Generic` sees, `typeid_of` returns,
  field metadata records, and `switch` matches are all the same type.

**A distinct type is a struct.** This is the standard C way to make a name
that the type system (and therefore reflection) can tell apart:

```c
typedef int Count;                     /* a name for int: reflects as int        */
typedef struct { int value; } Score;   /* a distinct type: TYPEID_Score, its own
                                          row, its own case label, dispatchable   */
```

The wrapper costs nothing at runtime (same size and alignment as the `int`,
passed the same way by common ABIs). It gets everything a struct gets:
`any_of(s)` → `TYPEID_Score`, its own `case`, and the demo printer shows
`{ .value = 120 }`.

**Why a typedef can't be told apart.** `typedef int Score;` is identical to
`int`, and so is every type built from it (`Score *`, `Score (*)(void)`,
`Score[4]`). C11 §6.5.1.1p2 forbids compatible types in one generic selection.
Checked with GCC 13 and Clang 18:

| Attempt | Result |
| --- | --- |
| `_Generic(x, int: …, Score: …)` | error: compatible types (both compilers) |
| function-pointer wrapper: `int (*)(void)` vs `Score (*)(void)` | error: compatible types |
| `int (*)[1]` vs `Score (*)[1]`, `int **` vs `Score **` | error: compatible types |
| `__builtin_types_compatible_p(Score (*)(void), int (*)(void))` | `1` |
| list only `Score (*)(void)`, pass a plain `int` | **selects `Score`**, so plain ints get mislabeled |

The generator *could* track typedef spellings from declarations, but
expressions would still lose them. That gives two answers for "what type is
this", plus helper macros to bridge them. Keeping one rule is simpler.

Template dispatch already works this way: `build_associations` (5115) dedupes
by canonical type. `Vec(Count)` and `Vec(int)` are the same specialization.

**Qualifiers are not part of `TypeId`.** `_Generic` drops them from the
controlling expression (lvalue conversion, C17 DR 481). They live in
`TypeField.quals`. Pointers keep *pointee* qualifiers in the key because
`const char *` and `char *` are distinct types.

### 5.4 `TYPEID_*` naming

- Reuse `type_suffix` (306): `char *` → `char_ptr`, `unsigned long` →
  `unsigned_long`, `Vec_int` stays as is.
- Tag-only records get a `struct_` / `union_` / `enum_` prefix.
- Anonymous nested records get a path name: `Outer__inner`.
- Array names: `int[4]` → `int_arr4`.
- **Collisions** are detected after naming (a sort plus adjacent compare).
  The later key gets a deterministic `_h<8 hex of USR hash>` suffix and a
  verbose note.
- `--reflect-prefix=FOO_` renames every generated identifier
  (`FOO_TypeId`, `FOO_TYPEID_int`, …) for libraries that ship reflection.

### 5.5 Kinds and their metadata

| Kind | `first`/`count` | `inner` | Notes |
| --- | --- | --- | --- |
| BOOL/INT/FLOAT | — | — | Interpreted by `size` plus `TYPEFLAG_SIGNED`, from libclang and checked by the compiler (§6.1) |
| POINTER | — | pointee | User code decides how to treat `char *` (the demo prints it as a string) |
| ARRAY | count = length | element | Arrays decay in `typeid_of(x)`; reachable through fields and `TYPEID(T)` |
| STRUCT/UNION | fields | — | Anonymous members are flattened with path offsets; checked with `offsetof(Outer, inner.x)` |
| ENUM | enumerators | underlying int | Values come from libclang and are checked against `Color_Red` in the section; dense detection at generation time |
| FUNCTION | params (in `RuntimeTypeIdPool`) | return | Only reached through function-pointer fields |
| OPAQUE | — | — | Incomplete or external types; size 0 if incomplete |

**Bitfields:** `offsetof` cannot name them. The bit offset comes from
`clang_Cursor_getOffsetOfField`, and the width from
`clang_getFieldDeclBitWidth`, like every other layout number (§6.1). The
section check covers the record's size and its non-bitfield members; bit
positions themselves cannot be checked by a constant expression.
Accessing them at runtime is left to user code; the demo skips them.

**Counted pointers:** `// @reflect-count len` (or clang's
`__counted_by(len)` where libclang exposes it) marks `data` as pointing to
`len` elements, so user code can walk a `Vec(T)`.

---

## 6. Generated-code design (consumer speed)

### 6.1 Layout numbers come from libclang; the compiler verifies them

*(Revised. The original plan emitted `sizeof(T)` / `offsetof(T, f)`
symbolically. That puts every project type in the table's initializer, so the
table could only be compiled where every type is declared: the PROBLEM.md
trap. A per-TU table built from guarded sections would have missing rows
for types the TU never saw, and an `Any` passed in from another TU would
then read zeroes.)*

The generator asks libclang for each reflected type's layout during the
collection it already does (§7.3): `clang_Type_getSizeOf`,
`clang_Type_getAlignOf`, `clang_Type_getOffsetOf` (bits), plus enumerator
values. The tables are then **plain integers**:

- They name no project type, so they are complete in every translation unit.
  The header-only mode is fully usable, and a TU that never includes
  `player.h` can still walk a `Player` it receives as an `Any`.
- `reflection.h` includes no project header, so there is no include order to
  get right and no include conflicts (old Q3).
- They are constant initializers in `.rodata` with no runtime init and no
  relocations.

The numbers are **verified by the consumer's compiler** in the guarded
sections (§3). Each section emits, for the types its header declares, one
`_Static_assert` comparing `sizeof`, `_Alignof`, `offsetof` and enumerator
values with the table:

```c
_Static_assert(sizeof(Point) == 8 && _Alignof(Point) == 4 &&
               offsetof(Point, x) == 0 && offsetof(Point, y) == 4,
               "tmplgen: reflected layout of Point differs from this compiler/target");
```

- A mismatch can only come from libclang seeing a different target or
  different macros than the compiler: a missing `--clang-arg=--target=…`,
  `-m32`, `-fshort-enums`, `#pragma pack`, or a header whose layout depends on
  a per-TU macro. It fails the build in every TU that sees the type; it is
  never silently wrong data where a check runs.
- Every TU that includes a type's header checks it. A build in which *no* TU
  includes the header has nothing to check, which is acceptable: no TU
  can create such a value either. `--compiled` adds a guaranteed check (§6.6).
- The checks use the same macros as structural constraints:
  `_Static_assert` in C11, `__extension__ _Static_assert` in GNU C99, and a
  negative-array typedef in strict C99.
- The bitfield target guard planned in §5.5 becomes one more case of this
  rule rather than a special mechanism.

### 6.2 Flags come from the same numbers

`NO_PADDING` is computed by the generator from libclang's field sizes and
offsets, and it is verified as a side effect of the layout check above.
`POINTER_FREE` is purely structural, so the generator computes it during
closure. `POINTER_FREE && NO_PADDING` tells user code it may copy, compare,
or write the object as raw bytes.

### 6.3 Strings: one pool, offsets everywhere

- All names go into **one** `char` array. Records hold `uint32_t` offsets, not
  `const char *`.
- Arrays of pointers in PIC/PIE need one dynamic relocation per pointer at load
  time and land in `.data.rel.ro`. Offsets need none.
- One literal is much cheaper for the compiler to process than thousands of
  small ones.
- For MSVC's 64 KB literal limit, large pools switch to a brace-enclosed
  `char` initializer.
- `"\0" "1abc"` style splits keep escapes unambiguous.

### 6.4 Dispatch cost

`typeid_of(x)` expands the whole association list at every use, so the list
length matters:

- **Observed-use dispatch (default).** Associations exist only for types
  actually seen as `typeid_of`/`typeinfo`/`any_of` arguments anywhere in the
  project. This is the same rule tmplgen already uses for function dispatch,
  and the lists stay tiny. `--reflect-dispatch=all` opts into every
  nameable type.
- **Unknown types:**
  - `strict` (default): a deliberate compile error, as with
    `max__no_match_for_argument_type`. In the editor the clangd plugin has
    already regenerated before the AST build, so this only shows up in stale
    standalone builds.
  - `--reflect-unknown=invalid`: yields `TYPEID_INVALID` instead.
- **The GNU backend** cannot use a linear `__builtin_choose_expr` chain for
  large lists. Clang's default `-fbracket-depth` is 256. The generator emits a
  **balanced tree**:

  ```
  choose_expr(compat(x, A) | compat(x, B), choose_expr(compat(x, A), A_ID, B_ID), …)
  ```

  This gives O(log n) nesting depth, at the cost of O(n log n) tests.
- **`TYPEID(T)` / `typeinfo_of(T)`** are pure token pastes with no dispatch
  cost. They are recommended inside hot macros and generic code.
- **Types not visible in the TU.** An association's type must be declared
  where the macro expands. Associations for project types are therefore added
  by hook macros defined in each requirement's section (§3), the same way as
  template dispatch (`build_dispatch_plan`, 6010). `reflection.h` includes no
  project header. `typeid_of(x)` on a type whose header was not included
  before `reflection.h` hits the strict unknown error *in that TU only*;
  including `reflection.h` again after the header fixes it.
- **Enums vs. their integer type (§10).** The "at most one compatible type
  per underlying integer type" rule is decided once at generation over the
  whole association set. Sections only add entries, so whatever subset of
  sections is active, the list never contains two compatible types.

### 6.5 Hot/cold split

`RuntimeTypeInfoTable` holds fixed 32-byte records, two per cache line, and
`RuntimeTypeFieldTable` holds every struct's fields back to back, so walking a
struct's fields is one linear scan. Enumerators are in their own small table,
so they are only touched by code that needs them. Lookup by name
(field or enumerator) is a linear scan over a type's own entries, which is
fine for the demo. If a real use case needs it later, a parallel name-hash
array can be added without changing the record layout.

### 6.6 Header-only vs compiled mode

This mirrors tmplgen's existing modes. Both need no project headers in
`reflection.h`.

- **Header-only (default):**
  - `static const` integer tables in `reflection.h`, complete in every TU.
  - Simple to use, and unused tables are discarded when nothing references
    them.
  - Every TU that includes the header compiles the tables (integers only) and
    the layout checks for the headers it includes.
  - A layout change (adding a field) changes the numbers, so every includer
    rebuilds.
- **`--compiled`:**
  - `reflection.h` holds the enum, schema types, `extern const` table
    declarations, and dispatch with hook sections. By default the sections
    contain only hooks, no layout numbers.
  - `reflection.gen.c` holds the table definitions. Being a single generated
    TU, it also includes **every** declaring header and runs **every** layout
    check, so each reflected layout is verified at least once per build.
  - `--reflect-verify=everywhere` also puts the checks in the header sections,
    to catch per-TU layout differences (config macros) at the cost of the
    header changing with the layout.
  - Large projects should use this. Changing a field then recompiles one TU
    instead of every includer, because the enum and dispatch are unchanged.

`write_file_if_changed` together with this split keeps rebuild cascades
minimal. Editing a function body never touches either file, and editing a
field only touches `reflection.gen.c` in compiled mode.

---

## 7. Generator design (generation speed, data-oriented)

### 7.1 Prerequisite refactor (M0)

The core today allocates every string with `malloc`, finds things by linear
scans, and stores data as arrays of structs. Reflection multiplies the number
of entities (every field of every project type), so the plan first adds three
small, reusable primitives, which existing paths can also adopt over time:

```c
typedef struct { char *base; size_t used, cap; } Arena;            /* bump allocator, reset per run */
typedef struct { uint32_t *slots; uint32_t cap; } HashIndex;       /* open addressing, u64 → u32 */
typedef struct { Arena bytes; uint32_t *offsets; uint64_t *hashes; uint32_t count, cap;
                 HashIndex index; } StrPool;                       /* interned strings → u32 ids */
```

- `StrPool` ids are also the emission order of `RuntimeTypeStrings`, so string
  offsets come for free.
- The hash is the existing FNV-1a (`signature_mix`, 9318), or wyhash for speed.

### 7.2 Graph storage (SoA)

```c
typedef struct {
    /* nodes: parallel arrays, indexed by node id */
    uint8_t  *kind;     uint8_t *scalar;  uint32_t *flags;
    uint32_t *a, *b, *c;                   /* structural key (§5.2) */
    uint32_t *name;                        /* StrPool id of C spelling  */
    uint32_t *display;                     /* StrPool id of pretty name */
    uint32_t *first, *count;               /* into fields / enumerators */
    uint32_t *decl_file;                   /* header that declares it   */
    uint64_t *stable_hash;
    uint32_t  node_count, node_cap;
    HashIndex node_index;                  /* key hash → node id */

    /* fields: parallel arrays */
    uint32_t *f_name, *f_type, *f_bit_offset, *f_attr;
    uint16_t *f_bit_width; uint8_t *f_quals, *f_flags;
    uint32_t *f_path;                      /* StrPool id of "a.b" for offsetof */
    uint32_t  field_count, field_cap;

    /* enumerators, param lists, attributes: same pattern */
} ReflectGraph;
```

The arrays are separate because:

- Closure only touches `kind`, `a`, and `b`.
- Naming only touches `name`, `kind`, and `a`.
- Emission walks everything once in id order.

Each pass reads only the arrays it needs, all contiguous.

### 7.3 Collection (R1): free-riding on the existing parse

In `collect_clang_source_cursor` (6939), pass 1:

- **Ownership rule.** Collect a declaration only when its location is in the
  **TU's main file**.
  - Headers are already parsed as their own source units, so every project
    declaration is collected **exactly once** across the project. Cost is
    O(declarations), not O(TUs × included headers).
  - Types from outside the source root are not collected eagerly. They become
    external nodes when a field references them (§5.1).
- Use `clang_Type_visitFields` for fields. It visits only fields and is cheaper
  than `clang_visitChildren`.
- Per record: `clang_Type_getSizeOf` / `clang_Type_getAlignOf` (§6.1); the
  declaring file and its requirement key come from the existing
  `TypeOrigin` facts.
- Per field:
  - `clang_getCursorType`
  - `clang_Cursor_getOffsetOfField` (bits), `clang_Cursor_isBitField` /
    `clang_getFieldDeclBitWidth`
  - `clang_Cursor_isAnonymousRecordDecl`, then recurse with a path prefix
- Type → node is iterative over the type structure:
  - `clang_getCanonicalType`, `clang_getPointeeType`,
    `clang_getArrayElementType`/`clang_getArraySize`,
    `clang_getResultType`/`clang_getArgType`
  - `clang_getTypeDeclaration`, then `clang_getCursorUSR`, for records and
    enums
  - `clang_Type_getNamedType` to strip elaborated types
  - The `clang_is*QualifiedType` family for qualifiers
  - Each step is a hash-cons lookup; spellings are not built.
- Enums: `clang_getEnumDeclIntegerType` and
  `clang_getEnumConstantDeclValue`/`UnsignedValue`.
- Annotations: `clang_Cursor_getRawCommentText` on the declaration or field,
  parsed with the existing `// @` directive tokenizer (`parse_directive` 3282)
  extended with `reflect*` directives.
- **Shape hash per file:** a 64-bit hash over the collected declarations
  (names, field names, field node keys, annotations), *not* over the file
  text. See §7.6.

### 7.4 Template specializations (R0)

While sources are parsed, `Vec_int` is a placeholder struct (pass-2 bootstrap,
`emit_bootstrap_header`, 8608), so libclang cannot report its fields or
layout. With layout numbers coming from libclang (§6.1), the fields alone are
not enough: the table needs `Vec_Point`'s real size and offsets.

- **Declaration-only re-parse (primary).**
  - After emission, parse one synthetic TU that includes the declaring header
    of every requirement key (paths from `header_path_for_guard`), then
    `gen/all.gen.h`. Every section is then active, so every specialization
    has its real definition. Use `CXTranslationUnit_SkipFunctionBodies`.
  - Read fields and layout of the records whose names match generated
    instances.
  - Cache the result by the content hash of the generated headers and the
    included declaring headers.
  - One extra parse, and only when the specialization set or an argument
    type's layout changes. `--compiled`'s `reflection.gen.c` is the same TU
    shape, so the plan reuses it.
- **Symbolic instantiation (later, fields only).**
  - During `analyze_template` (3180), record each template record's fields as
    *type expressions* over parameter indices, and substitute per
    `GenInstance`.
  - This gives fields and type graph edges without a parse, but not layout.
    Reimplementing the target's struct layout rules in the generator is
    explicitly out of scope, so the re-parse stays for numbers.

### 7.5 Finalize (R3): linear passes over flat arrays

1. **Merge per-file facts.** Each file's facts carry their own local node and
   string ids. Remap them into the global graph with a `local → global` array,
   one hash-cons lookup per node. Linear.
2. **Root set** from the mode, then **closure** with a worklist (`u32` stack
   plus a `seen` bitset).
3. **Specializations** (§7.4).
4. **Flags:** compute `POINTER_FREE`, `HAS_BITFIELDS`, `FLEXIBLE`, and
   `ENUM_DENSE` in reverse topological order. Pointer edges break cycles
   (self-referential lists), so the record graph without pointer edges is a
   DAG.
5. **Assign ids** deterministically:
   - builtins first, in fixed order;
   - then kind class (records/enums, then derived);
   - then `stable_hash`, with name as tie-breaker.

   This is a radix or `qsort` over `(u64 key, u32 node)` pairs. Equal inputs
   always give byte-identical output.
6. **Naming plus collision pass** (§5.4).

Each step reads and writes contiguous arrays. Nothing allocates per node except
amortized array growth.

### 7.6 Caching and incrementality

- **Facts format.** The existing facts file is text with hex-encoded strings,
  which is fine for small data but slow for type graphs. Add a **binary `REFL`
  section**: a header with counts, then the raw SoA arrays and the string pool
  bytes, written with `fwrite` and read with one `fread` (or `mmap`). Bump
  `TMPLGEN-FACTS-1` → `-2`.
- **Two keys per file:**
  - `refl_shape_hash`: the shape hash of the collected declarations (§7.3).
  - `refl_uses_hash`: the hash of the operator argument types seen in the file.
- **Output skipping:**
  - If no file's `refl_shape_hash` changed, the type graph and the tables are
    unchanged.
  - If additionally no `refl_uses_hash` changed, the dispatch is unchanged.
  - In the common case, editing function bodies, R3 and R4 are skipped
    entirely and the previous `reflection.h` stands.
- **Manifest:** `reflection.h` and `reflection.gen.c` join
  `cache_expected_outputs` (9538) so tampering and staleness are detected like
  the other outputs.
- **Plugin cache fix (prerequisite).** In plugin builds `g_executable_path` is
  NULL, so `compute_input_signature` disables both caches (9392). Every clangd
  rebuild currently re-analyzes everything. Fix this by having the plugin pass
  its own `.so` path (or a build-id string) as the executable identity. This
  already matters today and is essential before adding reflection.

### 7.7 Emission (R4)

- Size the output first: count the bytes per section from the arrays, then
  `buf_reserve` once.
- Write sections in a fixed order: ids, schema, strings, types, fields,
  enumerators, dispatch, inline accessors.
- Numbers go through `buf_put_uint` (761). Names come from the `StrPool`.
  Nothing is formatted with `printf` in the hot loop.
- With `--reflect-line`, `#line` directives map `TYPEID_*` enumerators to their
  declarations, so go-to-definition on an enumerator lands on the struct. This
  is the same trick dispatch macros use today.

### 7.8 Performance budget (targets to measure, not claims)

| Scenario | Target |
| --- | --- |
| `--reflect=project`, cold, 1k records / 10k fields | ≤ 10% over today's cold run (no extra parses) |
| Warm, body-only edit | R3/R4 skipped; ≤ 1 ms of reflection work |
| Warm, one struct edited | O(changed file + graph merge); ≤ 20 ms at 10k fields |
| `reflection.h` compile, 1k types, header-only | measure; target < 50 ms per includer at `-O0` |
| `typeid_of(x)`, observed-use dispatch | indistinguishable from a handwritten `_Generic` |

`test/bench` gets a synthetic generator for N records × M fields, plus
recorded results for every milestone.

---

## 8. CLI and configuration

| Flag | Plugin env (clangd) | Meaning |
| --- | --- | --- |
| `--reflect=none\|used\|annotated\|project\|all` | `TMPLGEN_REFLECT` | root set (§5.1) |
| `--reflect-output FILE` | `TMPLGEN_REFLECT_OUTPUT` | default `<gendir>/reflection.h` |
| `--reflect-external=opaque\|full` | `TMPLGEN_REFLECT_EXTERNAL` | depth for types outside the root |
| `--reflect-dispatch=used\|all` | `TMPLGEN_REFLECT_DISPATCH` | association set (§6.4) |
| `--reflect-unknown=error\|invalid` | `TMPLGEN_REFLECT_UNKNOWN` | default association |
| `--reflect-prefix=PFX` | `TMPLGEN_REFLECT_PREFIX` | identifier prefix |
| `--reflect-line` | — | `#line` per enumerator for editor navigation |
| `--reflect-verify=sections\|everywhere` | `TMPLGEN_REFLECT_VERIFY` | where layout checks go in `--compiled` mode (§6.6) |

The Clang plugin accepts the same keys as `-fplugin-arg-tmplgen-reflect=project`
and so on. `--dispatch` and `--compiled` apply to reflection exactly as they
do to templates. All flags are mixed into the config strings at 9380 and 9684.

---

## 9. Plugin integration (first-class editor experience)

### 9.1 Shared core

Both plugins already call `tmplgen_plugin_main` through `runTmplgen`
(`plugin/tmplgen_plugin_support.hpp:204`), so reflection works in them as soon
as the core supports it. The clangd plugin regenerates in `beforeExecute`
(`clangd_plugin.cpp:2521`) using the current unsaved buffer. Because of the
shape hash (§7.6), keystrokes inside function bodies never rewrite
`reflection.h`, and clangd does not invalidate preambles that include it.

### 9.2 A small query API from C core to C++

The clangd plugin re-derives everything from the AST today because the core
exports no queries. Reflection features want the graph, so the plan adds a
read-only C API that the core fills on each run, guarded by the existing
generator mutex:

```c
/* tmplgen_plugin_api.h */
typedef struct { const char *name; const char *display; unsigned kind, id; } tmplgen_type_view;
size_t tmplgen_reflect_type_count(void);
int    tmplgen_reflect_type(size_t index, tmplgen_type_view *out);
int    tmplgen_reflect_lookup(const char *usr_or_name, tmplgen_type_view *out);
size_t tmplgen_reflect_fields(unsigned id, const char **names, unsigned *types, size_t cap);
```

### 9.3 clangd features

| Feature | Behavior |
| --- | --- |
| **Hover** on `typeinfo(x)`, `any_of(x)`, `TYPEID_*` | id, kind, size/align (from the AST's layout), field table with offsets, flags (`POINTER_FREE`, `NO_PADDING`), and whether the type is in the dispatch set |
| **Go-to-definition** on `TYPEID_Point` / `typeinfo_of(Point)` | jumps to the struct declaration (`#line` mapping or query API) |
| **Completion** of `case TYPEID_` | all ids; the dispatchable ones ranked first |
| **Diagnostics** | `typeid_of(x)` on an enum value (§10), on an anonymous type, or on a type declared in a `.c` file, each with a fix-it suggesting `TYPEID(T)` or moving the type to a header |
| **Inlay hint** (optional) | `/* TYPEID_Point */` after `any_of(p)` |

### 9.4 Types declared in `.c` files

Same rule as templates: a type declared in a `.c` file has no include guard
that the generated code could test, so it is not reflected. Unlike templates
this is not an error: TU-private types are skipped, and `typeid_of(x)` on one
gets a clangd diagnostic. With libclang layout (§6.1) a TU-private type's row
*could* exist, but it could never be checked or dispatched to outside that
file. v2 can emit per-TU local sections (`gen/local/<file>.refl.h`, an
`#include` at the end of that `.c` file) with ids in a separate range,
`TYPEID_LOCAL_BASE + n`. This is an explicit opt-in because those ids are not
globally meaningful.

### 9.5 Clang frontend plugin

It runs the same generation before the main TU parses. No extra features are
needed. Optionally, it could verify the libclang bitfield layout against
`ASTContext::getASTRecordLayout` and warn on a mismatch.

---

## 10. Pitfalls this design addresses

1. **Enums collide with their underlying integer type in `_Generic`.** An enum
   type is compatible with an implementation-defined integer type (`unsigned
   int` for `enum { A, B }` on GCC and Clang). Checked with GCC 13 and Clang 18:
   - `_Generic(x, unsigned int: …, enum Color: …)` is rejected ("two compatible
     types").
   - `_Generic(x, int: …, enum Color: …)` is accepted, and an `enum Color`
     value selects `enum Color`.
   - Two enums with the same underlying type can both be listed, but then an
     `unsigned int` argument matches both and is rejected.

   Rule for the generator: **for each underlying integer type, the dispatch
   list contains at most one type compatible with it.** The underlying type
   comes from `clang_getEnumDeclIntegerType`. When several observed types
   compete (say `enum Color`, `enum Shape`, and `unsigned int`), the plain
   integer type wins and the enums are left out of dispatch. In that case
   `typeid_of(color)` yields `TYPEID_unsigned_int`, and the clangd plugin
   flags the call and suggests `TYPEID(enum_Color)`. Field metadata and
   `TYPEID(T)` always keep the enum's identity. C23 fixed-underlying enums
   follow the same rule.
2. **Typedefs are not distinct types** (`Count` vs `int`, `size_t` vs
   `unsigned long`). C cannot tell them apart in any type-selection construct
   (§5.3 table), so reflection doesn't try. Use a one-member struct for a
   distinct type.
3. **Qualifiers and decay:** `const Point` dispatches to `TYPEID_Point`, and
   `int[4]` dispatches to `TYPEID_int_ptr`. `TYPEID(T)` keeps array identity.
4. **`any_of(rvalue)`:** `any_of` requires an lvalue. `any_val` copies into a
   compound literal (C23 `typeof` or GNU `__typeof__`) that lives until the end
   of the enclosing block.
5. **Const-correctness of `Any`:** `void *data` (as requested) drops `const`.
   A parallel `AnyConst { const void *data; TypeId type; }` and
   `any_of_const(x)` cover read-only use such as printing.
6. **Bitfields:** they have no address, so `any_field` cannot point at them.
   They are flagged `TYPEFIELD_BITFIELD` with a bit offset and width. The
   section's layout check covers the record's size (§5.5). The same bitfield
   bug exists in today's structural `__typeof__` asserts; fix both together.
7. **Flexible array members:** `TYPEFLAG_FLEXIBLE`, with element count 0 in the
   table. Walking them needs `// @reflect-count`.
8. **Unions:** reflected and iterable. The table cannot know which member is
   active, so the demo prints and parses only the first one.
9. **Id stability:** dense ids shift when types are added. **Never persist raw
   `TypeId`s.** Store type or field *names* (as the demo format does), or the
   64-bit `stable_hash` of the USR/structural key. `--reflect-id-lock
   gen/typeids.lock` (v2) keeps an append-only id assignment for wire
   protocols.
10. **ABI and target:** the table's numbers come from libclang (§6.1), so
    cross-compiling users must pass the matching `--clang-arg=--target=…`
    (and flags such as `-m32` or `-fshort-enums`). A mismatch is not silent:
    the section checks fail in every TU that sees an affected type.
11. **Header inclusion conflicts:** gone. `reflection.h` includes no project
    header; it only includes the generated `all.gen.h` (§3).
12. **Include order:** the rules from `doc/usage.md` apply unchanged. A TU
    must include `reflection.h` after the headers whose types it names in
    `typeid_of`/`any_of` (or include it again). A TU that only *reads*
    `Any` values and tables needs no project headers at all.
13. **Per-TU layout differences:** a struct whose layout depends on a macro
    that differs between TUs (`#ifdef DEBUG` fields) breaks the "one table"
    assumption, exactly as it breaks C's own one-definition expectations.
    In header-only mode every TU that sees the type checks it, so such a
    project fails to build instead of reading wrong offsets.

---

## 11. Open questions

1. **`char *` fields:** should the generator mark them (for example a
   `// @reflect-string` annotation setting a field flag), or leave it to user
   code as the demo does?
2. **Header-only `static const` or `extern` by default?** The request says
   "static const global"; with integer tables this is now complete and
   correct in every TU. Large projects should still use `--compiled` for
   rebuild cost. Should `--reflect=project` imply compiled tables beyond some
   size threshold?
3. ~~**Dispatch visibility.**~~ Resolved by the PROBLEM.md convention:
   per-requirement sections with dispatch hooks, no project includes (§3,
   §6.4).
4. **Function reflection** (`FunctionInfo` for project functions): v2, or
   never?
5. **Naming:** keep the request's `RuntimeTypeInfoTable` / `TypeId` / `Any`
   unprefixed by default, or default to a `tmplgen_` prefix to avoid clashes
   in libraries?
6. **Verification coverage in compiled mode:** checks only in
   `reflection.gen.c` (header stable across layout changes), or also in
   every TU (`--reflect-verify=everywhere`, catches per-TU layout
   differences)? The plan defaults to the former.

---

## 12. Milestones

Each milestone ends with green `test/run.sh` and `test/negative.sh` (gcc and
clang), plus a golden `test/gen/reflection.h` checked with `--check`.

| # | Scope | Deliverables |
| --- | --- | --- |
| **M0** | Foundations | `Arena`, `StrPool`, `HashIndex`; `OutputRegistry` (§4.3); plugin cache identity fix (§7.6); fix bitfield `__typeof__` in structural asserts. *Already done by the PROBLEM.md fix:* bootstrap placeholders, type origins, include-guard detection, requirement keys, guarded sections, dispatch hooks |
| **M1** | Static tables | `ReflectGraph`; R1 collection for structs, unions, typedef'd records, pointers, arrays, builtins (other typedefs resolve to their target) **with libclang layout numbers**; closure; deterministic ids; `TypeId`, integer `RuntimeTypeInfoTable`, `RuntimeTypeFieldTable`, `RuntimeTypeStrings`; per-requirement sections with layout checks; `TYPEID(T)` / `typeinfo_of(T)`; `--reflect=project` |
| **M2** | Dispatch and `Any` | operators registered as built-in generic uses; observed-use `_Generic` / GNU balanced tree / auto / none through `build_dispatch_plan` hooks; strict unknown; `Any`, `AnyConst`, `any_of`, `any_val`, `any_field`; bootstrap overrides with sections (§4.4) |
| **M3** | Full type coverage | enums (value tables, the per-underlying-type dispatch rule from §10), bitfields, anonymous members, flexible arrays, function pointers, specializations via the declaration-only re-parse (§7.4), external opaque nodes, `--reflect=used\|annotated\|all` |
| **M4** | Demo | `examples/reflect_demo.c` with `reflect_print` and `reflect_parse` (§2.3); a test case that prints, parses the output back, and compares |
| **M5** | Incremental speed | binary `REFL` facts, shape and use hashes, R3/R4 skipping, `--compiled` split with checks in `reflection.gen.c`, symbolic field graph for specializations, benchmarks in `test/bench` |
| **M6** | Editor | query API (§9.2); clangd hover, definition, completions, diagnostics, inlay hints; `--reflect-line`; clang plugin layout verification |
| **M7** | Annotations | `@reflect`, `-ignore`, `-name`, `-count` |

### Test plan highlights

- **Golden outputs:** `test/gen/reflection.h` for the existing `test/` project
  with `--reflect=project`, in `generic`, `gnu`, and `none` dispatch modes.
- **Runtime cases** (`test/cases/reflect_*.c`, run under c99, c11, gnu99, and
  c23):
  - field iteration matches the declarations;
  - `offsetof` sums;
  - enum value ↔ name through `RuntimeTypeEnumTable`;
  - print → parse → compare round trip with the demo functions, covering
    nested structs, char arrays, fixed arrays, and enums;
  - a `switch` over `TypeId` with `-Wswitch -Werror`;
  - typedefs: a `Count` (`typedef int`) variable, field, and pointer reflect as
    `int`/`int *` with no extra ids; a wrapper `struct { int value; } Score`
    gets `TYPEID_Score`, its own `case`, and prints as `{ .value = … }`.
- **Partial includes** (the PROBLEM.md scenarios, as in `test/headers.sh`):
  - a printer TU that includes only `reflection.h` walks values of every
    project type it receives as `Any`;
  - a header that declares a type, includes `reflection.h` after it, and uses
    `typeid_of` on it;
  - including `reflection.h` before a type's header and again after it.
- **Negative cases:**
  - a layout mismatch (a TU compiled with a macro that changes a struct)
    fails the section check with the expected message;
  - an unknown type in strict mode, with the expected error text;
  - an enum and its underlying integer type both observed, which must
    still compile (§10);
  - `typeid_of(enum_value)` diagnostic in clangd when the enum was dropped
    from dispatch;
  - a TU-private type;
  - a forced id-name collision that resolves to the hashed suffix;
  - a target mismatch (`--clang-arg=-m32` with a native compile) caught by
    the section checks.
- **Cache:**
  - a body-only edit leaves `reflection.h` unchanged;
  - a field edit changes only the tables (`--compiled`);
  - a `--no-cache` run equals the cached run byte for byte.
- **Plugin smoke test:** the LSP script used to verify the clangd plugin opens
  a file that uses `typeinfo`, and expects zero diagnostics and a hover with
  the field table.
