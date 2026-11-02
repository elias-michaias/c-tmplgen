# Usage

This guide covers writing a template, triggering specialization, and consuming
the generated files. For dependencies and build-system details, see
[Tooling and integration](tooling.md).

## Write a template

A template is a `.tmpl.h` file containing ordinary C. Put a `// @template`
annotation immediately before each dependent top-level declaration. The
annotation lists the type variables that declaration may use:

```c
#ifndef TMPLGEN
typedef int T; /* editor-only fallback for standalone parsing */
#endif

// @template T
static inline T max(T left, T right)
{
    return left > right ? left : right;
}
```

The `TMPLGEN` fallback block helps editors parse the template on its own; the
generator removes the block when creating output. Templates are discovered from
the selected template directory as sorted top-level `*.tmpl.h` files.

Type constructors use function-like syntax in template source. For example:

```c
#include <stddef.h>

// @template T
typedef struct {
    T *data;
    size_t length;
} Vec;

// @template T
static inline void push(Vec(T) *items, T value)
{
    items->data[items->length++] = value;
}
```

When a source file uses `Vec(MyType)` or calls `push(&items, value)`, the
generator can request the corresponding concrete type and function. A template
may have any number of variables, for example `// @template T, E`; each
declaration can list only the variables it actually depends on. Applications
such as `Result(T, E)` preserve the relationship between the constructor's
parameters and a function's input and return types.

## Infer specializations from source

The generator parses `.c` and `.h` files under `--source-root`. It matches
observed function input types against annotated function parameters to infer
template variables. Constructor uses such as `Vec(MyType)` also request type
specializations. Specializations are deduplicated by their concrete type tuple,
and recognized calls between template functions are followed to a fixed point.

Inference comes from inputs, not return types: a template variable must be
recoverable from an input type or a constructor use. A function whose type
variable occurs only in its return type cannot be selected automatically from a
call. For type arguments at source use sites, use a typedef name or a built-in
type name; use a typedef for qualified or multi-token type spellings.

Use `<:` for structural field requirements and `|=` or `@where` for
compile-time satisfaction expressions. See the dedicated
[Constraints guide](constraints.md) for subtyping, satisfiability, and complete
examples.

## Generate and check

Build once, then run from the project root:

```sh
cc -std=c99 -Wall -Wextra -pedantic tmplgen.c -lclang -o tmplgen
./tmplgen --clang-arg=-std=c11
./tmplgen --check --clang-arg=-std=c11
```

The normal project defaults are `--source-root .`, `--template-dir tmpl`, and
`--output gen/all.gen.h`. Module headers are written beside the aggregate, for
example `gen/math.gen.h`. This repository keeps its example project under
`test/`, so its scripts pass `--template-dir test/tmpl --output
test/gen/all.gen.h` explicitly while keeping `--source-root .` at the repository
root. `--check` compares every expected generated file and returns failure if
one is missing or stale.

For a project in a different directory, specify its paths explicitly:

```sh
./tmplgen \
  --source-root /path/to/project \
  --template-dir /path/to/project/tmpl \
  --output /path/to/project/gen/all.gen.h \
  --clang-arg=-std=c11 \
  --clang-arg=-I/path/to/project/include
```

Pass compiler preprocessing and target options to libclang with repeatable
`--clang-arg` options. Match the real project’s language standard, include
paths, defines, and target flags so the generator sees the same declarations and
types as the compiler.

## Dispatch and output modes

`--dispatch` selects how calls reach the observed specializations:

| Mode | Generated call interface |
| --- | --- |
| `auto` | `_Generic` in C11-or-newer modes; GNU builtins in GNU C modes |
| `generic` | C11 `_Generic` dispatch |
| `gnu` | GNU `__typeof__` / `__builtin_choose_expr` dispatch |
| `none` | Only concrete suffixed function names, such as `max_int` |

The default is `auto`. The generator emits associations only for observed input
types; an unmatched call produces a deliberate compile-time error.

By default, concrete function bodies are emitted in generated headers. For
projects with many specializations or translation units, `--compiled` moves
function bodies into per-template `.gen.c` files while retaining declarations
and dispatch in the headers:

```sh
./tmplgen --compiled --clang-arg=-std=c11
cc -std=c11 -I. app.c gen/*.gen.c -o app
```

Compile each generated implementation file once and link it with the consuming
program. Do not include `.gen.c` files in application source files.

## Include generated headers after your types

