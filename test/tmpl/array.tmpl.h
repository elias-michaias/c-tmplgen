#include <stddef.h>

#ifndef TMPLGEN
typedef int T;
enum { N = 4 };
#define Array(type0, count0) Array
#endif

// @template T, size_t N
// @where N > 0
typedef struct {
  T data[N];
  size_t len;
} Array;

// @template T, size_t N
static inline size_t array_capacity(Array(T, N) * v) {
  (void)v;
  return N;
}

// @template T, size_t N
static inline T array_get(Array(T, N) * v, size_t i) { return v->data[i]; }

// The value argument folds to the same instance requested as Array(T, 2).
// @template T
static inline T array_pair_sum(Array(T, 1 + 1) * v) {
  return v->data[0] + v->data[1];
}
