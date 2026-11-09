# Traits: named interfaces with user-provided implementations

A **trait** names a set of method signatures over one or more type parameters,
and **impls** provide the concrete bodies for chosen argument types. Templates
declare which traits their type parameters support with **bounds**, and the
generator checks those bounds while authoring so a template can only use what
its bounds guarantee.

## Declaring a trait

```c
// clang-format off
#ifndef TMPLGEN
typedef int T;
#endif

// @trait Ord(T)
int compare(T lhs, T rhs);
// @end
```

A trait block starts with `// @trait Name(T)` and ends with `// @end` or a
blank line. Methods are plain C declarations using the trait's parameters.
Trait methods have no bodies; they are registered under the **prefixed public
name** `Ord_compare` and participate in the usual dispatch-macro machinery.

## Providing implementations

```c
// @impl Ord(int)
static inline int compare(int lhs, int rhs) { return (lhs > rhs) - (lhs < rhs); }
// @end

// @impl Ord(double)
static inline int compare(double lhs, double rhs)
{
    return (lhs > rhs) - (lhs < rhs);
}
```

An impl block starts with `// @impl Ord(int)` and likewise ends with `// @end`
or a blank line. Methods keep their **short names**; the generator emits each
body as the prefixed specialization symbol, e.g. `Ord_compare_int` and
`Ord_compare_double`. Impls must be complete (every trait method exactly once)
and must match the trait signatures after substituting the impl arguments;
duplicates and unknown traits or methods are diagnosed at generation time.

Impls may live in any `.tmpl.h` file; their bodies are emitted by the module
that declares the trait, guarded like any other specialization.

## Impls with free type variables

An impl argument may apply a type constructor to free variables. Declare those
variables with a `@template` line immediately before `@impl`:

```c
// @template T: Show
// @impl Show(Box(T))
static inline int show(Box(T) value) { return Show_show(value.value); }
// @end
```

The impl matches every specialization `Box(U)` and its body is rewritten with
`T = U` (so the call above becomes the appropriate `Show_show_*`). The free
variables must be inferable from the trait arguments; otherwise generation
reports `cannot infer type variable`. With several free variables, each trait
call is resolved from its argument expression, so different fields of
`Pair(T, U)` can dispatch to different impls.

Trait bounds belong on the `@template` line, just like template functions.
`@where` is for constraint expressions and is not accepted after an `@impl`
header.

When an instance is covered, the generator checks the impl's bounds for the
actual binding: `Show(Box(double))` above requires `Show(double)`, and a
missing impl for the bound is reported at the use site:

```
error: type 'int' does not implement trait 'Label' (required by @impl Show(Crate(T)))
```

## Choosing between impls

If several impls cover the same specialization, the most specific one wins.
A concrete pattern is more specific than a variable, and a constructor
application is more specific than the same constructor with a variable
argument; for example `@impl Show(Box(int))` overrides
`@impl Show(Box(T))` for `Box(int)` while the blanket impl still serves
`Box(double)`.

Two impls covering the same specialization at the same specificity are an
error:

```
error: ambiguous @impl for Show(int); candidates: Show(T), Show(U)
```

## Calling trait methods

Call the prefixed name. From ordinary `.c` code, `Ord_compare(2, 5)` dispatches
through the generated macro to every observed specialization, like any generic
function:

```c
#include "gen/all.gen.h"

int result = Ord_compare(2, 5);   /* Ord_compare_int(2, 5) */
```

## Bounds on templates

A `@template` parameter can require traits with `Name: Trait` (or
`Name: TraitA + TraitB`):

```c
// @template T: Ord
static inline T max_ord(T lhs, T rhs)
{
    return Ord_compare(lhs, rhs) >= 0 ? lhs : rhs;
}
```

Bounds are checked while authoring: inside `max_ord`, calling `Ord_compare` on
`T` values is allowed only because the bound promises it. Without the bound the
generator reports:

```
error: call to 'Ord_compare' requires 'T: Ord', but 'max_ord' has no bound on 'T'
```

Calls with concrete arguments bypass the bound check, and the callee's own
bounds must be implied by the caller's bounds. The check looks at every caller
parameter mentioned in an argument expression, so `Eq_equal(a + 0, b)` is
rejected just like `Eq_equal(a, b)` when `Eq` is not guaranteed.

## Missing implementations

Using a trait specialization without an impl is a generation error reported at
the use site:

```
error: type 'double' does not implement trait 'Ord' (needed by 'Ord_compare')
```

## Current limitations

- No supertraits and no bounds on struct/type templates yet.
- Trait parameters are type parameters only (no value or constructor
  parameters).
- Patterns are matched structurally through type-constructor applications; more
  elaborate type expressions fall back to exact spelling comparison.
