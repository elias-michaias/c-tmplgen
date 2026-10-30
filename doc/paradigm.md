# Generic programming in C: where tmplgen fits

`tmplgen` is a **source-aware, ahead-of-time specialization tool**. Annotate
ordinary C declarations with type variables; libclang analyzes the project’s
source calls and constructor uses; the generator emits concrete C types,
functions, and dispatch macros for the combinations it finds.

For example, a template function `max(T, T)` can become concrete `max` functions
for every type used at its call sites. A use of `Vec(MyType)` can request a
concrete `Vec_MyType` type. Dispatch is selected at compile time, and the output
is ordinary C that can be inspected and compiled separately.

The set of types does **not** need to be listed in a registry beforehand. Add a
new source-visible type use, rerun the generator, and it can produce another
specialization. The trade is a generation step and a libclang dependency during
generation in exchange for inferred, concrete C output.

## A map of the options

| Approach | Main mechanism | Strengths | Trade-offs |
| --- | --- | --- | --- |
| **tmplgen** | Analyze source uses and emit concrete C specializations | Keeps authored and generated code in C; infers actual type combinations; can generate arbitrary project types and functions | Adds a generation step and libclang; compiler flags must match; code size grows with requested combinations |
| **Hidden/shadow metadata pointers** | Store runtime metadata alongside an allocation and expose a typed pointer | Natural indexing and convenient dynamic containers; little per-type boilerplate | Pointer/relocation/lifetime rules are part of the API; specializes storage access patterns rather than arbitrary type-level declarations |
| **Pointer-fictions / encoded handles** | Encode type information in a special pointer type for macro dispatch | Powerful compile-time type encoding; can in principle describe more than containers | The interface must travel through a pointer-shaped handle; value-oriented APIs need wrappers or address-taking; implementation and optimizer interactions require care |
| **`void *` type erasure** | Pass untyped storage plus runtime size/type operations | Supports runtime-selected and open-ended types through one implementation | Casts and metadata/callbacks move type checking to the API boundary; operations may require sizes, copy/destructor callbacks, or indirect calls |
| **Handwritten `_Generic` API** | Select among explicitly supplied associations at compile time | Standard C11 facility; direct typed calls and no runtime type lookup | The ordinary association set is written and maintained by hand; it does not synthesize arbitrary struct or function definitions |
| **C-style include templates** | Define macros such as `T`, then include a reusable `.inc` file once per type | Pure preprocessor workflow; no separate generator or parser required | Every instantiation is manually requested; macro cleanup, naming, repeated inclusion, and diagnostics take care |
| **C-style C++** | Write C-like code as C++ and use language templates | Mature compiler-native generics, deduction, specialization, and constraints | The source and toolchain are C++; C consumers need an ABI boundary and cannot compile template declarations as C |
| **Manual instantiation** | Write a concrete type/function or wrapper for each supported type | Minimal machinery; explicit and easy to debug | Boilerplate grows with types and combinations; every new type needs source edits |
| **Lua/Python generation scripts** | Render source from scripts, templates, or a project-specific schema | Flexible and quick to prototype; general-purpose host language | Adds a generator runtime/tool; usage-driven type inference still needs compiler-aware parsing or a separate manifest |

These are design families, not mutually exclusive choices. For example, a
project could use `tmplgen` to specialize algorithms and use an stb-style
dynamic array for storage.

## Hidden metadata: the stb-style approach

