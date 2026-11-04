#ifndef TMPLGEN_INTERNAL_H
#define TMPLGEN_INTERNAL_H

/* ===================== CONFIGURATION ===================== */

/* Dispatch backends for the generated macros (pick one for GEN_DISPATCH). */
#define GEN_DISPATCH_NONE    0  /* only emit suffixed functions: max_int, max_double */
#define GEN_DISPATCH_GENERIC 1  /* emit _Generic macros (C11 and newer)              */
#define GEN_DISPATCH_GNU     2  /* emit __builtin_choose_expr/__typeof__ chains      */
#define GEN_DISPATCH_AUTO    3  /* use _Generic in C11+, GNU builtins in GNU C       */

#define GEN_TMPL_DIR    "tmpl"       /* project templates, relative to the project root */
#define GEN_TMPL_SUFFIX ".tmpl.h"    /* appended to each template stem                */
#define GEN_SCAN_ROOT  "."           /* recursively scan this tree for C source usages */
#ifndef GEN_OUTPUT
#define GEN_OUTPUT      "gen/all.gen.h" /* aggregate header, relative to project root    */
#endif
#define GEN_GUARD       "TMPL_H"     /* include guard for the aggregate header           */
#define GEN_LSP_GUARD "TMPLGEN" /* #ifndef block stripped from generated bodies */
#ifndef GEN_DISPATCH
#define GEN_DISPATCH    GEN_DISPATCH_AUTO /* C11 and GNU modes share one LSP-friendly header */
#endif
#ifndef GEN_EMIT_LINE
#define GEN_EMIT_LINE   0            /* 1 maps definitions to templates; 0 keeps them in generated headers */
#endif
#ifndef GEN_ENABLE_WATCH
#define GEN_ENABLE_WATCH     1       /* 1: compile --watch support; 0: disable --watch */
#endif
#ifndef GEN_WATCH_INTERVAL_MS
#define GEN_WATCH_INTERVAL_MS 500    /* input fingerprint poll interval for --watch  */
#endif

/* Text pasted right after the include guard. Put system includes and your own
   type headers here. */
#define GEN_PREAMBLE "#include <stddef.h>\n#include <stdlib.h>\n"

/* =================== END OF CONFIGURATION ================== */

#if GEN_ENABLE_WATCH && !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 199309L
#endif
#if defined(CLANGPLUGIN) && !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE /* dladdr: the plugin's own library identifies the generator */
#endif

/* ========================= INCLUDES ======================== */

#include <ctype.h>
#include <clang-c/Index.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#include <direct.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(CLANGPLUGIN)
#include <dlfcn.h>
#endif
#endif

#if defined(CLANGPLUGIN)
#define TMPLGEN_PLUGIN_BUILD 1
#else
#define TMPLGEN_PLUGIN_BUILD 0
#endif


typedef struct { char *p; size_t len, cap, newlines; } Buf;
typedef struct { char *path; Buf contents; } GeneratedFile;
typedef struct { GeneratedFile *items; size_t count, cap; } GeneratedFileList;
/* Every file tmplgen writes. All code that must know the outputs (cache
 * manifest, bootstrap placeholders, parsing overrides, output recognition)
 * goes through output_registry_build, so a new output cannot be missed. */
typedef enum {
    OUTPUT_MODULE,            /* gen/<stem>.gen.h */
    OUTPUT_MODULE_SOURCE,     /* gen/<stem>.gen.c (--compiled) */
    OUTPUT_AGGREGATE,         /* gen/all.gen.h */
    OUTPUT_REFLECTION,        /* gen/reflection.h (--reflect) */
    OUTPUT_REFLECTION_SOURCE  /* gen/reflection.gen.c (--reflect --compiled) */
} OutputKind;
typedef struct { char *path; OutputKind kind; size_t template_index; } OutputEntry;
typedef struct { OutputEntry *items; size_t count, cap; } OutputRegistry;
typedef struct {
    char *path;
    unsigned long long hash1, hash2;
} CacheRecord;
typedef struct { CacheRecord *items; size_t count, cap; } CacheRecordList;
typedef struct {
    unsigned long long signature;
    CacheRecordList dependencies;
    CacheRecordList outputs;
} CacheManifest;
typedef struct { char *c_type; char *canonical; } TypeArgument;
typedef struct {
    char *name;
    char *key;
    TypeArgument *arguments;
    size_t *parameter_indices;
    size_t argument_count;
    size_t template_index;
    char *origin_path;
    size_t origin_line;
} GenInstance;
typedef struct { char *functor; char *type_name; } TypeUse;
typedef struct {
    char *name;
    char *type_name;
    int dispatch;
    char *origin_path;
    size_t origin_line;
    unsigned *dispatch_indices;
    char **dispatch_types;
    char **dispatch_canonicals;
    size_t dispatch_count;
} GenericUse;
typedef struct {
    char *path;
    size_t line;
    char *function_name;
    char *parameter_name;
    char *first_type;
    char *second_type;
} TypeVariableMismatch;
typedef struct { char *name; char *canonical; } TypeAlias;
/* Where a project type name is declared: `name` is a typedef name or a tag
 * spelled with its keyword ("struct Foo"), `path` the absolute declaring file
 * and `offset` the declaration's byte offset in it. */
