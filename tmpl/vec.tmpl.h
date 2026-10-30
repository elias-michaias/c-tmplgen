/* Includes are ordinary template includes; the generator deduplicates them. */
#include <stddef.h>
#include <stdlib.h>

#ifndef TMPLGEN
/* Standalone-editor fallback. The generator strips this guarded block. */
typedef int T;
#endif

// @template T
typedef struct {
  T *data;
  size_t len, cap;
} Vec;

static inline void push(Vec *v, T x) {
  if (v->len == v->cap) {
    v->cap = v->cap ? v->cap * 2 : 8;
    v->data = (T *)realloc(v->data, v->cap * sizeof(T));
  }
  v->data[v->len++] = x;
}
