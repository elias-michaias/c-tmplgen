#include "result.tmpl.h"
#include "array.tmpl.h"
#include "triple.tmpl.h"

#ifndef TMPLGEN
typedef int F;
typedef int G;
typedef int H;
typedef int Z;
typedef int T;
typedef int A;
#define F(...) F
#define G(...) G
#define H(...) H
#define Z(...) Z
#define Boxed(...) Boxed
#define PlainBox(...) PlainBox
#endif

/* A constructor parameter is bound by name at the use site: Boxed(Option). */
// @template template(typename) F
typedef struct {
    F(int) value;
} Boxed;

// @template template(typename) F, typename T
static inline T container_unwrap(F(T) *box)
{
    (void)box;
    return (T){0};
}

// @template template(typename, size_t) G, typename T, size_t N
static inline T sized_unwrap(G(T, N) *box)
{
    (void)box;
    return (T){0};
}

// @template template(typename, typename, typename) H, typename A
static inline A triple_wrapped(H(A, A, A) *value)
{
    (void)value;
    return (A){0};
}

typedef struct {
    int x;
} Plain;

/* Zero-arity constructor signatures bind non-generic template types. */
// @template template() Z
typedef struct {
    Z value;
} PlainBox;
