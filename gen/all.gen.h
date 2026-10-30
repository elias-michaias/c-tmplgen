#ifndef TMPL_H
#define TMPL_H
/* AUTO-GENERATED aggregate. DO NOT EDIT. */
#ifndef TMPLGEN_AGGREGATING
#define TMPLGEN_AGGREGATING
#define TMPLGEN_AGGREGATING_LOCAL
#endif
#include "math.gen.h"
#include "result.gen.h"
#include "triple.gen.h"
#include "vec.gen.h"
#ifdef TMPLGEN_AGGREGATING_LOCAL
#undef TMPLGEN_AGGREGATING_LOCAL
#undef TMPLGEN_AGGREGATING
#endif

#undef ResultPayload
#define ResultPayload(type0, type1) ResultPayload_##type0##_##type1
#undef Result
#define Result(type0, type1) Result_##type0##_##type1
#undef OptionPayload
#define OptionPayload(type0) OptionPayload_##type0
#undef Option
#define Option(type0) Option_##type0
#undef TaggedBox
#define TaggedBox(type0) struct TaggedBox_##type0
#undef TaggedChoice
#define TaggedChoice(type0) union TaggedChoice_##type0
#undef Box
#define Box(type0) Box_##type0

#undef Triple
#define Triple(type0, type1, type2) Triple_##type0##_##type1##_##type2

#undef Vec
#define Vec(type0) Vec_##type0

/* Dispatch macros for all generated template functions. */
#ifndef GEN_ARG0
#define GEN_ARG0(...) GEN_ARG0_(__VA_ARGS__, 0)
#define GEN_ARG0_(arg0, ...) arg0
#endif
#ifndef GEN_ARG1
#define GEN_ARG1(...) GEN_ARG1_(__VA_ARGS__, 0, 0)
#define GEN_ARG1_(arg0, arg1, ...) arg1
#endif
#ifndef GEN_ARG2
#define GEN_ARG2(...) GEN_ARG2_(__VA_ARGS__, 0, 0, 0)
#define GEN_ARG2_(arg0, arg1, arg2, ...) arg2
#endif

#line 12 "tmpl/math.tmpl.h"
/// static inline TestInt max_TestInt(TestInt a, TestInt b)
static inline TestInt max_TestInt(TestInt a, TestInt b);
/// static inline double max_double(double a, double b)
static inline double max_double(double a, double b);
/// static inline char * max_char_ptr(char * a, char * b)
static inline char * max_char_ptr(char * a, char * b);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_max_argument { int unused; };
extern void max__no_match_for_argument_type(struct tmplgen_no_association_for_max_argument, ...);
#undef max
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline T max(T a, T b)
#line 12 "tmpl/math.tmpl.h"
#define max(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TestInt: max_TestInt, \
    double: max_double, \
    char *: max_char_ptr, \
    default: max__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline T max(T a, T b)
#line 12 "tmpl/math.tmpl.h"
#define max(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), TestInt)), \
        max_TestInt, \
        __builtin_choose_expr( \
            (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), double)), \
            max_double, \
            __builtin_choose_expr( \
                (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), char *)), \
                max_char_ptr, \
                max__no_match_for_argument_type)))(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 20 "tmpl/math.tmpl.h"
/// static inline TestInt chain_root_TestInt(TestInt value)
static inline TestInt chain_root_TestInt(TestInt value);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_chain_root_argument { int unused; };
extern void chain_root__no_match_for_argument_type(struct tmplgen_no_association_for_chain_root_argument, ...);
#undef chain_root
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline T chain_root(T value)
#line 20 "tmpl/math.tmpl.h"
#define chain_root(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TestInt: chain_root_TestInt, \
    default: chain_root__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline T chain_root(T value)
#line 20 "tmpl/math.tmpl.h"
#define chain_root(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), TestInt)), \
        chain_root_TestInt, \
        chain_root__no_match_for_argument_type)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 22 "tmpl/math.tmpl.h"
/// static inline TestInt recursive_value_TestInt(TestInt value, unsigned int count)
static inline TestInt recursive_value_TestInt(TestInt value, unsigned int count);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_recursive_value_argument { int unused; };
extern void recursive_value__no_match_for_argument_type(struct tmplgen_no_association_for_recursive_value_argument, ...);
#undef recursive_value
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline T recursive_value(T value, unsigned int count)
#line 22 "tmpl/math.tmpl.h"
#define recursive_value(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TestInt: recursive_value_TestInt, \
    default: recursive_value__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline T recursive_value(T value, unsigned int count)
