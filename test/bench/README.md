# tmplgen performance benchmark

This directory contains a synthetic project generator and a repeatable benchmark
runner. The generated projects contain a configurable set of named struct types,
any configured number of interdependent `.tmpl.h` modules, chains of templated
functions, and source files with concrete calls. Each `(type, module, function)` combination
should produce a function specialization, making the generated tree grow
predictably with the selected dimensions. The generator discovers every
top-level `*.tmpl.h` file in `--template-dir` at runtime.

## Run a benchmark sweep

From the repository root:

```sh
python3 test/bench/run.py \
  --sizes 8,32,128,512 \
  --modules 4 \
  --functions-per-module 6 \
  --repeats 7 \
  --warmups 2 \
  --csv /tmp/tmplgen-bench.csv
```

The runner builds `tmplgen` with `CC` and `CFLAGS` (defaults: `cc` and
`-O2 -std=c99 -Wall -Wextra`), creates a fresh project for each size, times the
first generation separately, verifies repeatability with `--check`, then times
repeated normal generation and/or `--check` passes. By default it passes
`--no-cache` so every sample measures discovery, libclang parsing, inference,
and emission; use `--use-cache` to measure the unchanged-input fast path instead.
Output is suppressed to keep terminal I/O out of the measurement. A successful
`--check` may update the ignored cache metadata, but never generated outputs.

The summary includes workload dimensions, source/generated sizes, emitted
specialization count, cold time, and median/min/max repeated timings. `--csv`
stores every measured sample and the associated workload metrics.

## Calibrating project sizes

