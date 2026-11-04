# Tooling and integration

`tmplgen` has two compiler-facing parts: the generator is compiled as C99, and
libclang parses templates and project sources to supply C declarations and
expression types. The generated C is then compiled with the project’s ordinary
compiler.

## Build requirements

Install a C compiler and the libclang development headers and library. The
generator includes `<clang-c/Index.h>` and links against libclang:

```sh
make
```

Some systems install libclang under a versioned library name or outside the
default linker paths; use the matching include and linker flags for that LLVM
installation. The generator’s C compiler does not need to be Clang, but its
libclang headers and library must come from a compatible installation.

Use `make -j` to compile the independent modules in parallel. The same C99
sources are reused by the Clang and clangd plugin builds.

## Source layout

The generator implementation lives under `src/`:

| File | Responsibility |
| --- | --- |
| `driver.c` | Top-level generation, CLI, watch mode, and entry points |
| `template.c` | Annotation parsing and libclang declaration analysis |
| `discover.c` | Project-source analysis and call-site inference |
| `model.c` | Template model, specialization registries, and shared queries |
| `rewrite.c` | Argument rewriting and dependency expansion |
| `emit.c` | Specialization, bootstrap, and dispatch emission |
| `output.c` | Output paths, output registry, and type requirements |
| `reflect.c` | Reflection graph construction and emission |
| `cache.c` | Input signatures and persistent caches |
| `util.c` | Buffers, strings, paths, and file utilities |
| `state.c` | Shared invocation and discovery state |
| `internal.h` | Shared types and cross-module declarations |

Module-private functions and data have internal linkage; only actual
cross-module interfaces are declared in `internal.h`.

## Keep libclang’s view aligned with the build

libclang, rather than the compiler that later builds the program, performs
template and source analysis. Pass the project’s relevant options through
`--clang-arg`:

```sh
./tmplgen \
  --clang-arg=-std=c11 \
  --clang-arg=-Iinclude \
  --clang-arg=-DPROJECT_FEATURE=1
```

Use the same language standard, include directories, feature defines, and target
options as the real build. This matters for conditional declarations, compiler
extensions, and inferred expression types. For example, C23 `auto` inference
requires a C23 mode for libclang; GNU `__auto_type` requires a GNU-compatible
mode. With `--reflect=project` it also decides the reflected layout numbers;
target options such as `--target=…`, `-m32` or `-fshort-enums` must match, or
the generated layout checks fail the build.

Some libclang installs cannot locate their own builtin headers (`stddef.h`,
`stdarg.h`). Unless `--clang-arg=-resource-dir=…` is given, tmplgen looks for
the headers of its libclang version in the usual install locations
(`/usr/lib/clang/<major>`, `/usr/lib/llvm-<major>/lib/clang/<major>`, …) and
passes that directory to libclang.

The generator recursively scans `.c` and `.h` files under `--source-root`.
Generated module outputs and the selected template directory are skipped, as
are dot-directories and directories named `build` or `dist`. Keep the source
root scoped to the project inputs you want analyzed; headers are parsed as
source units too.

Templates can include other `.tmpl.h` files. The corresponding include is
rewritten in generated modules to the matching `.gen.h`; include the template
module in the template source when one template calls another. Application
translation units should normally include the aggregate header.

## Dispatch and application compiler

The default `auto` backend follows the language mode given to libclang:

- C11 and newer: `_Generic` selects a concrete function name at compile time.
- GNU C modes: GNU type builtins select a concrete function name.

`generic` forces `_Generic`, `gnu` forces GNU builtins, and `none` omits dispatch
macros so callers use suffixed concrete functions directly. The consumer
compiler must accept the selected output syntax. The dispatch selection does
not perform runtime type lookup; it resolves to a concrete call while compiling.

## Diagnostics and source locations

There are two stages that can report errors:

1. **Generation errors** are written to stderr with a `tmplgen:` prefix. When a
   source location is known, the format is `tmplgen: path:line: error: message`.
   These include malformed annotations, unsupported template patterns, file
   errors, and failures to parse a translation unit. Command-line usage errors
   return status 2; generation failures and stale `--check` outputs return
   status 1.