#line 22 "tmpl/math.tmpl.h"
#define recursive_value(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), TestInt)), \
        recursive_value_TestInt, \
        recursive_value__no_match_for_argument_type)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 30 "tmpl/math.tmpl.h"
/// static inline TestInt mutual_first_TestInt(TestInt value, unsigned int count)
static inline TestInt mutual_first_TestInt(TestInt value, unsigned int count);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_mutual_first_argument { int unused; };
extern void mutual_first__no_match_for_argument_type(struct tmplgen_no_association_for_mutual_first_argument, ...);
#undef mutual_first
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline T mutual_first(T value, unsigned int count)
#line 30 "tmpl/math.tmpl.h"
#define mutual_first(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TestInt: mutual_first_TestInt, \
    default: mutual_first__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline T mutual_first(T value, unsigned int count)
#line 30 "tmpl/math.tmpl.h"
#define mutual_first(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), TestInt)), \
        mutual_first_TestInt, \
        mutual_first__no_match_for_argument_type)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 48 "tmpl/math.tmpl.h"
/// static inline void reorder_call_TestInt_double(TestInt first, double second)
static inline void reorder_call_TestInt_double(TestInt first, double second);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_reorder_call_argument { int unused; };
extern void reorder_call__no_match_for_argument_type(struct tmplgen_no_association_for_reorder_call_argument, ...);
#undef reorder_call
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline void reorder_call(T first, U second)
#line 48 "tmpl/math.tmpl.h"
#define reorder_call(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TestInt:     _Generic((GEN_ARG1(__VA_ARGS__)), \
        double: reorder_call_TestInt_double, \
        default: reorder_call__no_match_for_argument_type), \
    default: reorder_call__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline void reorder_call(T first, U second)
#line 48 "tmpl/math.tmpl.h"
#define reorder_call(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), TestInt) && __builtin_types_compatible_p(__typeof__(GEN_ARG1(__VA_ARGS__)), double)), \
        reorder_call_TestInt_double, \
        reorder_call__no_match_for_argument_type)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 53 "tmpl/math.tmpl.h"
/// static inline TestInt cross_root_TestInt(TestInt value)
static inline TestInt cross_root_TestInt(TestInt value);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_cross_root_argument { int unused; };
extern void cross_root__no_match_for_argument_type(struct tmplgen_no_association_for_cross_root_argument, ...);
#undef cross_root
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline T cross_root(T value)
#line 53 "tmpl/math.tmpl.h"
#define cross_root(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TestInt: cross_root_TestInt, \
    default: cross_root__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline T cross_root(T value)
#line 53 "tmpl/math.tmpl.h"
#define cross_root(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), TestInt)), \
        cross_root_TestInt, \
        cross_root__no_match_for_argument_type)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 76 "tmpl/result.tmpl.h"
/// static inline int result_is_ok_TestInt_ErrorCode(Result_TestInt_ErrorCode * result)
static inline int result_is_ok_TestInt_ErrorCode(Result_TestInt_ErrorCode * result);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_result_is_ok_argument { int unused; };
extern void result_is_ok__no_match_for_argument_type(struct tmplgen_no_association_for_result_is_ok_argument, ...);
#undef result_is_ok
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline int result_is_ok(Result(T, E) * result)
#line 76 "tmpl/result.tmpl.h"
#define result_is_ok(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    Result_TestInt_ErrorCode *: result_is_ok_TestInt_ErrorCode, \
    default: result_is_ok__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline int result_is_ok(Result(T, E) * result)
#line 76 "tmpl/result.tmpl.h"
#define result_is_ok(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), Result_TestInt_ErrorCode *)), \
        result_is_ok_TestInt_ErrorCode, \
        result_is_ok__no_match_for_argument_type)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 81 "tmpl/result.tmpl.h"
/// static inline int result_is_error_TestInt_ErrorCode(Result_TestInt_ErrorCode * result)
static inline int result_is_error_TestInt_ErrorCode(Result_TestInt_ErrorCode * result);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_result_is_error_argument { int unused; };
extern void result_is_error__no_match_for_argument_type(struct tmplgen_no_association_for_result_is_error_argument, ...);
#undef result_is_error
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline int result_is_error(Result(T, E) * result)
#line 81 "tmpl/result.tmpl.h"
#define result_is_error(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    Result_TestInt_ErrorCode *: result_is_error_TestInt_ErrorCode, \
    default: result_is_error__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline int result_is_error(Result(T, E) * result)