`cargo llvm-lines` is a useful Rust-side reference: its `Copies` total counts
LLVM function instances, including monomorphized generic functions. It reports
the selected root crate; dependency generics instantiated by that crate appear
there, while precompiled non-generic dependency code does not. On a fresh
checkout of production CLI [ripgrep 15.2.0](https://github.com/BurntSushi/ripgrep)
(commit `91f5d22`, 2026-10-27), using `cargo llvm-lines 0.4.48` and Rust
1.100.0-nightly:

| Build | LLVM IR lines | Function copies |
| --- | ---: | ---: |
| Debug (`cargo llvm-lines --bin rg`) | 250,444 | 6,896 |
| Release (`cargo llvm-lines --release --bin rg`) | 303,445 | 6,805 |

The current CSV run's 64 types × 12 modules × 8 functions produces 6,144
template specializations, close to ripgrep's total function-copy count. The
comparison is approximate: ripgrep's count includes non-generic functions, and
real projects have a skewed distribution of generic functions and type
combinations; this synthetic workload specializes every generated function for
every type.

Practical starting points for this harness:

| Label | Types × modules × functions/module | Generated specializations | Interpretation |
| --- | ---: | ---: | --- |
| Small tool/crate | 16 × 8 × 8 | 1,024 | Near the 1,222-copy example published for `cargo llvm-lines` itself |
| Production CLI | 64 × 12 × 8 | 6,144 | Same order as the measured ripgrep CLI |
| Large application stress | 128 × 16 × 8 | 16,384 | Roughly 2.4× the ripgrep reference |
| Very large workspace stress | 256 × 32 × 12 | 98,304 | Approaches a historical 115,658-monomorphization report for the full Servo build |

These are workload labels, not lines-of-code equivalents. For a typical
production CLI, a few thousand to roughly 10,000 generated specializations is a
reasonable benchmark range; 15,000–30,000 is a useful larger stress case. The
Servo figure is a historical whole-browser-build upper-scale reference, not a
typical application target. See [`cargo llvm-lines`](https://github.com/dtolnay/cargo-llvm-lines),
its [documented output](https://github.com/dtolnay/cargo-llvm-lines#output), and
the [Servo monomorphization report](https://internals.rust-lang.org/t/some-notes-on-reducing-monomorphizations/2459).

## One controlled compiler comparison

On 2026-10-30, I compared the 6,144-specialization case with a small Rust
generic equivalent: 64 concrete types, 12 modules, 8 chained generic functions
per module, and 768 calls. The Rust source was 62,439 bytes and was compiled to
an object directly with `rustc`; C used the normal `tmplgen` output (3.76 MB)
and six translation units compiled concurrently. This is a focused synthetic
comparison, not a timing prediction for arbitrary applications.

| Pipeline phase | Debug | Optimized |
| --- | ---: | ---: |
| Rust `rustc --emit=obj` (`opt-level=0/2`) | 0.281 s | 0.643 s |
| GCC 16.1.1 compile only (`-O0/-O2`, 6 TUs parallel) | 1.290 s | 0.426 s |
| Clang 22.1.5 compile only (`-O0/-O2`, 6 TUs parallel) | 0.311 s | 0.904 s |
| `tmplgen` cold generation | 2.825 s | 2.825 s |

For this shape, GCC/Clang's compile-only time is in the same ballpark as direct
`rustc` compilation; GCC `-O2` was faster than the Rust `opt-level=2` run.
Including the separate `tmplgen` discovery/analysis/emission pass makes the C
pipeline about 3.1–4.1 s end-to-end here, versus 0.28–0.64 s for direct `rustc`.
That extra pass dominates this workload. If generated headers are already
available and unchanged, the compiler-only row is the relevant comparison.

The Rust measurement used `#[inline(never)]` and `black_box` to keep the whole
generic call graph observable; C used the generated `static inline` functions.
The source languages, inlining heuristics, codegen-unit scheduling, and source
representation differ, so treat these as an indication of where time goes—not
a language ranking. The Rust timing excludes Cargo orchestration and dependency
builds; Cargo incremental builds can also reuse prior artifacts.

Useful controls:

```sh
python3 test/bench/run.py --sizes 1000,5000 --modules 8 \
  --functions-per-module 10 --calls-per-source 128 --repeats 3
python3 test/bench/run.py --sizes 64 --mode check --binary ./tmplgen
python3 test/bench/run.py --sizes 64 --mode check --use-cache
python3 test/bench/run.py --sizes 64 --compiled --compile-c --compile-jobs 8
python3 test/bench/run.py --sizes 64 --keep-projects --work-dir /tmp/bench-projects
python3 test/bench/incremental.py --types 64 --modules 12 \
  --functions-per-module 8 --repeats 3
```

For multi-translation-unit C projects, `tmplgen --compiled` emits each module's
specialized function bodies in a `.gen.c` file while keeping prototypes and
dispatch macros in `.gen.h`. Compile and link the generated `.gen.c` files with
the project; `--check` validates both file types. `--compile-c` makes the
runner compile all generated project translation units in parallel and report
that phase separately.

On this machine, with 64 types × 12 modules × 8 functions (6,144
specializations), `-O2`, and eight compile jobs, compiled output changed
six-caller-TU compile time from **0.44 s to 0.75 s with GCC 16.1.1**, but from
**0.90 s to 0.35 s with Clang 22.1.5**. This mode is workload and compiler
dependent: GCC paid for 12 additional implementation translation units, while
Clang benefited substantially from not reparsing the large inline bodies in
each caller. The in-tree tests also link and run a compiled-mode project.
Individual runs are saved in `header-compile.csv`, `compiled-compile.csv`,
`header-clang-compile.csv`, and `compiled-clang-compile.csv`.

For the same 6,144-specialization case, an uncached repeated `--check` took
about 2.81 s, while cached repeated generation/check passes took about 19 ms
after the initial 2.80 s generation. The cache validates source/template and
external include contents, compiler options, and generated outputs before
skipping analysis. `results.csv` records uncached scaling runs; `cache-results.csv`
records the cached path.

The dimensions are independent: `--sizes` sets the number of distinct concrete
types per project; `--modules` sets the number of interdependent template
modules and `--functions-per-module` controls each module's dependency depth;
`--calls-per-source` controls how source call sites are split among translation
units. The expected number of function specializations is
`types × modules × functions-per-module`.

`incremental.py` times a cold generation, then makes repeatable input edits and
measures unchanged cached checks. Its default `local-call` case keeps a new
concrete type in a shared header, initially unused, then changes one call site
to that type and updates dispatch. `--change-kind type-header` edits a shared
header and retargets all uses; `--change-kind body-only` adds a non-generic
function to one source without changing generic usage. It saves measurements to
`test/bench/incremental-results.csv` by default. The header and body-only samples
are saved separately in `incremental-header-results.csv` and
`incremental-body-results.csv`.

On 2026-10-30, for a 64 × 12 × 8 workload, the local call-site update takes
**1.64 s** versus **3.56 s** cold (about **2.2× faster**), reusing first- and
second-pass facts for the other seven tracked source units and changing two generated files.
Changing the shared type header instead takes **3.44 s** versus **3.44 s** cold:
all eight tracked units are invalidated, making it effectively a cold analysis.
A body-only source edit takes **1.58 s** versus **3.41 s** cold; it is about
**2.2× faster**, reuses both passes for the other seven units, and changes no
generated files. Each case settles back to about **33 ms** on the next unchanged
check.

## Generate a project without benchmarking

```sh
python3 test/bench/generate.py --root /tmp/tmplgen-project \
  --types 250 --modules 6 --functions-per-module 8
```

Then run tmplgen against it:

```sh
cc -O2 -std=c99 tmplgen.c -lclang -o /tmp/tmplgen
/tmp/tmplgen --source-root /tmp/tmplgen-project \
  --template-dir /tmp/tmplgen-project/tmpl \
  --output /tmp/tmplgen-project/gen/all.gen.h \
  --dispatch auto --clang-arg=-std=c11
```

Project generation refuses to populate a non-empty directory. Large sizes can
consume substantial time, memory, and disk space as specializations grow, so
increase the dimensions gradually when exploring the scaling curve.
