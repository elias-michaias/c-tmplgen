# Spec: `tmplgen.c`, a single-file C99 generic-template generator

Write **one C source file, `tmplgen.c`**, that compiles with any C99 compiler linked against the libclang C API. The executable reads `.tmpl.h` template files from a `tmpl/` directory and writes a mirrored set of headers under `gen/` plus the aggregate `gen/all.gen.h`. Each template gets its own generated file containing its concrete declarations and dispatch macros; the aggregate includes all of them.

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
  test/lib/          <- shared test/example headers (schema rows, test types)
  gen/                 <- GENERATED mirror of tmpl/, never hand-edited
    all.gen.h          <- aggregate include for all generated template headers
    math.gen.h
    vec.gen.h
    result.gen.h
    triple.gen.h
  .clangd            <- editor language modes for the dispatch test cases
  tmpl/
    math.tmpl.h      <- templates, plain C with declared type variables
    vec.tmpl.h
    result.tmpl.h
    triple.tmpl.h
  test/cases/ ...    <- API and dispatch tests include `../../gen/all.gen.h`
  src/ ...           <- user code does `#include "gen/all.gen.h"`
```

Typical use: `cc tmplgen.c -lclang -o tmplgen && ./tmplgen`

Run `./test/run.sh` for the positive suite. Run `./test/negative.sh` to rerun it and then compile every negative fixture, printing the expected diagnostics for inspection.

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
#define GEN_OUTPUT      "gen/all.gen.h" /* aggregate header, relative to cwd           */
#define GEN_GUARD       "TMPL_H"     /* include guard for the aggregate header        */
#define GEN_LSP_GUARD   "TMPLGEN"    /* #ifndef block stripped from template bodies  */
#define GEN_DISPATCH    GEN_DISPATCH_AUTO
#define GEN_EMIT_LINE   0            /* 1 maps locations to templates; 0 keeps
                                        concrete definitions in generated headers      */
#define GEN_ENABLE_WATCH     1       /* 1: compile --watch support; 0: disable --watch */
#define GEN_WATCH_INTERVAL_MS 500    /* poll period for --watch                       */

/* Text pasted right after the include guard. Put system includes and your own
   type headers here. */
#define GEN_PREAMBLE "#include <stddef.h>\n#include <stdlib.h>\n"

/* Top-level *.tmpl.h files in GEN_TMPL_DIR are discovered and sorted by stem.
   Types are discovered from source-file usages. */

/* =================== END OF CONFIGURATION ================== */
```

Template files are discovered from the selected directory at runtime. All other limits (line length, name length, and so on) must be either `#define`d here with comments or handled with dynamically grown buffers. Prefer dynamic buffers.

---

## 4. Template format

A template is ordinary C plus a declaration-scoped `@template` directive immediately above each dependent top-level typedef, struct/union definition, and function. The directive lists only the type variables that declaration is allowed to use. Ordinary non-generic declarations need no directive; an empty `// @template` is accepted when an explicit marker is useful.

```c
// @template T
```

or, for any number of variables:

```c
// @template T, E, A
```

For standalone editor parsing, declare fallback typedefs inside `#ifndef TMPLGEN`; the generator strips that block while preserving source offsets. Ordinary `#include` lines are gathered into the generated preamble. A template may include another configured `.tmpl.h`; the generated module rewrites that include to the matching `.gen.h`, making imported generic declarations visible to cross-file calls.

### Example `tmpl/math.tmpl.h`

```c
#ifndef TMPLGEN
typedef int T;
#endif
// @template T
static inline T max(T a, T b) { return a > b ? a : b; }
// @template T
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

// @template T
static inline void push(Vec(T) *v, T x) {
    if (v->len == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        v->data = (T *)realloc(v->data, v->cap * sizeof(T));
    }
    v->data[v->len++] = x;
}
```

Users then write `Vec(MyType) v = {0}; push(&v, value);`. `Vec(MyType)` requests the specialization and expands to the concrete `Vec_MyType` type.

Type applications can appear in template signatures. `Option(T)` means the concrete `Option` specialization whose payload is the function's `T`; the application is rewritten for each generated instance. Untypedef'd `struct` and `union` tags are also constructors, so `TaggedBox(T)` can denote a concrete `struct TaggedBox_TestInt`.

Template variables may require structural rows using `<:`. The names on the right refer to visible named struct/union definitions whose fields form required rows:

