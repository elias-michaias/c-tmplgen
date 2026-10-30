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
repeated normal generation and/or `--check` passes. Timed invocations run the
full source/template discovery, libclang parsing, inference, and emission
pipeline; output is suppressed to keep terminal I/O out of the measurement.
The steady-state normal-generation runs are idempotent, while `--check` avoids
output writes entirely.

The summary includes workload dimensions, source/generated sizes, emitted
specialization count, cold time, and median/min/max repeated timings. `--csv`
stores every measured sample and the associated workload metrics.

Useful controls:

```sh
python3 test/bench/run.py --sizes 1000,5000 --modules 8 \
  --functions-per-module 10 --calls-per-source 128 --repeats 3
python3 test/bench/run.py --sizes 64 --mode check --binary ./tmplgen
python3 test/bench/run.py --sizes 64 --keep-projects --work-dir /tmp/bench-projects
```

The dimensions are independent: `--sizes` sets the number of distinct concrete
types per project; `--modules` sets the number of interdependent template
modules and `--functions-per-module` controls each module's dependency depth;
`--calls-per-source` controls how source call sites are split among translation
units. The expected number of function specializations is
`types × modules × functions-per-module`.

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
