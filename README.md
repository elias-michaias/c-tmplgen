<h1 align="center">tmplgen</h1>

<p align="center">
  <strong>Generic C. Specialized at build time.</strong><br>
  Write reusable C templates. Infer concrete types from real call sites.<br>
  Generate ordinary C—ready for your compiler, debugger, and build system.
</p>

<p align="center">
  <a href="https://en.cppreference.com/w/c/99"><img alt="C99 generator" src="https://img.shields.io/badge/generator-C99-00599C?style=flat-square&logo=c"></a>
  <a href="https://clang.llvm.org/doxygen/group__CINDEX.html"><img alt="libclang analysis" src="https://img.shields.io/badge/analysis-libclang-262D3A?style=flat-square&logo=llvm"></a>
  <a href="doc/paradigm.md"><img alt="Ahead of time" src="https://img.shields.io/badge/specialization-ahead--of--time-287D73?style=flat-square"></a>
</p>

<table>
  <tr>
    <td align="center"><strong>Annotated C templates</strong></td>
    <td align="center">+</td>
    <td align="center"><strong>Types observed in source</strong></td>
    <td align="center">→</td>
    <td align="center"><strong>Concrete C specializations</strong></td>
  </tr>
</table>

<p align="center">
  <a href="code.png"><img src="code.png" alt="A generic Vec template and a C call site using Vec(int)" width="680"></a>
</p>

`tmplgen` is a source-aware generic-template generator for C. Mark the type
variables used by a declaration; libclang analyzes your project’s source and
infers the concrete specializations it needs. `tmplgen` emits concrete types,
functions, and compile-time dispatch macros. Your application then builds with
its normal C compiler.

It keeps the implementation in C without adding a runtime generic-object or
reflection layer. The generator is written in C99; libclang is needed when
generating, not when compiling the generated application.

## Quick start

### 1. Build `tmplgen`

Install a C compiler and the libclang development headers and library, then run
from the project root:

```sh
cc -std=c99 -Wall -Wextra -pedantic tmplgen.c -lclang -o tmplgen
```

### 2. Generate concrete code

```sh
./tmplgen --clang-arg=-std=c23
```

The default scan discovers `tmpl/*.tmpl.h` and C source/header files under the
current directory. Generated modules go to `gen/`; `gen/all.gen.h` is the
aggregate include.

### 3. Call the generic API

This repository’s `tmpl/math.tmpl.h` declares `max` with a type variable. The
call below is enough for the generator to infer the concrete type and emit the
matching implementation and dispatch association:

```c
#include "test/cases/test_types.h"
#include "gen/all.gen.h"

int main(void)
{
    TestInt low = 1;
    TestInt high = 2;
    return max(low, high) != 2;
}
```

For a walkthrough of your own templates and build integration, start with
[Usage](doc/usage.md).

## Highlights

| | |
| --- | --- |
| **Source-driven inference** | Infer type arguments from function inputs and constructor uses—no per-call type tables. |
| **Concrete C output** | Emit real specialized structs and functions that ordinary C tools can inspect. |
| **Compile-time dispatch** | Select observed specializations through `_Generic` or GNU builtins, with no runtime type lookup. |
| **Expressive constraints** | Require structural fields with `<:` or add compile-time checks with `@where`. |
| **Two output modes** | Use convenient header-only output, or move implementations into `.gen.c` files with `--compiled`. |
| **Incremental analysis** | Reuse validated whole-project and per-source facts; check generated files with `--check`. |

Generated dispatch can use C11 `_Generic`, GNU type-selection builtins, automatic
backend selection, or suffixed functions without dispatch macros. The chosen
backend must be supported by the application compiler; see
[Tooling](doc/tooling.md).

## Documentation

| Guide | What’s inside |
| --- | --- |
| [Usage & template syntax](doc/usage.md) | Annotations, type inference, constraints, CLI options, and output modes |
| [Tooling & integration](doc/tooling.md) | libclang, compiler flags, clangd, build integration, caches, and tests |
| [Design & alternatives](doc/paradigm.md) | The specialization model and trade-offs versus stb-style pointers, CC.h, C++, manual instantiation, and script generators |
| [Benchmarks](test/bench/README.md) | Benchmark methodology, workload controls, and recorded results |

## Develop and test

```sh
./test/run.sh
CC=clang ./test/run.sh
./test/negative.sh
```

Edit the `.tmpl.h` files and regenerate; files under `gen/` are generated
outputs. For the implementation model and a candid comparison with other
generic-programming approaches, see [Design & alternatives](doc/paradigm.md).
