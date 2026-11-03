# Template system enhancements (design sketch)

Status: parts 1–3 implemented; part 4 planned. This document
sketches a targeted expansion of the template
system in four parts, in the order they should be implemented. Parts 1–3 are
independently useful; part 4 builds on the parameter-kind machinery of part 1.

1. `typename` as an explicit type-parameter specifier.
2. Value generics: compile-time constants as template parameters.
3. Explicit type pseudo-arguments for function templates.
4. Higher-kinded templates: passing a type constructor of arbitrary arity
   and parameter kinds (types and values) as an argument.

## Background: what exists today

- A template parameter is always a *type* variable: `// @template T, E`.
- Constructor applications use function-like spelling (`Vec(T)`, `Result(T, E)`)
  and are rewritten to suffixed concrete names (`Vec_int`, display `Vec(int)`).
- Inference runs from *inputs*: observed call argument types are matched
  against annotated parameter spellings, constructor uses request type
  specializations, and recognized calls between template functions are followed
  to a fixed point. A variable that occurs only in a return type cannot be
  selected automatically.
- Constraints: structural rows (`T <: Entity`), satisfaction (`T |= EXPR`,
  `@where EXPR`), checked per specialization by emitted compile-time
  assertions.
- Dispatch: per-function generated macros/`_Generic`/GNU builtins over the
  observed specializations (`--dispatch`); constructor macros paste suffixes
  (`#define Vec(type0) Vec_##type0`).
- Specialization identity is the concrete argument tuple, encoded into an
  identifier-safe suffix; collisions are detected and rejected.

None of the four parts changes this model; they add *parameter kinds* and one
new argument-passing mode.

## Part 1: `typename` as a parameter specifier

Allow (but do not require) the keyword `typename` before a type parameter:

```c
// @template typename T, typename E
typedef struct { ... } Result;
```

- Grammar becomes `param := [typename] IDENT [constraints] | <part 2> | <part 4>`.
- `typename` is not a C keyword, so it is harmless in annotations, and the
  editor fallback can `#define typename` to nothing for standalone parsing
  (this matters for part 3, where it appears in real signatures).
- No semantic change: a bare `T` and `typename T` are the same kind. This part
  is parser + model groundwork so that later parts can distinguish parameter
  kinds unambiguously.

## Part 2: value generics

### Syntax

A parameter declared with a concrete scalar type instead of `typename` is a
*value parameter*:

```c
// @template T, size_t N
typedef struct {
    T items[N];
    size_t length;
} Array;

// @template T, size_t N
static inline T array_sum(Array(T, N) *array)
{
    T total = 0;
    for (size_t i = 0; i < N; ++i) total += array->items[i];
    return total;
}
```

- Initially restrict value types to integral types, `_Bool`, and enum types.
  Floating point and string literals are non-goals for now.
- Substitution uses the existing token rewriting: `N` in the body is replaced
  by the argument's token sequence, so `T items[N]` becomes `int items[10]`
  and folds naturally in `sizeof`, `_Generic`, and assertions.

### Arguments and identity

- A value argument must be an *integer constant expression* at the use site
  (`Array(int, 10)`, `Array(int, 2 * UNITS)`). The generator constant-folds it
  (clang AST evaluation) and normalizes it to a canonical integer.
- Specialization suffix encodes the canonical value in decimal, with `-`
  encoded as `n`: `Array(int, 10)` → `Array_int_10`, display `Array(int, 10)`.
  `Array(int, 4 + 6)` is the same specialization as `Array(int, 10)`.
- The existing suffix-collision detection covers the new segments unchanged.

### Generic and specific over values

- *Generic over N*: `array_sum` above works for any N. Inference decomposes an
  observed constructor name (`Array_int_10`) against the pattern
  `Array(T, N)`, recovering `T = int`, `N = 10` — the same mechanism that
  recovers type arguments today, with value segments parsed back as integers
  and range-checked against the parameter's type.
- *Specific over a value*: a literal in a parameter spelling pins it.

  ```c
  // @template T, size_t N
  static inline T first_of_ten(Array(T, 10) *array) { return array->items[0]; }
  ```

  Matching recovers `T` and requires `N == 10`; other lengths simply do not
  match this function's dispatch set.
- Constraints compose: `@where N > 0` or `T |= EXPR(N)`; after substitution the
  expressions are constant and are asserted per specialization as today.

### Validation

- Non-ICE value argument → generation error naming the use site.
- Out-of-range value for the parameter type → generation error.
- Value parameter used where a type is required (and vice versa) → generation
  error from the parameter-kind check.