#line 81 "tmpl/result.tmpl.h"
#define result_is_error(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), Result_TestInt_ErrorCode *)), \
        result_is_error_TestInt_ErrorCode, \
        result_is_error__no_match_for_argument_type)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 86 "tmpl/result.tmpl.h"
/// static inline TestInt result_value_TestInt_ErrorCode(Result_TestInt_ErrorCode * result)
static inline TestInt result_value_TestInt_ErrorCode(Result_TestInt_ErrorCode * result);
/// static inline InferredLong result_value_InferredLong_InferredError(Result_InferredLong_InferredError * result)
static inline InferredLong result_value_InferredLong_InferredError(Result_InferredLong_InferredError * result);
/// static inline Tag result_value_Tag_InferredError(Result_Tag_InferredError * result)
static inline Tag result_value_Tag_InferredError(Result_Tag_InferredError * result);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_result_value_argument { int unused; };
extern void result_value__no_match_for_argument_type(struct tmplgen_no_association_for_result_value_argument, ...);
#undef result_value
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline T result_value(Result(T, E) * result)
#line 86 "tmpl/result.tmpl.h"
#define result_value(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    Result_TestInt_ErrorCode *: result_value_TestInt_ErrorCode, \
    Result_InferredLong_InferredError *: result_value_InferredLong_InferredError, \
    Result_Tag_InferredError *: result_value_Tag_InferredError, \
    default: result_value__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline T result_value(Result(T, E) * result)
#line 86 "tmpl/result.tmpl.h"
#define result_value(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), Result_TestInt_ErrorCode *)), \
        result_value_TestInt_ErrorCode, \
        __builtin_choose_expr( \
            (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), Result_InferredLong_InferredError *)), \
            result_value_InferredLong_InferredError, \
            __builtin_choose_expr( \
                (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), Result_Tag_InferredError *)), \
                result_value_Tag_InferredError, \
                result_value__no_match_for_argument_type)))(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 92 "tmpl/result.tmpl.h"
/// static inline ErrorCode result_error_TestInt_ErrorCode(Result_TestInt_ErrorCode * result)
static inline ErrorCode result_error_TestInt_ErrorCode(Result_TestInt_ErrorCode * result);
/// static inline ErrorCode result_error_InferredDouble_ErrorCode(Result_InferredDouble_ErrorCode * result)
static inline ErrorCode result_error_InferredDouble_ErrorCode(Result_InferredDouble_ErrorCode * result);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_result_error_argument { int unused; };
extern void result_error__no_match_for_argument_type(struct tmplgen_no_association_for_result_error_argument, ...);
#undef result_error
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline E result_error(Result(T, E) * result)
#line 92 "tmpl/result.tmpl.h"
#define result_error(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    Result_TestInt_ErrorCode *: result_error_TestInt_ErrorCode, \
    Result_InferredDouble_ErrorCode *: result_error_InferredDouble_ErrorCode, \
    default: result_error__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline E result_error(Result(T, E) * result)
#line 92 "tmpl/result.tmpl.h"
#define result_error(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), Result_TestInt_ErrorCode *)), \
        result_error_TestInt_ErrorCode, \
        __builtin_choose_expr( \
            (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), Result_InferredDouble_ErrorCode *)), \
            result_error_InferredDouble_ErrorCode, \
            result_error__no_match_for_argument_type))(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 98 "tmpl/result.tmpl.h"
/// static inline TestInt result_identity_TestInt(TestInt value)
static inline TestInt result_identity_TestInt(TestInt value);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_result_identity_argument { int unused; };
extern void result_identity__no_match_for_argument_type(struct tmplgen_no_association_for_result_identity_argument, ...);
#undef result_identity
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline T result_identity(T value)
#line 98 "tmpl/result.tmpl.h"
#define result_identity(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TestInt: result_identity_TestInt, \
    default: result_identity__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline T result_identity(T value)
#line 98 "tmpl/result.tmpl.h"
#define result_identity(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), TestInt)), \
        result_identity_TestInt, \
        result_identity__no_match_for_argument_type)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 101 "tmpl/result.tmpl.h"