```c
// @template T <: Physics
// @template U <: Entity + Named
// @template T <: Physics, U <: Named + Actor
```

For `T <: Physics`, a concrete named type must provide every field declared by `Physics` with a compatible C field type. Extra fields are allowed and neither offsets nor total size are compared. `+` combines requirements, while commas continue to separate template variables. Put the schema definitions in the template or in an included header. The generated specialization emits `_Static_assert` checks for the required fields; GCC/Clang also use this assertion form in pre-C11 modes.

For an arbitrary compile-time constraint, use `|=`. Macro-call terms use the shorthand with the type argument omitted; the concrete type is injected as the first macro argument. A macro that only needs the type can be written without parentheses. Non-call expressions have occurrences of the template variable substituted directly. The result must be an integer constant expression accepted by `_Static_assert`:

```c
// @template T |= MY_CONSTRAINT(MyRandomTypeName) + MY_CONSTRAINT2
```

Top-level `+` separates constraints, while commas inside a parenthesized macro call remain part of that expression. Failures report that the variable failed to satisfy the expression.

Adjacent annotation lines are accumulated for the next declaration, allowing long parameter lists or constraints to wrap cleanly. Continuations may use either another `// @template` or the short `// @` form after an initial marker:

```c
// @template T <: MySubtype
// @template T |= MyConstraint(T)
// @template E <: OtherThing

// @template
// @ T <: MySubtype
// @ T |= MyConstraint(T)
// @ E <: OtherThing
```

For a multi-parameter type and its functions:

```c
#ifndef TMPLGEN
typedef int T;
typedef int E;
#endif
typedef enum { RESULT_OK, RESULT_ERROR } ResultTag;
typedef enum { OPTION_NONE, OPTION_SOME } OptionTag;

// @template T, E
typedef union { T ok; E error; } ResultPayload;
// @template T, E
typedef struct {
    ResultTag tag;
    ResultPayload(T, E) payload;
} Result;
// @template T
typedef union { T some; } OptionPayload;
// @template T
typedef struct {
    OptionTag tag;
    OptionPayload(T) payload;
} Option;
// @template T, E
static inline T result_value(Result(T, E) *r) { return r->payload.ok; }
// @template T, E
static inline Result(T, E) result_make(ResultTag tag, T value, E error) {
    Result r = { .tag = tag };
    if (tag == RESULT_OK) r.payload.ok = value;
    else r.payload.error = error;
    return r;
}
```

`Result(Value, Error)` requests the type tuple. The tag selects which union member is active; consumers can `switch (r.tag)` on `RESULT_OK` and `RESULT_ERROR`. `Option(T)` similarly uses `OPTION_SOME` and `OPTION_NONE`. `result_value`, `result_error`, and `option_value` assert that the matching variant is active before reading its payload.

Calls to `result_value(&r)` infer both variables from the input `Result(T, E) *`; calls to `result_make(RESULT_OK, value, error)` infer them from its two payload inputs. The tag is a monomorphic argument and does not contribute to the inferred type tuple.

---

## 5. Inference and rewriting

libclang parses each template and records its dependent typedefs, function signatures, and source ranges. The generator emits each dependent declaration for the concrete subset of template parameters it uses. This lets one `@template T, E` file contain a type depending on both parameters, a function depending only on `T`, and another function depending on both.

For each call to a dependent function, libclang supplies the argument-expression types. The generator structurally matches those input types against the function's parameter patterns to infer the template variables. Type applications preserve relationships rather than treating each constructor as an unrelated opaque type. For example:

- `T *` matched with `TestInt *` gives `T = TestInt` and dispatches on `TestInt *`.
- `T, E` matched with `TestInt, ErrorCode` gives the pair `(TestInt, ErrorCode)` and dispatches on both arguments.
- `Result(T, E) *` matched with `Result_TestInt_ErrorCode *` recovers both arguments from the constructor application.
- Two `Option(T) *` inputs bind the same `T`, so both must use the same concrete `Option` specialization.
- `Box(T) *` and `Box(U) *` inputs bind independent variables; a `Box(U)` return uses the type inferred for `U`.
- `T <: Physics` accepts a concrete type with compatible `x` and `y` fields even if it has additional fields; `U <: Entity + Named` requires both rows.

Return types are rewritten after resolving variables from input types; they are not used as dispatch inputs. If a function uses a variable that cannot be recovered from any input type, no automatic call dispatch can be formed for that use.