typedef struct { char *name; char *path; unsigned offset; } TypeOrigin;
typedef struct {
    char *name;
    int is_union;
    char **fields;
    size_t field_count, field_cap;
} StructuralSchema;
typedef struct {
    char *name;
    char *path;
    unsigned offset;
    size_t line;
    unsigned argument_index;
    size_t argument_file_index;
    unsigned argument_start;
    unsigned argument_end;
    char *type_spelling;
    char *canonical_spelling;
} ClangUse;
typedef struct { ClangUse *items; size_t count, cap; } ClangUseList;
typedef struct { char **items; size_t count, cap; } StringSet;
typedef struct { StringSet *all; StringSet *local; } InclusionCapture;
typedef struct { char *stem; } GenTemplate;

typedef struct { char *text; size_t line; int kind; } CToken;
typedef struct { CToken *items; size_t count, cap; } TokenList;
typedef struct {
    size_t template_index;
    char *functor;
    size_t argument_count;
    size_t *parameter_indices;
    char **spellings;
    char **canonicals;
} SourceConstructorFact;
typedef struct {
    ClangUseList pass1_uses, pass2_uses;
    TypeAlias *aliases;
    size_t alias_count, alias_cap;
    SourceConstructorFact *constructors;
    size_t constructor_count, constructor_cap;
    StructuralSchema *schemas;
    size_t schema_count, schema_cap;
    TypeOrigin *origins;
    size_t origin_count, origin_cap;
    StringSet dependencies;
    unsigned long long pass1_key, pass2_key;
    int loaded, pass1_valid, pass2_valid;
} SourceFacts;
/* --reflect: REFLECT_NONE (default) or REFLECT_PROJECT. */
enum { REFLECT_NONE = 0, REFLECT_PROJECT = 1 };

/* ---- value parameters: integer constant argument handling ---------- */

typedef struct {
    const char *p;
    int failed;
    int unknown;
} IntegerConstant;

typedef struct {
    const char *text;
    int level;
} IntegerOperator;

/* ====================== DYNAMIC BUFFER ===================== */

enum { C_TOKEN_IDENT = 1, C_TOKEN_NUMBER, C_TOKEN_STRING, C_TOKEN_CHAR, C_TOKEN_PUNCT };

typedef struct {
    char *path;
    Buf text;
    CToken *tokens;
    size_t token_count, token_cap;
    SourceFacts facts;
} SourceFile;

typedef struct {
    SourceFile *items;
    size_t count, cap;
} SourceList;

/* ====================== TEMPLATE PARSING =================== */

typedef struct {
    char *constructor;
    char **arguments;
    size_t argument_count, argument_cap;
} TemplateApplication;

typedef struct {
    char *type;
    char *name;
    char **type_parameters;
    size_t type_parameter_count, type_parameter_cap;
    TemplateApplication application;
    int explicit_type;
    char *explicit_parameter;
} TemplateArgument;

typedef struct {
    char *parameter_name;
    char **required_types;
    size_t required_type_count, required_type_cap;
    char **expressions;
    size_t expression_count, expression_cap;
    size_t line;
} TemplateConstraint;

typedef struct TemplateParameterKind {
    int is_value;
    char *value_type;
    int is_constructor;
    struct TemplateParameterKind *signature;
    size_t signature_count;
} TemplateParameterKind;

typedef struct {
    char **parameter_names;
    TemplateParameterKind *parameter_kinds;
    size_t parameter_count, parameter_cap;
    TemplateConstraint *constraints;
    size_t constraint_count, constraint_cap;
    size_t line;
    unsigned offset;
    int used;
} TemplateAnnotation;

