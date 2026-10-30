# Constraints: structural subtyping and satisfaction

`tmplgen` supports two complementary ways to constrain a template variable:

- **Structural subtyping** (`<:`) requires a concrete type to contain the fields
  of one or more named struct/union schemas.
- **Compile-time satisfaction** (`|=` or `@where`) requires an expression to be
  true for a concrete type or tuple of types.

Constraints are attached to declarations using the normal `// @template`
annotations. For each specialization discovered from project source, `tmplgen`
emits compile-time assertions. A mismatch is therefore reported by the
application compiler at the generated specialization, with a message mapped
back to the template declaration.

## Structural subtyping with `<:`

Treat a named struct or union definition as a row of required fields. A concrete
type satisfies the row when it has every required field with a compatible C
field type. It may have additional fields; field offsets and total size do not
need to match.

For example, define a schema and two candidate types:

```c
/* geometry.h */
struct Point2 {
    int x;
    int y;
};

struct Body {
    int x;
    int y;
    double mass; /* extra fields are fine */
};

struct BrokenBody {
    int x;
    double y;    /* incompatible with Point2.y */
};
```

Constrain a template parameter to the `Point2` row:

```c
/* tmpl/measure.tmpl.h */
#include "geometry.h"

#ifndef TMPLGEN
typedef struct Point2 Shape;
#endif

// @template Shape <: Point2
static inline int coordinate_sum(Shape *value)
{
    return value->x + value->y;
}
```

The application can use any compatible concrete type:

```c
#include "geometry.h"
#include "gen/all.gen.h"

int main(void)
{
    struct Body body = { .x = 3, .y = 4, .mass = 1.5 };
    return coordinate_sum(&body) != 7;
}
```

`struct Body` satisfies `Point2`: both required integer fields exist, and
`mass` is an allowed extra. If a source call requests
`coordinate_sum(&broken)` for a `struct BrokenBody`, the generated
specialization fails its assertion because `y` has the wrong type. For example,
this call intentionally fails during compilation:

```c
struct BrokenBody broken = { .x = 1, .y = 2.0 };
coordinate_sum(&broken); /* generated assertion: Point2.y has an incompatible type */
```

A missing required field fails similarly.

### Combining rows

Use `+` to require multiple rows at once. A candidate must satisfy every row:

```c
struct Entity {
    int id;
};

struct Named {
    const char *name;
};

struct Actor {
    int id;
    const char *name;
    unsigned flags;
};

// @template T <: Entity + Named
static inline int actor_id(T *actor)
{
    return actor->id;
}
```

`struct Actor` satisfies both `Entity` and `Named`, while a type with only an
`id` field does not. Multiple variables can have independent requirements:

```c
// @template P <: Point2, E <: Entity + Named
static inline int describe_pair(P *point, E *entity)
{
    return point->x + entity->id;
}
```

Requirements can also be accumulated on adjacent annotation lines:

```c
// @template E <: Entity
// @template E <: Named
static inline int actor_id(E *actor) { return actor->id; }
```

The schema definition must be visible to the template parser, usually by
including its header in the `.tmpl.h` file. Structural matching is about field
presence and C field types—not inheritance, object layout, or implicit
conversions.

## Arbitrary type predicates with `|=`

Use `|=` when a structural row is not the property you need. The expression must
be an integer constant expression accepted by `_Static_assert`. It can use
compile-time operators and macros visible to the template.

For a macro-call term, omit the template variable from the argument list:
`tmplgen` inserts the concrete type as the first argument for each
specialization. If the macro takes only that type, its name can appear without
parentheses; if it takes more arguments, write those arguments explicitly.

```c
/* constraints.h */
#define ALIGNMENT_AT_LEAST(Type, Other) \
    (_Alignof(Type) >= _Alignof(Other))
```

```c
/* tmpl/aligned.tmpl.h */
#include "constraints.h"

#ifndef TMPLGEN
typedef int T;
#endif

// @template T |= ALIGNMENT_AT_LEAST(int)
static inline T keep_value(T value)
{
    return value;
}
```

For `T = MyType`, the generated check is equivalent to
`ALIGNMENT_AT_LEAST(MyType, int)`. A multi-argument macro can therefore take
additional explicit arguments after the inferred type. This shorthand is for
macro-call terms; ordinary expressions can refer directly to template
variables.

Multiple `|=` requirements may be written on adjacent lines. A top-level `+`
separates requirements, so parenthesize an expression if it needs a top-level
addition operator.

## Relating variables with `@where`

Use `@where` for a condition over one or more template variables, such as a
relationship between a pair of inferred types. Unlike the `|=` macro shorthand,
write the variables explicitly in the expression.

```c
/* constraints.h */
#define SAME_SIZE(Left, Right) (sizeof(Left) == sizeof(Right))
```

```c
// @template T, U
// @where SAME_SIZE(T, U)
static inline int pair_has_equal_size(T left, U right)
{
    return sizeof(left) == sizeof(right);
}
```

The condition is checked for each inferred `(T, U)` pair. If an observed call
uses types of different sizes, compilation fails with a diagnostic naming the
failed `@where` expression. For example, `pair_has_equal_size(1, 2)` requests a
valid `int, int` specialization, while `pair_has_equal_size(1, 2.0)` requests an
`int, double` specialization and fails the `SAME_SIZE` assertion on common
platforms.

`@where` can be continued across adjacent template annotations when a condition
needs multiple lines:

```c
// @template T, U
// @where SAME_SIZE(T, U)
// @where ALIGNMENT_AT_LEAST(T, U)
static inline void operate_on_pair(T left, U right)
{
    (void)left;
    (void)right;
}
```

As with `|=`, every condition must be compile-time evaluable. Runtime values such
as `left`, `right`, or a function result cannot be used to select a
specialization constraint.

## Picking a constraint form

| Requirement | Use |
| --- | --- |
| A concrete type must expose named fields with compatible types | `<:` structural row |
| A type must satisfy a compile-time property | Type-variable satisfaction annotation |
| Several type variables must satisfy a relation | `@where` |
| A candidate needs both fields and an additional property | Combine a structural row with satisfaction |

Constraints are checked only for specializations requested by observed source
uses. They do not create specializations by themselves. For the full template
syntax and build flags, see [Usage](usage.md) and
[Tooling](tooling.md). The repository’s concrete examples are in
[`test/tmpl/result.tmpl.h`](../test/tmpl/result.tmpl.h) and
[`test/lib/physics.h`](../test/lib/physics.h).
