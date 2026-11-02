# Reflection

`--reflect=project` makes tmplgen also write `gen/reflection.h`: a type id
(`TypeTag`) for every type in the project, and tables that describe each one's
size, alignment, fields, and enumerators. Code can then inspect any value at
run time with plain loops over those tables, without hand-written per-type
metadata.

```c
#include "player.h"
#include "gen/all.gen.h"        /* if you use templates */
#include "gen/reflection.h"     /* last */

Player player = { ... };
const TypeInfo *info = typeinfo(typetag(Player));
const TypeField *fields = RuntimeTypeFieldTable + info->first;
for (uint32_t i = 0; i < info->count; i++)
    printf("%s: %s @%u\n", reflect_str(fields[i].name),
           typestr((TypeTag)fields[i].type), fields[i].offset);
```

```text
pos: Point @0
hp: int @8
kind: Kind @12
...
```

- [Enabling it](#enabling-it)
- [Which types are reflected](#which-types-are-reflected)
- [The API](#the-api)
- [Type tags](#type-tags)
- [The tables](#the-tables)
- [Values: `Any`](#values-any)
- [Enums](#enums)
- [Layout checks](#layout-checks)
- [Include rules](#include-rules)
- [Reflection in templates](#reflection-in-templates)
- [Symbol prefix](#symbol-prefix)
- [`--compiled`](#--compiled)
- [Cost](#cost)
- [Errors and limitations](#errors-and-limitations)
- [Examples](#examples)

## Enabling it

| Front end | Setting |
| --- | --- |
| Command line | `tmplgen --reflect=project ...` (default `--reflect=none`) |
| Environment (plugins, editors) | `TMPLGEN_REFLECT=project` |
| Clang / clangd plugin argument | `reflect=project` (see [Plugin modes](plugins.md)) |

The output is `gen/reflection.h`, next to the aggregate header (`--output`).
It works without templates: a project with no `.tmpl.h` files can use
tmplgen only for reflection.

tmplgen reads the layout with libclang, so pass the same target and
preprocessor flags you compile with (`--clang-arg=-std=c11`,
`--clang-arg=-DFOO`, `--clang-arg=--target=...`). Your compiler checks the
sizes, alignments, field offsets, and enumerator values against its own (see
[Layout checks](#layout-checks)), so a mismatch there is a build error.
Bit-field positions are the exception: C cannot take their offset, so they are
libclang's numbers, unchecked.

## Which types are reflected

- Every struct, union, and enum **declared in a project header** (a header
  under `--source-root`, outside the generated directory), named by a typedef
  or a tag.
- Every **template specialization** (`Vec_int`, `Result_Dog_ErrorCode`) and
  the plain types a template declares (an enum such as `ResultTag`).
- The **builtin** arithmetic types, `void`, and `_Bool`.
- Everything reachable from those through fields: pointers, arrays, function
  types, anonymous structs and unions, and external types such as `FILE`
  (size only).
- `T *` for every nameable struct and union, so `typetag_of(&value)` has an
  id.

Types declared in `.c` files are not reflected (no other file could check
their layout). Fields of anonymous struct and union members are flattened into
the enclosing record, with their real offsets.

## The API

| | |
| --- | --- |
| `typetag(T)` | Id of a type *name*: `typetag(Player)`, `typetag(Kind)`, `typetag(struct_Node)`, `typetag(Vec_int)`, `typetag(const_char_ptr)`. Needs no declaration. A macro. |
| `typetag_of(x)` | Id of the type of the expression `x`, chosen at compile time. Not for enums (gives the integer type's id). A macro. |
| `enumtag_of(x)` | Id of the enum type of the expression `x`. A macro. |
| `any_of(x)` | `Any` pointing at the lvalue `x`: `{ &x, typetag_of(x) }`. A macro. |
| `typeinfo(id)` | `const TypeInfo *` for an id: size, alignment, kind, flags, fields or enumerators. |
| `typestr(id)` | Display name, e.g. `"Vec(Item)"`, `"const char *"`, `"char[16]"`. |
| `reflect_str(offset)` | A name from the string table (`TypeField.name`, `TypeEnumerator.name`, `TypeInfo.name`). |
| `any_field(value, field)` | `Any` for one field of the value in `value`. |
| `RuntimeTypeInfoTable` | One `TypeInfo` per id. |
| `reflect_meta(field)` | Pointer to field metadata object from `@meta`, or `NULL` if none. Requires `reflection.h` and the declaring headers. |
| `reflect_meta_type(field)` | `TypeTag` of the metadata type, or `TYPETAG_INVALID` if none. |
| `RuntimeTypeFieldTable` | All fields; a record's are `[first, first + count)`. |
| `RuntimeTypeEnumTable` | All enumerators; an enum's are `[first, first + count)`. |
| `RuntimeTypeStrings` | The string pool. |

All of it is plain C. Fields and enumerators are walked with ordinary loops;
there are no iteration macros.

## Type tags

`TypeTag` is an enum with one enumerator per reflected type:

```c
typedef enum TypeTag {
    TYPETAG_INVALID = 0,
    TYPETAG_void, TYPETAG__Bool, TYPETAG_char, /* ...builtins... */
    TYPETAG_Hero, TYPETAG_Role, TYPETAG_Vec_Item, TYPETAG_struct_Packed, /* ... */
    TYPETAG_Hero_ptr, TYPETAG_const_char_ptr, TYPETAG_char_arr16, /* ... */
    TYPETAG_COUNT
} TypeTag;
```

`typetag(T)` pastes `TYPETAG_` and its argument, so it costs nothing to
compile, needs no declaration of the type, and is a constant usable in `case`
labels. The argument is the type's *name* as an identifier:

| Type | Name |
| --- | --- |
| typedef name, e.g. `Hero` | `Hero` |
| tag only, `struct Packed` / `union U` / `enum E` | `struct_Packed`, `union_U`, `enum_E` |
| specialization `Vec(Item)` | `Vec_Item` |
| builtin with spaces, `unsigned long` | `unsigned_long` |
| pointer, `Hero *` / `const char *` | `Hero_ptr`, `const_char_ptr` |
| array, `char[16]` / `int[]` | `char_arr16`, `int_arr` |
| anonymous struct (only reachable through a field) | `struct_anon_<hash>` |
| function type | `fn_<hash>` |

A typedef name is preferred over a tag when the project declares one. If two
types would get the same name, the later one gets a hash suffix (`_h1a2b3c4d`)
and tmplgen prints a note.

`typetag_of(x)` instead picks the id from the static type of an expression,
with C11 `_Generic` or, before C11, GNU `__builtin_choose_expr` (GCC and
Clang). Qualifiers on the expression's type are ignored (`const Hero` gives
`Hero`'s id). An expression whose type has no id fails to compile with
`tmplgen_reflect_no_typetag_for_this_argument_type` in the message. Each use
expands the list of all types, so prefer `typetag(T)` where the type is known
(see [Cost](#cost)).

Ids are dense and deterministic: builtins first, then the nameable project
types sorted by name, then derived types. They change when types are added or
removed, so **never store a `TypeTag`** in files or across builds; store names
(`TypeInfo.name`, enumerator names) instead.

## The tables

```c
typedef struct TypeInfo {
    uint32_t size;
    uint16_t align;
    uint8_t kind;       /* TypeKind */
    uint8_t reserved;
    uint32_t flags;     /* TYPEFLAG_* */
    uint32_t name;      /* offset into RuntimeTypeStrings: "Vec_Item" */
    uint32_t first;     /* first field or enumerator */
    uint32_t count;     /* fields, enumerators, or array length */
    uint32_t inner;     /* TypeTag: pointee, element, underlying, or result */
    uint32_t display;   /* pretty name: "Vec(Item)" */
} TypeInfo;

typedef struct TypeField {
    uint32_t name;
    uint32_t type;      /* TypeTag */
    uint32_t offset;    /* bytes; bits for TYPEFIELD_BITFIELD */
    uint16_t bit_width;
    uint8_t quals;      /* TYPEQUAL_CONST, TYPEQUAL_VOLATILE, TYPEQUAL_RESTRICT */
    uint8_t flags;      /* TYPEFIELD_BITFIELD */
    uint32_t attr;      /* @meta index; 0 = none */
    uint32_t meta_type; /* @meta TypeTag; TYPETAG_INVALID = none */
} TypeField;

typedef struct TypeEnumerator {
    int64_t value;
    uint32_t name;
    uint32_t pad;
} TypeEnumerator;
```

What `first`, `count` and `inner` mean depends on `kind`:

| `kind` | `first`, `count` | `inner` |
| --- | --- | --- |
| `TYPEKIND_STRUCT`, `TYPEKIND_UNION` | fields in `RuntimeTypeFieldTable` | – |
| `TYPEKIND_ENUM` | enumerators in `RuntimeTypeEnumTable` | underlying integer type |
| `TYPEKIND_POINTER` | – | pointee |
| `TYPEKIND_ARRAY` | `count` = length (0 for `[]`) | element |
| `TYPEKIND_FUNCTION` | – | result type |
| `TYPEKIND_INT`, `FLOAT`, `BOOL`, `VOID` | – | – |
| `TYPEKIND_OPAQUE` | – (incomplete or external type, size only) | – |

Flags (`TypeInfo.flags`):

| Flag | Meaning |
| --- | --- |
| `TYPEFLAG_SIGNED` | signed integer, or an enum whose underlying type is signed |
| `TYPEFLAG_POINTER_FREE` | no pointers anywhere inside: safe to copy with `memcpy` and write to disk |
| `TYPEFLAG_NO_PADDING` | every byte belongs to a field: safe to compare with `memcmp` and hash bytewise |
| `TYPEFLAG_HAS_BITFIELDS` | has bit-field members |
| `TYPEFLAG_FLEXIBLE` | ends in a flexible array member |
| `TYPEFLAG_ANONYMOUS` | no name; reachable only through a field |
| `TYPEFLAG_SPECIALIZED` | a template specialization; `typestr` shows `Vec(Item)` |
| `TYPEFLAG_ENUM_DENSE` | the enumerator values are consecutive |

The tables are plain integers taken from libclang. They name no project type,
so they are complete in every translation unit, and code that only reads
values (a printer taking an `Any`, a serializer) needs no project headers.

By default the tables are `static const` in `reflection.h`; with
[`--compiled`](#--compiled) they are defined once in `reflection.gen.c`.

## Values: `Any`

```c
typedef struct Any { void *data; TypeTag type; } Any;
```

An `Any` is a pointer plus the id of what it points at. `any_of(x)` makes one
from an lvalue; `any_field` steps into a field:

```c
static void print_ints(Any value)
{
    const TypeInfo *info = typeinfo(value.type);
    uint32_t i;
    if (info->kind != TYPEKIND_STRUCT) return;
    for (i = 0; i < info->count; i++) {
        const TypeField *field = &RuntimeTypeFieldTable[info->first + i];
        Any part = any_field(value, field);
        if (part.type == typetag(int))
            printf("%s = %d\n", reflect_str(field->name), *(int *)part.data);
    }
}

print_ints(any_of(player));
```

Because ids are enumerators, a `switch` over `value.type` works and
`-Wswitch` can warn about unhandled cases:

```c
switch (value.type) {
case typetag(Player): /* ... */ break;
case typetag(const_char_ptr): puts(*(const char **)value.data); break;
default: printf("<%s>\n", typestr(value.type)); break;
}
```

To read a scalar, switch on its tag and read it through its own type; for an
enum, use its underlying type (`TypeInfo.inner`), which it is compatible with:

```c
const TypeInfo *type = typeinfo(part.type);
TypeTag scalar = type->kind == TYPEKIND_ENUM ? (TypeTag)type->inner : part.type;
long long number = 0;
switch (scalar) {
case typetag(int): number = *(const int *)part.data; break;
case typetag(unsigned_int): number = *(const unsigned int *)part.data; break;
case typetag(long): number = *(const long *)part.data; break;
/* ...the other builtin integer types... */
default: break;
}
```

`test/tmpl/inspect.tmpl.h` does this for every builtin type.

## Enums

An enum's `TypeInfo` lists its enumerators with their values:

```c
const TypeInfo *kind = typeinfo(typetag(Kind));
for (uint32_t i = 0; i < kind->count; i++) {
    const TypeEnumerator *e = &RuntimeTypeEnumTable[kind->first + i];
    printf("%s = %lld\n", reflect_str(e->name), (long long)e->value);
}
```

`typetag(Kind)` works for enums like for any other name. Selecting by
expression is different: in C an enum type is compatible with its underlying
integer type, so `_Generic` cannot tell them apart, and `typetag_of(kind)`
gives the id of that integer type (usually `unsigned int`). Use
`enumtag_of(x)`, which selects among the project's enum types only:

```c
enumtag_of(player.kind) == typetag(Kind)
```

Two different enum types are never compatible with each other, so
`enumtag_of` selects exactly. An argument that is not of enum type is an
error:

- **tmplgen rejects it when generating** (`enumtag_of() argument has type
  'unsigned int', which is not an enum type`). C alone cannot: a plain
  integer whose type is the underlying type of exactly one enum in scope would
  quietly select that enum. Enumerator constants (`KIND_MAGE`) have type `int`
  in C, so they are rejected too; name the type with `typetag(Kind)` instead.
- **The compiler rejects the rest** (`tmplgen_reflect_no_enumtag_for_this_argument_type`):
  structs, pointers, and anything else with no compatible enum.

tmplgen checks `enumtag_of(...)` and `TMPLGEN_ENUMTAG_OF(...)` where they are
written in project sources. A call inside another macro of yours is left to
the compiler.

`any_of` uses `typetag_of`; for an enum, write `(Any){ &x, enumtag_of(x) }`.

## Layout checks

The tables are libclang's numbers, so `reflection.h` lets your compiler
confirm them. For each header that declares reflected types it has a section
guarded by that header's include guard:

```c
#if defined(PLAYER_H) && !defined(TMPLGEN_REFLECT_HAVE_PLAYER_H)
#define TMPLGEN_REFLECT_HAVE_PLAYER_H
TMPLGEN_REFLECT_CHECK(sizeof(Player) == 48 && TMPLGEN_REFLECT_ALIGN_IS(Player, 8) &&
    offsetof(Player, pos) == 0 &&
    offsetof(Player, hp) == 8 && ...,
    "tmplgen: reflected layout of Player differs from this compiler or target; "
    "regenerate with matching --clang-arg flags");
/* ...typetag_of / enumtag_of entries for the header's types... */
#endif
```

Specializations are checked in sections keyed on the generated section that
declares them, so `Vec_Item` is checked once `gen/all.gen.h` has declared it.
A translation unit checks the types whose headers it includes. With
`--compiled`, `reflection.gen.c` includes every declaring header and checks
everything once.

If the build fails with "reflected layout of T differs", the flags passed with
`--clang-arg` do not match your compiler's (target, `-m32`, packing pragmas
behind a macro, a `-D` that changes a struct).

What is not checked:

- **Bit-fields.** Their bit offsets and widths come from libclang as they are;
  C has no `offsetof` for a bit-field, so the compiler cannot confirm them. A
  record's `sizeof` is still checked, which catches most layout differences
  around them, but not a reordering inside the same storage.
- **Types whose header a translation unit has not included** before
  `reflection.h`. Their sections are skipped there; some other translation
  unit that includes the header checks them (with `--compiled`,
  `reflection.gen.c` always does).

## Include rules

`reflection.h` follows the same rules as the generated template headers (see
[Include generated headers after your types](usage.md#include-generated-headers-after-your-types)):

1. **Headers that declare reflected types need an include guard** written as
   `#ifndef NAME` / `#define NAME` (or `#if !defined(NAME)`). `#pragma once`
   alone is an error, because the sections test the macro.
2. **Include `reflection.h` after the headers of the types you use, and after
   `gen/all.gen.h`** if you use specializations. A section is compiled when
   its header has been included; the tables themselves are always complete.
   Including `reflection.h` again later is fine and activates the sections
   that became available.
3. **A header that includes `reflection.h` must declare its types first.**
   tmplgen reports a header that includes `reflection.h` before declaring a
   reflected type.
4. **Project headers must compile together**, because tmplgen parses them in
   one translation unit to read the layout. Two headers that define the same
   name in incompatible ways are reported.

A file that only reads values (a printer taking `Any`) can include
`reflection.h` alone.

## Reflection in templates

A template can use the reflection API in its bodies, so one generic function
works for any reflected type. `test/tmpl/inspect.tmpl.h` is a complete
example:

```c
/* tmpl/inspect.tmpl.h */
#include <stdio.h>
#include "../gen/reflection.h"

#ifndef TMPLGEN
typedef int T;
#endif

// @template T
static inline uint32_t inspect(T *value, FILE *out)
{
    TypeTag tag = typetag_of(*value);
    const TypeInfo *info = typeinfo(tag);
    uint32_t i, count = info->kind == TYPEKIND_STRUCT ? info->count : 0;
    fprintf(out, "%s (%u bytes)\n", typestr(tag), (unsigned)info->size);
    for (i = 0; i < count; i++) {
        const TypeField *field = &RuntimeTypeFieldTable[info->first + i];
        fprintf(out, "  %s: %s\n", reflect_str(field->name),
                typestr((TypeTag)field->type));
    }
    return count;
}
```

```c
/* main.c */
#include "hero.h"
#include "gen/all.gen.h"        /* the inspect() dispatch */
#include "gen/reflection.h"     /* compiles inspect_Hero */

inspect(&hero, stdout);
inspect(&items, stdout);         /* Vec(Item) works too */
```

How it works:

- tmplgen recognizes the template's `#include "../gen/reflection.h"` (any
  include of a `reflection.h` that is this project's, not written yet, or
  written by tmplgen) and **does not copy it** into the generated header,
  which would be an include cycle.
- A specialization such as `inspect_Hero` needs `reflection.h`'s declarations
  and its `typetag_of` entry for `Hero`, so its section also requires
  `TMPLGEN_REFLECT_HAVE_<guard>` for each argument's header
  (`TMPLGEN_REFLECTION_H` when only builtins are involved).
- `reflection.h` ends by including the headers of such templates, sections
  only (no dispatch macros, so the template still parses in an editor). So
  including `reflection.h` last is enough to compile the specializations.

Rules:

- A project that **specializes** such a template needs `--reflect=project`;
  otherwise generation fails naming the template. A project that only has it
  in its template directory is unaffected.
- Write the template against the project's names: with a
  [symbol prefix](#symbol-prefix), use the prefixed ones.
- Use `T *` rather than `const T *` for the parameter: an argument of type
  `Hero *` is not yet matched against `const T *`.

## Symbol prefix

Every identifier in `reflection.h` (`TypeTag`, `TYPETAG_Hero`, `TypeInfo`,
`Any`, `typeinfo`, the tables, ...) is spelled `TMPLGEN_SYM(name)`. Defining
`TMPLGEN_SYMBOL_PREFIX` renames them all:

```c
/* project-wide, e.g. -DTMPLGEN_SYMBOL_PREFIX=rt_ */
rt_TypeTag tag = TMPLGEN_TYPETAG(Hero);          /* rt_TYPETAG_Hero */
const rt_TypeInfo *info = rt_typeinfo(tag);
rt_Any value = TMPLGEN_ANY_OF(hero);
```

With a prefix the short macros (`typetag`, `typetag_of`, `enumtag_of`,
`any_of`) are not defined; use `TMPLGEN_TYPETAG(T)`, `TMPLGEN_TYPETAG_OF(x)`,
`TMPLGEN_ENUMTAG_OF(x)` and `TMPLGEN_ANY_OF(x)`. The prefix must be the same in
every translation unit, since the tables have one name.

## `--compiled`

With `--compiled`, the tables move into `gen/reflection.gen.c` (compile and
link it with the project), and `reflection.h` only declares them. That file
includes every header that declares a reflected type and runs every layout
check once. `reflection.h` then changes only when the set of types changes,
not when a layout does, so fewer files recompile.

## Cost

- **The tables** are data: a few dozen bytes per type and field, `static
  const` (shared through `reflection.gen.c` with `--compiled`).
- **`typetag(T)`** is a name: free.
- **`typetag_of`, `enumtag_of`, `any_of`** expand a `_Generic` (or
  `__builtin_choose_expr` chain) over every type visible at the call, at every
  use. With hundreds of types and many uses this is measurable (in a stress
  test with 800 types and 600 uses it took 0.6-1.3 s more than `typetag`).
  Use `typetag(T)` in hot paths such as large `switch` statements.
- **Layout checks** are one `_Static_assert` per type, in the translation
  units that include its header.

## Errors and limitations

| Message | Cause |
| --- | --- |
| `reflected type 'T' is declared in a header without an #ifndef/#define include guard` | The header uses `#pragma once` only. |
| `includes gen/reflection.h before declaring reflected type 'T'` | Move the `#include` below the declaration. |
| `reflection: project headers do not compile together: ...` | Two project headers conflict when included in one translation unit. |
| `reflected layout of T differs from this compiler or target` | `--clang-arg` flags differ from the compiler's. |
| `tmplgen_reflect_no_typetag_for_this_argument_type` | `typetag_of`/`any_of` on a type without an id (declared in a `.c` file, or its header not included before `reflection.h`). |
| `tmplgen_reflect_no_enumtag_for_this_argument_type` | `enumtag_of` on something that is not an enum. |
| `enumtag_of() argument has type '...', which is not an enum type` | `enumtag_of` on an integer (or an enumerator constant), which C alone would accept. Use `typetag_of`, or `typetag(T)` for an enum type. |
| `... needs a specialization of ..., which includes gen/reflection.h; generate with --reflect=project` | A template using reflection is specialized without `--reflect=project`. |

Limitations:

- `typetag_of` does not select enums; use `enumtag_of`.
- Ids are not stable across builds; store names.
- Types declared in `.c` files are not reflected.
- External types (from system headers) have a size but no fields.
- Bit-fields have bit offsets and widths but no layout check (C has no
  `offsetof` for them).

## Examples

- `examples/reflect_demo.c`: a pretty printer and a parser for C
  designated-initializer text, written only against the tables; it includes
  no project header and handles every reflected type. `test/reflect.sh` runs a
  print → parse → `memcmp` round trip.
- `test/cases/reflect_fields.c`: walking fields, enumerators, a
  specialization, bit-fields, a union.
- `test/cases/reflect_any.c`: `Any`, `typetag_of`, `enumtag_of`, `switch` on
  tags, `any_field`, with both dispatch backends.
- `test/cases/reflect_template.c` with `test/tmpl/inspect.tmpl.h`: a template
  that uses reflection.
- `test/reflect.sh`: a standalone project covering the include rules, the
  symbol prefix, `--compiled`, layout mismatches, and the generation errors.
