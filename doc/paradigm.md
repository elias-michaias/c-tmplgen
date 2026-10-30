# The tmplgen paradigm and its trade-offs

`tmplgen` implements **source-aware, ahead-of-time specialization** for C. A
template is ordinary C plus declaration-scoped type-variable annotations. The
generator asks libclang what types appear at source call sites, infers concrete
type tuples from function inputs and constructor uses, then emits concrete C
declarations and functions for the observed cases. Dispatch macros map calls to
those concrete functions during compilation.

For example, an annotated `max(T, T)` can produce a real `max_int` function
when the project calls it with `int` values, and a real `max_double` when it is
called with `double` values. A `Vec(T)` type use similarly requests a concrete
`Vec_<type>` representation. Generated code is ordinary C; there is no runtime
generic object, type registry, or reflection layer added by the specialization
mechanism.

The key trade is that generic intent is written once, but specialization is
driven by what the source tree actually uses. This can reduce handwritten
boilerplate and keep concrete operations type-specific, while adding a code
generation step and a dependency on libclang.

## How it differs from related approaches

| Approach | What it gives you | Advantages | Costs and boundaries |
| --- | --- | --- | --- |
| **tmplgen** | Concrete C types/functions inferred from observed project calls | Stays in C; creates real typed specializations; can emit only observed type tuples; source-aware inference and compile-time dispatch | Requires a generator step and libclang; compiler flags must match; generated output can grow; supported template/type patterns are narrower than a full language type system |
| **Header-backed pointers (stb-style)** | A typed pointer API with metadata stored alongside or before the allocation | Lightweight, natural element access, few setup declarations; excellent for dynamic arrays and hash tables | The pointer does not describe the full container state to C’s type system; macros and allocation/reallocation rules matter; this is a container representation technique, not a general way to specialize arbitrary user functions and types |
| **Encoded pointer handles (CC.h)** | A pointer type encodes container/type information used by macros and compile-time selection | Ergonomic generic container API without defining a new ordinary container struct for each element type | Relies on intricate type encodings and compiler features such as `typeof` and `_Generic` (or newer modes); behavior and handle rules are part of the API; it targets generic containers rather than source-driven specialization of arbitrary C templates |
| **C-style C++ with templates** | Compiler-native type and function templates, often using a C-like coding style | Mature, expressive compile-time generic programming; no separate source generator; strong compiler integration | The code is still C++, not C; consumers need a C++ toolchain and must account for C++ language and ABI boundaries |
| **Manual instantiation** | Handwritten concrete types/functions or wrappers for each supported type | Simple toolchain, explicit output, straightforward debugging; works in very restricted C environments | Repeated boilerplate and maintenance; adding a type requires edits; users must keep the supported set and dispatch code synchronized |
| **`_Generic` or `void *` APIs** | C-native macro dispatch or runtime type-erased interfaces | No separate generator; `_Generic` is standardized in C11; `void *` can support open-ended runtime sets | `_Generic` associations are normally written and maintained by the library author; it does not generate arbitrary concrete struct definitions. `void *` APIs trade away some static type information and often need sizes, callbacks, or casts |
| **Lua/Python or other code-generation scripts** | Arbitrary templating or programmatic source generation | Flexible, fast to prototype, and can use a rich scripting ecosystem | Adds a host-language runtime/tool to the build; plain text generation does not itself provide C type inference; maintaining parser, flags, generated files, and cross-build behavior becomes project work |

These are design families, not strict competitors: a project can combine them.
For example, a library can use generated concrete algorithms and still use a
header-backed dynamic array for storage.

## Pointer-backed approaches

“Pointer-backed generic container” covers more than one technique. The classic
stb stretchy-buffer interface exposes a `T *` while keeping length/capacity
metadata before the returned allocation. Related `stb_ds.h` APIs use macro
interfaces and hidden metadata for dynamic arrays and hash tables. This gives
convenient container operations without generating a distinct public container
struct for every type. It does not, by itself, provide a way to infer and emit a
family of arbitrary user-defined C function and type specializations from
application call sites.

[CC.h by Jackson Allan](https://github.com/JacksonAllan/CC) takes a different,
more type-encoded route: its container handle is a specially formed pointer type
that carries type information for its macro API, with compile-time selection
techniques including `typeof` and `_Generic`. This is sometimes described as a
“fake pointer” or pointer-fiction technique; the pointer type is an encoding
used by the API, not simply a normal `T *` with a hidden allocation header.
CC.h demonstrates how far compile-time type selection can go inside C macros.
`tmplgen` instead runs an explicit analysis/generation phase and emits ordinary
concrete declarations and functions. The right choice depends on whether the
problem is primarily generic container ergonomics or generating a project’s
own reusable C algorithms and types.

## When tmplgen is a good fit

Consider it when:

- The project must remain C, and operations benefit from concrete types rather
  than a runtime-erased `void *` interface.
- The set of needed specializations is naturally visible in source call sites.
- You can run a generator during development/build, or can check generated
  files into source control for downstream consumers.
- You want reusable template declarations but want final code to be ordinary C
  that can be inspected, compiled, and debugged with familiar tools.

Prefer another approach when there are only a few fixed concrete types and
manual instantiation is clearer; when a dynamic open-ended type set is required;
when a compact container API is the actual need; when C-style C++ is already an
accepted implementation choice; or when adding a generator/libclang dependency is
undesirable.

## Limitations to understand

- Inference is based on function inputs and constructor uses, not return types.
  A variable must be inferable from an input position to dispatch a call.
- The generator analyzes files on disk. It does not read unsaved editor buffers.
- libclang must see the same preprocessor definitions, include paths, target,
  and language mode that matter to the real build.
- Dispatch associations reflect observed uses. A new call type requires
  regeneration before the corresponding specialization exists.
- Generated code is part of the build contract: stale generated files can cause
  compile errors or omit a desired specialization.

## Further reading

- [stb stretchy buffer](https://github.com/nothings/stb/blob/master/deprecated/stretchy_buffer.h)
  and [`stb_ds.h`](https://github.com/nothings/stb/blob/master/stb_ds.h)
- [CC.h](https://github.com/JacksonAllan/CC) and its
  [README’s implementation overview](https://github.com/JacksonAllan/CC#how-does-it-work)
- C11 [generic selection](https://en.cppreference.com/w/c/language/generic)
- C++ [templates](https://en.cppreference.com/w/cpp/language/templates)