2. **Generated-C errors** are reported by the application compiler. Structural
   and satisfaction constraints become `_Static_assert`s with messages that
   identify the failed row or expression. For example:

   ```text
   test/tmpl/result.tmpl.h:101:1: error: static assertion failed: "tmplgen: TP is not a subtype of Physics (incompatible field y)"
   ```

   An unmatched dispatch similarly names the missing association, such as
   `max__no_match_for_argument_type`, and uses a deliberately incompatible
   parameter type to make the failure clear.

Generated files use `#line` directives so the compiler can attribute important
generated constructs to their template source. Dispatch macro definitions and
constraint assertions are mapped to template locations by default. Function
and type bodies remain attributed to the generated file unless `--emit-line` is
set:

```sh
./tmplgen --emit-line --clang-arg=-std=c11
```

With `--emit-line`, body diagnostics point back to the template declaration and
the generator restores the generated-file location after each body. Use
`--no-emit-line` to return to the default for a run. Source paths in `#line`
directives follow the paths used when loading templates; running from the
project root gives concise project-relative locations, while an absolute
`--template-dir` produces absolute paths. Pass `--line-path-base DIR` to write
template paths relative to `DIR` however the template directory was spelled;
for example `--template-dir "$PWD/tmpl" --line-path-base "$PWD"` produces the
same output as `--template-dir tmpl` run from the project root. The Clang and
clangd plugins pass the discovered project root this way, so editor and
standalone generation write identical headers.

The generator uses libclang for parsing and type extraction; it is not a
replacement for the application compiler’s diagnostic output. When a generated
call or body fails, compile the consuming translation unit with the same
language mode, include paths, and defines passed through `--clang-arg`. Use
`--verbose` to inspect which templates and source facts were read or reused.

The project’s `.clangd` demonstrates a related editor setup: template files
have `TMPLGEN` fallback declarations for standalone parsing, while test files
receive language modes matching their dispatch and inference requirements.
Those settings are examples for this repository, not a global configuration
installed by the generator.

## Generated files and build integration

Treat `<output-dir>/<stem>.gen.h`, `<output-dir>/<stem>.gen.c`, and the aggregate
header as generated outputs. The normal project defaults are `tmpl/` and
`gen/all.gen.h`, relative to the source root (`.`). This repository’s example
project lives under `test/`; its scripts pass `--template-dir test/tmpl` and
`--output test/gen/all.gen.h` explicitly. Build systems should run `tmplgen`
before compiling files that include generated headers. Regenerate when templates
change or when source changes add, remove, or alter generic calls or constructor
uses. A generated header can be committed if consumers should not need the
generator at build time, but it must be refreshed when those inputs change.

Header-only output is convenient and has no extra generated compilation unit;
it can increase repeated parse and compile work when large specializations are
included by many translation units. `--compiled` centralizes implementations
into generated `.gen.c` files, at the cost of compiling and linking those files.
Measure both modes with the application’s compiler and build topology.

## Caches and watch mode

Normal generation validates an unchanged-input cache and can skip the full
analysis if inputs and generated outputs still match. When analysis is needed,
per-source facts allow unaffected source units to be reused if their source,
included dependencies, and relevant analysis inputs remain valid. Cache files
are ignored build metadata under the generated directory. `--no-cache` disables
cache reuse and forces analysis; it is also useful for checking that cached
generation agrees with a full run. `--check --no-cache` compares outputs without
writing generated files.

`--watch` polls source and template contents, regenerating when they change. It
observes files on disk, not unsaved editor buffers. Use `--verbose` to inspect
reads, output updates, and source-facts reuse.

## Tests and benchmarks

Run the project test suites from the repository root:

```sh
./test/run.sh
CC=clang ./test/run.sh
./test/negative.sh
```

The benchmark runner and incremental-cache measurements are documented in
[the benchmark guide](../test/bench/README.md). Benchmarks are workload- and
compiler-dependent; use them to understand a project’s own analysis, generation,
and generated-code compilation costs rather than as universal performance
claims.

Optional integration builds are described in
[Plugin modes](plugins.md). Clang frontend plugins and clangd FeatureModules are
different host APIs; both reuse the C generator core, but each needs a
host-specific adapter and matching LLVM/Clang build.