Some C containers expose an ordinary typed pointer while keeping length,
capacity, or hash-table state in hidden metadata associated with the allocation.
The classic stb stretchy-buffer design stores its bookkeeping immediately before
the user-visible array pointer. [`stb_ds.h`](https://github.com/nothings/stb/blob/master/stb_ds.h)
provides macro APIs for typed dynamic arrays and hash tables using related
header-managed techniques.

This is a strong fit for **runtime-sized storage**: the caller gets familiar
`T *` indexing, and the library can recover the container bookkeeping from the
pointer. The pointer may change when storage grows, so callers must follow the
library’s update and lifetime rules; macros also impose evaluation rules on
their arguments. This representation technique is primarily about storing and
accessing containers. It does not by itself analyze a project and emit
specializations of arbitrary user-defined types and algorithms.

## Pointer-fictions: the CC.h approach

[CC.h by Jackson Allan](https://github.com/JacksonAllan/CC) uses a different
pointer-based technique. The container handle’s **pointer type** encodes
information such as the container identity and element/key types; macros use
that information with C type-selection features to route operations. Its
README’s implementation overview describes a handle shaped like
`element_type (*(*)[container_type_id])(key_type *)`.

Pointer-fictions are not inherently limited to containers. In principle, a
compile-time pointer-type encoding can be generalized to represent arbitrary
type identities and select type-specific operations or declarations. CC.h is a
particularly developed application of that idea to containers.

The pointer-shaped transport is also the central trade-off. A value-oriented
interface such as `T make_value(...)` or `consume(T value)` does not fit the
same handle model directly: it tends to need a wrapper, an address, or a buffer
parameter to carry the encoded handle. This can make function signatures more
pointer-heavy than the underlying operation requires and can constrain
optimization opportunities around aliasing and value flow. If an implementation
uses sentinel or fabricated pointer values, it must additionally ensure those
values are never treated as ordinary object addresses; provenance, validity,
and optimizer assumptions need careful handling. These concerns depend on the
specific encoding and implementation—pointer-fictions are a general technique,
not one fixed implementation.

`tmplgen` takes a different route: it observes ordinary typed expressions in
the source, then materializes real concrete C types and functions. The caller
continues to pass a `T`, `T *`, or other natural signature from the template;
the type information is used by the generator rather than carried in a
runtime pointer handle.

## C-style templates: repeated inclusion

A common C technique is to put a declaration body in an intentionally
re-includable file and set macros before each inclusion:

```c
/* vec_template.inc — intentionally has no ordinary include guard */
#include <stddef.h>

typedef struct {
    T *data;
    size_t length;
} Vec;

static inline void vec_push(Vec *vec, T value)
{
    vec->data[vec->length++] = value;
}
```

Instantiate it explicitly by defining the type and generated names:

```c
#define T int
#define Vec Vec_int
#define vec_push vec_push_int
#include "vec_template.inc"
#undef vec_push
#undef Vec
#undef T

#define T double
#define Vec Vec_double
#define vec_push vec_push_double
#include "vec_template.inc"
#undef vec_push
#undef Vec
#undef T
```

This stays within the C preprocessor and makes each instantiation visible in
the including file. It is a good option for a small, fixed set of types or a
small reusable library that already owns its instantiation list. The application
author must decide which types to include, keep names distinct, and manage
macros and include guards carefully. It does not infer new instantiations from
ordinary source calls. `tmplgen` automates that selection and emits separate,
inspectable generated modules instead of requiring each consumer to repeat the
include protocol.

## C-style C++

Another option is to write C-like code as C++ and use native templates:

```cpp
template<class T>
T max_value(T left, T right)
{
    return left > right ? left : right;
}
```

The style can remain procedural and close to C, but the program is still C++:
the compiler, language rules, standard library, and ABI are C++. This is often
the cleanest choice when C++ is acceptable. If the public interface must remain
C, C++ implementations can be wrapped in `extern "C"` functions, but template
types themselves do not cross that C ABI boundary.

By comparison, `tmplgen` writes and emits C. A normal C compiler builds the
result, and the generated declarations can remain part of a C API. The cost is
that generic syntax and inference live in the generator rather than in the C
language/compiler.

## Runtime erasure and manual `_Generic`

These two C APIs solve different problems and should not be conflated.

### `void *` APIs: runtime type erasure

A `void *` API accepts storage without preserving its element type in the
function signature. A reusable vector may take an element size; a generic map
may also take hash/equality callbacks, copy functions, or a vtable. This supports
types chosen at runtime—including plugin or dynamically loaded types that could
not be known during generation—but transfers type safety and operation
selection to metadata and conventions. The user often writes casts, and the
implementation may use byte copies or indirect calls.

Choose this when runtime openness and one type-erased implementation matter more
than a concrete API for each type. It is different from `tmplgen`: `tmplgen`
specializes all types visible in source, while `void *` supports values whose
type may be selected only at runtime.

### Handwritten `_Generic`: a compile-time overload list

An ordinary `_Generic` API chooses from an association list written by the
library author:

```c
#define max_value(a, b) \
    _Generic((a), int: max_int, double: max_double)((a), (b))
```

For a small, known set of types, this is direct and effective: selection occurs
at compile time and resolves to a concrete function call. But `_Generic` only
selects among expressions already listed. It does not declare `Vec_int`, emit
`max_int`, or discover additional types from the source tree. Macro systems can
make association lists extensible, but then the library author must build and
maintain that extension mechanism.

`tmplgen` uses compile-time dispatch too; its distinction is that it discovers
the associations and emits the concrete declarations/functions from observed
source uses. Use handwritten `_Generic` when the association list is small and
stable, and generation when both the concrete implementations and dispatch set
should follow actual project use.

## Manual instantiation and scripted generation

For a handful of types, handwrite the concrete declarations or wrappers. This
has the smallest toolchain and the clearest control flow. It becomes repetitive
when a library supports many types or tuples, and callers must remember to add
instances and dispatch cases consistently.

Lua, Python, or another scripting language can make a generator easier to
prototype and more expressive than C macros. A script can generate arbitrary
source, but it does not automatically know what types a C call uses. That
requires a manifest or a compiler-aware parser plus the right flags. `tmplgen`
uses libclang specifically to derive specializations from C source while
keeping the final build artifacts in C.

## When tmplgen fits

`tmplgen` is a good fit when:

- You want to keep the implementation and public API in C.
- Type-specific code should have real concrete C types and functions rather
  than a runtime-erased representation.
- You want the specialization set to grow with the source: adding a new call
  with a new type can request a new instance without editing a type registry.
- You can run a generation step with libclang, or can check generated C into the
  project for consumers that do not run the generator.
- You want to inspect and debug the final C using ordinary compiler tooling.

Prefer another approach when a handful of manual instances are clearer; when
the type is selected only at runtime and must not require regeneration; when
`_Generic` over a short fixed list is enough; when a pointer-backed container
API is the actual goal; or when C++ is already an accepted implementation
language. An **open-ended set of source-visible C types is not a limitation**:
`tmplgen` can add a specialization for each newly observed use on the next
generation/build.

## Practical boundaries

- Every specialization must be inferable from a constructor use or function
  input type. Return types do not drive inference.
- The source and its preprocessor environment must be visible to libclang. Types
  that appear only after runtime loading cannot be inferred at build time.
- More type combinations mean more generated C and potentially more compile
  work. `--compiled` can move implementations out of widely included headers.
- Generated files should be regenerated when templates, source call sites, or
  relevant included declarations change.

## Further reading

- [stb stretchy buffer](https://github.com/nothings/stb/blob/master/deprecated/stretchy_buffer.h)
  and [`stb_ds.h`](https://github.com/nothings/stb/blob/master/stb_ds.h)
- [CC.h](https://github.com/JacksonAllan/CC) and its
  [README’s implementation overview](https://github.com/JacksonAllan/CC#how-does-it-work)
- C11 [generic selection](https://en.cppreference.com/w/c/language/generic)
- C++ [templates](https://en.cppreference.com/w/cpp/language/templates)