typedef struct {
    char *name;
    char **declared_parameters;
    size_t declared_parameter_count, declared_parameter_cap;
    TemplateConstraint *constraints;
    size_t constraint_count, constraint_cap;
    TemplateArgument *parameters;
    size_t parameter_count;
    char *return_type;
    char **return_parameters;
    size_t return_parameter_count, return_parameter_cap;
    TemplateApplication return_application;
    unsigned *dispatch_indices;
    size_t dispatch_count, dispatch_cap;
    char **type_parameters;
    size_t type_parameter_count, type_parameter_cap;
    unsigned start_offset, end_offset;
    unsigned start_line;
    int is_static;
    int is_inline;
    int has_template_annotation;
} TemplateFunction;

typedef struct {
    char *name;
    char **declared_parameters;
    size_t declared_parameter_count, declared_parameter_cap;
    TemplateConstraint *constraints;
    size_t constraint_count, constraint_cap;
    char **parameter_names;
    size_t parameter_count, parameter_cap;
    unsigned start_offset, end_offset;
    unsigned start_line;
    int is_struct_tag;
    int is_union_tag;
} TemplateType;

typedef struct {
    TemplateAnnotation *annotations;
    size_t annotation_count, annotation_cap, next_annotation;
    char **parameter_names;
    TemplateParameterKind *parameter_kinds;
    size_t parameter_count, parameter_cap;
    TemplateFunction *functions;
    size_t function_count, function_cap;
    TemplateType *constructors;
    size_t constructor_count, constructor_cap;
    size_t template_index;
    char **includes;
    size_t include_count, include_cap;
    Buf body;
    char *path;
} TemplateInfo;

typedef struct {
    size_t caller_template_index;
    char *caller_function;
    char *caller_instance;
    size_t callee_template_index;
    char *callee_function;
    char *callee_instance;
} FunctionCallDependency;

typedef struct {
    TemplateInfo *info;
    CXTranslationUnit tu;
    CXFile template_file;
    unsigned offset_bias;
    int failed;
} TemplateAnalysis;

typedef struct {
    unsigned *indices;
    char **types;
    char **keys;
    size_t count;
    char *function_name;
} DispatchAssoc;

typedef struct {
    DispatchAssoc *items;
    size_t count, cap;
} AssocList;

/* ===================== ARGUMENT-TYPE REQUIREMENTS ===================

   A specialization such as Vec_Foo can only be compiled where Foo is
   declared. Generated headers therefore never name a project type in
   unconditional code: everything that mentions it is emitted in a section
   guarded by the include guard of the header that declares it,

       #if defined(FOO_H) && !defined(TMPLGEN_HAVE_..._FOO_H)

   and the generated headers have no include guard around those sections, so
   including them again after the type's header activates them. An instance's
   "requirement" is the sorted set of such guard macros, joined by ','. */

typedef struct {
    char *path;
    char *guard;       /* NULL when the header has no include-guard macro */
} HeaderGuard;

/* ---- dispatch with per-requirement hooks ----------------------------------
 * A dispatch macro may only name types that exist where it expands. The
 * associations that need a project type are therefore moved into hook macros
 * that start out empty (or pass-through for the GNU chain) and are redefined
 * inside the requirement's guarded section. Macros expand at the call, so a
 * hook redefined after the dispatch macro is still picked up. */

typedef struct {
    char *name;
    char *requirement;
    int gnu;               /* GNU hooks take (next, ...), _Generic hooks (...) */
    Buf body;
} DispatchHook;

typedef struct {
    const char *name;
    const AssocList *assocs;
    char **requirements;   /* per association */
    DispatchHook **hooks;
    size_t hook_count, hook_cap;
    unsigned next_node;
} DispatchPlan;

typedef struct {
    size_t instance;
    size_t constructor;
} TypeInstanceRef;

typedef struct {
    char *name;
    size_t file_index;
    char *type_spelling;
    char *canonical_spelling;
    unsigned offset;
} SourceVariable;

typedef struct {
    SourceVariable *items;
    size_t count, cap;
} SourceVariableList;

/* One enumtag_of(x) call: x's source range and what its type turned out to be.
 * C cannot tell an enum from its underlying integer type, so the generated
 * macro would quietly give a plain integer an enum's id; tmplgen checks the
 * argument's type instead (see check_enumtag_arguments). */
typedef struct {
    char *path;
    size_t line;
    size_t argument_file_index;
    unsigned argument_start, argument_end;
    int is_enum;
    char *other_type;
} EnumTagCheck;

typedef struct {
    EnumTagCheck *items;
    size_t count, cap;
} EnumTagCheckList;