After source calls seed specializations, the generator follows recognized generic calls across template files until the set of function and type instances stops growing. Callee variables are mapped from the caller's actual argument patterns, including renamed and reordered variables. Instances are deduplicated by their concrete type tuple, so self-recursive and mutually recursive calls converge; concrete forward declarations are emitted before definitions so recursive bodies have prototypes. A cross-file call needs the caller template to include the callee's `.tmpl.h`; constructor applications owned by another template file are not yet available as caller-side type patterns.

The source-range rewriter substitutes declared type variables and suffixes dependent declarations. It skips comments and literals, and preserves offsets so emitted `#line` directives map declarations back to their template locations.

---

## 6. Generated output

For each template, write `gen/<stem>.gen.h` with:

1. A template-specific include guard and `/* AUTO-GENERATED by tmplgen.c. DO NOT EDIT. */` comment.
2. `GEN_PREAMBLE` and that template's ordinary `#include` directives.
3. Only the dependent typedefs and functions requested by observed type-constructor uses and function calls, each rewritten for its own dependency subset.
   Constrained specializations include static assertions at the template declaration location, and dispatch macros use `#line` directives so macro diagnostics map back to their source functions.
4. Type-constructor macros for dependent typedefs and tagged types. For example, `Result(T, E)` maps to `Result_TestInt_ErrorCode`.
5. Argument helper macros through the greatest function argument index used by that template's inferred dispatch:
   ```c
   #define GEN_ARG0(...) GEN_ARG0_(__VA_ARGS__, 0)
   #define GEN_ARG0_(a, ...) a
   #define GEN_ARG1(...) GEN_ARG1_(__VA_ARGS__, 0, 0)
   #define GEN_ARG1_(a, b, ...) b
   ```
   The trailing dummy values keep these `-pedantic`-clean in C99 when the call has few arguments.
6. One dispatch macro per called dependent function, **after** declarations so template bodies are not re-expanded. Associations contain only input-type combinations observed at calls:
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
     - `GEN_DISPATCH_AUTO`: emit `_Generic` under C11 or newer and GNU builtins under GNU-compatible C99. This lets the generated modules serve both dispatch test modes; `.clangd` assigns the corresponding standard to each test file.
    - `GEN_DISPATCH_NONE`: emit nothing here.
      - The `..._no_match_for_argument_type` identifier is intentionally **never defined**, so a bad call fails with a diagnostic that names it. It is declared as `extern void <fn>__no_match_for_argument_type(struct tmplgen_no_association_for_<fn>_argument, ...);` with a unique mismatched struct parameter. The generated module comments that this can mean either a wrong argument type or stale generated headers that need regeneration.
      - For one type variable, dispatch uses one `_Generic` selection. For multiple independent input variables, selections nest over the corresponding argument positions.

Before each dispatch macro, emit navigation prototypes for its concrete targets, then a `///` comment containing the original template function signature. The macro associations refer to those concrete function names; go-to-definition on a target name can navigate to its generated definition in that template's module.

`gen/all.gen.h` has its own include guard, includes every generated module in sorted template-stem order, then emits all constructor and dispatch macros after every declaration. While included through the aggregate, modules suppress their local macro section to prevent an earlier module's macro from rewriting a later module body. A module included directly emits its own constructor and dispatch macros. Sources should normally include the aggregate; editors can open a specific module to navigate within that template's generated output.

### Dedup rule for `_Generic`

`_Generic` is a hard error if two associations name compatible types. Before emission, deduplicate observed input-type combinations by their libclang canonical type tuple. Keep compile tests for aliases and multi-argument associations.

---

## 7. Command line

```
tmplgen                generate gen/<stem>.gen.h files and gen/all.gen.h (default)
tmplgen --compiled     also emit gen/<stem>.gen.c implementations
tmplgen --check        compare every generated file; exit 1 if any differ
tmplgen --no-cache     always run full libclang analysis and generation
tmplgen --watch        poll inputs every GEN_WATCH_INTERVAL_MS and regenerate on change
tmplgen --verbose      print what was read/written to stderr
tmplgen --help         usage
```

Exit codes: `0` success (including "unchanged"), `1` error or `--check` mismatch, `2` bad usage.