The generated headers hold the specializations requested by the whole project,
but a translation unit only has to declare the types it uses itself. Everything
that names a project type, such as `Vec_Point`, `push_Point`, and the dispatch
entries for them, is emitted in a section guarded by the **include guard of the
header that declares that type**:

```c
/* gen/vec.gen.h (sketch) */
#if defined(POINT_H) && !defined(TMPLGEN_HAVE_..._POINT_H)
typedef struct { Point *data; size_t length; } Vec_Point;
static inline void push_Point(Vec_Point *items, Point value) { ... }
/* ...adds `Vec_Point *: push_Point` to push()... */
#endif
```

Builtin types and types that the template itself includes are always
available. This gives three rules:

1. **Headers that declare template argument types need an include guard**
   written as `#ifndef NAME` / `#define NAME` (or `#if !defined(NAME)`) at the
   top. `#pragma once` alone is not enough, because the generated code tests
   the macro. tmplgen reports a missing guard when it generates.
2. **Include the generated header after the headers declaring the types you
   use.** A section is compiled when both are present; the generated headers
   have no include guard around their sections, so including them again later
   is fine and activates the sections that became available. A file that never
   does this gets an error naming the missing specialization (`unknown type name
   'Vec_Point'`). Files that do not use `Point` are unaffected.
3. **A header that declares types and also includes the generated header must
   declare its types first:**

   ```c
   #ifndef INVENTORY_H
   #define INVENTORY_H
   typedef struct { double weight; } Item;
   #include "gen/all.gen.h"              /* after Item */
   struct Inventory { Vec(Item) items; };
   #endif
   ```

   tmplgen reports a header that includes a generated header before declaring
   a type used as a template argument.

A specialization whose argument is another template's type, such as
`inspect(Vec(Item))`, also waits for the section that declares `Vec_Item`; the
generated headers include each other as needed, so their order does not
matter.

Types declared in a `.c` file cannot be template arguments, since no other
file could compile their specializations; tmplgen reports them as errors.
Specializations whose arguments come from several headers, such as
`Pair(Point, Item)`, need all of them.

## Reflection

`--reflect=project` also writes `gen/reflection.h`: a type id for every struct,
union, and enum declared in the project's headers (plus template
specializations, builtins, and the pointer and array types they use), and
tables describing them.

```c
#include "player.h"
#include "gen/all.gen.h"        /* if you use templates */
#include "gen/reflection.h"     /* last */

Player player = { ... };
const TypeInfo *info = typeinfo(typetag_of(player));   /* or typetag(Player) */
const TypeInfo *kind = typeinfo(typetag(Kind));         /* enums too */
const TypeField *fields = RuntimeTypeFieldTable + info->first;
for (uint32_t i = 0; i < info->count; i++)
    printf("%s @%u\n", reflect_str(fields[i].name), fields[i].offset);

Any value = any_of(player);                     /* { &player, TYPETAG_Player } */
switch (value.type) {
case typetag(Player): /* ... */ break;
default: printf("<%s>\n", typestr(value.type)); break;
}
```

The whole API:

| | |
| --- | --- |
| `typetag(T)` | Id of a type *name* (`Point`, `Kind`, `struct_Node`, `Vec_int`, `const_char_ptr`, `Point_ptr`); needs no declaration. A macro. |
| `typetag_of(x)` | Id of an expression's type, picked with C11 `_Generic` or GNU builtins. Gives an enum value its integer type's id. A macro. |
| `enumtag_of(x)` | Id of an enum-typed expression's type. A macro. |
| `any_of(x)` | `Any` (`{ void *data; TypeTag type; }`) pointing at the lvalue `x`. A macro. |
| `typeinfo(id)` | `const TypeInfo *`: size, alignment, kind, flags, and where its fields or enumerators are. |
| `typestr(id)` | Display name, e.g. `"Vec(Item)"`. |
| `reflect_str(offset)` | A name from the string table (`TypeField.name`, `TypeEnumerator.name`). |
| `any_field(value, field)` | `Any` for one field of a value. |
| `RuntimeTypeInfoTable`, `RuntimeTypeFieldTable`, `RuntimeTypeEnumTable`, `RuntimeTypeStrings` | The tables. A record's fields are `RuntimeTypeFieldTable[first .. first+count)`, an enum's enumerators `RuntimeTypeEnumTable[first .. first+count)`. |

- **The tables are plain numbers.** tmplgen asks libclang for sizes,
  alignments, offsets, and enumerator values, so the tables name no project
  type and are complete in every translation unit. Code that only reads
  values, such as a printer taking an `Any`, needs no project headers;
  `examples/reflect_demo.c` is a complete pretty printer and parser written
  that way.
