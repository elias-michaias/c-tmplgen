#ifndef TMPLGEN
typedef int A;
typedef int B;
typedef int C;
#define Triple(type0, type1, type2) Triple
#endif

// @template typename A, typename B, typename C
typedef struct {
    A first;
    B second;
    C third;
} Triple;

// @template typename A, typename B, typename C
static inline Triple(A, B, C) triple_make(A first, B second, C third)
{
    Triple(A, B, C) result = { first, second, third };
    return result;
}

// @template typename A, typename B, typename C
static inline A triple_first(Triple(A, B, C) *value) { return value->first; }