typedef struct {
    CXTranslationUnit tu;
    const TemplateInfo *infos;
    size_t template_count;
    SourceList *files;
    ClangUseList *uses;
    SourceFacts *facts;
    CXFile *scanned_files;
    size_t scanned_file_count;
    SourceVariableList variables;
    EnumTagCheckList enumtag_checks;
    int pass;
    int failed;
} ClangContext;

typedef struct {
    CXType type;
} InitializerTypeSearch;

/* ============================ REFLECTION ===========================

   --reflect=project writes gen/reflection.h: an id per reflected type and
   integer tables (size, alignment, fields, offsets, enumerators) computed by
   libclang in one declaration parse after the template outputs are generated.
   The tables name no project type, so they are complete in every translation
   unit; the consumer's compiler verifies the numbers in sections guarded by
   each declaring header's include guard, the same convention as the template
   outputs (doc/usage.md). See doc/reflection.md. */

enum {
    RK_INVALID, RK_VOID, RK_BOOL, RK_INT, RK_FLOAT, RK_POINTER, RK_ARRAY,
    RK_STRUCT, RK_UNION, RK_ENUM, RK_FUNCTION, RK_OPAQUE
};
enum {
    RF_SIGNED = 1u << 0, RF_POINTER_FREE = 1u << 1, RF_NO_PADDING = 1u << 2,
    RF_HAS_BITFIELDS = 1u << 3, RF_FLEXIBLE = 1u << 4, RF_ANONYMOUS = 1u << 5,
    RF_SPECIALIZED = 1u << 6, RF_ENUM_DENSE = 1u << 7
};
enum { RFIELD_BITFIELD = 1u << 0 };
enum { RQUAL_CONST = 1u << 0, RQUAL_VOLATILE = 1u << 1, RQUAL_RESTRICT = 1u << 2 };

typedef struct {
    int kind;
    char *key;          /* identity: builtin name, USR, or derived key */
    char *name;         /* TYPETAG_<name> */
    char *display;      /* human-readable spelling, e.g. "Vec(int)" */
    char *spelling;     /* how C code names it (for checks), or NULL */
    char *header;       /* declaring project header, or NULL */
    char *generated;    /* declaring generated header, or NULL */
    char *requirement;  /* section key for its check ("" = core) */
    unsigned decl_offset;
    unsigned long long size, align, count;
    unsigned flags;
    size_t inner;       /* node index or (size_t)-1 */
    size_t first_field, field_count;
    size_t first_enumerator, enumerator_count;
    int project;        /* declared in the project: fields are reflected */
    int checked;        /* gets a layout check */
    unsigned id;
} ReflectNode;

typedef struct {
    char *name;
    size_t type;
    unsigned long long offset_bits;
    unsigned bit_width;
    unsigned quals;
    unsigned flags;
    char *meta_type;    /* @meta type text, or NULL */
    char *meta_init;    /* @meta initializer text, or NULL */
    size_t meta_node;   /* ReflectNode index for the metadata type */
} ReflectField;

typedef struct { long long value; int is_unsigned; char *name; } ReflectEnumerator;

typedef struct {
    ReflectNode *nodes;
    size_t node_count, node_cap;
    ReflectField *fields;
    size_t field_count, field_cap;
    ReflectEnumerator *enumerators;
    size_t enumerator_count, enumerator_cap;
    StringSet project_headers;   /* headers whose records/enums are reflected */
    char **typedef_usrs;         /* record/enum USR -> first typedef name */
    char **typedef_names;
    size_t typedef_count, typedef_cap;
    char **meta_types;           /* unique @meta type spellings */
    size_t *meta_type_nodes;     /* their ReflectNode indices */
    size_t meta_type_count, meta_type_cap;
} ReflectGraph;

typedef struct {
    ReflectField *items;
    size_t count, cap;
} ReflectFieldList;
typedef struct {
    ReflectGraph *graph;
    ReflectFieldList *fields;
    unsigned long long base_bits;
    unsigned depth;
} ReflectFieldVisit;

typedef struct {
    ReflectGraph *graph;
    int is_unsigned;
} ReflectEnumVisit;

typedef struct {
    ReflectGraph *graph;
    int pass;   /* 1: typedef names and builtin probes, 2: declarations */
} ReflectCollect;

/* ---- emission ----------------------------------------------------------------- */

typedef struct {
    Buf pool;
    char **strings;
    unsigned *offsets;
    size_t count, cap;
} ReflectStrings;

