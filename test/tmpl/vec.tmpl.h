#include <stddef.h>
#include <stdlib.h>

#ifndef TMPLGEN
typedef int T;
#define Vec(type) Vec
#endif

// @template T
typedef struct {
  T *data;
  size_t len, cap;
} Vec;

// @template T
static inline void push(Vec(T) * v, T x) {
  if (v->len == v->cap) {
    v->cap = v->cap ? v->cap * 2 : 8;
    v->data = (T *)realloc(v->data, v->cap * sizeof(T));
  }
  v->data[v->len++] = x;
}