Normal runs and successful `--check` runs use a content-validated cache in the generated directory to skip libclang when inputs and generated outputs are unchanged. The cache is ignored build metadata; `--check` does not write generated outputs. `--no-cache` forces full analysis. Runs print one `path: written` or `path: unchanged` line for every generated output.

`--compiled` keeps declarations, constructors, and dispatch macros in the generated headers while moving generic function bodies into per-template `.gen.c` files. Compile and link those implementation files with the project. The default remains header-only.

**Watch mode** fingerprints discovered source and template file contents on each poll, and runs full generation only when that fingerprint changes. This avoids repeated libclang parsing while inputs are idle; the pre-scan still reads the inputs at the configured interval. Errors print and the watcher continues. Since it reads from disk, it cannot see unsaved editor-buffer changes.

Pass the project language mode to libclang when needed, for example `--clang-arg=-std=c23` for C23 `auto`, or `--clang-arg=-std=gnu11` for GNU extensions such as `__auto_type`.

---

## 8. Program structure (suggested order in the file)

Banner-commented sections, top to bottom:

1. Header comment: what this is, how to build, how to run, and a 10-line "how it works".
2. Configuration (section 3).
3. Includes (`stdio.h`, `stdlib.h`, `string.h`, `ctype.h`, `<clang-c/Index.h>`).
4. **Dynamic string buffer**: `typedef struct { char *p; size_t len, cap; } Buf;` with `buf_init`, `buf_putc`, `buf_puts`, `buf_putn`, `buf_printf_line_directive`, `buf_free`. All allocation failures call `die("out of memory")`.
5. **Helpers**: `die(const char *fmt, ...)`, dynamic strings/lists, C tokenization, source-tree traversal, and file comparison.
6. **Template analysis (libclang)**: parse each `@template` file, identify dependent typedefs and functions, and record the template variables used by each declaration and each function input.
7. **Source scan (libclang)**: pass 1 captures type-constructor uses, typedef aliases, and function-input tuples that can be inferred directly. Pass 2 supplies tuple-specific placeholder types and typed function prototypes/macros, so inferred declarations such as C23 `auto value = result_make(...)` carry the correct composite return type when later calls are analyzed.
8. **Rewrite function**: substitute template variables and suffix each dependent declaration using only the variables it depends on.
9. **Emitters**: preamble/includes, dependent declaration fragments, type-constructor macros, argument helpers, and inferred dispatch macros (including nested multi-input dispatch).
10. **`generate(GeneratedFileList *out)`**: orchestrates scanning and emits each module plus the aggregate.
11. **Platform section**: directory enumeration and optional watch sleeping.
12. **`main`**: parse args, call `generate`, then write or compare.

Each function should have a one-line comment saying what it does. Keep functions under ~50 lines where practical.

---

## 9. Validation and errors

Fail early with messages of the form `tmplgen: tmpl/math.tmpl.h: <problem>` and exit code 1, for:

- A discovered template file is unreadable or malformed.
- Missing declaration-scoped `@template` directives, unused directives, duplicate parameters, unlisted variables, and malformed constructor applications (wrong arity or unsupported type-argument tokens).
- A generic call whose input types do not determine all type variables used by that function (warn and omit that dispatch association).
- A dependent function or type constructor name declared by more than one template.
- Colliding generated suffixes for distinct type-argument tuples.

An empty template directory is valid and produces an aggregate header with only the guard and preamble.

---

## 10. Examples the implementation must reproduce

With `typedef int TestInt;` and `typedef enum { ErrorNone } ErrorCode;` visible before `gen/all.gen.h`, source such as:

```c
Result(TestInt, ErrorCode) result = result_make(RESULT_OK, value, error);
TestInt value = result_value(&result);
```

must generate the pair specialization `Result_TestInt_ErrorCode`, the functions `result_make_TestInt_ErrorCode` and `result_value_TestInt_ErrorCode`, a two-type-input dispatch for `result_make`, and a dispatch on `Result_TestInt_ErrorCode *` for `result_value`. The returned `TestInt` type comes from matching the input `Result(TestInt, ErrorCode) *`.

`Triple(A, B, C)` and a function taking `A`, `B`, and `C` must likewise produce a three-input dispatch. A function in the same `@template T, E` file that only depends on `T` must produce a `..._T` instance rather than requiring an unrelated `E` type. Calls whose tuples appear only in inputs still work through inferred declarations:

