# Problem: generated headers need every template argument type declared beforehand

> Status: **fixed** (header-guarded sections). Found 2026-11-01 while planning
> reflection (`REFLECTION.md`). Reproduced with the standalone `tmplgen` built
> from this repo against LLVM main's libclang, compiling with GCC 13.
>
> **Resolution.** Everything a specialization emits that names a project type
> (struct, prototypes, bodies, dispatch entries) now lives in a section guarded
> by the include guard of the header declaring that type, e.g.
> `#if defined(FOO_H) && !defined(TMPLGEN_HAVE_..._FOO_H)`. The generated
> headers have no include guard around those sections, and dispatch macros pick
> up the sections' associations through hook macros that are redefined inside
> them (macros expand at the call). Cases 1 and 2 compile; case 3 (a type
> declared in a `.c` file), a declaring header without an `#ifndef`/`#define`
> guard, and a header that includes the generated header before declaring its
> type are reported by tmplgen as errors. The rules are documented in
> `doc/usage.md` ("Include generated headers after your types");
> `test/headers.sh` covers the scenarios below with every dispatch backend and
> `--compiled`. The generator's own parsing stand-in header follows the same
> layout, which also fixed spurious "cannot infer" warnings in projects whose
> files don't all see the same types.

## Summary

`gen/all.gen.h` (and every `gen/<stem>.gen.h`) contains the specializations
requested by **the whole project**, written in terms of their argument types.
The generated headers only carry the *template's* own `#include`s. They never
include the headers that declare the *argument* types. So:

> Every translation unit that includes the generated header must already have
> declared **every type used as a template argument anywhere in the project**,
> before its **first** include of the generated header.

This contract is not documented anywhere. It also breaks *non-locally*: adding
`Vec(Foo)` in one file can stop an unrelated file from compiling, and a template
argument type declared inside a `.c` file breaks every file that includes the
generated header.

## Reproduction

A project with one template (`Vec`, `push`, the same as `test/tmpl/vec.tmpl.h`),
one shared type, and independent source files:

```c
/* foo.h */
typedef struct Foo { int id; } Foo;

/* a.c: uses Vec(Foo), includes its type first, as the README shows */
#include "foo.h"
#include "gen/all.gen.h"
void a_use(void) { Vec(Foo) foos = {0}; Foo f = { 1 }; push(&foos, f); free(foos.data); }

/* b.c: only uses Vec(int); never heard of Foo */
#include "gen/all.gen.h"
int main(void) { Vec(int) xs = {0}; push(&xs, 42); int r = xs.data[0]; free(xs.data); return r == 42 ? 0 : 1; }
```

```sh
tmplgen --clang-arg=-std=c11 && cc -std=c11 -c a.c && cc -std=c11 -c b.c
```

`gen/vec.gen.h` contains both specializations, unconditionally:

```c
typedef struct { int *data; size_t len, cap; } Vec_int;
typedef struct { Foo *data; size_t len, cap; } Vec_Foo;     /* needs Foo */
static inline void push_Foo(Vec_Foo * v, Foo x);            /* needs Foo */
```

### Results

| # | Scenario | Result |
| --- | --- | --- |
| 1 | `a.c` (includes `foo.h`, then `gen/all.gen.h`) | compiles |
| 1 | `b.c` (includes only `gen/all.gen.h`, uses `Vec(int)`) | **5 errors**: `unknown type name 'Foo'` |
| 2 | Add `inventory.h`: declares `Item`, includes `gen/all.gen.h`, declares `struct Inventory { Vec(Item) items; }` | `a.c`, **unchanged and unrelated, now fails**: `unknown type name 'Item'` |
| 2 | `c.c`: includes `inventory.h`, then `foo.h`, then `gen/all.gen.h` (everything is included) | **fails**: `unknown type name 'Foo'`. The first include of `all.gen.h` happens inside `inventory.h`, before `foo.h`, and its include guard makes the later include a no-op |
| 2 | `d.c`: same headers, `foo.h` first | compiles |
| 3 | `e.c` declares `typedef struct Local {…} Local;` locally and uses `Vec(Local)` | **every** file fails (`a.c`, `b.c`, `e.c` itself): `unknown type name 'Local'`. No include order can fix this, because `Local` cannot be declared anywhere else |

Same contract inside this repo: a file that includes only `test/gen/all.gen.h`
and calls `max(1.0, 2.0)` fails with 178 errors starting at
`test/gen/math.gen.h:8: unknown type name 'TestInt'`. Including `all.gen.h`
*before* `test/cases/test_types.h` gives the same errors. Only
types-then-generated compiles.

## Root cause

- **Generation is project-wide and the output is one shared header set.**
  `discover_source_usages` (`tmplgen.c:9036`) collects instances from every
  source file into the global `g_instances`. `emit_template_module` (`5567`)
  emits all of a template's instances into one `<stem>.gen.h`, and
  `emit_aggregate_header` (`5653`) includes every module.
- **Only template includes are propagated.** `emit_preamble` (`5891`) writes
  `GEN_PREAMBLE` plus the template's own `#include`s (rewriting `.tmpl.h` →
  `.gen.h`). Nothing records or emits where each *argument* type is declared.
- **Bodies are header-only by default.** Specializations are `static inline`
  definitions (`emit_instance`, `4876`). Those need complete argument types at
  the include point, not just declarations.
- **The include guard (`TMPL_H`) makes the first include the only one that
  counts**, so the requirement attaches to the first include anywhere in the
  include graph, including one nested inside an unrelated header (case 2).
- **Dispatch macros add the same requirement** (`emit_dispatch_macro`, `5365`):
  `_Generic` association lists name every observed argument type. But they
  expand later, at the call site, so the declarations above are what fail
  first.

