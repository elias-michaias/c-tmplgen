# Spec: `tmplgen.c`, a single-file C99 generic-template generator

Write **one C source file, `tmplgen.c`**, that compiles with any C99 compiler linked against the libclang C API. The executable reads `.tmpl.h` template files from a `tmpl/` directory and writes one include-guarded header, `tmpl.h`, at the project root. That header contains concrete instantiations of the templates plus type-dispatching macros so users can call plain names like `max(a, b)`.

The generator is the entire build system. There is no Python, no Lua, and no external config file; its only dependency is a C compiler plus `libclang` (used to parse the project and infer types accurately).

---

## 1. Hard constraints

1. **One file.** Everything lives in `tmplgen.c`; the only external library is `libclang` through its C API (`<clang-c/Index.h>`). Link with `-lclang`.
2. **Strict ISO C99** for the generator itself. It must compile warning-free with:
   `cc -std=c99 -Wall -Wextra -pedantic -Wconversion -Wshadow tmplgen.c -lclang -o tmplgen`
   No `typeof`, no statement expressions, no VLAs, no `//`-free assumptions, no compiler extensions in the generator. Source-tree traversal is isolated in platform code. Do not rely on `strdup`, `getline`, `strcasecmp`, etc.; write tiny local helpers instead.
3. **All config at the top of the file** as `#define`s and `static const` tables, each with an explanatory comment. No config file, no environment variables, and no required CLI flags (the optional CLI overrides documented in section 7 are additive).
4. **Understandable code over clever code.** Plain functions, short bodies, no macros-as-control-flow, no function-pointer tables unless they clearly simplify things. A C beginner should be able to read it top to bottom. Use section banner comments.
5. **The *generated* header** may use `_Generic` (C11) or GNU/Clang builtins, depending on a config switch. The generator never uses them itself.
6. **Include guard, not `#pragma once`**, in the output.
7. **Deterministic output.** The same inputs always produce byte-identical output (no timestamps). The output file is only rewritten when its content changes, so clangd and build systems don't see needless modifications.
8. **Infer specialization from declarations and calls.** Each template declares its type variables once. libclang analyzes dependent typedefs and function signatures, then source uses supply concrete type arguments from constructors and function inputs.

---

## 2. Project layout

```
project/
  tmplgen.c          <- the whole tool (this spec)
  tmpl.h             <- GENERATED, never hand-edited, committed or not at the user's choice
  .clangd            <- editor language modes for the dispatch test cases
  tmpl/
    math.tmpl.h      <- templates, plain C with declared type variables
    vec.tmpl.h
    result.tmpl.h
    triple.tmpl.h
  test/cases/ ...    <- API and dispatch tests include `../../tmpl.h`
  src/ ...           <- user code does `#include "tmpl.h"`
```

Typical use: `cc tmplgen.c -lclang -o tmplgen && ./tmplgen`

---

## 3. Config block (top of `tmplgen.c`)

Reproduce this block near-verbatim, with the comments, and adjust only for syntax. Every option is explained in its comment.

```c
/* ===================== CONFIGURATION ===================== */

/* Dispatch backends for the generated macros (pick one for GEN_DISPATCH). */
#define GEN_DISPATCH_NONE    0  /* only emit suffixed functions: max_int, max_double */
#define GEN_DISPATCH_GENERIC 1  /* emit _Generic macros (C11 and newer)              */
#define GEN_DISPATCH_GNU     2  /* emit __builtin_choose_expr/__typeof__ chains      */
#define GEN_DISPATCH_AUTO    3  /* _Generic for C11+, GNU builtins for GNU C         */

#define GEN_TMPL_DIR    "tmpl"       /* directory holding the templates               */
#define GEN_TMPL_SUFFIX ".tmpl.h"    /* appended to each template stem                */
#define GEN_SCAN_ROOT  "."           /* recursively scan this tree for C source usages */
#define GEN_OUTPUT      "tmpl.h"     /* the generated header, relative to cwd         */
#define GEN_GUARD       "TMPL_H"     /* include-guard macro name in the output        */
#define GEN_LSP_GUARD   "TMPLGEN"    /* #ifndef block stripped from template bodies  */
#define GEN_DISPATCH    GEN_DISPATCH_AUTO
#define GEN_EMIT_LINE   0            /* 1 maps locations to templates; 0 keeps
                                        concrete definitions in tmpl.h                */
