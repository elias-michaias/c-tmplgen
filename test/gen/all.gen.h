/* AUTO-GENERATED aggregate. DO NOT EDIT.
   Include it after the headers that declare the types you use
   as template arguments; including it again later is fine. */
#ifndef TMPLGEN_AGGREGATING
#define TMPLGEN_AGGREGATING
#define TMPLGEN_AGGREGATING_LOCAL
#endif
#include "inspect.gen.h"
#include "math.gen.h"
#include "result.gen.h"
#include "triple.gen.h"
#include "vec.gen.h"
#ifdef TMPLGEN_AGGREGATING_LOCAL
#undef TMPLGEN_AGGREGATING_LOCAL
#undef TMPLGEN_AGGREGATING
#endif

#ifndef TMPL_H
#define TMPL_H
#line 37 "test/tmpl/result.tmpl.h"
#undef ResultPayload
#define ResultPayload(type0, type1) ResultPayload_##type0##_##type1
#line 43 "test/tmpl/result.tmpl.h"
#undef Result
#define Result(type0, type1) Result_##type0##_##type1
#line 53 "test/tmpl/result.tmpl.h"
#undef OptionPayload
#define OptionPayload(type0) OptionPayload_##type0
#line 59 "test/tmpl/result.tmpl.h"
#undef Option
#define Option(type0) Option_##type0
#line 62 "test/tmpl/result.tmpl.h"
#undef TaggedBox
#define TaggedBox(type0) struct TaggedBox_##type0
#line 67 "test/tmpl/result.tmpl.h"
#undef TaggedChoice
#define TaggedChoice(type0) union TaggedChoice_##type0
#line 74 "test/tmpl/result.tmpl.h"
#undef Box
#define Box(type0) Box_##type0
#line 80 "test/tmpl/result.tmpl.h"
#undef Pair
#define Pair(type0, type1) Pair_##type0##_##type1

#line 13 "test/tmpl/triple.tmpl.h"
#undef Triple
#define Triple(type0, type1, type2) Triple_##type0##_##type1##_##type2

#line 13 "test/tmpl/vec.tmpl.h"
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

#line 18 "test/tmpl/inspect.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_inspect_argument { int unused; };
extern void inspect__no_match_for_argument_type(struct tmplgen_no_association_for_inspect_argument, ...);
#ifndef TMPLGEN_GENERIC_inspect_N0_REFLECT_TYPES_H_AND_TMPLGEN_REFLECT_HAVE_REFLECT_TYPES_H
#define TMPLGEN_GENERIC_inspect_N0_REFLECT_TYPES_H_AND_TMPLGEN_REFLECT_HAVE_REFLECT_TYPES_H(...)
#endif
#ifndef TMPLGEN_GENERIC_inspect_N0_POINT_H_AND_TMPLGEN_REFLECT_HAVE_POINT_H
#define TMPLGEN_GENERIC_inspect_N0_POINT_H_AND_TMPLGEN_REFLECT_HAVE_POINT_H(...)
#endif
#ifndef TMPLGEN_GENERIC_inspect_N0_TMPLGEN_REFLECTION_H
#define TMPLGEN_GENERIC_inspect_N0_TMPLGEN_REFLECTION_H(...)
#endif
#ifndef TMPLGEN_GENERIC_inspect_N0_REFLECT_TYPES_H_AND_TMPLGEN_HAVE_TMPL_H_VEC_GEN_H_REFLECT_TYPES_H_AND_TMPLGEN_REFLECT_HAVE_REFLECT_TYPES_H_AND_TMPLGEN_REFLECT_HAVE_TMPLGEN_HAVE_TMPL_H_VEC_GEN_H_REFLECT_TYPES_H
#define TMPLGEN_GENERIC_inspect_N0_REFLECT_TYPES_H_AND_TMPLGEN_HAVE_TMPL_H_VEC_GEN_H_REFLECT_TYPES_H_AND_TMPLGEN_REFLECT_HAVE_REFLECT_TYPES_H_AND_TMPLGEN_REFLECT_HAVE_TMPLGEN_HAVE_TMPL_H_VEC_GEN_H_REFLECT_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_inspect_TMPLGEN_REFLECTION_H
#define TMPLGEN_GNU_inspect_TMPLGEN_REFLECTION_H(next, ...) next
#endif
#ifndef TMPLGEN_GNU_inspect_POINT_H_AND_TMPLGEN_REFLECT_HAVE_POINT_H
#define TMPLGEN_GNU_inspect_POINT_H_AND_TMPLGEN_REFLECT_HAVE_POINT_H(next, ...) next
#endif
#ifndef TMPLGEN_GNU_inspect_REFLECT_TYPES_H_AND_TMPLGEN_REFLECT_HAVE_REFLECT_TYPES_H
#define TMPLGEN_GNU_inspect_REFLECT_TYPES_H_AND_TMPLGEN_REFLECT_HAVE_REFLECT_TYPES_H(next, ...) next
#endif
#ifndef TMPLGEN_GNU_inspect_REFLECT_TYPES_H_AND_TMPLGEN_HAVE_TMPL_H_VEC_GEN_H_REFLECT_TYPES_H_AND_TMPLGEN_REFLECT_HAVE_REFLECT_TYPES_H_AND_TMPLGEN_REFLECT_HAVE_TMPLGEN_HAVE_TMPL_H_VEC_GEN_H_REFLECT_TYPES_H
#define TMPLGEN_GNU_inspect_REFLECT_TYPES_H_AND_TMPLGEN_HAVE_TMPL_H_VEC_GEN_H_REFLECT_TYPES_H_AND_TMPLGEN_REFLECT_HAVE_REFLECT_TYPES_H_AND_TMPLGEN_REFLECT_HAVE_TMPLGEN_HAVE_TMPL_H_VEC_GEN_H_REFLECT_TYPES_H(next, ...) next
#endif
#undef inspect
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline uint32_t inspect(T * value, FILE * out)
#line 18 "test/tmpl/inspect.tmpl.h"
#define inspect(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_inspect_N0_REFLECT_TYPES_H_AND_TMPLGEN_REFLECT_HAVE_REFLECT_TYPES_H(__VA_ARGS__) \
    TMPLGEN_GENERIC_inspect_N0_POINT_H_AND_TMPLGEN_REFLECT_HAVE_POINT_H(__VA_ARGS__) \
    TMPLGEN_GENERIC_inspect_N0_TMPLGEN_REFLECTION_H(__VA_ARGS__) \
    TMPLGEN_GENERIC_inspect_N0_REFLECT_TYPES_H_AND_TMPLGEN_HAVE_TMPL_H_VEC_GEN_H_REFLECT_TYPES_H_AND_TMPLGEN_REFLECT_HAVE_REFLECT_TYPES_H_AND_TMPLGEN_REFLECT_HAVE_TMPLGEN_HAVE_TMPL_H_VEC_GEN_H_REFLECT_TYPES_H(__VA_ARGS__) \
    default: inspect__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline uint32_t inspect(T * value, FILE * out)