/// static inline int physics_sum_PhysicsBody(PhysicsBody * value)
static inline int physics_sum_PhysicsBody(PhysicsBody * value);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_physics_sum_argument { int unused; };
extern void physics_sum__no_match_for_argument_type(struct tmplgen_no_association_for_physics_sum_argument, ...);
#undef physics_sum
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline int physics_sum(TP * value)
#line 101 "tmpl/result.tmpl.h"
#define physics_sum(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    PhysicsBody *: physics_sum_PhysicsBody, \
    default: physics_sum__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline int physics_sum(TP * value)
#line 101 "tmpl/result.tmpl.h"
#define physics_sum(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), PhysicsBody *)), \
        physics_sum_PhysicsBody, \
        physics_sum__no_match_for_argument_type)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 104 "tmpl/result.tmpl.h"
/// static inline int named_actor_id_NamedActor(NamedActor * value)
static inline int named_actor_id_NamedActor(NamedActor * value);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_named_actor_id_argument { int unused; };
extern void named_actor_id__no_match_for_argument_type(struct tmplgen_no_association_for_named_actor_id_argument, ...);
#undef named_actor_id
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline int named_actor_id(UE * value)
#line 104 "tmpl/result.tmpl.h"
#define named_actor_id(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    NamedActor *: named_actor_id_NamedActor, \
    default: named_actor_id__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline int named_actor_id(UE * value)
#line 104 "tmpl/result.tmpl.h"
#define named_actor_id(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), NamedActor *)), \
        named_actor_id_NamedActor, \
        named_actor_id__no_match_for_argument_type)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 106 "tmpl/result.tmpl.h"
/// static inline int structural_pair_PhysicsBody_NamedActor(PhysicsBody * physics, NamedActor * entity)
static inline int structural_pair_PhysicsBody_NamedActor(PhysicsBody * physics, NamedActor * entity);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_structural_pair_argument { int unused; };
extern void structural_pair__no_match_for_argument_type(struct tmplgen_no_association_for_structural_pair_argument, ...);
#undef structural_pair
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline int structural_pair(TP * physics, UE * entity)
#line 106 "tmpl/result.tmpl.h"
#define structural_pair(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    PhysicsBody *:     _Generic((GEN_ARG1(__VA_ARGS__)), \
        NamedActor *: structural_pair_PhysicsBody_NamedActor, \
        default: structural_pair__no_match_for_argument_type), \
    default: structural_pair__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline int structural_pair(TP * physics, UE * entity)
#line 106 "tmpl/result.tmpl.h"
#define structural_pair(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), PhysicsBody *) && __builtin_types_compatible_p(__typeof__(GEN_ARG1(__VA_ARGS__)), NamedActor *)), \
        structural_pair_PhysicsBody_NamedActor, \
        structural_pair__no_match_for_argument_type)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 112 "tmpl/result.tmpl.h"
/// static inline int same_size_TestInt_TestInt(TestInt left, TestInt right)
static inline int same_size_TestInt_TestInt(TestInt left, TestInt right);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_same_size_argument { int unused; };
extern void same_size__no_match_for_argument_type(struct tmplgen_no_association_for_same_size_argument, ...);
#undef same_size
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline int same_size(T left, U right)
#line 112 "tmpl/result.tmpl.h"
#define same_size(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TestInt:     _Generic((GEN_ARG1(__VA_ARGS__)), \
        TestInt: same_size_TestInt_TestInt, \
        default: same_size__no_match_for_argument_type), \
    default: same_size__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline int same_size(T left, U right)
#line 112 "tmpl/result.tmpl.h"
#define same_size(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), TestInt) && __builtin_types_compatible_p(__typeof__(GEN_ARG1(__VA_ARGS__)), TestInt)), \
        same_size_TestInt_TestInt, \
        same_size__no_match_for_argument_type)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 118 "tmpl/result.tmpl.h"
/// static inline int alignment_ok_PhysicsBody(PhysicsBody * value)
static inline int alignment_ok_PhysicsBody(PhysicsBody * value);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_alignment_ok_argument { int unused; };
extern void alignment_ok__no_match_for_argument_type(struct tmplgen_no_association_for_alignment_ok_argument, ...);
#undef alignment_ok
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline int alignment_ok(AI * value)
#line 118 "tmpl/result.tmpl.h"
#define alignment_ok(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    PhysicsBody *: alignment_ok_PhysicsBody, \
    default: alignment_ok__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline int alignment_ok(AI * value)
#line 118 "tmpl/result.tmpl.h"
#define alignment_ok(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), PhysicsBody *)), \
        alignment_ok_PhysicsBody, \
        alignment_ok__no_match_for_argument_type)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 126 "tmpl/result.tmpl.h"