#define GEN_ENABLE_WATCH     1       /* 1: compile --watch support; 0: disable --watch */
#define GEN_WATCH_INTERVAL_MS 500    /* poll period for --watch                       */

/* Text pasted right after the include guard. Put system includes and your own
   type headers here. */
#define GEN_PREAMBLE "#include <stddef.h>\n#include <stdlib.h>\n"

/* Template table. Types are discovered from source-file usages. */
typedef struct { const char *stem; } GenTemplate;
static const GenTemplate GEN_TEMPLATES[] = {
    { "math" },
    { "vec"  },
    { "result" },
    { "triple" }
};

/* =================== END OF CONFIGURATION ================== */
```

Static array sizes are derived with `sizeof(a)/sizeof((a)[0])`. All other limits (line length, name length, and so on) must be either `#define`d here with comments or handled with dynamically grown buffers. Prefer dynamic buffers.

---

## 4. Template format

A template is ordinary C plus exactly one type-variable declaration. The generator analyzes declarations in the file with libclang; no per-type, per-function, or per-dispatch annotations are used.

```c
// @template T
```

or, for any number of variables:

```c
// @template T, E, A
```

For standalone editor parsing, declare fallback typedefs inside `#ifndef TMPLGEN`; the generator strips that block while preserving source offsets. Ordinary `#include` lines are gathered into the generated preamble.

### Example `tmpl/math.tmpl.h`

```c
#ifndef TMPLGEN
typedef int T;
#endif
// @template T
static inline T max(T a, T b) { return a > b ? a : b; }
static inline T clamp(T x, T lo, T hi) { return max(lo, x) > hi ? hi : max(lo, x); }
```

### Example `tmpl/vec.tmpl.h`

```c
#include <stddef.h>
#include <stdlib.h>
#ifndef TMPLGEN
typedef int T;
#endif
// @template T
typedef struct { T *data; size_t len, cap; } Vec;

static inline void push(Vec *v, T x) {
    if (v->len == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        v->data = (T *)realloc(v->data, v->cap * sizeof(T));
    }
    v->data[v->len++] = x;
}
```

Users then write `Vec(MyType) v = {0}; push(&v, value);`. `Vec(MyType)` requests the specialization and expands to the concrete `Vec_MyType` type.

For a multi-parameter type and its functions:

```c
#ifndef TMPLGEN
typedef int T;
typedef int E;
#endif
// @template T, E
typedef struct { T value; E error; } Result;
static inline T result_value(Result *r) { return r->value; }
static inline Result result_make(T value, E error) {
    Result r = { value, error };
    return r;
}
```

`Result(Value, Error)` requests the type tuple. Calls to `result_value(&r)` infer both variables from the input `Result(T, E) *`; calls to `result_make(value, error)` infer them from the two input argument types. The resulting `T` return type is substituted from those inputs.

---

## 5. Inference and rewriting

libclang parses each template and records its dependent typedefs, function signatures, and source ranges. The generator emits each dependent declaration for the concrete subset of template parameters it uses. This lets one `@template T, E` file contain a type depending on both parameters, a function depending only on `T`, and another function depending on both.

For each call to a dependent function, libclang supplies the argument-expression types. The generator structurally matches those input types against the function's parameter types to infer the template variables. For example:

- `T *` matched with `TestInt *` gives `T = TestInt` and dispatches on `TestInt *`.
- `T, E` matched with `TestInt, ErrorCode` gives the pair `(TestInt, ErrorCode)` and dispatches on both arguments.
- `Result *` matched with `Result_TestInt_ErrorCode *` recovers both arguments from the known `Result(TestInt, ErrorCode)` constructor instance.

