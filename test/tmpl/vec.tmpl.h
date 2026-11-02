#include <stddef.h>
#include <stdlib.h>

#ifndef TMPLGEN
typedef int T;
#define Vec(type) Vec
#endif

/* Field metadata type for @meta annotations (see test/cases/reflect_meta.c). */
typedef struct FieldInfo { const char *key; int code; } FieldInfo;

// @template T
typedef struct {
  // @meta (FieldInfo){.key = "data", .code = 1}
  T *data;
  // @meta (FieldInfo){.key = "len", .code = 2}
  size_t len;
  size_t cap;
} Vec;

// @template T
static inline void push(Vec(T) * v, T x) {
  if (v->len == v->cap) {
    v->cap = v->cap ? v->cap * 2 : 8;
    v->data = (T *)realloc(v->data, v->cap * sizeof(T));
  }
  v->data[v->len++] = x;
}
