#ifndef TMPLGEN
typedef int T;
typedef int U;
#define typename
#endif

/* Rejected: T is demanded explicitly while U would have to be inferred. */
// @template T, U
static inline T bad_mix(typename T, U value)
{
    (void)value;
    return (T){0};
}