```c
auto result = result_make(RESULT_OK, long_value, error_code); /* C23 */
__auto_type other = result_make(RESULT_ERROR, tag, inferred_error); /* compiler extension in C11 mode */
```

The bootstrap used for the second libclang pass provides the concrete return type, allowing later uses such as `result_value(&result)` or `result_error(&other)` to recover the tuple.

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
- At source use sites, type-constructor arguments are single-token typedef names or built-in type keywords (for example `int`). In template signatures, applications such as `Box(U)` use declared template-variable names or concrete single-token type names. Use a typedef for qualified, pointer, or multi-token type arguments.
- A direct suffixed call to an n-ary function requires its type tuple to have been discovered from a constructor use or another inferable call; the suffix is not reverse-parsed into ambiguous underscore-separated type names.
- No source-buffer integration with clangd; scanning observes files on disk.
- Go-to-definition on an unsuffixed generic call still lands on its macro. Each dispatch block has a `///` template-signature comment and adjacent prototypes for concrete suffixed targets; go-to-definition on one of those targets leads to its module implementation.
- No CMake/Makefile generation.

---

## 13. Acceptance checklist

- [ ] Builds warning-free with `-std=c99 -Wall -Wextra -pedantic -Wconversion -Wshadow ... -lclang` on gcc and clang (MSVC is out of scope because libclang's C API is linked dynamically).
- [ ] Running twice in a row reports every generated module and the aggregate as `unchanged` the second time and does not touch them.
- [ ] `--watch` regenerates after source/template content changes and skips full generation while inputs are unchanged.
- [ ] `--check` exits 1 after editing a template and 0 after regenerating.
- [ ] Each dependent top-level type/function has its own `@template` directive; non-generic declarations need none.
- [ ] One-, two-, and three-parameter constructors and free functions instantiate from observed uses, including direct struct/union tags.
- [ ] `Option(T), Option(T)` enforces a shared type variable, while `Box(T), Box(U) -> Box(U)` supports distinct input types and a dependent return.
- [ ] `<:` constraints accept row-width structural matches, enforce every `+` requirement, and report incompatible/missing fields with template-located static assertions.
- [ ] A `T`-only function in a `@template T, E` file specializes on `T` without requiring `E`.
- [ ] A `Result(T, E) *` input infers both types and produces a function with a substituted `T` return type.
- [ ] Direct `T *`, two-argument `T, E`, and three-argument `A, B, C` signatures generate matching dispatch keys.
- [ ] C23 `auto` and GNU `__auto_type` preserve a generic call's composite return type, allowing a later call to infer its tuple even when no `Result(T, E)` use appears in source.
- [ ] Template parameters inside comments, strings, character literals, and longer identifiers are untouched.
- [ ] `gen/all.gen.h` and each per-template header compile under C11 and GNU99 with `GEN_DISPATCH_AUTO`; clangd gets matching standards from `.clangd` for the dispatch test files.
- [ ] There is no manual type table or per-function dispatch annotation; types come from constructor uses and inferred function inputs.
- [ ] `max(1, 2)` and `max(1.0, 2.0)` resolve to the right instance, and a call whose argument type has no discovered association fails with an error naming `max__no_match_for_argument_type`.
- [ ] `Vec(TestInt) v = {0}; push(&v, value);` compiles, and `Vec(TestInt)` requests the `Vec_TestInt` specialization.
- [ ] `Result(TestInt, ErrorCode)` and `Triple(TestInt, ErrorCode, Tag)` compile; `result_make` and `triple_make` dispatch on two and three input types.
- [ ] clangd go-to-definition on `max_int` lands in `tmpl/math.tmpl.h` when `GEN_EMIT_LINE` is 1.
- [ ] Source scanning excludes `tmpl/`, `gen/`, dot-directories, and build output.
- [ ] A new `Vec(T)` / `push` or multi-argument constructor use is discovered even when generated headers are stale, because discovery parses sources against an in-memory bootstrap header instead of checked-in output.

---

## 14. Instructions to the code generator (LLM)

- Output the complete `tmplgen.c` in one code block, with no placeholders, "..." elisions, or TODOs.
- Put `// @template T[, E, ...]` immediately above each dependent top-level declaration; non-generic declarations need no directive.
- Infer function and type specialization from libclang declarations and observed inputs.
- Prefer clear, boring code. No clever pointer tricks, and one concept per function.
- After the code, give three short lines: the build command, a minimal run example, and how to switch the dispatch backend.