/// static inline TestInt result_pointer_TestInt(TestInt * value)
static inline TestInt result_pointer_TestInt(TestInt * value);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_result_pointer_argument { int unused; };
extern void result_pointer__no_match_for_argument_type(struct tmplgen_no_association_for_result_pointer_argument, ...);
#undef result_pointer
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline T result_pointer(T * value)
#line 126 "tmpl/result.tmpl.h"
#define result_pointer(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TestInt *: result_pointer_TestInt, \
    default: result_pointer__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline T result_pointer(T * value)
#line 126 "tmpl/result.tmpl.h"
#define result_pointer(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), TestInt *)), \
        result_pointer_TestInt, \
        result_pointer__no_match_for_argument_type)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 128 "tmpl/result.tmpl.h"
/// static inline Option_TestInt option_some_TestInt(TestInt value)
static inline Option_TestInt option_some_TestInt(TestInt value);
/// static inline Option_double option_some_double(double value)
static inline Option_double option_some_double(double value);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_option_some_argument { int unused; };
extern void option_some__no_match_for_argument_type(struct tmplgen_no_association_for_option_some_argument, ...);
#undef option_some
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline Option(T) option_some(T value)
#line 128 "tmpl/result.tmpl.h"
#define option_some(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TestInt: option_some_TestInt, \
    double: option_some_double, \
    default: option_some__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline Option(T) option_some(T value)
#line 128 "tmpl/result.tmpl.h"
#define option_some(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), TestInt)), \
        option_some_TestInt, \
        __builtin_choose_expr( \
            (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), double)), \
            option_some_double, \
            option_some__no_match_for_argument_type))(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 136 "tmpl/result.tmpl.h"
/// static inline void option_none_TestInt(Option_TestInt * option)
static inline void option_none_TestInt(Option_TestInt * option);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_option_none_argument { int unused; };
extern void option_none__no_match_for_argument_type(struct tmplgen_no_association_for_option_none_argument, ...);
#undef option_none
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline void option_none(Option(T) * option)
#line 136 "tmpl/result.tmpl.h"
#define option_none(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    Option_TestInt *: option_none_TestInt, \
    default: option_none__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline void option_none(Option(T) * option)
#line 136 "tmpl/result.tmpl.h"
#define option_none(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), Option_TestInt *)), \
        option_none_TestInt, \
        option_none__no_match_for_argument_type)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 141 "tmpl/result.tmpl.h"
/// static inline int option_is_some_TestInt(Option_TestInt * option)
static inline int option_is_some_TestInt(Option_TestInt * option);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_option_is_some_argument { int unused; };
extern void option_is_some__no_match_for_argument_type(struct tmplgen_no_association_for_option_is_some_argument, ...);
#undef option_is_some
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline int option_is_some(Option(T) * option)
#line 141 "tmpl/result.tmpl.h"
#define option_is_some(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    Option_TestInt *: option_is_some_TestInt, \
    default: option_is_some__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline int option_is_some(Option(T) * option)
#line 141 "tmpl/result.tmpl.h"
#define option_is_some(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), Option_TestInt *)), \
        option_is_some_TestInt, \
        option_is_some__no_match_for_argument_type)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 146 "tmpl/result.tmpl.h"
/// static inline TestInt option_value_TestInt(Option_TestInt * option)
static inline TestInt option_value_TestInt(Option_TestInt * option);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_option_value_argument { int unused; };
extern void option_value__no_match_for_argument_type(struct tmplgen_no_association_for_option_value_argument, ...);
#undef option_value
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline T option_value(Option(T) * option)
#line 146 "tmpl/result.tmpl.h"
#define option_value(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    Option_TestInt *: option_value_TestInt, \
    default: option_value__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline T option_value(Option(T) * option)
#line 146 "tmpl/result.tmpl.h"
#define option_value(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), Option_TestInt *)), \
        option_value_TestInt, \
        option_value__no_match_for_argument_type)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 152 "tmpl/result.tmpl.h"
/// static inline TestInt tagged_box_value_TestInt(struct TaggedBox_TestInt * box)
static inline TestInt tagged_box_value_TestInt(struct TaggedBox_TestInt * box);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_tagged_box_value_argument { int unused; };
extern void tagged_box_value__no_match_for_argument_type(struct tmplgen_no_association_for_tagged_box_value_argument, ...);
#undef tagged_box_value
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline T tagged_box_value( TaggedBox(T) * box)
#line 152 "tmpl/result.tmpl.h"
#define tagged_box_value(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    struct TaggedBox_TestInt *: tagged_box_value_TestInt, \
    default: tagged_box_value__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline T tagged_box_value( TaggedBox(T) * box)