Return types are rewritten after resolving variables from input types; they are not used as dispatch inputs. If a function uses a variable that cannot be recovered from any input type, no automatic call dispatch can be formed for that use.

The source-range rewriter substitutes declared type variables and suffixes dependent declarations. It skips comments and literals, and preserves offsets so emitted `#line` directives map declarations back to their template locations.

---

## 6. Output format of `tmpl.h`

Order of emission:

1. `#ifndef GEN_GUARD` / `#define GEN_GUARD`
2. A comment: `/* AUTO-GENERATED by tmplgen.c. DO NOT EDIT. */` (no timestamps)
3. `GEN_PREAMBLE`, then the deduplicated normal `#include` lines found in templates
4. Emit only dependent typedefs and functions requested by observed type-constructor uses and function calls. Each emitted declaration is rewritten for its own dependency subset.
5. Emit a type-constructor macro for each used dependent typedef. For example, `Result(T, E)` maps to a concrete suffix such as `Result_TestInt_ErrorCode`.
6. Emit argument helper macros through the greatest function argument index used by inferred dispatch, only when dispatch associations are emitted:
   ```c
   #define GEN_ARG0(...) GEN_ARG0_(__VA_ARGS__, 0)
   #define GEN_ARG0_(a, ...) a
   #define GEN_ARG1(...) GEN_ARG1_(__VA_ARGS__, 0, 0)
   #define GEN_ARG1_(a, b, ...) b
   ```
   The trailing dummy values keep these `-pedantic`-clean in C99 when the call has few arguments.
7. One dispatch macro per called dependent function, **after** all declarations so template bodies are not re-expanded. Associations contain only input-type combinations observed at calls:
   - `GEN_DISPATCH_GENERIC`:
     ```c
     #define max(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
         int: max_int, long: max_long, double: max_double, \
         default: max__no_match_for_argument_type)(__VA_ARGS__)
     ```
    - `GEN_DISPATCH_GNU` (uses `__typeof__`, which works even under `-std=c99`):
     ```c
      #define max(...) __builtin_choose_expr( \
         __builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), int), max_int, \
         __builtin_choose_expr( \
         __builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), double), max_double, \
          max__no_match_for_argument_type))(__VA_ARGS__)
      ```
    - `GEN_DISPATCH_AUTO`: emit `_Generic` under C11 or newer and GNU builtins under GNU-compatible C99. This lets one `tmpl.h` serve both dispatch test modes; `.clangd` assigns the corresponding standard to each test file.
   - `GEN_DISPATCH_NONE`: emit nothing here.
    - The `..._no_match_for_argument_type` identifier is intentionally **never defined**, so a bad call fails with a diagnostic that names it. It is declared as `extern void <fn>__no_match_for_argument_type(struct tmplgen_no_association_for_<fn>_argument, ...);` with a unique mismatched struct parameter: an unmatched first argument produces a hard Sema error naming the argument type and the missing association, instead of a confusing arity error. The generated header also comments that this can mean either a wrong argument type or a stale `tmpl.h` that needs regeneration. The default arm must not be the only thing keeping an unsupported call alive.
     - For one type variable, dispatch uses one `_Generic` selection. For multiple independent input variables, selections nest over the corresponding argument positions.
8. `#endif /* GEN_GUARD */`

### Dedup rule for `_Generic`

`_Generic` is a hard error if two associations name compatible types. Before emission, deduplicate observed input-type combinations by their libclang canonical type tuple. Keep compile tests for aliases and multi-argument associations.

---

## 7. Command line

```
tmplgen            generate tmpl.h (default)
tmplgen --check    generate in memory, compare with the existing file; exit 1 if different (for CI)
tmplgen --watch    poll inputs every GEN_WATCH_INTERVAL_MS and regenerate on change
tmplgen --verbose  print what was read/written to stderr (combinable with the others)
tmplgen --help     usage
```

Exit codes: `0` success (including "unchanged"), `1` error or `--check` mismatch, `2` bad usage.

On a normal run print one line: `tmpl.h: written` or `tmpl.h: unchanged`.