/* Shared invocation and discovery state (defined in state.c). */
extern GenInstance *g_instances;
extern size_t g_instance_count, g_instance_cap;
extern TypeUse *g_type_uses;
extern size_t g_type_use_count, g_type_use_cap;
extern GenericUse *g_generic_uses;
extern size_t g_generic_use_count, g_generic_use_cap;
extern TypeVariableMismatch *g_type_variable_mismatches;
extern size_t g_type_variable_mismatch_count, g_type_variable_mismatch_cap;
extern TypeAlias *g_aliases;
extern size_t g_alias_count, g_alias_cap;
extern StructuralSchema *g_structural_schemas;
extern size_t g_structural_schema_count, g_structural_schema_cap;
extern TypeOrigin *g_type_origins;
extern size_t g_type_origin_count, g_type_origin_cap;
extern char **g_instance_requirements;
extern size_t g_instance_requirement_count;
extern char *g_source_root;
extern char *g_template_dir;
extern char *g_absolute_template_dir;
extern char *g_line_path_base;
extern GenTemplate *g_templates;
extern size_t g_template_count, g_template_cap;
extern char *g_output_path;
extern char *g_generated_dir;
extern char *g_executable_path;
extern int g_reflect_mode;
extern Buf g_reflection_bootstrap;
extern int g_dispatch_backend;
extern int g_emit_line;
extern int g_compiled_output;
extern int g_cache_enabled;
extern int g_tu_cache_enabled;
extern StringSet g_clang_dependencies;
extern char **g_clang_args;
extern size_t g_clang_arg_count, g_clang_arg_cap;
extern size_t g_error_count;
#if defined(CLANGPLUGIN)
extern char *g_unsaved_source_path;
#endif
#if defined(CLANGPLUGIN)
extern char *g_unsaved_source_contents;
#endif
#if defined(CLANGPLUGIN)
extern size_t g_unsaved_source_length;
#endif
extern int g_verbose;
extern TemplateInfo *g_all_template_infos;
extern size_t g_all_template_info_count;
extern FunctionCallDependency *g_function_call_dependencies;
extern size_t g_function_call_dependency_count;
extern size_t g_function_call_dependency_cap;
extern HeaderGuard *g_header_guards;
extern size_t g_header_guard_count, g_header_guard_cap;
extern StringSet g_reported_requirement_errors;

/* Internal cross-module interfaces. */
void type_origin_list_add(TypeOrigin **items, size_t *count, size_t *cap,
                                 const char *name, const char *path,
                                 unsigned offset);
void type_origin_list_free(TypeOrigin *items, size_t count);
const TypeOrigin *find_type_origin(const char *name);
const char *find_alias_canonical(const char *name);
void add_alias(const char *name, const char *canonical);
int type_arguments_compatible(const char *left_spelling,
                                     const char *left_canonical,
                                     const char *right_spelling,
                                     const char *right_canonical);
char *canonical_value_argument(const char *text);
char *instance_suffix(const char *const *spellings, size_t count);
GenInstance *find_instance(size_t template_index, const char *name,
                                  const size_t *parameter_indices, size_t arity);
GenInstance *add_instance(size_t template_index,
                                 const size_t *parameter_indices,
                                 const char *const *spellings,
                                 const char *const *canonicals, size_t count);
void set_instance_origin(GenInstance *instance, const char *path,
                                size_t line);
int add_type_use(const char *functor, const char *type_name);
int add_generic_use(const char *name, const char *type_name, int dispatch,
                           const unsigned *indices, const char *const *types,
                           const char *const *canonicals, size_t dispatch_count);
void set_generic_use_origin(const char *name, const char *type_name,
                                   const char *path, size_t line);
void record_type_variable_mismatch(const char *path, size_t line,
                                         const char *function_name,
                                         const char *parameter_name,
                                         const char *first_type,
                                         const char *second_type);
int has_type_variable_mismatch(const char *path, size_t line,
                                      const char *function_name);
int generic_use_matches(const char *name, const char *type_name,
                               int dispatch_only);
int type_use_matches(const char *functor, const char *type_name);
size_t gen_template_count(void);
void die(const char *fmt, ...);
void buf_init(Buf *b);
void buf_putn(Buf *b, const char *s, size_t n);
void buf_puts(Buf *b, const char *s);
void buf_putc(Buf *b, char c);
void buf_put_uint(Buf *b, unsigned long value);
void buf_free(Buf *b);
void report_error(const char *path, size_t line, const char *fmt, ...);
void report_warning(const char *path, size_t line, const char *fmt, ...);
int str_eq(const char *a, const char *b);
int is_ident_start(char c);
int is_ident_char(char c);
int valid_identifier(const char *s);
char *copy_range(const char *s, size_t n);
char *copy_string(const char *s);
int read_file(const char *path, Buf *out);
char *join_path(const char *directory, const char *name);
char *absolute_path(const char *path);
#if defined(CLANGPLUGIN)
void tmplgen_plugin_clear_unsaved_source(void);
#endif
#if defined(CLANGPLUGIN)
void tmplgen_plugin_set_unsaved_source(const char *path,
                                       const char *contents, size_t length);
