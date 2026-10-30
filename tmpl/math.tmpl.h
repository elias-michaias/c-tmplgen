#ifndef TMPLGEN
/* Standalone-editor fallback. The generator strips this guarded block. */
typedef int T;
#endif

// @template T
static inline T max(T a, T b) { return a > b ? a : b; }
// @template T
static inline T clamp(T x, T lo, T hi) { return max(lo, x) > hi ? hi : max(lo, x); }
// @template T
static inline T chain_leaf(T value) { return value + (T)1; }
// @template T
static inline T chain_middle(T value) { return chain_leaf(value); }
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