#line 152 "tmpl/result.tmpl.h"
#define tagged_box_value(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), struct TaggedBox_TestInt *)), \
        tagged_box_value_TestInt, \
        tagged_box_value__no_match_for_argument_type)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 154 "tmpl/result.tmpl.h"
/// static inline TestInt tagged_choice_value_TestInt(union TaggedChoice_TestInt * choice)
static inline TestInt tagged_choice_value_TestInt(union TaggedChoice_TestInt * choice);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_tagged_choice_value_argument { int unused; };
extern void tagged_choice_value__no_match_for_argument_type(struct tmplgen_no_association_for_tagged_choice_value_argument, ...);
#undef tagged_choice_value
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline T tagged_choice_value( TaggedChoice(T) * choice)
#line 154 "tmpl/result.tmpl.h"
#define tagged_choice_value(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    union TaggedChoice_TestInt *: tagged_choice_value_TestInt, \
    default: tagged_choice_value__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline T tagged_choice_value( TaggedChoice(T) * choice)
#line 154 "tmpl/result.tmpl.h"
#define tagged_choice_value(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), union TaggedChoice_TestInt *)), \
        tagged_choice_value_TestInt, \
        tagged_choice_value__no_match_for_argument_type)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 156 "tmpl/result.tmpl.h"
/// static inline TestInt option_pair_TestInt(Option_TestInt * left, Option_TestInt * right)
static inline TestInt option_pair_TestInt(Option_TestInt * left, Option_TestInt * right);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_option_pair_argument { int unused; };
extern void option_pair__no_match_for_argument_type(struct tmplgen_no_association_for_option_pair_argument, ...);
#undef option_pair
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline T option_pair(Option(T) * left, Option(T) * right)
#line 156 "tmpl/result.tmpl.h"
#define option_pair(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    Option_TestInt *: option_pair_TestInt, \
    default: option_pair__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline T option_pair(Option(T) * left, Option(T) * right)
#line 156 "tmpl/result.tmpl.h"
#define option_pair(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), Option_TestInt *)), \
        option_pair_TestInt, \
        option_pair__no_match_for_argument_type)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 163 "tmpl/result.tmpl.h"
/// static inline Box_double box_select_TestInt_double(Box_TestInt * first, Box_double * second)
static inline Box_double box_select_TestInt_double(Box_TestInt * first, Box_double * second);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_box_select_argument { int unused; };
extern void box_select__no_match_for_argument_type(struct tmplgen_no_association_for_box_select_argument, ...);
#undef box_select
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline Box(U) box_select(Box(T) * first, Box(U) * second)
#line 163 "tmpl/result.tmpl.h"
#define box_select(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    Box_TestInt *:     _Generic((GEN_ARG1(__VA_ARGS__)), \
        Box_double *: box_select_TestInt_double, \
        default: box_select__no_match_for_argument_type), \
    default: box_select__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline Box(U) box_select(Box(T) * first, Box(U) * second)
#line 163 "tmpl/result.tmpl.h"
#define box_select(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), Box_TestInt *) && __builtin_types_compatible_p(__typeof__(GEN_ARG1(__VA_ARGS__)), Box_double *)), \
        box_select_TestInt_double, \
        box_select__no_match_for_argument_type)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 170 "tmpl/result.tmpl.h"