#endif
#if defined(CLANGPLUGIN) && !defined(_WIN32)
char *plugin_library_path(void);
#endif
#if defined(CLANGPLUGIN) && !defined(_WIN32)
#else
char *plugin_library_path(void);
#endif
char *resolve_executable_path(const char *argument_zero);
const char *path_leaf(const char *path);
int path_is_within_directory(const char *path, const char *directory);
void cache_add_dependency_path(StringSet *dependencies, const char *path);
void cache_collect_inclusion(CXFile included_file,
                                    CXSourceLocation *inclusion_stack,
                                    unsigned include_length,
                                    CXClientData client_data);
void cache_collect_source_inclusion(CXFile included_file,
                                           CXSourceLocation *inclusion_stack,
                                           unsigned include_length,
                                           CXClientData client_data);
void clear_template_registry(void);
int discover_template_files(void);
char *path_parent(const char *path);
int is_generated_output_file(const char *path);
int scan_source_directory(SourceList *files, const char *directory);
void tokenize_source(SourceFile *file);
void source_file_free(SourceFile *file);
void source_list_free(SourceList *files);
void generated_file_list_add(GeneratedFileList *files, const char *path,
                                    Buf *contents);
void generated_file_list_free(GeneratedFileList *files);
int token_is(const SourceFile *file, size_t index, const char *text);
int cache_file_hash(const char *path, unsigned long long *hash1,
                           unsigned long long *hash2);
int ensure_parent_directories(const char *path);
char *normalize_spaces(const char *s);
const char *next_word(const char **p);
char *word_copy(const char *start, const char *end);
int list_contains(char **items, size_t count, const char *s);
void list_add(char ***items, size_t *count, size_t *cap, const char *s);
void list_free(char **items, size_t count);
void add_function_call_dependency(size_t caller_template_index,
                                         const TemplateFunction *caller,
                                         const GenInstance *caller_instance,
                                         size_t callee_template_index,
                                         const TemplateFunction *callee,
                                         const GenInstance *callee_instance);
int template_requires_constraint(const TemplateInfo *ti, const char *name);
int any_template_requires_constraint(const char *name);
void template_info_init(TemplateInfo *ti);
void template_application_free(TemplateApplication *application);
void template_constraint_array_free(TemplateConstraint *constraints,
                                           size_t count);
void template_constraint_add(TemplateConstraint **constraints,
                                    size_t *constraint_count,
                                    size_t *constraint_cap,
                                    const char *parameter_name,
                                    const char *required_type,
                                    size_t line);
void template_constraint_add_expression(TemplateConstraint **constraints,
                                              size_t *constraint_count,
                                              size_t *constraint_cap,
                                              const char *parameter_name,
                                              const char *expression,
                                              size_t line);
void template_parameter_kind_clear(TemplateParameterKind *kind);
void template_parameter_kind_copy(TemplateParameterKind *target,
                                         const TemplateParameterKind *source);
int template_parameter_kind_equal(const TemplateParameterKind *left,
                                         const TemplateParameterKind *right);
void template_parameter_kinds_free(TemplateParameterKind *kinds,
                                          size_t count);
size_t function_explicit_count(const TemplateFunction *function);
int function_has_explicit_parameters(const TemplateFunction *function);
void template_info_free(TemplateInfo *ti);
int template_parameter_index(const TemplateInfo *ti, const char *name);
int template_parameter_is_value(const TemplateInfo *ti, size_t index);
int template_parameter_is_constructor(const TemplateInfo *ti, size_t index);
int function_has_constructor_parameters(const TemplateInfo *ti,
                                               const TemplateFunction *function);
void copy_template_constraints(TemplateConstraint **target,
                                     size_t *target_count, size_t *target_cap,
                                     const TemplateConstraint *source,
                                     size_t source_count);
void add_template_type(TemplateInfo *ti, const char *name,
                              char **declared_parameters, size_t declared_count,
                              const TemplateConstraint *constraints,
                              size_t constraint_count,
                              char **parameters, size_t parameter_count,
                              unsigned start_offset, unsigned end_offset,
                              unsigned start_line, int is_struct_tag,
                              int is_union_tag);
