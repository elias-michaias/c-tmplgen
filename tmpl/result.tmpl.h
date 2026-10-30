// clang-format off
#include "../physics.h"
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
#define Option(type) Option
#define TaggedBox(type) struct TaggedBox
#define TaggedChoice(type) union TaggedChoice
#define Box(type) Box
#endif

// @template T, E
typedef struct {
    T value;
    E error;
} Result;

// @template T
typedef struct {
    T value;
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
static inline T result_value(Result(T, E) *result) { return result->value; }
// @template T, E
static inline E result_error(Result(T, E) *result) { return result->error; }
// @template T
static inline T result_identity(T value) { return value; }
// @template TP <: Physics
static inline int physics_sum(TP *value) { return value->x + value->y; }
// @template UE <: Entity + Named
static inline int named_actor_id(UE *value) { return value->id; }
// @template TP <: Physics, UE <: Entity + Named
static inline int structural_pair(TP *physics, UE *entity)
{
    return physics->x + entity->id;
}
// @template AI |= NAMED_INT_ALIGNMENT(Physics, AI) + INT_ALIGNMENT(AI)
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
static inline T option_value(Option(T) *option) { return option->value; }
// @template T
static inline T tagged_box_value(TaggedBox(T) *box) { return box->value; }
// @template T
static inline T tagged_choice_value(TaggedChoice(T) *choice) { return choice->value; }
// @template T
static inline T option_pair(Option(T) *left, Option(T) *right)
{
    return left->value + right->value;
}
// @template T, U
static inline Box(U) box_select(Box(T) *first, Box(U) *second)
{
    (void)first;
    return *second;
}
static inline int result_version(void) { return 1; }
// @template T, E
static inline Result(T, E) result_make(T value, E error)
{
    Result(T, E) result = { value, error };
    return result;
}
