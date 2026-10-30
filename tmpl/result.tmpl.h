// clang-format off
#include <assert.h>
#include "../test/lib/physics.h"
#ifndef TMPLGEN
typedef int T;
typedef int E;
typedef int U;
typedef struct {
    int x;
    int y;
} TP;
typedef struct {
    int id;
    const char *name;
    unsigned flags;
} UE;
typedef int AI;
#define Result(type0, type1) Result
#define ResultPayload(type0, type1) ResultPayload
#define Option(type) Option
#define OptionPayload(type) OptionPayload
#define TaggedBox(type) struct TaggedBox
#define TaggedChoice(type) union TaggedChoice
#define Box(type) Box
#endif

typedef enum {
    RESULT_OK,
    RESULT_ERROR
} ResultTag;

// @template T, E
typedef union {
    T ok;
    E error;
} ResultPayload;

// @template T, E
typedef struct {
    ResultTag tag;
    ResultPayload(T, E) payload;
} Result;

typedef enum {
    OPTION_NONE,
    OPTION_SOME
} OptionTag;

// @template T
typedef union {
    T some;
} OptionPayload;

// @template T
typedef struct {
    OptionTag tag;
    OptionPayload(T) payload;
} Option;

// @template T
struct TaggedBox {
    T value;
};

// @template T
union TaggedChoice {
    T value;
};

// @template T
typedef struct {
    T value;
} Box;

// @template T, E
static inline int result_is_ok(Result(T, E) *result)
{
    return result->tag == RESULT_OK;
}
// @template T, E
static inline int result_is_error(Result(T, E) *result)
{
    return result->tag == RESULT_ERROR;
}
// @template T, E
static inline T result_value(Result(T, E) *result)
{
    assert(result->tag == RESULT_OK);
    return result->payload.ok;
}
// @template T, E
static inline E result_error(Result(T, E) *result)
{
    assert(result->tag == RESULT_ERROR);
    return result->payload.error;
}
// @template T
static inline T result_identity(T value) { return value; }
// @template TP <: Physics
// @template TP |= INT_ALIGNMENT(TP)
static inline int physics_sum(TP *value) { return value->x + value->y; }
// @template UE <: Entity
// @template UE <: Named
static inline int named_actor_id(UE *value) { return value->id; }
// @template TP <: Physics, UE <: Entity + Named
static inline int structural_pair(TP *physics, UE *entity)
{
    return physics->x + entity->id;
}
// @template
// @ AI |= NAMED_INT_ALIGNMENT(Physics, AI) + INT_ALIGNMENT(AI)
static inline int alignment_ok(AI *value)
{
    (void)value;
    return NAMED_INT_ALIGNMENT(Physics, AI);
}
// @template U
static inline U cross_helper(U value) { return value + 2; }
// @template T
static inline T result_pointer(T *value) { return *value; }
// @template T
static inline Option(T) option_some(T value)
{
    Option(T) option = {0};
    option.tag = OPTION_SOME;
    option.payload.some = value;
    return option;
}
// @template T
static inline void option_none(Option(T) *option)
{
    option->tag = OPTION_NONE;
}
// @template T
static inline int option_is_some(Option(T) *option)
{
    return option->tag == OPTION_SOME;
}
// @template T
static inline T option_value(Option(T) *option)
{
    assert(option->tag == OPTION_SOME);
    return option->payload.some;
}
// @template T
static inline T tagged_box_value(TaggedBox(T) *box) { return box->value; }
// @template T
static inline T tagged_choice_value(TaggedChoice(T) *choice) { return choice->value; }
// @template T
static inline T option_pair(Option(T) *left, Option(T) *right)
{
    assert(left->tag == OPTION_SOME);
    assert(right->tag == OPTION_SOME);
    return left->payload.some + right->payload.some;
}
// @template T, U
static inline Box(U) box_select(Box(T) *first, Box(U) *second)
{
    (void)first;
    return *second;
}
static inline int result_version(void) { return 1; }
// @template T, E
static inline Result(T, E) result_make(ResultTag tag, T value, E error)
{
    Result(T, E) result = {0};
    assert(tag == RESULT_OK || tag == RESULT_ERROR);
    result.tag = tag;
    if (tag == RESULT_OK) result.payload.ok = value;
    else result.payload.error = error;
    return result;
}