**Watch mode** fingerprints discovered source and template file contents on each poll, and runs full generation only when that fingerprint changes. This avoids repeated libclang parsing while inputs are idle; the pre-scan still reads the inputs at the configured interval. Errors print and the watcher continues. Since it reads from disk, it cannot see unsaved editor-buffer changes.

---

## 8. Program structure (suggested order in the file)

Banner-commented sections, top to bottom:

1. Header comment: what this is, how to build, how to run, and a 10-line "how it works".
2. Configuration (section 3).
3. Includes (`stdio.h`, `stdlib.h`, `string.h`, `ctype.h`, `<clang-c/Index.h>`).
4. **Dynamic string buffer**: `typedef struct { char *p; size_t len, cap; } Buf;` with `buf_init`, `buf_putc`, `buf_puts`, `buf_putn`, `buf_printf_line_directive`, `buf_free`. All allocation failures call `die("out of memory")`.
5. **Helpers**: `die(const char *fmt, ...)`, dynamic strings/lists, C tokenization, source-tree traversal, and file comparison.
6. **Template analysis (libclang)**: parse each `@template` file, identify dependent typedefs and functions, and record the template variables used by each declaration and each function input.
7. **Source scan (libclang)**: pass 1 captures type-constructor argument tuples and typedef aliases. Pass 2 supplies tuple-specific placeholder types, captures calls to dependent functions, and matches argument expression cursors to input parameters to infer each function's parameter tuple.
8. **Rewrite function**: substitute template variables and suffix each dependent declaration using only the variables it depends on.
9. **Emitters**: preamble/includes, dependent declaration fragments, type-constructor macros, argument helpers, and inferred dispatch macros (including nested multi-input dispatch).
10. **`generate(Buf *out)`**: orchestrates scanning and emission.
11. **Platform section**: directory enumeration and optional watch sleeping.
12. **`main`**: parse args, call `generate`, then write or compare.

Each function should have a one-line comment saying what it does. Keep functions under ~50 lines where practical.

---

## 9. Validation and errors

Fail early with messages of the form `tmplgen: tmpl/math.tmpl.h: <problem>` and exit code 1, for:

- Template file missing or unreadable.
- Missing or repeated `@template` declarations, duplicate parameters, and malformed constructor calls (wrong arity or unsupported type-argument tokens).
- A generic call whose input types do not determine all type variables used by that function (warn and omit that dispatch association).
- A dependent function or type constructor name declared by more than one template.
- Colliding generated suffixes for distinct type-argument tuples.

An empty `GEN_TEMPLATES` is valid and produces a header with only the guard and preamble.

---

## 10. Examples the implementation must reproduce

With `typedef int TestInt;` and `typedef enum { ErrorNone } ErrorCode;` visible before `tmpl.h`, source such as:

```c
Result(TestInt, ErrorCode) result = result_make(value, error);
TestInt value = result_value(&result);
```

must generate the pair specialization `Result_TestInt_ErrorCode`, the functions `result_make_TestInt_ErrorCode` and `result_value_TestInt_ErrorCode`, a two-input dispatch for `result_make`, and a dispatch on `Result_TestInt_ErrorCode *` for `result_value`. The returned `TestInt` type comes from matching the input `Result(TestInt, ErrorCode) *`.

`Triple(A, B, C)` and a function taking `A`, `B`, and `C` must likewise produce a three-input dispatch. A function in the same `@template T, E` file that only depends on `T` must produce a `..._T` instance rather than requiring an unrelated `E` type.

The user owns typedefs such as `TestInt`, `ErrorCode`, and `Tag`; the generator does not invent aliases or emit typedef rows for them.

---

## 11. Optional platform section (isolated)

Directory traversal is platform-specific and isolated: POSIX uses `opendir`/`readdir`, Windows uses `FindFirstFile`. Watch sleeping is also isolated in `static void sleep_ms(unsigned ms)`:

