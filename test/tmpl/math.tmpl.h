// clang-format off
#ifndef TMPLGEN
typedef int T;
typedef int U;
typedef int A;
typedef int B;
#endif

#include "result.tmpl.h"

// @template T
static inline T max(T a, T b) { return a > b ? a : b; }
// @template T
static inline T clamp(T x, T lo, T hi) { return max(lo, x) > hi ? hi : max(lo, x); }
// @template T
static inline T chain_leaf(T value) { return value + (T)1; }
// @template U
static inline U chain_middle(U value) { return chain_leaf(value); }
// @template T
static inline T chain_root(T value) { return chain_middle(value); }
// @template T
static inline T recursive_value(T value, unsigned count)
{
    if (count == 0U) return value;
    return recursive_value(value, count - 1U);
}
// @template T
static inline T mutual_other(T value, unsigned count);
// @template T
static inline T mutual_first(T value, unsigned count)
{
    if (count == 0U) return value;
    return mutual_other(value, count - 1U);
}
// @template T
static inline T mutual_other(T value, unsigned count)
{
    if (count == 0U) return value;
    return mutual_first(value, count - 1U);
}
// @template A, B
static inline void reorder_sink(B second, A first)
{
    (void)first;
    (void)second;
}
// @template T, U
static inline void reorder_call(T first, U second)
{
    reorder_sink(first, second);
}
// @template T
static inline T cross_root(T value)
{
    return cross_helper(value);
}