## Why the test suite does not catch it

Every `test/cases/*.c` file includes the same `test_types.h` before
`../gen/all.gen.h`. Every other argument type (`PhysicsBody`, `NamedActor`)
comes in through the template's own include of `../lib/physics.h`, which *is*
propagated. No test has a type used by one source file but not another, a
type declared in a `.c` file, or a header that includes the generated header.

## Impact

- **Non-local breakage.** A new template use in one file can break the build of
  files that don't use it (case 2). In a real project this shows up as
  "someone else's change broke my file" errors pointing into `gen/`.
- **Headers can't safely use templates.** A header that needs `Vec(Item)` in its
  own struct must include the generated header partway through, and that fixes
  the include order for every file that includes it (case 2, `c.c`).
- **Types local to a `.c` file can't be template arguments at all** (case 3),
  even though nothing in the docs or diagnostics says so.
- **Editor:** the clangd plugin regenerates on every parse. The breakage appears
  as soon as any file adds a new argument type, in every other open file that
  includes the generated header.
- **Reflection** (`REFLECTION.md`) has the same root problem at a larger scale.
  Its table would name *every* project type. There it's addressed by moving
  the tables into a `.gen.c` and making dispatch use tag pointers. That split
  isn't directly available to templates, whose bodies are header-only.

## Possible directions (not decided)

1. **Document the contract** in `doc/usage.md` / `doc/tooling.md`, and have
   tmplgen report scenario 3 (argument type declared in a `.c` file) as a
   generation error. This is cheap and honest, but fixes neither the
   non-locality nor scenario 2.
2. **Have generated modules include the declaring header of each argument
   type.** libclang knows the declaration location (`clang_getTypeDeclaration`
   → `clang_getCursorLocation`). This fixes cases 1 and most of case 2. It
   cannot fix case 3. It risks include cycles when a header both declares a type
   and uses a specialization of it; that works only if the type is declared
   before the header's include of the generated header. It also makes every
   includer parse every argument type's header.
3. **Forward declarations instead of definitions (tagged types + `--compiled`).**
   - Checked with GCC 13 and Clang 18 (`-Wall -Wextra -pedantic`, no
     warnings): a specialization like
     `typedef struct Vec_Foo { struct Foo *data; … } Vec_Foo;` plus a
     prototype `void push_Foo(Vec_Foo *, struct Foo x);` compiles without
     `struct Foo` ever being defined.
   - So for **tagged** argument types, with bodies moved to `.gen.c`, the
     headers need no argument-type includes.
   - Limits: tagless typedefs (`typedef struct { … } Foo;`) can't be forward
     declared, templates that hold `T` by value (`Box(T) { T value; }`) still
     need the complete type, and case 3 still can't be compiled in a shared
     `.gen.c`.
4. **Partition output by who uses it.** For example, emit per-argument-type or
   per-source-file headers so a file only sees specializations whose types it
   can name. This fixes non-locality and case 3 (per-file output for local
   types), at the cost of more generated files and a less simple
   include-one-header story.
5. **Combinations.** For example, 3 for tagged types with `--compiled`, 2 or 4
   as the fallback for tagless and by-value cases, and 1's diagnostic
   for case 3.

## Open questions

- Is "declare all argument types before the first include" acceptable as a
  documented rule, or is project-wide non-locality a blocker?
- Should argument types declared in `.c` files be supported (needs per-file
  output) or rejected with a diagnostic?
- Should `--compiled` become the default if that enables the forward-declaration
  approach?
- Should tmplgen require or encourage tagged structs, as `REFLECTION.md` now
  proposes for value dispatch?

## Repro script

```sh
#!/bin/sh
# Builds tmplgen against a libclang, creates a scratch project, and runs cases 1 and 3.
# Adjust LLVM to an LLVM build or install that provides clang-c/Index.h and libclang.
set -e
LLVM=$HOME/Projects/llvm-project
REPO=$HOME/Projects/c-tmplgen
W=$(mktemp -d)
cc -std=c99 "$REPO/tmplgen.c" -I"$LLVM/clang/include" -L"$LLVM/build/lib" -lclang \
   -Wl,-rpath,"$LLVM/build/lib" -o "$W/tmplgen"
mkdir -p "$W/proj/tmpl" && cd "$W/proj"
cp "$REPO/test/tmpl/vec.tmpl.h" tmpl/
printf '#ifndef FOO_H\n#define FOO_H\ntypedef struct Foo { int id; } Foo;\n#endif\n' > foo.h
printf '#include "foo.h"\n#include "gen/all.gen.h"\nvoid a_use(void) { Vec(Foo) v = {0}; Foo f = {1}; push(&v, f); free(v.data); }\n' > a.c
printf '#include "gen/all.gen.h"\nint main(void) { Vec(int) v = {0}; push(&v, 42); free(v.data); return 0; }\n' > b.c
"$W/tmplgen" --clang-arg=-std=c11
for f in a b; do printf '%s.c: ' $f; cc -std=c11 -c $f.c -o /dev/null 2>/dev/null && echo ok || echo FAIL; done
# expected: a.c: ok   b.c: FAIL  (unknown type name 'Foo')
printf '#include "gen/all.gen.h"\ntypedef struct Local { int x; } Local;\nvoid e_use(void) { Vec(Local) v = {0}; Local l = {1}; push(&v, l); free(v.data); }\n' > e.c
"$W/tmplgen" --clang-arg=-std=c11
for f in a b e; do printf '%s.c: ' $f; cc -std=c11 -c $f.c -o /dev/null 2>/dev/null && echo ok || echo FAIL; done
# expected: all FAIL  (unknown type name 'Local')
echo "scratch dir: $W"
```