- `_WIN32`: `Sleep(ms)` from `<windows.h>`.
- POSIX: `nanosleep` from `<time.h>`. This needs `#define _POSIX_C_SOURCE 199309L` **before** any include, so place that define behind the same `#if` guard at the very top of the file.
- Fallback: a `clock()` busy-wait with a comment that it burns CPU, so users should prefer an OS where one of the above exists.

With `GEN_ENABLE_WATCH 0`, the watch sleep code and its platform headers are excluded; platform directory-enumeration support remains enabled for source discovery.

---

## 12. Non-goals

- No hand-rolled C type checker. libclang supplies declaration and expression types; the generator matches input types against template parameter patterns.
- No raw return-type inference. Return types are substituted from template variables inferred from input types; a variable absent from all inputs cannot be selected by return type alone.
- No inference through a macro that discards its dependent input before libclang can see the call argument. The association is skipped with a warning.
- Type-constructor arguments are single-token typedef names or built-in type keywords (for example `int`). Use a typedef for qualified, pointer, or multi-token C type declarators.
- A direct suffixed call to an n-ary function requires its type tuple to have been discovered from a constructor use or another inferable call; the suffix is not reverse-parsed into ambiguous underscore-separated type names.
- No source-buffer integration with clangd; scanning observes files on disk.
- Go-to-definition on a generic function-like macro may land on its dispatch association; use the suffixed function name when direct implementation navigation matters.
- No CMake/Makefile generation.

---

## 13. Acceptance checklist

- [ ] Builds warning-free with `-std=c99 -Wall -Wextra -pedantic -Wconversion -Wshadow ... -lclang` on gcc and clang (MSVC is out of scope because libclang's C API is linked dynamically).
- [ ] Running twice in a row prints `unchanged` the second time and does not touch the file.
- [ ] `--watch` regenerates after source/template content changes and skips full generation while inputs are unchanged.
- [ ] `--check` exits 1 after editing a template and 0 after regenerating.
- [ ] One-, two-, and three-parameter type constructors and free functions instantiate from observed uses.
- [ ] A `T`-only function in a `@template T, E` file specializes on `T` without requiring `E`.
- [ ] A `Result(T, E) *` input infers both types and produces a function with a substituted `T` return type.
- [ ] Direct `T *`, two-argument `T, E`, and three-argument `A, B, C` signatures generate matching dispatch keys.
- [ ] Template parameters inside comments, strings, character literals, and longer identifiers are untouched.
- [ ] `tmpl.h` compiles under C11 and GNU99 with `GEN_DISPATCH_AUTO`; clangd gets matching standards from `.clangd` for the dispatch test files.
- [ ] There is no manual type table or per-function dispatch annotation; types come from constructor uses and inferred function inputs.
- [ ] `max(1, 2)` and `max(1.0, 2.0)` resolve to the right instance, and a call whose argument type has no discovered association fails with an error naming `max__no_match_for_argument_type`.
- [ ] `Vec(TestInt) v = {0}; push(&v, value);` compiles, and `Vec(TestInt)` requests the `Vec_TestInt` specialization.
- [ ] `Result(TestInt, ErrorCode)` and `Triple(TestInt, ErrorCode, Tag)` compile; `result_make` and `triple_make` dispatch on two and three input types.
- [ ] clangd go-to-definition on `max_int` lands in `tmpl/math.tmpl.h` when `GEN_EMIT_LINE` is 1.
- [ ] Source scanning excludes `tmpl/`, dot-directories, build output, and the generated `tmpl.h` itself.
- [ ] A new `Vec(T)` / `push` or multi-argument constructor use is discovered from a stale `tmpl.h`, because discovery parses sources against an in-memory bootstrap header instead of the checked-in output.

---

## 14. Instructions to the code generator (LLM)

- Output the complete `tmplgen.c` in one code block, with no placeholders, "..." elisions, or TODOs.
- Use only the unified `// @template T[, E, ...]` directive; do not add legacy per-function/type/dispatch directives.
- Infer function and type specialization from libclang declarations and observed inputs.
- Prefer clear, boring code. No clever pointer tricks, and one concept per function.
- After the code, give three short lines: the build command, a minimal run example, and how to switch the dispatch backend.
