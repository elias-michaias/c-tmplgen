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

For this repository, the defaults are `--source-root .`, `--template-dir
test/tmpl`, and `--output test/gen/all.gen.h`. Module headers are written beside
the aggregate, for example `test/gen/math.gen.h`. `--check` compares every
expected generated file and returns failure if one is missing or stale.

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