/// static inline Result_TestInt_ErrorCode result_make_TestInt_ErrorCode(ResultTag tag, TestInt value, ErrorCode error)
static inline Result_TestInt_ErrorCode result_make_TestInt_ErrorCode(ResultTag tag, TestInt value, ErrorCode error);
/// static inline Result_InferredLong_InferredError result_make_InferredLong_InferredError(ResultTag tag, InferredLong value, InferredError error)
static inline Result_InferredLong_InferredError result_make_InferredLong_InferredError(ResultTag tag, InferredLong value, InferredError error);
/// static inline Result_InferredDouble_ErrorCode result_make_InferredDouble_ErrorCode(ResultTag tag, InferredDouble value, ErrorCode error)
static inline Result_InferredDouble_ErrorCode result_make_InferredDouble_ErrorCode(ResultTag tag, InferredDouble value, ErrorCode error);
/// static inline Result_Tag_InferredError result_make_Tag_InferredError(ResultTag tag, Tag value, InferredError error)
static inline Result_Tag_InferredError result_make_Tag_InferredError(ResultTag tag, Tag value, InferredError error);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_result_make_argument { int unused; };
extern void result_make__no_match_for_argument_type(struct tmplgen_no_association_for_result_make_argument, ...);
#undef result_make
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline Result(T, E) result_make(ResultTag tag, T value, E error)
#line 170 "tmpl/result.tmpl.h"
#define result_make(...) _Generic((GEN_ARG1(__VA_ARGS__)), \
    TestInt:     _Generic((GEN_ARG2(__VA_ARGS__)), \
        ErrorCode: result_make_TestInt_ErrorCode, \
        default: result_make__no_match_for_argument_type), \
    InferredLong:     _Generic((GEN_ARG2(__VA_ARGS__)), \
        InferredError: result_make_InferredLong_InferredError, \
        default: result_make__no_match_for_argument_type), \
    InferredDouble:     _Generic((GEN_ARG2(__VA_ARGS__)), \
        ErrorCode: result_make_InferredDouble_ErrorCode, \
        default: result_make__no_match_for_argument_type), \
    Tag:     _Generic((GEN_ARG2(__VA_ARGS__)), \
        InferredError: result_make_Tag_InferredError, \
        default: result_make__no_match_for_argument_type), \
    default: result_make__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline Result(T, E) result_make(ResultTag tag, T value, E error)
#line 170 "tmpl/result.tmpl.h"
#define result_make(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG1(__VA_ARGS__)), TestInt) && __builtin_types_compatible_p(__typeof__(GEN_ARG2(__VA_ARGS__)), ErrorCode)), \
        result_make_TestInt_ErrorCode, \
        __builtin_choose_expr( \
            (__builtin_types_compatible_p(__typeof__(GEN_ARG1(__VA_ARGS__)), InferredLong) && __builtin_types_compatible_p(__typeof__(GEN_ARG2(__VA_ARGS__)), InferredError)), \
            result_make_InferredLong_InferredError, \
            __builtin_choose_expr( \
                (__builtin_types_compatible_p(__typeof__(GEN_ARG1(__VA_ARGS__)), InferredDouble) && __builtin_types_compatible_p(__typeof__(GEN_ARG2(__VA_ARGS__)), ErrorCode)), \
                result_make_InferredDouble_ErrorCode, \
                __builtin_choose_expr( \
                    (__builtin_types_compatible_p(__typeof__(GEN_ARG1(__VA_ARGS__)), Tag) && __builtin_types_compatible_p(__typeof__(GEN_ARG2(__VA_ARGS__)), InferredError)), \
                    result_make_Tag_InferredError, \
                    result_make__no_match_for_argument_type))))(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 16 "tmpl/triple.tmpl.h"
/// static inline Triple_TestInt_ErrorCode_Tag triple_make_TestInt_ErrorCode_Tag(TestInt first, ErrorCode second, Tag third)
static inline Triple_TestInt_ErrorCode_Tag triple_make_TestInt_ErrorCode_Tag(TestInt first, ErrorCode second, Tag third);
/// static inline Triple_double_ErrorCode_Tag triple_make_double_ErrorCode_Tag(double first, ErrorCode second, Tag third)
static inline Triple_double_ErrorCode_Tag triple_make_double_ErrorCode_Tag(double first, ErrorCode second, Tag third);
/// static inline Triple_Tag_InferredError_InferredLong triple_make_Tag_InferredError_InferredLong(Tag first, InferredError second, InferredLong third)
static inline Triple_Tag_InferredError_InferredLong triple_make_Tag_InferredError_InferredLong(Tag first, InferredError second, InferredLong third);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_triple_make_argument { int unused; };
extern void triple_make__no_match_for_argument_type(struct tmplgen_no_association_for_triple_make_argument, ...);
#undef triple_make
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline Triple(A, B, C) triple_make(A first, B second, C third)
#line 16 "tmpl/triple.tmpl.h"
#define triple_make(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TestInt:     _Generic((GEN_ARG1(__VA_ARGS__)), \
        ErrorCode:         _Generic((GEN_ARG2(__VA_ARGS__)), \
            Tag: triple_make_TestInt_ErrorCode_Tag, \
            default: triple_make__no_match_for_argument_type), \
        default: triple_make__no_match_for_argument_type), \
    double:     _Generic((GEN_ARG1(__VA_ARGS__)), \
        ErrorCode:         _Generic((GEN_ARG2(__VA_ARGS__)), \
            Tag: triple_make_double_ErrorCode_Tag, \
            default: triple_make__no_match_for_argument_type), \
        default: triple_make__no_match_for_argument_type), \
    Tag:     _Generic((GEN_ARG1(__VA_ARGS__)), \
        InferredError:         _Generic((GEN_ARG2(__VA_ARGS__)), \
            InferredLong: triple_make_Tag_InferredError_InferredLong, \
            default: triple_make__no_match_for_argument_type), \
        default: triple_make__no_match_for_argument_type), \
    default: triple_make__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline Triple(A, B, C) triple_make(A first, B second, C third)
