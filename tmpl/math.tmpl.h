#ifndef TMPLGEN
/* Standalone-editor fallback. The generator strips this guarded block. */
typedef int T;
#endif

// @template T
static inline T max(T a, T b) { return a > b ? a : b; }
static inline T clamp(T x, T lo, T hi) { return max(lo, x) > hi ? hi : max(lo, x); }