## Part 3: explicit type pseudo-arguments

### Problem and syntax

Today a variable that occurs only in the return type cannot be inferred.
A function can instead *demand* the type by writing it as a pseudo-parameter:

```c
// @template T
static inline T zero_value(typename T)
{
    return (T){0};
}

// @template T
static inline T read_as(typename T, const void *data)
{
    T value;
    memcpy(&value, data, sizeof value);
    return value;
}
```

- `typename T` in the signature is not a value parameter; it marks `T` as
  *explicit*: the caller supplies the type, the dispatcher consumes it, and the
  emitted specialized function does not have that parameter:

  ```c
  static inline Point read_as_Point(const void *data) { ... }
  ```

### Call site and dispatch

```c
Point p = read_as(Point, buffer);
```

- The generated call interface is a macro that pastes the first argument as a
  type token, like constructor macros today:

  ```c
  #define read_as(T, ...) read_as_##T(__VA_ARGS__)
  ```

  An unknown type produces the usual deliberate compile-time error. If other
  parameters remain inferable, the pasted name feeds into the existing
  `_Generic`/GNU dispatch chain.
- In the editor fallback, `#define typename` to nothing makes
  `read_as(T, const void *)` parse as an unnamed parameter of the fallback
  typedef `T`, so templates still parse standalone.
- Explicit parameters may appear anywhere in the signature; each is removed
  from the emitted specialization and consumed left-to-right by the dispatch
  macro. Restricting them to leading positions is acceptable for v1 if that
  simplifies the macro.

As implemented, explicit parameters must be leading and every template
variable of the function must be explicit; mixing explicit and inferred
variables is rejected with a diagnostic.

## Part 4: higher-kinded templates (constructor parameters)

### Syntax

A parameter declared `template(params) F` binds a *type constructor*, where
`params` is a parameter signature using the same kinds as `@template` itself:
`typename` (or bare identifiers) for type parameters and concrete scalar types
for value parameters (part 2). Arity is arbitrary, including zero.

```c
// @template template(typename) F
typedef struct {
    F(int) value;
} Foo;

// @template template(typename, size_t) G, typename T, size_t N
static inline T array_first(G(T, N) *array) { ... }
```

- Names may be given to the signature parameters for readability
  (`template(typename Item, size_t Count) G`); they are documentation only —
  matching is by kind and value type, positionally.
- `F` is bound by name to another template type constructor, e.g. `Option` in
  `Foo(Option)` → specialization `Foo_Option`, display `Foo(Option)`.
- Applications `G(args)` inside bodies use the existing constructor-application
  rewriting and request the applied specialization recursively
  (`G(int, 10)` with `G = Array` requests `Array_int_10`); the fixed-point
  machinery already follows such uses. Value arguments in applications are
  validated exactly as in part 2 (ICE, canonicalized).

### Binding and checking

- At a use site, the argument for a constructor parameter must be the bare name
  of a known struct/union template whose parameter signature matches the
  declaration **kind-by-kind**: same arity, and each position is either both
  `typename` or both value parameters with the same value type (`size_t N`
  matches `size_t`, not `int`). A mismatch is a generation error naming the
  use site — passing `Array` where `template(typename) F` is declared fails,
  because `Array` needs a value argument.
- This keeps value-generic constructors first-class: `Array(T, N)` can be
  passed wherever `template(typename, size_t)` is expected, and multi-type
  constructors like `Result(T, E)` wherever `template(typename, typename)` is
  expected.
- Constructor parameters participate in inference the same way type parameters
  do, and the decomposition generalizes part 2: matching an observed
  `Array_int_10` against a parameter spelling `G(T, N)` recovers
  `G = Array`, `T = int`, `N = 10` — the outer name binds the constructor,
  then its suffix segments decompose per the declared kinds. Function
  templates can therefore be generic over a container shape, including shapes
  with value parameters:

  ```c
  // @template template(typename) F, typename T
  static inline T container_unwrap(F(T) *box);

  // @template template(typename, size_t) G, typename T, size_t N
  static inline T sized_unwrap(G(T, N) *box) { ... }
  ```

- Non-goals for v1: partial application (`Result(int, _)` as an argument),
  constraints on constructors themselves, function templates as constructor
  arguments, and builtin pseudo-constructors (pointers, arrays).

## Cross-cutting changes by subsystem

