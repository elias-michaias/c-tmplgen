/* Definitions of shared invocation and discovery state. */
#include "internal.h"


GenInstance *g_instances;
size_t g_instance_count, g_instance_cap;
TypeUse *g_type_uses;
size_t g_type_use_count, g_type_use_cap;
GenericUse *g_generic_uses;
size_t g_generic_use_count, g_generic_use_cap;
TypeVariableMismatch *g_type_variable_mismatches;
size_t g_type_variable_mismatch_count, g_type_variable_mismatch_cap;
TypeAlias *g_aliases;
size_t g_alias_count, g_alias_cap;
StructuralSchema *g_structural_schemas;
size_t g_structural_schema_count, g_structural_schema_cap;
TypeOrigin *g_type_origins;
size_t g_type_origin_count, g_type_origin_cap;
/* Per instance (parallel to g_instances): the sorted include-guard macros of
 * the headers declaring its argument types, joined by ','; "" when the
 * instance only needs builtin or template-provided types. */
char **g_instance_requirements;
size_t g_instance_requirement_count;
char *g_source_root;
char *g_template_dir;
char *g_absolute_template_dir;
/* When set (absolute), #line paths of template files below it are written
 * relative to it, so output does not depend on how the template directory
 * was spelled (e.g. relative on the command line, absolute from a plugin). */
char *g_line_path_base;
GenTemplate *g_templates;
size_t g_template_count, g_template_cap;
char *g_output_path;
char *g_generated_dir;
char *g_executable_path;
int g_reflect_mode;
/* Parsing stand-in for gen/reflection.h (see emit_reflection_bootstrap). */
Buf g_reflection_bootstrap;
int g_dispatch_backend;
int g_emit_line;
int g_compiled_output;
int g_cache_enabled;
int g_tu_cache_enabled;
StringSet g_clang_dependencies;
char **g_clang_args;
size_t g_clang_arg_count, g_clang_arg_cap;
size_t g_error_count;
#if defined(CLANGPLUGIN)
char *g_unsaved_source_path;
#endif
#if defined(CLANGPLUGIN)
char *g_unsaved_source_contents;
#endif
#if defined(CLANGPLUGIN)
size_t g_unsaved_source_length;
#endif

/* =========================== HELPERS ======================= */

int g_verbose;

TemplateInfo *g_all_template_infos;
size_t g_all_template_info_count;
FunctionCallDependency *g_function_call_dependencies;
size_t g_function_call_dependency_count;
size_t g_function_call_dependency_cap;

HeaderGuard *g_header_guards;
size_t g_header_guard_count, g_header_guard_cap;
StringSet g_reported_requirement_errors;