#line 18 "test/tmpl/inspect.tmpl.h"
#define inspect(...) \
    TMPLGEN_GNU_inspect_TMPLGEN_REFLECTION_H(TMPLGEN_GNU_inspect_POINT_H_AND_TMPLGEN_REFLECT_HAVE_POINT_H(TMPLGEN_GNU_inspect_REFLECT_TYPES_H_AND_TMPLGEN_REFLECT_HAVE_REFLECT_TYPES_H(TMPLGEN_GNU_inspect_REFLECT_TYPES_H_AND_TMPLGEN_HAVE_TMPL_H_VEC_GEN_H_REFLECT_TYPES_H_AND_TMPLGEN_REFLECT_HAVE_REFLECT_TYPES_H_AND_TMPLGEN_REFLECT_HAVE_TMPLGEN_HAVE_TMPL_H_VEC_GEN_H_REFLECT_TYPES_H(inspect__no_match_for_argument_type, __VA_ARGS__), __VA_ARGS__), __VA_ARGS__), __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 12 "test/tmpl/math.tmpl.h"
/// static inline double max_double(double a, double b)
static inline double max_double(double a, double b);
/// static inline char * max_char_ptr(char * a, char * b)
static inline char * max_char_ptr(char * a, char * b);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_max_argument { int unused; };
extern void max__no_match_for_argument_type(struct tmplgen_no_association_for_max_argument, ...);
#ifndef TMPLGEN_GENERIC_max_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_max_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_max_TEST_TYPES_H
#define TMPLGEN_GNU_max_TEST_TYPES_H(next, ...) next
#endif
#undef max
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline T max(T a, T b)
#line 12 "test/tmpl/math.tmpl.h"
#define max(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    double: max_double, \
    char *: max_char_ptr, \
    TMPLGEN_GENERIC_max_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: max__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline T max(T a, T b)
#line 12 "test/tmpl/math.tmpl.h"
#define max(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), double)), \
        max_double, \
        __builtin_choose_expr( \
            (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), char *)), \
            max_char_ptr, \
            TMPLGEN_GNU_max_TEST_TYPES_H(max__no_match_for_argument_type, __VA_ARGS__)))(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 27 "test/tmpl/math.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_chain_root_argument { int unused; };
extern void chain_root__no_match_for_argument_type(struct tmplgen_no_association_for_chain_root_argument, ...);
#ifndef TMPLGEN_GENERIC_chain_root_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_chain_root_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_chain_root_TEST_TYPES_H
#define TMPLGEN_GNU_chain_root_TEST_TYPES_H(next, ...) next
#endif
#undef chain_root
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline T chain_root(T value)
#line 27 "test/tmpl/math.tmpl.h"
#define chain_root(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_chain_root_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: chain_root__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline T chain_root(T value)
#line 27 "test/tmpl/math.tmpl.h"
#define chain_root(...) \
    TMPLGEN_GNU_chain_root_TEST_TYPES_H(chain_root__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 29 "test/tmpl/math.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_recursive_value_argument { int unused; };
extern void recursive_value__no_match_for_argument_type(struct tmplgen_no_association_for_recursive_value_argument, ...);
#ifndef TMPLGEN_GENERIC_recursive_value_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_recursive_value_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_recursive_value_TEST_TYPES_H
#define TMPLGEN_GNU_recursive_value_TEST_TYPES_H(next, ...) next
#endif
#undef recursive_value
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline T recursive_value(T value, unsigned int count)
#line 29 "test/tmpl/math.tmpl.h"
#define recursive_value(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_recursive_value_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: recursive_value__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline T recursive_value(T value, unsigned int count)
#line 29 "test/tmpl/math.tmpl.h"
#define recursive_value(...) \
    TMPLGEN_GNU_recursive_value_TEST_TYPES_H(recursive_value__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 37 "test/tmpl/math.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_mutual_first_argument { int unused; };
extern void mutual_first__no_match_for_argument_type(struct tmplgen_no_association_for_mutual_first_argument, ...);
#ifndef TMPLGEN_GENERIC_mutual_first_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_mutual_first_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_mutual_first_TEST_TYPES_H
#define TMPLGEN_GNU_mutual_first_TEST_TYPES_H(next, ...) next
#endif
#undef mutual_first
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline T mutual_first(T value, unsigned int count)
#line 37 "test/tmpl/math.tmpl.h"
#define mutual_first(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_mutual_first_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: mutual_first__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline T mutual_first(T value, unsigned int count)
#line 37 "test/tmpl/math.tmpl.h"
#define mutual_first(...) \
    TMPLGEN_GNU_mutual_first_TEST_TYPES_H(mutual_first__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 55 "test/tmpl/math.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_reorder_call_argument { int unused; };
extern void reorder_call__no_match_for_argument_type(struct tmplgen_no_association_for_reorder_call_argument, ...);
#ifndef TMPLGEN_GENERIC_reorder_call_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_reorder_call_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_reorder_call_TEST_TYPES_H
#define TMPLGEN_GNU_reorder_call_TEST_TYPES_H(next, ...) next
#endif
#undef reorder_call
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline void reorder_call(T first, U second)
#line 55 "test/tmpl/math.tmpl.h"
#define reorder_call(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_reorder_call_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: reorder_call__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline void reorder_call(T first, U second)
#line 55 "test/tmpl/math.tmpl.h"
#define reorder_call(...) \
    TMPLGEN_GNU_reorder_call_TEST_TYPES_H(reorder_call__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 60 "test/tmpl/math.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_cross_root_argument { int unused; };
extern void cross_root__no_match_for_argument_type(struct tmplgen_no_association_for_cross_root_argument, ...);
#ifndef TMPLGEN_GENERIC_cross_root_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_cross_root_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_cross_root_TEST_TYPES_H
#define TMPLGEN_GNU_cross_root_TEST_TYPES_H(next, ...) next
#endif
#undef cross_root
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline T cross_root(T value)
#line 60 "test/tmpl/math.tmpl.h"
#define cross_root(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_cross_root_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: cross_root__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline T cross_root(T value)
#line 60 "test/tmpl/math.tmpl.h"
#define cross_root(...) \
    TMPLGEN_GNU_cross_root_TEST_TYPES_H(cross_root__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 83 "test/tmpl/result.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_result_is_ok_argument { int unused; };
extern void result_is_ok__no_match_for_argument_type(struct tmplgen_no_association_for_result_is_ok_argument, ...);
#ifndef TMPLGEN_GENERIC_result_is_ok_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_result_is_ok_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_result_is_ok_TEST_TYPES_H
#define TMPLGEN_GNU_result_is_ok_TEST_TYPES_H(next, ...) next
#endif
#undef result_is_ok
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline int result_is_ok(Result(T, E) * result)
#line 83 "test/tmpl/result.tmpl.h"
#define result_is_ok(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_result_is_ok_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: result_is_ok__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline int result_is_ok(Result(T, E) * result)
#line 83 "test/tmpl/result.tmpl.h"
#define result_is_ok(...) \
    TMPLGEN_GNU_result_is_ok_TEST_TYPES_H(result_is_ok__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 88 "test/tmpl/result.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_result_is_error_argument { int unused; };
extern void result_is_error__no_match_for_argument_type(struct tmplgen_no_association_for_result_is_error_argument, ...);
#ifndef TMPLGEN_GENERIC_result_is_error_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_result_is_error_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_result_is_error_TEST_TYPES_H
#define TMPLGEN_GNU_result_is_error_TEST_TYPES_H(next, ...) next
#endif
#undef result_is_error
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline int result_is_error(Result(T, E) * result)
#line 88 "test/tmpl/result.tmpl.h"
#define result_is_error(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_result_is_error_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: result_is_error__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline int result_is_error(Result(T, E) * result)
#line 88 "test/tmpl/result.tmpl.h"
#define result_is_error(...) \
    TMPLGEN_GNU_result_is_error_TEST_TYPES_H(result_is_error__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 93 "test/tmpl/result.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_result_value_argument { int unused; };
extern void result_value__no_match_for_argument_type(struct tmplgen_no_association_for_result_value_argument, ...);
#ifndef TMPLGEN_GENERIC_result_value_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_result_value_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_result_value_TEST_TYPES_H
#define TMPLGEN_GNU_result_value_TEST_TYPES_H(next, ...) next
#endif
#undef result_value
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline T result_value(Result(T, E) * result)
#line 93 "test/tmpl/result.tmpl.h"
#define result_value(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_result_value_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: result_value__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline T result_value(Result(T, E) * result)
#line 93 "test/tmpl/result.tmpl.h"
#define result_value(...) \
    TMPLGEN_GNU_result_value_TEST_TYPES_H(result_value__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 99 "test/tmpl/result.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_result_error_argument { int unused; };
extern void result_error__no_match_for_argument_type(struct tmplgen_no_association_for_result_error_argument, ...);
#ifndef TMPLGEN_GENERIC_result_error_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_result_error_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_result_error_TEST_TYPES_H
#define TMPLGEN_GNU_result_error_TEST_TYPES_H(next, ...) next
#endif
#undef result_error
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline E result_error(Result(T, E) * result)
#line 99 "test/tmpl/result.tmpl.h"
#define result_error(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_result_error_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: result_error__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline E result_error(Result(T, E) * result)
#line 99 "test/tmpl/result.tmpl.h"
#define result_error(...) \
    TMPLGEN_GNU_result_error_TEST_TYPES_H(result_error__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 105 "test/tmpl/result.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_result_identity_argument { int unused; };
extern void result_identity__no_match_for_argument_type(struct tmplgen_no_association_for_result_identity_argument, ...);
#ifndef TMPLGEN_GENERIC_result_identity_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_result_identity_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_result_identity_TEST_TYPES_H
#define TMPLGEN_GNU_result_identity_TEST_TYPES_H(next, ...) next
#endif
#undef result_identity
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline T result_identity(T value)
#line 105 "test/tmpl/result.tmpl.h"
#define result_identity(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_result_identity_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: result_identity__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline T result_identity(T value)
#line 105 "test/tmpl/result.tmpl.h"
#define result_identity(...) \
    TMPLGEN_GNU_result_identity_TEST_TYPES_H(result_identity__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 108 "test/tmpl/result.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_physics_sum_argument { int unused; };
extern void physics_sum__no_match_for_argument_type(struct tmplgen_no_association_for_physics_sum_argument, ...);
#ifndef TMPLGEN_GENERIC_physics_sum_N0_TMPLGEN_PHYSICS_H
#define TMPLGEN_GENERIC_physics_sum_N0_TMPLGEN_PHYSICS_H(...)
#endif
#ifndef TMPLGEN_GNU_physics_sum_TMPLGEN_PHYSICS_H
#define TMPLGEN_GNU_physics_sum_TMPLGEN_PHYSICS_H(next, ...) next
#endif
#undef physics_sum
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline int physics_sum(TP * value)
#line 108 "test/tmpl/result.tmpl.h"
#define physics_sum(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_physics_sum_N0_TMPLGEN_PHYSICS_H(__VA_ARGS__) \
    default: physics_sum__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline int physics_sum(TP * value)
#line 108 "test/tmpl/result.tmpl.h"
#define physics_sum(...) \
    TMPLGEN_GNU_physics_sum_TMPLGEN_PHYSICS_H(physics_sum__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 111 "test/tmpl/result.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_named_actor_id_argument { int unused; };
extern void named_actor_id__no_match_for_argument_type(struct tmplgen_no_association_for_named_actor_id_argument, ...);
#ifndef TMPLGEN_GENERIC_named_actor_id_N0_TMPLGEN_PHYSICS_H
#define TMPLGEN_GENERIC_named_actor_id_N0_TMPLGEN_PHYSICS_H(...)
#endif
#ifndef TMPLGEN_GNU_named_actor_id_TMPLGEN_PHYSICS_H
#define TMPLGEN_GNU_named_actor_id_TMPLGEN_PHYSICS_H(next, ...) next
#endif
#undef named_actor_id
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline int named_actor_id(UE * value)
#line 111 "test/tmpl/result.tmpl.h"
#define named_actor_id(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_named_actor_id_N0_TMPLGEN_PHYSICS_H(__VA_ARGS__) \
    default: named_actor_id__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline int named_actor_id(UE * value)
#line 111 "test/tmpl/result.tmpl.h"
#define named_actor_id(...) \
    TMPLGEN_GNU_named_actor_id_TMPLGEN_PHYSICS_H(named_actor_id__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 113 "test/tmpl/result.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_structural_pair_argument { int unused; };
extern void structural_pair__no_match_for_argument_type(struct tmplgen_no_association_for_structural_pair_argument, ...);
#ifndef TMPLGEN_GENERIC_structural_pair_N0_TMPLGEN_PHYSICS_H
#define TMPLGEN_GENERIC_structural_pair_N0_TMPLGEN_PHYSICS_H(...)
#endif
#ifndef TMPLGEN_GNU_structural_pair_TMPLGEN_PHYSICS_H
#define TMPLGEN_GNU_structural_pair_TMPLGEN_PHYSICS_H(next, ...) next
#endif
#undef structural_pair
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline int structural_pair(TP * physics, UE * entity)
#line 113 "test/tmpl/result.tmpl.h"
#define structural_pair(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_structural_pair_N0_TMPLGEN_PHYSICS_H(__VA_ARGS__) \
    default: structural_pair__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline int structural_pair(TP * physics, UE * entity)
#line 113 "test/tmpl/result.tmpl.h"
#define structural_pair(...) \
    TMPLGEN_GNU_structural_pair_TMPLGEN_PHYSICS_H(structural_pair__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 119 "test/tmpl/result.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_same_size_argument { int unused; };
extern void same_size__no_match_for_argument_type(struct tmplgen_no_association_for_same_size_argument, ...);
#ifndef TMPLGEN_GENERIC_same_size_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_same_size_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_same_size_TEST_TYPES_H
#define TMPLGEN_GNU_same_size_TEST_TYPES_H(next, ...) next
#endif
#undef same_size
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline int same_size(T left, U right)
#line 119 "test/tmpl/result.tmpl.h"
#define same_size(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_same_size_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: same_size__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline int same_size(T left, U right)
#line 119 "test/tmpl/result.tmpl.h"
#define same_size(...) \
    TMPLGEN_GNU_same_size_TEST_TYPES_H(same_size__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 125 "test/tmpl/result.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_alignment_ok_argument { int unused; };
extern void alignment_ok__no_match_for_argument_type(struct tmplgen_no_association_for_alignment_ok_argument, ...);
#ifndef TMPLGEN_GENERIC_alignment_ok_N0_TMPLGEN_PHYSICS_H
#define TMPLGEN_GENERIC_alignment_ok_N0_TMPLGEN_PHYSICS_H(...)
#endif
#ifndef TMPLGEN_GNU_alignment_ok_TMPLGEN_PHYSICS_H
#define TMPLGEN_GNU_alignment_ok_TMPLGEN_PHYSICS_H(next, ...) next
#endif
#undef alignment_ok
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline int alignment_ok(AI * value)
#line 125 "test/tmpl/result.tmpl.h"
#define alignment_ok(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_alignment_ok_N0_TMPLGEN_PHYSICS_H(__VA_ARGS__) \
    default: alignment_ok__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline int alignment_ok(AI * value)
#line 125 "test/tmpl/result.tmpl.h"
#define alignment_ok(...) \
    TMPLGEN_GNU_alignment_ok_TMPLGEN_PHYSICS_H(alignment_ok__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 133 "test/tmpl/result.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_result_pointer_argument { int unused; };
extern void result_pointer__no_match_for_argument_type(struct tmplgen_no_association_for_result_pointer_argument, ...);
#ifndef TMPLGEN_GENERIC_result_pointer_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_result_pointer_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_result_pointer_TEST_TYPES_H
#define TMPLGEN_GNU_result_pointer_TEST_TYPES_H(next, ...) next
#endif
#undef result_pointer
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline T result_pointer(T * value)
#line 133 "test/tmpl/result.tmpl.h"
#define result_pointer(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_result_pointer_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: result_pointer__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline T result_pointer(T * value)
#line 133 "test/tmpl/result.tmpl.h"
#define result_pointer(...) \
    TMPLGEN_GNU_result_pointer_TEST_TYPES_H(result_pointer__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 135 "test/tmpl/result.tmpl.h"
/// static inline Option_double option_some_double(double value)
static inline Option_double option_some_double(double value);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_option_some_argument { int unused; };
extern void option_some__no_match_for_argument_type(struct tmplgen_no_association_for_option_some_argument, ...);
#ifndef TMPLGEN_GENERIC_option_some_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_option_some_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_option_some_TEST_TYPES_H
#define TMPLGEN_GNU_option_some_TEST_TYPES_H(next, ...) next
#endif
#undef option_some
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline Option(T) option_some(T value)
#line 135 "test/tmpl/result.tmpl.h"
#define option_some(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    double: option_some_double, \
    TMPLGEN_GENERIC_option_some_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: option_some__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline Option(T) option_some(T value)
#line 135 "test/tmpl/result.tmpl.h"
#define option_some(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), double)), \
        option_some_double, \
        TMPLGEN_GNU_option_some_TEST_TYPES_H(option_some__no_match_for_argument_type, __VA_ARGS__))(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 143 "test/tmpl/result.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_option_none_argument { int unused; };
extern void option_none__no_match_for_argument_type(struct tmplgen_no_association_for_option_none_argument, ...);
#ifndef TMPLGEN_GENERIC_option_none_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_option_none_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_option_none_TEST_TYPES_H
#define TMPLGEN_GNU_option_none_TEST_TYPES_H(next, ...) next
#endif
#undef option_none
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline void option_none(Option(T) * option)
#line 143 "test/tmpl/result.tmpl.h"
#define option_none(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_option_none_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: option_none__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline void option_none(Option(T) * option)
#line 143 "test/tmpl/result.tmpl.h"
#define option_none(...) \
    TMPLGEN_GNU_option_none_TEST_TYPES_H(option_none__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 148 "test/tmpl/result.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_option_is_some_argument { int unused; };
extern void option_is_some__no_match_for_argument_type(struct tmplgen_no_association_for_option_is_some_argument, ...);
#ifndef TMPLGEN_GENERIC_option_is_some_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_option_is_some_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_option_is_some_TEST_TYPES_H
#define TMPLGEN_GNU_option_is_some_TEST_TYPES_H(next, ...) next
#endif
#undef option_is_some
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline int option_is_some(Option(T) * option)
#line 148 "test/tmpl/result.tmpl.h"
#define option_is_some(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_option_is_some_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: option_is_some__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline int option_is_some(Option(T) * option)
#line 148 "test/tmpl/result.tmpl.h"
#define option_is_some(...) \
    TMPLGEN_GNU_option_is_some_TEST_TYPES_H(option_is_some__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 153 "test/tmpl/result.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_option_value_argument { int unused; };
extern void option_value__no_match_for_argument_type(struct tmplgen_no_association_for_option_value_argument, ...);
#ifndef TMPLGEN_GENERIC_option_value_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_option_value_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_option_value_TEST_TYPES_H
#define TMPLGEN_GNU_option_value_TEST_TYPES_H(next, ...) next
#endif
#undef option_value
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline T option_value(Option(T) * option)
#line 153 "test/tmpl/result.tmpl.h"
#define option_value(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_option_value_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: option_value__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline T option_value(Option(T) * option)
#line 153 "test/tmpl/result.tmpl.h"
#define option_value(...) \
    TMPLGEN_GNU_option_value_TEST_TYPES_H(option_value__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 159 "test/tmpl/result.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_tagged_box_value_argument { int unused; };
extern void tagged_box_value__no_match_for_argument_type(struct tmplgen_no_association_for_tagged_box_value_argument, ...);
#ifndef TMPLGEN_GENERIC_tagged_box_value_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_tagged_box_value_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_tagged_box_value_TEST_TYPES_H
#define TMPLGEN_GNU_tagged_box_value_TEST_TYPES_H(next, ...) next
#endif
#undef tagged_box_value
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline T tagged_box_value( TaggedBox(T) * box)
#line 159 "test/tmpl/result.tmpl.h"
#define tagged_box_value(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_tagged_box_value_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: tagged_box_value__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline T tagged_box_value( TaggedBox(T) * box)
#line 159 "test/tmpl/result.tmpl.h"
#define tagged_box_value(...) \
    TMPLGEN_GNU_tagged_box_value_TEST_TYPES_H(tagged_box_value__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 161 "test/tmpl/result.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_tagged_choice_value_argument { int unused; };
extern void tagged_choice_value__no_match_for_argument_type(struct tmplgen_no_association_for_tagged_choice_value_argument, ...);
#ifndef TMPLGEN_GENERIC_tagged_choice_value_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_tagged_choice_value_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_tagged_choice_value_TEST_TYPES_H
#define TMPLGEN_GNU_tagged_choice_value_TEST_TYPES_H(next, ...) next
#endif
#undef tagged_choice_value
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline T tagged_choice_value( TaggedChoice(T) * choice)
#line 161 "test/tmpl/result.tmpl.h"
#define tagged_choice_value(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_tagged_choice_value_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: tagged_choice_value__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline T tagged_choice_value( TaggedChoice(T) * choice)
#line 161 "test/tmpl/result.tmpl.h"
#define tagged_choice_value(...) \
    TMPLGEN_GNU_tagged_choice_value_TEST_TYPES_H(tagged_choice_value__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 163 "test/tmpl/result.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_option_pair_argument { int unused; };
extern void option_pair__no_match_for_argument_type(struct tmplgen_no_association_for_option_pair_argument, ...);
#ifndef TMPLGEN_GENERIC_option_pair_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_option_pair_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_option_pair_TEST_TYPES_H
#define TMPLGEN_GNU_option_pair_TEST_TYPES_H(next, ...) next
#endif
#undef option_pair
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline T option_pair(Option(T) * left, Option(T) * right)
#line 163 "test/tmpl/result.tmpl.h"
#define option_pair(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_option_pair_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: option_pair__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline T option_pair(Option(T) * left, Option(T) * right)
#line 163 "test/tmpl/result.tmpl.h"
#define option_pair(...) \
    TMPLGEN_GNU_option_pair_TEST_TYPES_H(option_pair__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 170 "test/tmpl/result.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_box_select_argument { int unused; };
extern void box_select__no_match_for_argument_type(struct tmplgen_no_association_for_box_select_argument, ...);
#ifndef TMPLGEN_GENERIC_box_select_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_box_select_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_box_select_TEST_TYPES_H
#define TMPLGEN_GNU_box_select_TEST_TYPES_H(next, ...) next
#endif
#undef box_select
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline Box(U) box_select(Box(T) * first, Box(U) * second)
#line 170 "test/tmpl/result.tmpl.h"
#define box_select(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_box_select_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: box_select__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline Box(U) box_select(Box(T) * first, Box(U) * second)
#line 170 "test/tmpl/result.tmpl.h"
#define box_select(...) \
    TMPLGEN_GNU_box_select_TEST_TYPES_H(box_select__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 176 "test/tmpl/result.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_box_make_argument { int unused; };
extern void box_make__no_match_for_argument_type(struct tmplgen_no_association_for_box_make_argument, ...);
#ifndef TMPLGEN_GENERIC_box_make_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_box_make_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_box_make_TEST_TYPES_H
#define TMPLGEN_GNU_box_make_TEST_TYPES_H(next, ...) next
#endif
#undef box_make
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline Box(T) box_make(T value)
#line 176 "test/tmpl/result.tmpl.h"
#define box_make(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_box_make_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: box_make__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline Box(T) box_make(T value)
#line 176 "test/tmpl/result.tmpl.h"
#define box_make(...) \
    TMPLGEN_GNU_box_make_TEST_TYPES_H(box_make__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 182 "test/tmpl/result.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_box_get_argument { int unused; };
extern void box_get__no_match_for_argument_type(struct tmplgen_no_association_for_box_get_argument, ...);
#ifndef TMPLGEN_GENERIC_box_get_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_box_get_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_box_get_TEST_TYPES_H
#define TMPLGEN_GNU_box_get_TEST_TYPES_H(next, ...) next
#endif
#undef box_get
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline T box_get(Box(T) * box)
#line 182 "test/tmpl/result.tmpl.h"
#define box_get(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_box_get_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: box_get__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline T box_get(Box(T) * box)
#line 182 "test/tmpl/result.tmpl.h"
#define box_get(...) \
    TMPLGEN_GNU_box_get_TEST_TYPES_H(box_get__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 187 "test/tmpl/result.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_pair_make_argument { int unused; };
extern void pair_make__no_match_for_argument_type(struct tmplgen_no_association_for_pair_make_argument, ...);
#ifndef TMPLGEN_GENERIC_pair_make_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_pair_make_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_pair_make_TEST_TYPES_H
#define TMPLGEN_GNU_pair_make_TEST_TYPES_H(next, ...) next
#endif
#undef pair_make
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline Pair(T, U) pair_make(T first, U second)
#line 187 "test/tmpl/result.tmpl.h"
#define pair_make(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_pair_make_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: pair_make__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline Pair(T, U) pair_make(T first, U second)
#line 187 "test/tmpl/result.tmpl.h"
#define pair_make(...) \
    TMPLGEN_GNU_pair_make_TEST_TYPES_H(pair_make__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 193 "test/tmpl/result.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_pair_flip_argument { int unused; };
extern void pair_flip__no_match_for_argument_type(struct tmplgen_no_association_for_pair_flip_argument, ...);
#ifndef TMPLGEN_GENERIC_pair_flip_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_pair_flip_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_pair_flip_TEST_TYPES_H
#define TMPLGEN_GNU_pair_flip_TEST_TYPES_H(next, ...) next
#endif
#undef pair_flip
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline Pair(U, T) pair_flip(Pair(T, U) pair)
#line 193 "test/tmpl/result.tmpl.h"
#define pair_flip(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_pair_flip_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: pair_flip__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline Pair(U, T) pair_flip(Pair(T, U) pair)
#line 193 "test/tmpl/result.tmpl.h"
#define pair_flip(...) \
    TMPLGEN_GNU_pair_flip_TEST_TYPES_H(pair_flip__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 205 "test/tmpl/result.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_result_make_argument { int unused; };
extern void result_make__no_match_for_argument_type(struct tmplgen_no_association_for_result_make_argument, ...);
#ifndef TMPLGEN_GENERIC_result_make_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_result_make_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_result_make_TEST_TYPES_H
#define TMPLGEN_GNU_result_make_TEST_TYPES_H(next, ...) next
#endif
#undef result_make
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline Result(T, E) result_make(ResultTag tag, T value, E error)
#line 205 "test/tmpl/result.tmpl.h"
#define result_make(...) _Generic((GEN_ARG1(__VA_ARGS__)), \
    TMPLGEN_GENERIC_result_make_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: result_make__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline Result(T, E) result_make(ResultTag tag, T value, E error)
#line 205 "test/tmpl/result.tmpl.h"
#define result_make(...) \
    TMPLGEN_GNU_result_make_TEST_TYPES_H(result_make__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 16 "test/tmpl/triple.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_triple_make_argument { int unused; };
extern void triple_make__no_match_for_argument_type(struct tmplgen_no_association_for_triple_make_argument, ...);
#ifndef TMPLGEN_GENERIC_triple_make_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_triple_make_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_triple_make_TEST_TYPES_H
#define TMPLGEN_GNU_triple_make_TEST_TYPES_H(next, ...) next
#endif
#undef triple_make
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline Triple(A, B, C) triple_make(A first, B second, C third)
#line 16 "test/tmpl/triple.tmpl.h"
#define triple_make(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_triple_make_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: triple_make__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline Triple(A, B, C) triple_make(A first, B second, C third)
#line 16 "test/tmpl/triple.tmpl.h"
#define triple_make(...) \
    TMPLGEN_GNU_triple_make_TEST_TYPES_H(triple_make__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 23 "test/tmpl/triple.tmpl.h"
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_triple_first_argument { int unused; };
extern void triple_first__no_match_for_argument_type(struct tmplgen_no_association_for_triple_first_argument, ...);
#ifndef TMPLGEN_GENERIC_triple_first_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_triple_first_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_triple_first_TEST_TYPES_H
#define TMPLGEN_GNU_triple_first_TEST_TYPES_H(next, ...) next
#endif
#undef triple_first
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline A triple_first(Triple(A, B, C) * value)
#line 23 "test/tmpl/triple.tmpl.h"
#define triple_first(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    TMPLGEN_GENERIC_triple_first_N0_TEST_TYPES_H(__VA_ARGS__) \
    default: triple_first__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline A triple_first(Triple(A, B, C) * value)
#line 23 "test/tmpl/triple.tmpl.h"
#define triple_first(...) \
    TMPLGEN_GNU_triple_first_TEST_TYPES_H(triple_first__no_match_for_argument_type, __VA_ARGS__)(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif

#line 16 "test/tmpl/vec.tmpl.h"
/// static inline void push_int(Vec_int * v, int x)
static inline void push_int(Vec_int * v, int x);
/* No dispatch association yet: either the argument type is wrong,
   or generated headers need regeneration after requesting this type. */
struct tmplgen_no_association_for_push_argument { int unused; };
extern void push__no_match_for_argument_type(struct tmplgen_no_association_for_push_argument, ...);
#ifndef TMPLGEN_GENERIC_push_N0_TEST_TYPES_H
#define TMPLGEN_GENERIC_push_N0_TEST_TYPES_H(...)
#endif
#ifndef TMPLGEN_GENERIC_push_N0_REFLECT_TYPES_H
#define TMPLGEN_GENERIC_push_N0_REFLECT_TYPES_H(...)
#endif
#ifndef TMPLGEN_GNU_push_REFLECT_TYPES_H
#define TMPLGEN_GNU_push_REFLECT_TYPES_H(next, ...) next
#endif
#ifndef TMPLGEN_GNU_push_TEST_TYPES_H
#define TMPLGEN_GNU_push_TEST_TYPES_H(next, ...) next
#endif
#undef push
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
/// static inline void push(Vec(T) * v, T x)
#line 16 "test/tmpl/vec.tmpl.h"
#define push(...) _Generic((GEN_ARG0(__VA_ARGS__)), \
    Vec_int *: push_int, \
    TMPLGEN_GENERIC_push_N0_TEST_TYPES_H(__VA_ARGS__) \
    TMPLGEN_GENERIC_push_N0_REFLECT_TYPES_H(__VA_ARGS__) \
    default: push__no_match_for_argument_type)(__VA_ARGS__)

#elif defined(__GNUC__) || defined(__clang__)
/// static inline void push(Vec(T) * v, T x)
#line 16 "test/tmpl/vec.tmpl.h"
#define push(...) \
    __builtin_choose_expr( \
        (__builtin_types_compatible_p(__typeof__(GEN_ARG0(__VA_ARGS__)), Vec_int *)), \
        push_int, \
        TMPLGEN_GNU_push_REFLECT_TYPES_H(TMPLGEN_GNU_push_TEST_TYPES_H(push__no_match_for_argument_type, __VA_ARGS__), __VA_ARGS__))(__VA_ARGS__)

#else
#error "GEN_DISPATCH_AUTO requires C11 or GNU-compatible C"
#endif


#endif /* TMPL_H */
#if defined(REFLECT_TYPES_H) && !defined(TMPLGEN_HAVE_TMPL_H_REFLECT_TYPES_H)
#define TMPLGEN_HAVE_TMPL_H_REFLECT_TYPES_H
#line 16 "test/tmpl/vec.tmpl.h"
/// static inline void push_Item(Vec_Item * v, Item x)
static inline void push_Item(Vec_Item * v, Item x);
#line 1113 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#endif
#if defined(TEST_TYPES_H) && !defined(TMPLGEN_HAVE_TMPL_H_TEST_TYPES_H)
#define TMPLGEN_HAVE_TMPL_H_TEST_TYPES_H
#line 12 "test/tmpl/math.tmpl.h"
/// static inline TestInt max_TestInt(TestInt a, TestInt b)
static inline TestInt max_TestInt(TestInt a, TestInt b);
#line 1120 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 27 "test/tmpl/math.tmpl.h"
/// static inline TestInt chain_root_TestInt(TestInt value)
static inline TestInt chain_root_TestInt(TestInt value);
#line 1124 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 29 "test/tmpl/math.tmpl.h"
/// static inline TestInt recursive_value_TestInt(TestInt value, unsigned int count)
static inline TestInt recursive_value_TestInt(TestInt value, unsigned int count);
#line 1128 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 37 "test/tmpl/math.tmpl.h"
/// static inline TestInt mutual_first_TestInt(TestInt value, unsigned int count)
static inline TestInt mutual_first_TestInt(TestInt value, unsigned int count);
#line 1132 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 55 "test/tmpl/math.tmpl.h"
/// static inline void reorder_call_TestInt_double(TestInt first, double second)
static inline void reorder_call_TestInt_double(TestInt first, double second);
#line 1136 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 60 "test/tmpl/math.tmpl.h"
/// static inline TestInt cross_root_TestInt(TestInt value)
static inline TestInt cross_root_TestInt(TestInt value);
#line 1140 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 83 "test/tmpl/result.tmpl.h"
/// static inline int result_is_ok_TestInt_ErrorCode(Result_TestInt_ErrorCode * result)
static inline int result_is_ok_TestInt_ErrorCode(Result_TestInt_ErrorCode * result);
#line 1144 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 88 "test/tmpl/result.tmpl.h"
/// static inline int result_is_error_TestInt_ErrorCode(Result_TestInt_ErrorCode * result)
static inline int result_is_error_TestInt_ErrorCode(Result_TestInt_ErrorCode * result);
#line 1148 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 93 "test/tmpl/result.tmpl.h"
/// static inline TestInt result_value_TestInt_ErrorCode(Result_TestInt_ErrorCode * result)
static inline TestInt result_value_TestInt_ErrorCode(Result_TestInt_ErrorCode * result);
/// static inline Tag result_value_Tag_InferredError(Result_Tag_InferredError * result)
static inline Tag result_value_Tag_InferredError(Result_Tag_InferredError * result);
/// static inline InferredLong result_value_InferredLong_InferredError(Result_InferredLong_InferredError * result)
static inline InferredLong result_value_InferredLong_InferredError(Result_InferredLong_InferredError * result);
#line 1156 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 99 "test/tmpl/result.tmpl.h"
/// static inline ErrorCode result_error_TestInt_ErrorCode(Result_TestInt_ErrorCode * result)
static inline ErrorCode result_error_TestInt_ErrorCode(Result_TestInt_ErrorCode * result);
/// static inline ErrorCode result_error_InferredDouble_ErrorCode(Result_InferredDouble_ErrorCode * result)
static inline ErrorCode result_error_InferredDouble_ErrorCode(Result_InferredDouble_ErrorCode * result);
#line 1162 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 105 "test/tmpl/result.tmpl.h"
/// static inline TestInt result_identity_TestInt(TestInt value)
static inline TestInt result_identity_TestInt(TestInt value);
#line 1166 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 119 "test/tmpl/result.tmpl.h"
/// static inline int same_size_TestInt_TestInt(TestInt left, TestInt right)
static inline int same_size_TestInt_TestInt(TestInt left, TestInt right);
#line 1170 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 133 "test/tmpl/result.tmpl.h"
/// static inline TestInt result_pointer_TestInt(TestInt * value)
static inline TestInt result_pointer_TestInt(TestInt * value);
#line 1174 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 135 "test/tmpl/result.tmpl.h"
/// static inline Option_TestInt option_some_TestInt(TestInt value)
static inline Option_TestInt option_some_TestInt(TestInt value);
#line 1178 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 143 "test/tmpl/result.tmpl.h"
/// static inline void option_none_TestInt(Option_TestInt * option)
static inline void option_none_TestInt(Option_TestInt * option);
#line 1182 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 148 "test/tmpl/result.tmpl.h"
/// static inline int option_is_some_TestInt(Option_TestInt * option)
static inline int option_is_some_TestInt(Option_TestInt * option);
#line 1186 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 153 "test/tmpl/result.tmpl.h"
/// static inline TestInt option_value_TestInt(Option_TestInt * option)
static inline TestInt option_value_TestInt(Option_TestInt * option);
#line 1190 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 159 "test/tmpl/result.tmpl.h"
/// static inline TestInt tagged_box_value_TestInt(struct TaggedBox_TestInt * box)
static inline TestInt tagged_box_value_TestInt(struct TaggedBox_TestInt * box);
#line 1194 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 161 "test/tmpl/result.tmpl.h"
/// static inline TestInt tagged_choice_value_TestInt(union TaggedChoice_TestInt * choice)
static inline TestInt tagged_choice_value_TestInt(union TaggedChoice_TestInt * choice);
#line 1198 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 163 "test/tmpl/result.tmpl.h"
/// static inline TestInt option_pair_TestInt(Option_TestInt * left, Option_TestInt * right)
static inline TestInt option_pair_TestInt(Option_TestInt * left, Option_TestInt * right);
#line 1202 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 170 "test/tmpl/result.tmpl.h"
/// static inline Box_double box_select_TestInt_double(Box_TestInt * first, Box_double * second)
static inline Box_double box_select_TestInt_double(Box_TestInt * first, Box_double * second);
#line 1206 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 176 "test/tmpl/result.tmpl.h"
/// static inline Box_Tag box_make_Tag(Tag value)
static inline Box_Tag box_make_Tag(Tag value);
#line 1210 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 182 "test/tmpl/result.tmpl.h"
/// static inline Tag box_get_Tag(Box_Tag * box)
static inline Tag box_get_Tag(Box_Tag * box);
#line 1214 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 187 "test/tmpl/result.tmpl.h"
/// static inline Pair_Tag_double pair_make_Tag_double(Tag first, double second)
static inline Pair_Tag_double pair_make_Tag_double(Tag first, double second);
#line 1218 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 193 "test/tmpl/result.tmpl.h"
/// static inline Pair_double_Tag pair_flip_Tag_double(Pair_Tag_double pair)
static inline Pair_double_Tag pair_flip_Tag_double(Pair_Tag_double pair);
#line 1222 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 205 "test/tmpl/result.tmpl.h"
/// static inline Result_TestInt_ErrorCode result_make_TestInt_ErrorCode(ResultTag tag, TestInt value, ErrorCode error)
static inline Result_TestInt_ErrorCode result_make_TestInt_ErrorCode(ResultTag tag, TestInt value, ErrorCode error);
/// static inline Result_Tag_InferredError result_make_Tag_InferredError(ResultTag tag, Tag value, InferredError error)
static inline Result_Tag_InferredError result_make_Tag_InferredError(ResultTag tag, Tag value, InferredError error);
/// static inline Result_InferredLong_InferredError result_make_InferredLong_InferredError(ResultTag tag, InferredLong value, InferredError error)
static inline Result_InferredLong_InferredError result_make_InferredLong_InferredError(ResultTag tag, InferredLong value, InferredError error);
/// static inline Result_InferredDouble_ErrorCode result_make_InferredDouble_ErrorCode(ResultTag tag, InferredDouble value, ErrorCode error)
static inline Result_InferredDouble_ErrorCode result_make_InferredDouble_ErrorCode(ResultTag tag, InferredDouble value, ErrorCode error);
/// static inline Result_Dog_ErrorCode result_make_Dog_ErrorCode(ResultTag tag, Dog value, ErrorCode error)
static inline Result_Dog_ErrorCode result_make_Dog_ErrorCode(ResultTag tag, Dog value, ErrorCode error);
/// static inline Result_Dog_int result_make_Dog_int(ResultTag tag, Dog value, int error)
static inline Result_Dog_int result_make_Dog_int(ResultTag tag, Dog value, int error);
#line 1236 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 16 "test/tmpl/triple.tmpl.h"
/// static inline Triple_TestInt_ErrorCode_Tag triple_make_TestInt_ErrorCode_Tag(TestInt first, ErrorCode second, Tag third)
static inline Triple_TestInt_ErrorCode_Tag triple_make_TestInt_ErrorCode_Tag(TestInt first, ErrorCode second, Tag third);
/// static inline Triple_double_ErrorCode_Tag triple_make_double_ErrorCode_Tag(double first, ErrorCode second, Tag third)
static inline Triple_double_ErrorCode_Tag triple_make_double_ErrorCode_Tag(double first, ErrorCode second, Tag third);
/// static inline Triple_Tag_InferredError_InferredLong triple_make_Tag_InferredError_InferredLong(Tag first, InferredError second, InferredLong third)
static inline Triple_Tag_InferredError_InferredLong triple_make_Tag_InferredError_InferredLong(Tag first, InferredError second, InferredLong third);
#line 1244 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 23 "test/tmpl/triple.tmpl.h"
/// static inline TestInt triple_first_TestInt_ErrorCode_Tag(Triple_TestInt_ErrorCode_Tag * value)
static inline TestInt triple_first_TestInt_ErrorCode_Tag(Triple_TestInt_ErrorCode_Tag * value);
/// static inline Tag triple_first_Tag_InferredError_InferredLong(Triple_Tag_InferredError_InferredLong * value)
static inline Tag triple_first_Tag_InferredError_InferredLong(Triple_Tag_InferredError_InferredLong * value);
#line 1250 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 16 "test/tmpl/vec.tmpl.h"
/// static inline void push_TestInt(Vec_TestInt * v, TestInt x)
static inline void push_TestInt(Vec_TestInt * v, TestInt x);
#line 1254 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#endif
#if defined(TMPLGEN_PHYSICS_H) && !defined(TMPLGEN_HAVE_TMPL_H_TMPLGEN_PHYSICS_H)
#define TMPLGEN_HAVE_TMPL_H_TMPLGEN_PHYSICS_H
#line 108 "test/tmpl/result.tmpl.h"
/// static inline int physics_sum_PhysicsBody(PhysicsBody * value)
static inline int physics_sum_PhysicsBody(PhysicsBody * value);
#line 1261 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 111 "test/tmpl/result.tmpl.h"
/// static inline int named_actor_id_NamedActor(NamedActor * value)
static inline int named_actor_id_NamedActor(NamedActor * value);
#line 1265 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 113 "test/tmpl/result.tmpl.h"
/// static inline int structural_pair_PhysicsBody_NamedActor(PhysicsBody * physics, NamedActor * entity)
static inline int structural_pair_PhysicsBody_NamedActor(PhysicsBody * physics, NamedActor * entity);
#line 1269 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#line 125 "test/tmpl/result.tmpl.h"
/// static inline int alignment_ok_PhysicsBody(PhysicsBody * value)
static inline int alignment_ok_PhysicsBody(PhysicsBody * value);
#line 1273 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#endif
#if defined(TMPLGEN_REFLECTION_H) && !defined(TMPLGEN_HAVE_TMPL_H_TMPLGEN_REFLECTION_H)
#define TMPLGEN_HAVE_TMPL_H_TMPLGEN_REFLECTION_H
#line 18 "test/tmpl/inspect.tmpl.h"
/// static inline uint32_t inspect_int(int * value, FILE * out)
static inline uint32_t inspect_int(int * value, FILE * out);
#line 1280 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#endif
#if defined(POINT_H) && defined(TMPLGEN_REFLECT_HAVE_POINT_H) && !defined(TMPLGEN_HAVE_TMPL_H_POINT_H_AND_TMPLGEN_REFLECT_HAVE_POINT_H)
#define TMPLGEN_HAVE_TMPL_H_POINT_H_AND_TMPLGEN_REFLECT_HAVE_POINT_H
#line 18 "test/tmpl/inspect.tmpl.h"
/// static inline uint32_t inspect_Point(Point * value, FILE * out)
static inline uint32_t inspect_Point(Point * value, FILE * out);
#line 1287 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#endif
#if defined(REFLECT_TYPES_H) && defined(TMPLGEN_REFLECT_HAVE_REFLECT_TYPES_H) && !defined(TMPLGEN_HAVE_TMPL_H_REFLECT_TYPES_H_AND_TMPLGEN_REFLECT_HAVE_REFLECT_TYPES_H)
#define TMPLGEN_HAVE_TMPL_H_REFLECT_TYPES_H_AND_TMPLGEN_REFLECT_HAVE_REFLECT_TYPES_H
#line 18 "test/tmpl/inspect.tmpl.h"
/// static inline uint32_t inspect_Hero(Hero * value, FILE * out)
static inline uint32_t inspect_Hero(Hero * value, FILE * out);
/// static inline uint32_t inspect_Item(Item * value, FILE * out)
static inline uint32_t inspect_Item(Item * value, FILE * out);
/// static inline uint32_t inspect_Number(Number * value, FILE * out)
static inline uint32_t inspect_Number(Number * value, FILE * out);
#line 1298 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#endif
#if defined(REFLECT_TYPES_H) && defined(TMPLGEN_HAVE_TMPL_H_VEC_GEN_H_REFLECT_TYPES_H) && defined(TMPLGEN_REFLECT_HAVE_REFLECT_TYPES_H) && defined(TMPLGEN_REFLECT_HAVE_TMPLGEN_HAVE_TMPL_H_VEC_GEN_H_REFLECT_TYPES_H) && !defined(TMPLGEN_HAVE_TMPL_H_REFLECT_TYPES_H_AND_TMPLGEN_HAVE_TMPL_H_VEC_GEN_H_REFLECT_TYPES_H_AND_TMPLGEN_REFLECT_HAVE_REFLECT_TYPES_H_AND_TMPLGEN_REFLECT_HAVE_TMPLGEN_HAVE_TMPL_H_VEC_GEN_H_REFLECT_TYPES_H)
#define TMPLGEN_HAVE_TMPL_H_REFLECT_TYPES_H_AND_TMPLGEN_HAVE_TMPL_H_VEC_GEN_H_REFLECT_TYPES_H_AND_TMPLGEN_REFLECT_HAVE_REFLECT_TYPES_H_AND_TMPLGEN_REFLECT_HAVE_TMPLGEN_HAVE_TMPL_H_VEC_GEN_H_REFLECT_TYPES_H
#line 18 "test/tmpl/inspect.tmpl.h"
/// static inline uint32_t inspect_Vec_Item(Vec_Item * value, FILE * out)
static inline uint32_t inspect_Vec_Item(Vec_Item * value, FILE * out);
#line 1305 "/home/elias/Projects/c-tmplgen/test/gen/all.gen.h"
#endif
