#ifndef TMPLGEN
typedef int T;
typedef int E;
#endif

// @template T, E
typedef struct {
    T value;
    E error;
} Result;

typedef struct {
    T value;
} Option;

static inline T result_value(Result *result) { return result->value; }
static inline E result_error(Result *result) { return result->error; }
static inline T result_identity(T value) { return value; }
static inline T result_pointer(T *value) { return *value; }
static inline T option_value(Option *option) { return option->value; }
static inline int result_version(void) { return 1; }
static inline Result result_make(T value, E error)
{
    Result result = { value, error };
    return result;
}