- **Annotation parsing / model** (`// @template` grammar): parameter kinds
  (type / value / constructor) with per-kind data (value-type spelling,
  arity); `typename` acceptance; clear errors for malformed kinds.
- **Rewriter/substitution**: value parameters substitute constant token
  sequences; constructor parameters substitute constructor names and rewrite
  their applications; pseudo-parameters are dropped from emitted signatures.
- **Inference**: suffix/pattern decomposition extended to value segments and
  constructor bindings; literal-value matching in spellings; validation of
  ICE arguments at use sites.
- **Specialization identity**: canonical value encoding and constructor names
  in suffixes and display names; collision checks unchanged.
- **Emission & dispatch**: dispatch macro for explicit type arguments,
  composed with existing `_Generic`/GNU chains; constructor macro pasting
  already exists.
- **Reflection**: value- and constructor-specialized types appear under their
  suffixed names with pretty displays (`Array(int, 10)`, `Foo(Option)`);
  `@meta` and layout tables are unaffected.
- **clangd plugin**: highlight `typename` in annotations and signatures as a
  keyword, value-parameter type spellings as types, `template(...)` as
  keyword + parameter; completion and diagnostics for the new parameter
  kinds; fallback `#define typename` support.
- **Docs**: `doc/usage.md` (parameter kinds, explicit arguments),
  `doc/constraints.md` (value-parameter satisfaction examples), this file kept
  as the roadmap.

## Compatibility

- Existing `@template T, E` annotations are unchanged (bare identifiers remain
  type parameters).
- Existing suffixes and generated names are unchanged; the new segments only
  appear in specializations that use the new kinds.
- `--dispatch`, `--compiled`, reflection, and `@meta` keep working; the new
  kinds are additive.

## Testing plan

Real fixtures under `test/tmpl/` and cases under `test/cases/` (no heredoc
projects in shell scripts):

- `array.tmpl.h`: `Array(T, N)` with a fixed buffer, generic `array_sum`, and
  an `Array(T, 10)`-specific function; case exercises lengths 3 and 10,
  `2 * 5` argument normalization, and reflection of `Array_int_10`.
- `explicit.tmpl.h`: `zero_value(typename T)` / `read_as(typename T, ...)`
  called with explicit types; `--dispatch` modes all compile the macro call.
- `hkt.tmpl.h`: `Foo(F)` over `Option` and a one-parameter constructor of the
  project; `container_unwrap` inferred from `Option_int`; a
  `template(typename, size_t)` parameter bound to `Array` with
  `sized_unwrap` inferred from `Array_int_10`; a zero-arity constructor
  parameter bound to a concrete no-parameter template.
- Negative cases (`test/negative/`): non-ICE value argument, out-of-range
  value, arity mismatch on a constructor argument, unknown constructor name,
  value parameter used as a type, `typename` pseudo-parameter in a
  struct/union (rejected: functions only), suffix collision with a
  lookalike type name.
- Golden-output checks in `test/run.sh` for names, dispatch macros, and
  `@where` assertions mentioning `N`.

## Phasing

1. **Part 1** — `typename` specifier: parser/model only, no semantic change;
  ship with tests that `@template typename T` equals `@template T`.
2. **Part 2** — value generics: the largest piece (normalization, suffixes,
  inference decomposition, literal matching, ICE validation).
3. **Part 3** — explicit pseudo-arguments: signature rewriting + dispatch
  macro; relatively small once kinds exist.
4. **Part 4** — HKTs: constructor binding, arity checking, application
  rewriting, inference binding.

Each phase lands with its fixtures, negative tests, docs, and regenerated
`test/gen/` in one reviewable change.

## Open questions

- Should dependent value expressions (`Array(T, N + 1)` as an argument inside
  another template) be allowed in v1, or rejected until there is a use case?
  (Leaning: reject initially; canonicalization is only defined for use-site
  constants.)
- Should enum value arguments use the enumerator's *name* in the suffix for
  readability (`Mode_fast`) or the canonical integer (uniformity, avoids
  rename breakage)? (Leaning: canonical integer, pretty name in display.)
- Exact dispatch-macro shape when explicit pseudo-arguments mix with inferred
  ones (paste-then-`_Generic` vs. full name pasting of all type arguments).
- Whether constructor parameters may be constrained (`F(T) <: Entity`-style
  rows on applications) in a later phase.
- Whether value-type matching in constructor signatures must be exact
  (`size_t` vs `int`) or should allow any integral value type, with
  range-checking deferred to applications. (Leaning: exact match in v1;
  relaxation is backward compatible.)
