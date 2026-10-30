#ifndef TMPLGEN
typedef int A;
typedef int B;
typedef int C;
#endif

// @template A, B, C
typedef struct {
    A first;
    B second;
    C third;
} Triple;

static inline Triple triple_make(A first, B second, C third)
{
    Triple result = { first, second, third };
    return result;
}

static inline A triple_first(Triple *value) { return value->first; }