void add_template_function(TemplateInfo *ti, TemplateFunction *function);
void collect_cursor_template_parameters(CXTranslationUnit tu, CXCursor cursor,
                                               const TemplateInfo *ti,
                                               char ***names, size_t *count,
                                               size_t *capacity);
int analyze_template(TemplateInfo *ti);
size_t identifier_line_in_code(const char *src, const char *name);
int parse_template(TemplateInfo *ti, const char *path);
int instance_parameter_position(const GenInstance *instance,
                                      size_t parameter_index);
int parse_text_type_arguments(const char *source, size_t open,
                                     char ***arguments, size_t *argument_count,
                                     size_t *end_out);
char *expand_short_constraint_call(const char *expression,
                                          const char *parameter,
                                          const char *concrete_type);
const TemplateType *find_template_constructor_any(
    const TemplateInfo *infos, size_t count, const char *name,
    size_t *template_index);
int constructor_signature_matches(const TemplateParameterKind *expected,
                                         const TemplateInfo *candidate_ti,
                                         const TemplateType *candidate);
void rewrite(const char *src, const TemplateInfo *ti,
                    const GenInstance *instance, Buf *out);
void rewrite_c_type(const char *type, const TemplateInfo *ti,
                           const GenInstance *instance,
                           const TemplateApplication *application, Buf *out);
void emit_template_type_pattern(Buf *out, const char *type,
                                       const TemplateApplication *application);
int string_set_has(const StringSet *set, const char *s);
void string_set_add(StringSet *set, const char *s);
void string_set_free(StringSet *set);
void emit_include(Buf *out, const char *header);
char *include_key(const char *header);
void collect_preamble_includes(StringSet *includes, const char *preamble);
void emit_line_directive(Buf *out, unsigned long line,
                                const char *original_path);
void emit_escaped_c_string(Buf *out, const char *text);
size_t skip_c_trivia(const char *source, size_t offset);
size_t skip_c_literal(const char *source, size_t offset);
char *join_suffix(const char *base, const char *suffix);
int assoc_add(AssocList *list, const unsigned *indices,
                     const char *const *types, const char *const *keys,
                     size_t association_count, const char *function_name,
                     int deduplicate);
int instance_matches_parameters(const TemplateInfo *ti,
                                       const GenInstance *instance,
                                       char *const *parameter_names,
                                       size_t parameter_count);
const HeaderGuard *header_guard_for(const char *path);
const char *header_path_for_guard(const char *guard);
void clear_requirement_state(void);
size_t generated_include_before_target(const char *path, unsigned offset,
                                             const char *only_target);
int path_has_suffix(const char *path, const char *suffix);
int template_include_is_reflection(const TemplateInfo *ti, const char *header);
int template_uses_reflection(const TemplateInfo *ti);
int template_index_uses_reflection(size_t template_index);
char *instance_section_marker(const GenInstance *instance);
int compare_strings(const void *left, const void *right);
int compute_instance_requirements(void);
const char *instance_requirement(const GenInstance *instance);
void buf_put_requirement_id(Buf *out, const char *requirement);
void buf_put_requirement_condition(Buf *out, const char *requirement);
void sort_requirements(StringSet *requirements);
char *requirement_intersection(const char *a, const char *b);
void emit_no_match_declaration(Buf *out, const char *name);
void emit_spaces(Buf *out, unsigned count);
void buf_put_section_open(Buf *out, const char *have_prefix,
                                 const char *requirement);
char *relative_path_between(const char *from, const char *to);
void emit_template_module(Buf *out, size_t template_index,
                                 const TemplateInfo *ti, const char *stem,
                                 const char *output_path,
                                 const char *guard, Buf *implementation);
void emit_aggregate_header(Buf *out, const TemplateInfo *infos,
                                  size_t template_count);
int validate_template_info(const TemplateInfo *infos, size_t count);
void build_template_path(Buf *path, const char *stem);
char *template_include_target(const char *header, const char *current_stem);
char *relative_module_include(const char *current_stem,
                                    const char *target_stem);
void emit_preamble(Buf *out, const char *guard,
                          const TemplateInfo *infos, size_t count,
                          const char *current_stem, StringSet *includes);
char *generated_module_path(const char *stem);
char *generated_module_source_path(const char *stem);
char *reflection_header_path(void);
char *reflection_source_path(void);
void output_registry_build(OutputRegistry *registry);
void output_registry_free(OutputRegistry *registry);
int output_is_header(OutputKind kind);
char *generated_module_guard(const char *stem);
unsigned template_type_name_line(const TemplateInfo *ti,
                                        const TemplateType *constructor);