#line 16 "tmpl/triple.tmpl.h"
#define triple_make(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), TestInt) && __builtin_types_compatible_p(__typeof__(GEN_ARG1(__VA_ARGS__)), ErrorCode) && __builtin_types_compatible_p(__typeof__(GEN_ARG2(__VA_ARGS__)), Tag)), \
        triple_make_TestInt_ErrorCode_Tag, \
        __builtin_choose_expr( \
            (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), double) && __builtin_types_compatible_p(__typeof__(GEN_ARG1(__VA_ARGS__)), ErrorCode) && __builtin_types_compatible_p(__typeof__(GEN_ARG2(__VA_ARGS__)), Tag)), \
            triple_make_double_ErrorCode_Tag, \
            __builtin_choose_expr( \
                (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), Tag) && __builtin_types_compatible_p(__typeof__(GEN_ARG1(__VA_ARGS__)), InferredError) && __builtin_types_compatible_p(__typeof__(GEN_ARG2(__VA_ARGS__)), InferredLong)), \
                triple_make_Tag_InferredError_InferredLong, \
                triple_make__no_match_for_argument_type)))(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 23 "tmpl/triple.tmpl.h"
/// static inline TestInt triple_first_TestInt_ErrorCode_Tag(Triple_TestInt_ErrorCode_Tag * value)
static inline TestInt triple_first_TestInt_ErrorCode_Tag(Triple_TestInt_ErrorCode_Tag * value);
/// static inline Tag triple_first_Tag_InferredError_InferredLong(Triple_Tag_InferredError_InferredLong * value)
static inline Tag triple_first_Tag_InferredError_InferredLong(Triple_Tag_InferredError_InferredLong * value);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_triple_first_argument { int unused; };
extern void triple_first__no_match_for_argument_type(struct tmplgen_no_association_for_triple_first_argument, ...);
#undef triple_first
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline A triple_first(Triple(A, B, C) * value)
#line 23 "tmpl/triple.tmpl.h"
#define triple_first(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    Triple_TestInt_ErrorCode_Tag *: triple_first_TestInt_ErrorCode_Tag, \
    Triple_Tag_InferredError_InferredLong *: triple_first_Tag_InferredError_InferredLong, \
    default: triple_first__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline A triple_first(Triple(A, B, C) * value)
#line 23 "tmpl/triple.tmpl.h"
#define triple_first(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), Triple_TestInt_ErrorCode_Tag *)), \
        triple_first_TestInt_ErrorCode_Tag, \
        __builtin_choose_expr( \
            (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), Triple_Tag_InferredError_InferredLong *)), \
            triple_first_Tag_InferredError_InferredLong, \
            triple_first__no_match_for_argument_type))(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 16 "tmpl/vec.tmpl.h"
/// static inline void push_TestInt(Vec_TestInt * v, TestInt x)
static inline void push_TestInt(Vec_TestInt * v, TestInt x);
/// static inline void push_int(Vec_int * v, int x)
static inline void push_int(Vec_int * v, int x);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_push_argument { int unused; };
extern void push__no_match_for_argument_type(struct tmplgen_no_association_for_push_argument, ...);
#undef push
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline void push(Vec(T) * v, T x)
#line 16 "tmpl/vec.tmpl.h"
#define push(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    Vec_TestInt *: push_TestInt, \
    Vec_int *: push_int, \
    default: push__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline void push(Vec(T) * v, T x)
#line 16 "tmpl/vec.tmpl.h"
#define push(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), Vec_TestInt *)), \
        push_TestInt, \
        __builtin_choose_expr( \
            (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), Vec_int *)), \
            push_int, \
            push__no_match_for_argument_type))(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif


#endif /* TMPL_H */