- **The compiler checks the numbers.** For each header that declares
  reflected types, `reflection.h` has a section guarded by that header's
  include guard, with a `_Static_assert` comparing `sizeof`, `_Alignof`,
  `offsetof` and enumerator values against the table. If libclang saw a
  different target or different macros than your compiler (pass the same
  `--clang-arg` flags), the build fails with "reflected layout of T differs
  from this compiler or target". Bit-field offsets are the exception: C cannot
  take them, so they are libclang's numbers, unchecked.
- **`typetag(T)` pastes a name**: it is the enumerator `TYPETAG_<name>`, so it
  costs nothing to compile and works without the type's declaration. Names are
  spelled as identifiers: `struct_Node`, `Vec_int`, `const_char_ptr`,
  `Point_ptr`.
- **`typetag_of(x)`, `enumtag_of(x)` and `any_of(x)`** select by the static
  type, like the template dispatch, and follow the same include rule: include
  `reflection.h` after the headers of the types you name, and after the
  generated headers for specializations such as `Vec(Item)` (or include it
  again). Each use
  expands the whole list of types, so prefer `typetag(T)` where the type is
  known. An enum type is compatible with its integer type, so `typetag_of`
  gives an enum value the id of that integer type; `enumtag_of` selects among
  the enums only. Two enum types are never compatible with each other, so it
  selects exactly. An argument that is not of enum type is an error: tmplgen
  rejects integers when generating (C alone would let a plain integer select
  the one enum with that underlying type), and the compiler rejects anything
  else. `any_of(x)` uses `typetag_of`; for an
  enum write `(Any){ &x, enumtag_of(x) }`.
- **Rules.** Headers declaring reflected types need an `#ifndef`/`#define`
  include guard (`#pragma once` alone is an error), must declare their types
  before any `#include "gen/reflection.h"`, and must compile when included
  together, because tmplgen parses them in one translation unit to read the
  layout. Types declared in `.c` files are not reflected.
- **Prefix.** Every identifier (`TypeTag`, `TYPETAG_Point`, `Any`,
  `typeinfo`, the tables) is spelled through a macro, so defining
  `TMPLGEN_SYMBOL_PREFIX tmplgen_` (project-wide, e.g. with `-D`) renames them
  to `tmplgen_TypeTag` and so on. The macros are then only available as
  `TMPLGEN_TYPETAG(T)`, `TMPLGEN_TYPETAG_OF(x)`, `TMPLGEN_ENUMTAG_OF(x)` and
  `TMPLGEN_ANY_OF(x)`.
- **`--compiled`** moves the tables into `gen/reflection.gen.c`, which
  includes every declaring header and runs every layout check once; the header
  then only changes when the set of types changes.

### Reflection in templates

A template can use the reflection API in its bodies, so one generic function
works on any reflected type:

```c
/* tmpl/inspect.tmpl.h */
#include <stdio.h>
#include "../gen/reflection.h"

// @template T
static inline uint32_t inspect(T *value, FILE *out)
{
    const TypeInfo *info = typeinfo(typetag_of(*value));
    /* ... walk RuntimeTypeFieldTable + info->first ... */
}
```

The body needs `reflection.h`'s declarations and its `typetag_of` entry for
`T`, so a specialization such as `inspect_Hero` is compiled only once
`reflection.h` has seen `Hero`'s header: its section additionally requires
`TMPLGEN_REFLECT_HAVE_<guard>` (or `TMPLGEN_REFLECTION_H` for builtins). The
generated header does not include `reflection.h` back; instead `reflection.h`
includes the headers of such templates at its end, so the usual order works:

```c
#include "hero.h"
#include "gen/all.gen.h"        /* inspect() dispatch */
#include "gen/reflection.h"     /* compiles inspect_Hero */

inspect(&hero, stdout);
```

A project that specializes such a template needs `--reflect=project`; one that
only has it in its template directory does not. Write the template against the
project's names (with `TMPLGEN_SYMBOL_PREFIX`, the prefixed ones).

Worked examples live in `test/cases/reflect_*.c` (field tables and enumerators,
`Any` with both dispatch backends, the printer/parser round trip, a template
using reflection from `test/tmpl/inspect.tmpl.h`), built from
`test/cases/reflect_types.h` by `test/run.sh`; `test/reflect.sh` covers the
prefix and `--compiled`.

Never store raw `TypeTag` values: ids are dense and shift when types are added.
Store names instead, as the demo's text format does. See
[Reflection](reflection.md) for the full reference.
