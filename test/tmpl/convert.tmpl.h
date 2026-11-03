#include <string.h>

#ifndef TMPLGEN
typedef int T;
#define typename
#endif

// @template T
static inline T zero_value(typename T)
{
    return (T){0};
}

// @template T
static inline T read_as(typename T, const void *data)
{
    T value;
    memcpy(&value, data, sizeof value);
    return value;
}