char *copy_cx_string(CXString value);
void register_structural_schema(CXCursor cursor, SourceFacts *facts);
const StructuralSchema *find_structural_schema(const char *name,
                                                       int *ambiguous);
int validate_structural_constraints(const TemplateInfo *infos,
                                           size_t template_count);
void source_variable_add(SourceVariableList *variables, const char *name,
                                size_t file_index, const char *type_spelling,
                                const char *canonical_spelling, unsigned offset);
const TemplateType *find_template_type(const TemplateInfo *ti,
                                              const char *name);
int find_type_constructor(const TemplateInfo *infos, size_t count,
                                 const char *name, size_t *template_index,
                                 const TemplateType **constructor);
int builtin_type_argument(const char *name);
int generic_function_template(const TemplateInfo *infos, size_t count,
                                     const char *name, size_t *template_index);
const TemplateFunction *find_template_function(const TemplateInfo *ti,
                                                       const char *name);
int function_explicit_index(const TemplateFunction *function,
                                   const char *parameter);
int macro_type_arguments(CXTranslationUnit tu, CXCursor cursor,
                                char ***arguments, size_t **token_counts,
                                size_t *argument_count);
size_t source_list_index_for_path(const SourceList *files,
                                         const char *path);
void source_facts_free(SourceFacts *facts);
void source_facts_reset(SourceFacts *facts);
void source_facts_add_alias(SourceFacts *facts, const char *name,
                                   const char *canonical);
void source_facts_add_origin(SourceFacts *facts, const char *name,
                                    const char *path, unsigned offset);
void source_facts_add_constructor(SourceFacts *facts,
                                        size_t template_index,
                                        const char *functor,
                                        const size_t *parameter_indices,
                                        const char *const *spellings,
                                        const char *const *canonicals,
                                        size_t argument_count);
void source_facts_add_schema(SourceFacts *facts,
                                    const StructuralSchema *schema);
void build_bootstrap_associations(size_t template_index,
                                         const TemplateInfo *ti,
                                         const TemplateFunction *function,
                                         AssocList *assocs);
void emit_bootstrap_header(Buf *out, const TemplateInfo *infos,
                                  size_t template_count, int second_pass);
int extract_template_argument(const char *pattern, const char *actual,
                                     const char *parameter, char **value_out);
void request_instance_types(const TemplateInfo *ti,
                                   const TemplateFunction *requester,
                                   const GenInstance *instance);
int process_clang_use_group(ClangUseList *uses, size_t first,
                                   const TemplateInfo *infos, size_t template_count,
                                   unsigned char *processed,
                                   int report_incomplete);
GenInstance *project_instance(const TemplateInfo *ti,
                                     const GenInstance *source,
                                     char *const *parameter_names,
                                     size_t parameter_count);
char *specialize_function_calls(const char *fragment,
                                       const TemplateInfo *ti,
                                       const GenInstance *instance,
                                       const TemplateFunction *caller,
                                       int discover_dependencies,
                                       int report_unresolved);
void expand_dependencies_to_fixpoint(const TemplateInfo *infos,
                                            size_t template_count,
                                            int report_unresolved);
void collect_suffix_api_uses(SourceFile *file, const TemplateInfo *infos,
                                    size_t template_count);
int discover_source_usages(const TemplateInfo *infos, size_t template_count,
                                  SourceList *files);
void clear_discovery_state(void);
void emit_reflection_bootstrap(Buf *out);
int reflect_generate(GeneratedFileList *outputs);
void signature_mix(unsigned long long *hash, const char *data, size_t length);
int source_file_path_compare(const void *left, const void *right);
int compute_input_signature(unsigned long long *signature);
int cache_try_hit(unsigned long long signature, int check_only);
int cache_write_manifest(unsigned long long signature,
                                const GeneratedFileList *outputs);
unsigned long long source_facts_key(const SourceFile *source,
                                           const Buf *bootstrap, int pass);
int source_facts_load(SourceFile *source, const SourceList *files);
int source_facts_save(const SourceFile *source,
                             const SourceList *files);
#if GEN_ENABLE_WATCH
void sleep_ms(unsigned ms);
#endif
#if defined(CLANGPLUGIN)
int tmplgen_plugin_main(int argc, char **argv);
#endif
#if defined(CLANGPLUGIN)
#else
int main(int argc, char **argv);
#endif

#endif /* TMPLGEN_INTERNAL_H */
