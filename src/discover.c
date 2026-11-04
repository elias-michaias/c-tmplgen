/* Libclang project analysis and call-site inference. */
#include "internal.h"

#if !TMPLGEN_PLUGIN_BUILD && !defined(_WIN32)
#include <pthread.h>
#define GEN_PARALLEL_PARSE 1
#else
#define GEN_PARALLEL_PARSE 0
#endif


/* A schema is a set of field names. Schemas are merged from every source file
 * (and from cached facts), so additions must not duplicate existing fields:
 * appending blindly doubled the list for each file that saw the schema. */
static void schema_add_field(StructuralSchema *schema, const char *field)
{
    if (list_contains(schema->fields, schema->field_count, field)) return;
    list_add(&schema->fields, &schema->field_count, &schema->field_cap, field);
}

static enum CXChildVisitResult collect_structural_schema_field(CXCursor cursor,
                                                               CXCursor parent,
                                                               CXClientData client_data)
{
    StructuralSchema *schema = (StructuralSchema *)client_data;
    if (clang_getCursorKind(cursor) == CXCursor_FieldDecl) {
        char *name = copy_cx_string(clang_getCursorSpelling(cursor));
        if (valid_identifier(name)) schema_add_field(schema, name);
        free(name);
    }
    (void)parent;
    return CXChildVisit_Continue;
}

void register_structural_schema(CXCursor cursor, SourceFacts *facts)
{
    enum CXCursorKind kind = clang_getCursorKind(cursor);
    char *name = copy_cx_string(clang_getCursorSpelling(cursor));
    StructuralSchema *schema = NULL;
    StructuralSchema *next;
    size_t i;
    int is_union = kind == CXCursor_UnionDecl;
    if (!valid_identifier(name)) {
        free(name);
        return;
    }
    for (i = 0U; i < g_structural_schema_count; ++i) {
        if (g_structural_schemas[i].is_union == is_union &&
            str_eq(g_structural_schemas[i].name, name)) {
            schema = &g_structural_schemas[i];
            break;
        }
    }
    if (schema == NULL) {
        if (g_structural_schema_count == g_structural_schema_cap) {
            size_t new_cap = g_structural_schema_cap == 0U ? 8U :
                             g_structural_schema_cap * 2U;
            if (new_cap < g_structural_schema_cap ||
                new_cap > (size_t)-1 / sizeof(*g_structural_schemas)) {
                die("too many structural schemas");
            }
            next = (StructuralSchema *)realloc(g_structural_schemas,
                new_cap * sizeof(*g_structural_schemas));
            if (next == NULL) die("out of memory");
            g_structural_schemas = next;
            g_structural_schema_cap = new_cap;
        }
        schema = &g_structural_schemas[g_structural_schema_count++];
        memset(schema, 0, sizeof(*schema));
        schema->name = copy_string(name);
        schema->is_union = is_union;
    }
    free(name);
    clang_visitChildren(cursor, collect_structural_schema_field, schema);
    source_facts_add_schema(facts, schema);
}

static enum CXChildVisitResult find_concrete_initializer_type(
    CXCursor cursor, CXCursor parent, CXClientData client_data)
{
    InitializerTypeSearch *search = (InitializerTypeSearch *)client_data;
    (void)parent;
    if (clang_isExpression(clang_getCursorKind(cursor)) != 0U) {
        CXType type = clang_getCursorType(cursor);
        if (type.kind != CXType_Invalid) {
            char *spelling = copy_cx_string(clang_getTypeSpelling(type));
            int is_auto = str_eq(spelling, "__auto_type") ||
                          str_eq(spelling, "auto");
            free(spelling);
            if (!is_auto) {
                search->type = type;
                return CXChildVisit_Break;
            }
        }
    }
    return CXChildVisit_Recurse;
}

static CXType source_variable_initializer_type(CXCursor variable)
{
    CXCursor initializer = clang_Cursor_getVarDeclInitializer(variable);
    CXType type;
    if (clang_Cursor_isNull(initializer) == 0U) {
        type = clang_getCursorType(initializer);
        if (type.kind != CXType_Invalid) {
            char *spelling = copy_cx_string(clang_getTypeSpelling(type));
            int is_auto = str_eq(spelling, "__auto_type") ||
                          str_eq(spelling, "auto");
            free(spelling);
            if (!is_auto) return type;
        }
    }
    {
        InitializerTypeSearch search;
        search.type = clang_getCursorType(clang_getNullCursor());
        clang_visitChildren(variable, find_concrete_initializer_type, &search);
        return search.type;
    }
}

static void source_variable_list_free(SourceVariableList *variables)
{
    size_t i;
    for (i = 0U; i < variables->count; ++i) {
        free(variables->items[i].name);
        free(variables->items[i].type_spelling);
        free(variables->items[i].canonical_spelling);
    }
    free(variables->items);
    memset(variables, 0, sizeof(*variables));
}

static size_t clang_file_index(const ClangContext *context, CXFile file)
{
    size_t i;
    for (i = 0U; i < context->scanned_file_count; ++i) {
        if (context->scanned_files[i] != NULL &&
            clang_File_isEqual(context->scanned_files[i], file) != 0U) return i;
    }
    return (size_t)-1;
}

static const TemplateFunction *explicit_function_template(
    const TemplateInfo *infos, size_t count, const char *name,
    size_t *template_index)
{
    size_t i;
    for (i = 0U; i < count; ++i) {
        const TemplateFunction *function = find_template_function(&infos[i], name);
        if (function != NULL && function_has_explicit_parameters(function)) {
            *template_index = i;
            return function;
        }
    }
    return NULL;
}

static int macro_argument_source_range(CXTranslationUnit tu, CXCursor cursor,
                                       unsigned wanted, char **path_out,
                                       unsigned *start_out, unsigned *end_out)
{
    CXToken *tokens = NULL;
    unsigned count = 0U;
    unsigned open = 0U;
    unsigned i;
    unsigned argument = 0U;
    unsigned start;
    size_t parens = 0U, brackets = 0U, braces = 0U;
    int found_open = 0;
    int found_range = 0;
    clang_tokenize(tu, clang_getCursorExtent(cursor), &tokens, &count);
    for (i = 0U; i < count; ++i) {
        char *text = copy_cx_string(clang_getTokenSpelling(tu, tokens[i]));
        if (!found_open) {
            if (str_eq(text, "(")) {
                open = i;
                found_open = 1;
            }
            free(text);
            continue;
        }
        free(text);
        break;
    }
    if (found_open) {
        start = open + 1U;
        for (i = start; i < count; ++i) {
            char *text = copy_cx_string(clang_getTokenSpelling(tu, tokens[i]));
            int boundary = 0;
            int closing = str_eq(text, ")");
            if (str_eq(text, "(")) ++parens;
            else if (str_eq(text, ")")) {
                if (parens == 0U && brackets == 0U && braces == 0U) boundary = 1;
                else if (parens != 0U) --parens;
            } else if (str_eq(text, "[")) {
                ++brackets;
            } else if (str_eq(text, "]")) {
                if (brackets != 0U) --brackets;
            } else if (str_eq(text, "{")) {
                ++braces;
            } else if (str_eq(text, "}")) {
                if (braces != 0U) --braces;
            } else {
                boundary = str_eq(text, ",") && parens == 0U &&
                           brackets == 0U && braces == 0U;
            }
            free(text);
            if (boundary) {
                if (argument == wanted && start < i) {
                    CXSourceLocation start_location = clang_getTokenLocation(tu, tokens[start]);
                    CXSourceRange last_range = clang_getTokenExtent(tu, tokens[i - 1U]);
                    CXSourceLocation end_location = clang_getRangeEnd(last_range);
                    CXFile start_file = NULL, end_file = NULL;
                    unsigned line, column;
                    clang_getSpellingLocation(start_location, &start_file, &line,
                                              &column, start_out);
                    clang_getSpellingLocation(end_location, &end_file, &line,
                                              &column, end_out);
                    if (start_file != NULL && end_file != NULL &&
                        clang_File_isEqual(start_file, end_file)) {
                        *path_out = copy_cx_string(clang_getFileName(start_file));
                        found_range = 1;
                    }
                    break;
                }
                ++argument;
                start = i + 1U;
                if (closing) break;
            }
        }
    }
    clang_disposeTokens(tu, tokens, count);
    return found_range;
}

size_t source_list_index_for_path(const SourceList *files,
                                         const char *path)
{
    size_t i;
    for (i = 0U; i < files->count; ++i) {
        if (str_eq(files->items[i].path, path)) return i;
    }
    return (size_t)-1;
}

static void clang_use_add(ClangUseList *list, const char *name, const char *path,
                          unsigned offset, size_t line, unsigned argument_index,
                          size_t argument_file_index,
                          unsigned argument_start, unsigned argument_end)
{
    size_t i;
    ClangUse *next;
    size_t new_cap;
    for (i = 0U; i < list->count; ++i) {
        if (str_eq(list->items[i].name, name) && str_eq(list->items[i].path, path) &&
            list->items[i].offset == offset &&
            list->items[i].argument_index == argument_index) return;
    }
    if (list->count == list->cap) {
        new_cap = list->cap == 0U ? 8U : list->cap * 2U;
        if (new_cap < list->cap || new_cap > (size_t)-1 / sizeof(*list->items)) {
            die("too many Clang macro expansions");
        }
        next = (ClangUse *)realloc(list->items, new_cap * sizeof(*list->items));
        if (next == NULL) die("out of memory");
        list->items = next;
        list->cap = new_cap;
    }
    list->items[list->count].name = copy_string(name);
    list->items[list->count].path = copy_string(path);
    list->items[list->count].offset = offset;
    list->items[list->count].line = line;
    list->items[list->count].argument_index = argument_index;
    list->items[list->count].argument_file_index = argument_file_index;
    list->items[list->count].argument_start = argument_start;
    list->items[list->count].argument_end = argument_end;
    list->items[list->count].type_spelling = NULL;
    list->items[list->count].canonical_spelling = NULL;
    ++list->count;
}

static void clang_use_list_free(ClangUseList *uses)
{
    size_t i;
    for (i = 0U; i < uses->count; ++i) {
        free(uses->items[i].name);
        free(uses->items[i].path);
        free(uses->items[i].type_spelling);
        free(uses->items[i].canonical_spelling);
    }
    free(uses->items);
    memset(uses, 0, sizeof(*uses));
}

static void source_constructor_fact_free(SourceConstructorFact *fact)
{
    size_t i;
    free(fact->functor);
    for (i = 0U; i < fact->argument_count; ++i) {
        free(fact->spellings[i]);
        free(fact->canonicals[i]);
    }
    free(fact->spellings);
    free(fact->canonicals);
    free(fact->parameter_indices);
    memset(fact, 0, sizeof(*fact));
}

void source_facts_free(SourceFacts *facts)
{
    size_t i;
    clang_use_list_free(&facts->pass1_uses);
    clang_use_list_free(&facts->pass2_uses);
    for (i = 0U; i < facts->alias_count; ++i) {
        free(facts->aliases[i].name);
        free(facts->aliases[i].canonical);
    }
    free(facts->aliases);
    for (i = 0U; i < facts->constructor_count; ++i) {
        source_constructor_fact_free(&facts->constructors[i]);
    }
    free(facts->constructors);
    for (i = 0U; i < facts->schema_count; ++i) {
        free(facts->schemas[i].name);
        list_free(facts->schemas[i].fields, facts->schemas[i].field_count);
    }
    free(facts->schemas);
    type_origin_list_free(facts->origins, facts->origin_count);
    string_set_free(&facts->dependencies);
    memset(facts, 0, sizeof(*facts));
}

void source_facts_reset(SourceFacts *facts)
{
    source_facts_free(facts);
}

void source_facts_add_alias(SourceFacts *facts, const char *name,
                                   const char *canonical)
{
    size_t i;
    TypeAlias *next;
    size_t new_cap;
    if (facts == NULL) return;
    for (i = 0U; i < facts->alias_count; ++i) {
        if (str_eq(facts->aliases[i].name, name)) {
            if (!str_eq(facts->aliases[i].canonical, canonical)) {
                free(facts->aliases[i].canonical);
                facts->aliases[i].canonical = copy_string(canonical);
            }
            return;
        }
    }
    if (facts->alias_count == facts->alias_cap) {
        new_cap = facts->alias_cap == 0U ? 8U : facts->alias_cap * 2U;
        if (new_cap < facts->alias_cap ||
            new_cap > (size_t)-1 / sizeof(*facts->aliases)) {
            die("too many source typedef aliases");
        }
        next = (TypeAlias *)realloc(facts->aliases,
                                    new_cap * sizeof(*facts->aliases));
        if (next == NULL) die("out of memory");
        facts->aliases = next;
        facts->alias_cap = new_cap;
    }
    facts->aliases[facts->alias_count].name = copy_string(name);
    facts->aliases[facts->alias_count].canonical = copy_string(canonical);
    ++facts->alias_count;
}

void source_facts_add_origin(SourceFacts *facts, const char *name,
                                    const char *path, unsigned offset)
{
    if (facts == NULL) return;
    type_origin_list_add(&facts->origins, &facts->origin_count,
                         &facts->origin_cap, name, path, offset);
}

void source_facts_add_constructor(SourceFacts *facts,
                                        size_t template_index,
                                        const char *functor,
                                        const size_t *parameter_indices,
                                        const char *const *spellings,
                                        const char *const *canonicals,
                                        size_t argument_count)
{
    size_t i, j;
    SourceConstructorFact *next;
    size_t new_cap;
    if (facts == NULL) return;
    for (i = 0U; i < facts->constructor_count; ++i) {
        SourceConstructorFact *existing = &facts->constructors[i];
        if (existing->template_index != template_index ||
            existing->argument_count != argument_count ||
            !str_eq(existing->functor, functor)) continue;
        for (j = 0U; j < argument_count; ++j) {
            if (existing->parameter_indices[j] != parameter_indices[j] ||
                !str_eq(existing->spellings[j], spellings[j]) ||
                !str_eq(existing->canonicals[j], canonicals[j])) break;
        }
        if (j == argument_count) return;
    }
    if (facts->constructor_count == facts->constructor_cap) {
        new_cap = facts->constructor_cap == 0U ? 4U :
                  facts->constructor_cap * 2U;
        if (new_cap < facts->constructor_cap ||
            new_cap > (size_t)-1 / sizeof(*facts->constructors)) {
            die("too many source type-constructor uses");
        }
        next = (SourceConstructorFact *)realloc(facts->constructors,
            new_cap * sizeof(*facts->constructors));
        if (next == NULL) die("out of memory");
        facts->constructors = next;
        facts->constructor_cap = new_cap;
    }
    {
        SourceConstructorFact *fact = &facts->constructors[facts->constructor_count++];
        memset(fact, 0, sizeof(*fact));
        fact->template_index = template_index;
        fact->functor = copy_string(functor);
        fact->argument_count = argument_count;
        if (argument_count != 0U) {
            fact->parameter_indices = (size_t *)malloc(
                argument_count * sizeof(*fact->parameter_indices));
            fact->spellings = (char **)calloc(argument_count,
                                              sizeof(*fact->spellings));
            fact->canonicals = (char **)calloc(argument_count,
                                               sizeof(*fact->canonicals));
            if (fact->parameter_indices == NULL || fact->spellings == NULL ||
                fact->canonicals == NULL) die("out of memory");
            for (j = 0U; j < argument_count; ++j) {
                fact->parameter_indices[j] = parameter_indices[j];
                fact->spellings[j] = copy_string(spellings[j]);
                fact->canonicals[j] = copy_string(canonicals[j]);
            }
        }
    }
}

void source_facts_add_schema(SourceFacts *facts,
                                    const StructuralSchema *schema)
{
    StructuralSchema *copy;
    size_t i;
    size_t new_cap;
    if (facts == NULL) return;
    for (i = 0U; i < facts->schema_count; ++i) {
        StructuralSchema *existing = &facts->schemas[i];
        if (existing->is_union == schema->is_union &&
            str_eq(existing->name, schema->name)) {
            size_t field_index;
            for (field_index = 0U; field_index < schema->field_count; ++field_index) {
                schema_add_field(existing, schema->fields[field_index]);
            }
            return;
        }
    }
    if (facts->schema_count == facts->schema_cap) {
        new_cap = facts->schema_cap == 0U ? 4U : facts->schema_cap * 2U;
        if (new_cap < facts->schema_cap ||
            new_cap > (size_t)-1 / sizeof(*facts->schemas)) {
            die("too many source structural schemas");
        }
        copy = (StructuralSchema *)realloc(facts->schemas,
            new_cap * sizeof(*facts->schemas));
        if (copy == NULL) die("out of memory");
        facts->schemas = copy;
        facts->schema_cap = new_cap;
    }
    copy = &facts->schemas[facts->schema_count++];
    memset(copy, 0, sizeof(*copy));
    copy->name = copy_string(schema->name);
    copy->is_union = schema->is_union;
    for (i = 0U; i < schema->field_count; ++i) {
        schema_add_field(copy, schema->fields[i]);
    }
}

static void source_facts_apply(SourceFacts *facts)
{
    size_t i;
    for (i = 0U; i < facts->alias_count; ++i) {
        add_alias(facts->aliases[i].name, facts->aliases[i].canonical);
    }
    for (i = 0U; i < facts->origin_count; ++i) {
        type_origin_list_add(&g_type_origins, &g_type_origin_count,
                             &g_type_origin_cap, facts->origins[i].name,
                             facts->origins[i].path, facts->origins[i].offset);
    }
    for (i = 0U; i < facts->constructor_count; ++i) {
        SourceConstructorFact *fact = &facts->constructors[i];
        GenInstance *instance = add_instance(fact->template_index,
            fact->parameter_indices, (const char *const *)fact->spellings,
            (const char *const *)fact->canonicals, fact->argument_count);
        size_t argument_index;
        for (argument_index = 0U; argument_index < fact->argument_count;
             ++argument_index) {
            add_alias(fact->spellings[argument_index],
                      fact->canonicals[argument_index]);
        }
        {
            size_t constructor_template_index = 0U;
            const TemplateType *constructor = NULL;
            if (find_type_constructor(g_all_template_infos, g_all_template_info_count,
                                      fact->functor, &constructor_template_index,
                                      &constructor)) {
                (void)add_type_use(fact->functor, instance->name);
            } else if (fact->template_index < g_all_template_info_count) {
                /* Explicit type pseudo-argument calls are stored in the same
                 * fact form, but request a function, not a type use. */
                const TemplateInfo *ti = &g_all_template_infos[fact->template_index];
                const TemplateFunction *function = find_template_function(
                    ti, fact->functor);
                if (function != NULL && function_has_explicit_parameters(function)) {
                    request_instance_types(ti, function, instance);
                    (void)add_generic_use(fact->functor, instance->name, 0,
                                          NULL, NULL, NULL, 0U);
                }
            }
        }
    }
    for (i = 0U; i < facts->schema_count; ++i) {
        const StructuralSchema *source = &facts->schemas[i];
        StructuralSchema *target = NULL;
        StructuralSchema *next;
        size_t schema_index;
        for (schema_index = 0U; schema_index < g_structural_schema_count;
             ++schema_index) {
            if (g_structural_schemas[schema_index].is_union == source->is_union &&
                str_eq(g_structural_schemas[schema_index].name, source->name)) {
                target = &g_structural_schemas[schema_index];
                break;
            }
        }
        if (target == NULL) {
            if (g_structural_schema_count == g_structural_schema_cap) {
                size_t new_cap = g_structural_schema_cap == 0U ? 8U :
                                 g_structural_schema_cap * 2U;
                if (new_cap < g_structural_schema_cap ||
                    new_cap > (size_t)-1 / sizeof(*g_structural_schemas)) {
                    die("too many structural schemas");
                }
                next = (StructuralSchema *)realloc(g_structural_schemas,
                    new_cap * sizeof(*g_structural_schemas));
                if (next == NULL) die("out of memory");
                g_structural_schemas = next;
                g_structural_schema_cap = new_cap;
            }
            target = &g_structural_schemas[g_structural_schema_count++];
            memset(target, 0, sizeof(*target));
            target->name = copy_string(source->name);
            target->is_union = source->is_union;
        }
        for (schema_index = 0U; schema_index < source->field_count; ++schema_index) {
            schema_add_field(target, source->fields[schema_index]);
        }
    }
}

static int infer_auto_variable_from_call(const ClangContext *context,
                                         size_t file_index,
                                         const char *variable_name,
                                         unsigned declaration_line,
                                         char **type_out,
                                         char **canonical_out)
{
    const SourceFile *source;
    const char *text;
    size_t line_start = 0U;
    size_t line_number = 1U;
    size_t position;
    size_t declaration_end;
    size_t call_line;
    char *call_name = NULL;
    size_t template_index;
    const TemplateFunction *function;
    const TemplateInfo *ti;
    size_t *indices;
    size_t parameter_index;
    GenInstance *instance = NULL;
    size_t use_index;
    if (file_index >= context->files->count) return 0;
    source = &context->files->items[file_index];
    text = source->text.p != NULL ? source->text.p : "";
    while (line_number < declaration_line && text[line_start] != '\0') {
        if (text[line_start++] == '\n') ++line_number;
    }
    if (line_number != declaration_line) return 0;
    position = line_start;
    declaration_end = line_start;
    while (text[declaration_end] != '\0' && text[declaration_end] != ';') {
        ++declaration_end;
    }
    while (position < declaration_end) {
        size_t start;
        size_t end;
        position = skip_c_trivia(text, position);
        if (position >= declaration_end) break;
        if (text[position] == '"' || text[position] == '\'') {
            position = skip_c_literal(text, position);
            continue;
        }
        if (!is_ident_start(text[position])) {
            ++position;
            continue;
        }
        start = position++;
        while (is_ident_char(text[position])) ++position;
        end = position;
        if (end - start != strlen(variable_name) ||
            memcmp(text + start, variable_name, end - start) != 0) continue;
        position = skip_c_trivia(text, position);
        if (text[position] != '=') continue;
        position = skip_c_trivia(text, position + 1U);
        if (!is_ident_start(text[position])) return 0;
        start = position++;
        while (is_ident_char(text[position])) ++position;
        call_name = copy_range(text + start, position - start);
        position = skip_c_trivia(text, position);
        if (text[position] != '(') {
            free(call_name);
            return 0;
        }
        call_line = declaration_line;
        for (start = line_start; start < position; ++start) {
            if (text[start] == '\n') ++call_line;
        }
        break;
    }
    if (call_name == NULL ||
        !generic_function_template(context->infos, context->template_count,
                                   call_name, &template_index)) {
        free(call_name);
        return 0;
    }
    ti = &context->infos[template_index];
    function = find_template_function(ti, call_name);
    if (function == NULL || function->return_type == NULL) {
        free(call_name);
        return 0;
    }
    indices = (size_t *)malloc(function->type_parameter_count * sizeof(*indices));
    if (indices == NULL) die("out of memory");
    for (parameter_index = 0U; parameter_index < function->type_parameter_count;
         ++parameter_index) {
        int mapped = template_parameter_index(
            ti, function->type_parameters[parameter_index]);
        if (mapped < 0) die("invalid auto variable return parameter map");
        indices[parameter_index] = (size_t)mapped;
    }
    for (use_index = 0U; use_index < g_generic_use_count; ++use_index) {
        const GenericUse *use = &g_generic_uses[use_index];
        if (!str_eq(use->name, call_name) || use->origin_path == NULL ||
            !str_eq(use->origin_path, source->path) ||
            use->origin_line != call_line) continue;
        instance = find_instance(template_index, use->type_name, indices,
                                 function->type_parameter_count);
        if (instance != NULL) break;
    }
    free(indices);
    free(call_name);
    if (instance == NULL) return 0;
    {
        Buf rewritten;
        const char *alias;
        buf_init(&rewritten);
        if (function->return_application.constructor != NULL) {
            rewrite_c_type(function->return_type, ti, instance,
                           &function->return_application, &rewritten);
        } else {
            rewrite(function->return_type, ti, instance, &rewritten);
        }
        *type_out = copy_string(rewritten.p != NULL ? rewritten.p : "");
        alias = valid_identifier(*type_out) ? find_alias_canonical(*type_out) : NULL;
        *canonical_out = NULL;
        if (function->return_application.constructor == NULL &&
            function->return_parameter_count == 1U) {
            int return_parameter = template_parameter_index(
                ti, function->return_parameters[0]);
            int return_argument = return_parameter >= 0 ?
                instance_parameter_position(instance,
                                            (size_t)return_parameter) : -1;
            if (return_argument >= 0) {
                free(*type_out);
                *type_out = copy_string(instance->arguments[return_argument].c_type);
                *canonical_out = copy_string(
                    instance->arguments[return_argument].canonical);
            }
        }
        if (*canonical_out == NULL) {
            *canonical_out = copy_string(alias != NULL ? alias : *type_out);
        }
        buf_free(&rewritten);
    }
    return (*type_out)[0] != '\0';
}

static void enumtag_check_add(ClangContext *context, CXCursor cursor,
                              const char *path, unsigned line)
{
    EnumTagCheckList *list = &context->enumtag_checks;
    char *argument_path = NULL;
    unsigned start = 0U, end = 0U;
    size_t file_index, i;
    if (!macro_argument_source_range(context->tu, cursor, 0U, &argument_path,
                                     &start, &end)) {
        free(argument_path);
        return;
    }
    file_index = argument_path != NULL ?
        source_list_index_for_path(context->files, argument_path) : (size_t)-1;
    free(argument_path);
    if (file_index == (size_t)-1) return;
    /* enumtag_of expands TMPLGEN_ENUMTAG_OF: one check per argument range. */
    for (i = 0U; i < list->count; ++i) {
        if (list->items[i].argument_file_index == file_index &&
            list->items[i].argument_start == start &&
            list->items[i].argument_end == end) return;
    }
    if (list->count == list->cap) {
        size_t cap = list->cap != 0U ? list->cap * 2U : 8U;
        EnumTagCheck *items = (EnumTagCheck *)realloc(list->items,
                                                      cap * sizeof(*items));
        if (items == NULL) die("out of memory");
        list->items = items;
        list->cap = cap;
    }
    memset(&list->items[list->count], 0, sizeof(list->items[list->count]));
    list->items[list->count].path = copy_string(path != NULL ? path : "");
    list->items[list->count].line = (size_t)line;
    list->items[list->count].argument_file_index = file_index;
    list->items[list->count].argument_start = start;
    list->items[list->count].argument_end = end;
    ++list->count;
}

static void enumtag_check_list_free(EnumTagCheckList *list)
{
    size_t i;
    for (i = 0U; i < list->count; ++i) {
        free(list->items[i].path);
        free(list->items[i].other_type);
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}

/* Records the type of every expression that is exactly an enumtag_of
 * argument. Several cursors can share that range (an implicit conversion
 * around the operand); the argument is an enum if any of them is. */
static enum CXChildVisitResult check_enumtag_arguments(CXCursor cursor,
                                                       CXCursor parent,
                                                       CXClientData client_data)
{
    ClangContext *context = (ClangContext *)client_data;
    CXSourceRange extent;
    CXFile start_file = NULL, end_file = NULL;
    unsigned start = 0U, end = 0U, start_line = 0U, end_line = 0U;
    unsigned start_column = 0U, end_column = 0U;
    size_t file_index, i;
    (void)parent;
    if (!clang_isExpression(clang_getCursorKind(cursor))) return CXChildVisit_Recurse;
    extent = clang_getCursorExtent(cursor);
    clang_getSpellingLocation(clang_getRangeStart(extent), &start_file,
                              &start_line, &start_column, &start);
    clang_getSpellingLocation(clang_getRangeEnd(extent), &end_file,
                              &end_line, &end_column, &end);
    if (start_file == NULL || end_file == NULL ||
        !clang_File_isEqual(start_file, end_file)) return CXChildVisit_Recurse;
    file_index = clang_file_index(context, start_file);
    if (file_index == (size_t)-1) return CXChildVisit_Recurse;
    for (i = 0U; i < context->enumtag_checks.count; ++i) {
        EnumTagCheck *check = &context->enumtag_checks.items[i];
        CXType type;
        if (check->argument_file_index != file_index ||
            check->argument_start != start || check->argument_end != end) continue;
        type = clang_getCursorType(cursor);
        if (type.kind == CXType_Invalid) continue;
        if (clang_getCanonicalType(type).kind == CXType_Enum) {
            check->is_enum = 1;
        } else if (check->other_type == NULL) {
            check->other_type = copy_cx_string(clang_getTypeSpelling(type));
        }
    }
    return CXChildVisit_Recurse;
}

static enum CXChildVisitResult collect_clang_source_cursor(CXCursor cursor,
                                                           CXCursor parent,
                                                           CXClientData client_data)
{
    ClangContext *context = (ClangContext *)client_data;
    enum CXCursorKind kind = clang_getCursorKind(cursor);
    CXSourceLocation location = clang_getCursorLocation(cursor);
    CXFile source_file = NULL;
    unsigned line = 0U;
    unsigned column = 0U;
    unsigned offset = 0U;
    size_t source_file_index;
    char *path;
    int scanned;
    (void)parent;
    clang_getExpansionLocation(location, &source_file, &line, &column, &offset);
    if (source_file == NULL) return CXChildVisit_Recurse;
    source_file_index = clang_file_index(context, source_file);
    scanned = source_file_index != (size_t)-1;
    if ((kind == CXCursor_StructDecl || kind == CXCursor_UnionDecl) &&
        clang_isCursorDefinition(cursor) != 0U) {
        int required = scanned;
        if (!required) {
            CXString spelling = clang_getCursorSpelling(cursor);
            const char *record_name = clang_getCString(spelling);
            required = record_name != NULL &&
                       any_template_requires_constraint(record_name);
            clang_disposeString(spelling);
        }
        if (required) {
            register_structural_schema(cursor, context->facts);
        }
    }
    if (!scanned) return CXChildVisit_Recurse;
    if (context->pass == 1 &&
        (kind == CXCursor_TypedefDecl ||
         ((kind == CXCursor_StructDecl || kind == CXCursor_UnionDecl ||
           kind == CXCursor_EnumDecl) && clang_isCursorDefinition(cursor) != 0U))) {
        /* Remember where project types are declared, so generated code that
         * names them can be guarded by the declaring header's include guard. */
        char *spelling = copy_cx_string(clang_getCursorSpelling(cursor));
        if (valid_identifier(spelling)) {
            Buf origin_name;
            buf_init(&origin_name);
            if (kind == CXCursor_StructDecl) buf_puts(&origin_name, "struct ");
            else if (kind == CXCursor_UnionDecl) buf_puts(&origin_name, "union ");
            else if (kind == CXCursor_EnumDecl) buf_puts(&origin_name, "enum ");
            buf_puts(&origin_name, spelling);
            source_facts_add_origin(context->facts, origin_name.p,
                                    context->files->items[source_file_index].path,
                                    offset);
            buf_free(&origin_name);
        }
        free(spelling);
    }
    if (kind != CXCursor_VarDecl && kind != CXCursor_TypedefDecl &&
        kind != CXCursor_MacroExpansion) return CXChildVisit_Recurse;
    path = kind == CXCursor_MacroExpansion ?
        copy_cx_string(clang_getFileName(source_file)) : NULL;
    if (kind == CXCursor_VarDecl) {
        char *name = copy_cx_string(clang_getCursorSpelling(cursor));
        CXType declared_type = clang_getCursorType(cursor);
        CXType canonical_type = clang_getCanonicalType(declared_type);
        char *type_spelling = copy_cx_string(clang_getTypeSpelling(declared_type));
        char *canonical_spelling = copy_cx_string(
            clang_getTypeSpelling(canonical_type));
        if (str_eq(type_spelling, "__auto_type") ||
            str_eq(type_spelling, "auto")) {
            CXType initializer_type = source_variable_initializer_type(cursor);
            if (initializer_type.kind != CXType_Invalid) {
                char *inferred_spelling = copy_cx_string(
                    clang_getTypeSpelling(initializer_type));
                char *inferred_canonical = copy_cx_string(clang_getTypeSpelling(
                    clang_getCanonicalType(initializer_type)));
                if (inferred_spelling[0] != '\0' &&
                    !str_eq(inferred_spelling, "__auto_type") &&
                    !str_eq(inferred_spelling, "auto")) {
                    free(type_spelling);
                    free(canonical_spelling);
                    type_spelling = inferred_spelling;
                    canonical_spelling = inferred_canonical;
                } else {
                    free(inferred_spelling);
                    free(inferred_canonical);
                }
            }
            if (str_eq(type_spelling, "__auto_type") ||
                str_eq(type_spelling, "auto")) {
                char *inferred_spelling = NULL;
                char *inferred_canonical = NULL;
                if (infer_auto_variable_from_call(context, source_file_index,
                        name, line, &inferred_spelling, &inferred_canonical)) {
                    free(type_spelling);
                    free(canonical_spelling);
                    type_spelling = inferred_spelling;
                    canonical_spelling = inferred_canonical;
                } else {
                    free(inferred_spelling);
                    free(inferred_canonical);
                }
            }
        }
        if (valid_identifier(name) && type_spelling[0] != '\0') {
            source_variable_add(&context->variables, name, source_file_index,
                                type_spelling,
                                canonical_spelling, offset);
        }
        free(name);
        free(type_spelling);
        free(canonical_spelling);
    } else if (kind == CXCursor_TypedefDecl) {
        char *name = copy_cx_string(clang_getCursorSpelling(cursor));
        CXType underlying = clang_getTypedefDeclUnderlyingType(cursor);
        CXType canonical = clang_getCanonicalType(underlying);
        char *canonical_name = copy_cx_string(clang_getTypeSpelling(canonical));
        if (valid_identifier(name) && canonical_name[0] != '\0') {
            add_alias(name, canonical_name);
            source_facts_add_alias(context->facts, name, canonical_name);
        }
        free(name);
        free(canonical_name);
    } else if (kind == CXCursor_MacroExpansion) {
        char *name = copy_cx_string(clang_getCursorSpelling(cursor));
        size_t type_template_index = 0U;
        const TemplateType *constructor = NULL;
        if (context->pass == 1 && g_reflect_mode != REFLECT_NONE &&
            (str_eq(name, "enumtag_of") || str_eq(name, "TMPLGEN_ENUMTAG_OF"))) {
            enumtag_check_add(context, cursor, path, line);
        } else if (context->pass == 1 &&
            find_type_constructor(context->infos, context->template_count, name,
                                  &type_template_index, &constructor)) {
            char **arguments = NULL;
            char **canonical_arguments = NULL;
            size_t *argument_tokens = NULL;
            const TemplateInfo *type_ti = &context->infos[type_template_index];
            size_t argument_count = 0U;
            size_t j;
            int argument_error = 0;
            if (!macro_type_arguments(context->tu, cursor, &arguments,
                                      &argument_tokens, &argument_count) ||
                argument_count != constructor->parameter_count) {
                report_error(path, line,
                             "%s() requires %lu type or value arguments",
                             name, (unsigned long)constructor->parameter_count);
                context->failed = 1;
            } else {
                canonical_arguments = (char **)calloc(argument_count,
                                                      sizeof(*canonical_arguments));
                if (canonical_arguments == NULL) die("out of memory");
                for (j = 0U; j < argument_count; ++j) {
                    int parameter = template_parameter_index(type_ti,
                        constructor->parameter_names[j]);
                    const char *canonical;
                    if (parameter < 0) die("invalid template constructor parameter map");
                    if (template_parameter_is_constructor(type_ti, (size_t)parameter)) {
                        size_t bound_template_index = 0U;
                        const TemplateType *bound = valid_identifier(arguments[j]) ?
                            find_template_constructor_any(g_all_template_infos,
                                g_all_template_info_count, arguments[j],
                                &bound_template_index) : NULL;
                        if (argument_tokens[j] != 1U || bound == NULL ||
                            !constructor_signature_matches(
                                &type_ti->parameter_kinds[parameter],
                                &g_all_template_infos[bound_template_index], bound)) {
                            report_error(path, line,
                                "%s() argument '%s' is not a template constructor matching '%s'",
                                name, arguments[j], constructor->parameter_names[j]);
                            context->failed = 1;
                            argument_error = 1;
                            break;
                        }
                        canonical_arguments[j] = copy_string(arguments[j]);
                        continue;
                    }
                    if (template_parameter_is_value(type_ti, (size_t)parameter)) {
                        char *value;
                        size_t digit = 0U;
                        int decimal = 1;
                        while (arguments[j][digit] >= '0' &&
                               arguments[j][digit] <= '9') ++digit;
                        if (digit == 0U || arguments[j][digit] != '\0' ||
                            (digit > 1U && arguments[j][0] == '0')) {
                            decimal = 0;
                        }
                        if (argument_tokens[j] != 1U ||
                            (!valid_identifier(arguments[j]) && !decimal)) {
                            report_error(path, line,
                                "%s() requires a single-token value argument for '%s'",
                                name, constructor->parameter_names[j]);
                            context->failed = 1;
                            argument_error = 1;
                            break;
                        }
                        value = canonical_value_argument(arguments[j]);
                        if (value == NULL) {
                            report_error(path, line,
                                "%s() value argument for '%s' must be an integer constant expression",
                                name, constructor->parameter_names[j]);
                            context->failed = 1;
                            argument_error = 1;
                            break;
                        }
                        free(arguments[j]);
                        arguments[j] = copy_string(value);
                        canonical_arguments[j] = value;
                        continue;
                    }
                    if (argument_tokens[j] != 1U ||
                        !(valid_identifier(arguments[j]) ||
                          builtin_type_argument(arguments[j]))) {
                        report_error(path, line,
                            "%s() requires a single-token type argument for '%s'",
                            name, constructor->parameter_names[j]);
                        context->failed = 1;
                        argument_error = 1;
                        break;
                    }
                    canonical = find_alias_canonical(arguments[j]);
                    canonical_arguments[j] = copy_string(canonical != NULL ?
                                                         canonical : arguments[j]);
                    add_alias(arguments[j], canonical_arguments[j]);
                }
                if (!argument_error) {
                    size_t *parameter_indices =
                        (size_t *)malloc(argument_count * sizeof(*parameter_indices));
                    GenInstance *instance;
                    if (parameter_indices == NULL) die("out of memory");
                    for (j = 0U; j < argument_count; ++j) {
                        int parameter = template_parameter_index(
                            type_ti, constructor->parameter_names[j]);
                        if (parameter < 0) die("invalid template constructor parameter map");
                        parameter_indices[j] = (size_t)parameter;
                    }
                    instance = add_instance(
                        type_template_index, parameter_indices,
                        (const char *const *)arguments,
                        (const char *const *)canonical_arguments, argument_count);
                    set_instance_origin(instance, path, line);
                    (void)add_type_use(name, instance->name);
                    source_facts_add_constructor(context->facts,
                        type_template_index, name, parameter_indices,
                        (const char *const *)arguments,
                        (const char *const *)canonical_arguments,
                        argument_count);
                    free(parameter_indices);
                }
                for (j = 0U; j < argument_count; ++j) free(canonical_arguments[j]);
                free(canonical_arguments);
            }
            free(argument_tokens);
            list_free(arguments, argument_count);
        } else if (context->uses != NULL) {
            size_t function_template_index = 0U;
            const TemplateFunction *explicit_function = context->pass == 1 ?
                explicit_function_template(context->infos, context->template_count,
                                           name, &function_template_index) : NULL;
            if (explicit_function != NULL) {
                const TemplateInfo *function_ti =
                    &context->infos[function_template_index];
                size_t explicit_count = function_explicit_count(explicit_function);
                size_t type_count = explicit_function->type_parameter_count;
                char **arguments = NULL;
                size_t *argument_tokens = NULL;
                size_t argument_count = 0U;
                char **canonical_arguments = NULL;
                const char **spellings = NULL;
                size_t *parameter_indices = NULL;
                size_t j;
                int argument_error = 0;
                if (!macro_type_arguments(context->tu, cursor, &arguments,
                                          &argument_tokens, &argument_count) ||
                    argument_count < explicit_count) {
                    report_error(path, line,
                                 "%s() requires at least %lu type arguments",
                                 name, (unsigned long)explicit_count);
                    context->failed = 1;
                } else {
                    canonical_arguments = (char **)calloc(type_count,
                        sizeof(*canonical_arguments));
                    spellings = (const char **)calloc(type_count, sizeof(*spellings));
                    parameter_indices = (size_t *)malloc(type_count *
                        sizeof(*parameter_indices));
                    if (canonical_arguments == NULL || spellings == NULL ||
                        parameter_indices == NULL) die("out of memory");
                    for (j = 0U; j < type_count; ++j) {
                        int explicit_index = function_explicit_index(
                            explicit_function, explicit_function->type_parameters[j]);
                        const char *argument;
                        const char *canonical;
                        int parameter;
                        if (explicit_index < 0) die("invalid explicit type parameter map");
                        argument = arguments[(size_t)explicit_index];
                        if (argument_tokens[explicit_index] != 1U ||
                            !(valid_identifier(argument) ||
                              builtin_type_argument(argument))) {
                            report_error(path, line,
                                "%s() requires a single-token type argument for '%s'",
                                name, explicit_function->type_parameters[j]);
                            context->failed = 1;
                            argument_error = 1;
                            break;
                        }
                        canonical = find_alias_canonical(argument);
                        spellings[j] = argument;
                        canonical_arguments[j] = copy_string(canonical != NULL ?
                                                             canonical : argument);
                        add_alias(argument, canonical_arguments[j]);
                        parameter = template_parameter_index(function_ti,
                            explicit_function->type_parameters[j]);
                        if (parameter < 0) die("invalid explicit template parameter map");
                        parameter_indices[j] = (size_t)parameter;
                    }
                    if (!argument_error) {
                        GenInstance *instance = add_instance(function_template_index,
                            parameter_indices, spellings,
                            (const char *const *)canonical_arguments, type_count);
                        set_instance_origin(instance, path, line);
                        request_instance_types(function_ti, explicit_function, instance);
                        (void)add_generic_use(name, instance->name, 0,
                                              NULL, NULL, NULL, 0U);
                        set_generic_use_origin(name, instance->name, path, line);
                        source_facts_add_constructor(context->facts,
                            function_template_index, name, parameter_indices,
                            spellings, (const char *const *)canonical_arguments,
                            type_count);
                    }
                }
                if (canonical_arguments != NULL) {
                    for (j = 0U; j < type_count; ++j) free(canonical_arguments[j]);
                }
                free(canonical_arguments);
                free(spellings);
                free(parameter_indices);
                free(argument_tokens);
                list_free(arguments, argument_count);
            } else {
            size_t template_index;
            if (generic_function_template(context->infos, context->template_count,
                                          name, &template_index)) {
                const TemplateFunction *function = find_template_function(
                    &context->infos[template_index], name);
                if (function != NULL) {
                    size_t k;
                    for (k = 0U; k < function->parameter_count; ++k) {
                        unsigned argument = (unsigned)k;
                        char *argument_path = NULL;
                        unsigned argument_start = 0U;
                        unsigned argument_end = 0U;
                        if (macro_argument_source_range(context->tu, cursor, argument,
                                                        &argument_path, &argument_start,
                                                        &argument_end)) {
                            clang_use_add(context->uses, name, path, offset,
                                          (size_t)line, argument,
                                          source_list_index_for_path(context->files,
                                                                     argument_path),
                                          argument_start, argument_end);
                            if (g_verbose) {
                                fprintf(stderr,
                                        "tmplgen: clang macro %s %s:%u arg%u=%u..%u\n",
                                        name, path, line, argument,
                                        argument_start, argument_end);
                            }
                        }
                        free(argument_path);
                    }
                }
            }
            }
        }
        free(name);
    }
    free(path);
    (void)column;
    return CXChildVisit_Recurse;
}

static enum CXChildVisitResult collect_clang_expression_type(CXCursor cursor,
                                                             CXCursor parent,
                                                             CXClientData client_data)
{
    ClangContext *context = (ClangContext *)client_data;
    CXSourceLocation location;
    CXFile source_file = NULL;
    unsigned line = 0U;
    unsigned column = 0U;
    unsigned offset = 0U;
    size_t source_file_index;
    size_t i;
    (void)parent;
    if (!clang_isExpression(clang_getCursorKind(cursor))) return CXChildVisit_Recurse;
    location = clang_getCursorLocation(cursor);
    clang_getExpansionLocation(location, &source_file, &line, &column, &offset);
    if (source_file == NULL) return CXChildVisit_Recurse;
    source_file_index = clang_file_index(context, source_file);
    if (source_file_index == (size_t)-1) return CXChildVisit_Recurse;
    for (i = 0U; i < context->uses->count; ++i) {
        ClangUse *use = &context->uses->items[i];
        CXSourceRange extent = clang_getCursorExtent(cursor);
        CXFile start_file = NULL;
        CXFile end_file = NULL;
        unsigned start = 0U, end = 0U, start_line = 0U, start_column = 0U;
        unsigned end_line = 0U, end_column = 0U;
        CXType expression_type;
        CXType canonical_type;
        if (use->type_spelling != NULL ||
            use->argument_file_index != source_file_index) continue;
        clang_getSpellingLocation(clang_getRangeStart(extent), &start_file,
                                  &start_line, &start_column, &start);
        clang_getSpellingLocation(clang_getRangeEnd(extent), &end_file,
                                  &end_line, &end_column, &end);
        if (start_file == NULL || end_file == NULL ||
            !clang_File_isEqual(start_file, end_file)) continue;
        if (start != use->argument_start || end != use->argument_end) continue;
        expression_type = clang_getCursorType(cursor);
        if (expression_type.kind == CXType_Invalid) continue;
        canonical_type = clang_getCanonicalType(expression_type);
        use->type_spelling = copy_cx_string(clang_getTypeSpelling(expression_type));
        use->canonical_spelling = copy_cx_string(clang_getTypeSpelling(canonical_type));
        if (g_verbose) {
            char *path = copy_cx_string(clang_getFileName(source_file));
            fprintf(stderr, "tmplgen: clang type %s:%u arg=%u..%u type=%s\n",
                    path, line, start, end, use->type_spelling);
            free(path);
        }
    }
    (void)line;
    (void)column;
    return CXChildVisit_Recurse;
}

static int infer_use_type_from_variable(ClangContext *context, ClangUse *use)
{
    const SourceFile *source;
    char *expression;
    const char *p;
    char *name;
    int address = 0;
    int dereference = 0;
    size_t i;
    const SourceVariable *variable = NULL;
    if (use->argument_file_index >= context->files->count ||
        use->argument_end <= use->argument_start) return 0;
    source = &context->files->items[use->argument_file_index];
    if ((size_t)use->argument_end > source->text.len) return 0;
    expression = copy_range(source->text.p + use->argument_start,
                            (size_t)(use->argument_end - use->argument_start));
    p = expression;
    while (isspace((unsigned char)*p)) ++p;
    if (*p == '&') {
        address = 1;
        ++p;
    } else if (*p == '*') {
        dereference = 1;
        ++p;
    }
    while (isspace((unsigned char)*p)) ++p;
    if (!is_ident_start(*p)) {
        free(expression);
        return 0;
    }
    {
        const char *start = p++;
        while (is_ident_char(*p)) ++p;
        name = copy_range(start, (size_t)(p - start));
    }
    while (isspace((unsigned char)*p)) ++p;
    if (*p != '\0') {
        free(name);
        free(expression);
        return 0;
    }
    for (i = 0U; i < context->variables.count; ++i) {
        const SourceVariable *candidate = &context->variables.items[i];
        if (!str_eq(candidate->name, name) ||
            candidate->file_index != use->argument_file_index ||
            candidate->offset > use->offset) continue;
        if (variable == NULL || candidate->offset > variable->offset) {
            variable = candidate;
        }
    }
    free(name);
    free(expression);
    if (variable == NULL) return 0;
    {
        Buf type, canonical;
        buf_init(&type);
        buf_init(&canonical);
        buf_puts(&type, variable->type_spelling);
        buf_puts(&canonical, variable->canonical_spelling);
        if (address) {
            buf_puts(&type, " *");
            buf_puts(&canonical, " *");
        } else if (dereference) {
            char *star = strrchr(type.p, '*');
            char *canonical_star = strrchr(canonical.p, '*');
            if (star != NULL) *star = '\0';
            if (canonical_star != NULL) *canonical_star = '\0';
        }
        use->type_spelling = normalize_spaces(type.p != NULL ? type.p : "");
        use->canonical_spelling = normalize_spaces(
            canonical.p != NULL ? canonical.p : "");
        buf_free(&type);
        buf_free(&canonical);
    }
    return use->type_spelling[0] != '\0';
}

static int parse_source_translation_unit(CXIndex index, const SourceFile *source,
                                         const Buf *bootstrap,
                                         CXTranslationUnit *out_tu,
                                         enum CXErrorCode *out_result)
{
    const char **arguments;
    int argument_count = (int)g_clang_arg_count + 2;
    int i;
    struct CXUnsavedFile *unsaved;
    char **unsaved_paths;
    OutputRegistry registry;
    size_t unsaved_count;
    size_t unsaved_index;
    CXTranslationUnit tu = NULL;
    enum CXErrorCode result;
    arguments = (const char **)malloc((size_t)argument_count * sizeof(*arguments));
    if (arguments == NULL) die("out of memory");
    arguments[0] = "-x";
    arguments[1] = "c";
    for (i = 0; i < (int)g_clang_arg_count; ++i) {
        arguments[i + 2] = g_clang_args[i];
    }
    /* Generated headers are replaced by stand-ins: the template bootstrap,
     * and for reflection.h its own stand-in; the main file by its (possibly
     * unsaved) text; an editor's unsaved file by its buffer. */
    output_registry_build(&registry);
    unsaved_count = registry.count + 2U;
    unsaved = (struct CXUnsavedFile *)calloc(unsaved_count, sizeof(*unsaved));
    unsaved_paths = (char **)calloc(unsaved_count, sizeof(*unsaved_paths));
    if (unsaved == NULL || unsaved_paths == NULL) die("out of memory");
    unsaved_count = 0U;
    for (unsaved_index = 0U; unsaved_index < registry.count; ++unsaved_index) {
        const OutputEntry *entry = &registry.items[unsaved_index];
        const Buf *text = entry->kind == OUTPUT_REFLECTION ?
            &g_reflection_bootstrap : bootstrap;
        if (!output_is_header(entry->kind)) continue;
        unsaved_paths[unsaved_count] = absolute_path(entry->path);
        unsaved[unsaved_count].Filename = unsaved_paths[unsaved_count];
        unsaved[unsaved_count].Contents = text->p != NULL ? text->p : "";
        unsaved[unsaved_count].Length = (unsigned long)text->len;
        ++unsaved_count;
    }
    output_registry_free(&registry);
    unsaved_paths[unsaved_count] = copy_string(source->path);
    unsaved[unsaved_count].Filename = unsaved_paths[unsaved_count];
    unsaved[unsaved_count].Contents = source->text.p != NULL ? source->text.p : "";
    unsaved[unsaved_count].Length = (unsigned long)source->text.len;
    ++unsaved_count;
#if defined(CLANGPLUGIN)
    if (g_unsaved_source_path != NULL &&
        !str_eq(g_unsaved_source_path, source->path)) {
        /* e.g. the editor's unsaved header included by this file */
        unsaved_paths[unsaved_count] = copy_string(g_unsaved_source_path);
        unsaved[unsaved_count].Filename = unsaved_paths[unsaved_count];
        unsaved[unsaved_count].Contents = g_unsaved_source_contents != NULL ?
            g_unsaved_source_contents : "";
        unsaved[unsaved_count].Length = (unsigned long)g_unsaved_source_length;
        ++unsaved_count;
    }
#endif
    result = clang_parseTranslationUnit2(index, source->path, arguments,
        argument_count, unsaved, (unsigned)unsaved_count,
        CXTranslationUnit_DetailedPreprocessingRecord |
            CXTranslationUnit_KeepGoing, &tu);
    free(arguments);
    for (unsaved_index = 0U; unsaved_index < unsaved_count; ++unsaved_index) {
        free(unsaved_paths[unsaved_index]);
    }
    free(unsaved_paths);
    free(unsaved);
    *out_result = result;
    if (result != CXError_Success || tu == NULL) {
        if (tu != NULL) clang_disposeTranslationUnit(tu);
        *out_tu = NULL;
        return 0;
    }
    *out_tu = tu;
    return 1;
}

static int analyze_source_translation_unit(CXTranslationUnit tu,
                                           int pass,
                                           const TemplateInfo *infos,
                                           size_t template_count,
                                           SourceList *files,
                                           ClangUseList *uses,
                                           SourceFacts *facts)
{
    ClangContext context;
    InclusionCapture capture;
    size_t unsaved_index;
    capture.all = &g_clang_dependencies;
    capture.local = g_tu_cache_enabled ? &facts->dependencies : NULL;
    if (g_cache_enabled || g_tu_cache_enabled) {
        clang_getInclusions(tu, cache_collect_source_inclusion, &capture);
    }
    memset(&context.variables, 0, sizeof(context.variables));
    context.scanned_files = (CXFile *)calloc(files->count,
                                              sizeof(*context.scanned_files));
    if (context.scanned_files == NULL && files->count != 0U) die("out of memory");
    context.scanned_file_count = files->count;
    for (unsaved_index = 0U; unsaved_index < files->count; ++unsaved_index) {
        context.scanned_files[unsaved_index] =
            clang_getFile(tu, files->items[unsaved_index].path);
    }
    context.tu = tu;
    context.infos = infos;
    context.template_count = template_count;
    context.files = files;
    context.uses = uses;
    context.facts = pass == 1 ? facts : NULL;
    context.pass = pass;
    context.failed = 0;
    memset(&context.enumtag_checks, 0, sizeof(context.enumtag_checks));
    clang_visitChildren(clang_getTranslationUnitCursor(tu),
                        collect_clang_source_cursor, &context);
    if (context.enumtag_checks.count != 0U) {
        size_t check_index;
        clang_visitChildren(clang_getTranslationUnitCursor(tu),
                            check_enumtag_arguments, &context);
        for (check_index = 0U; check_index < context.enumtag_checks.count; ++check_index) {
            const EnumTagCheck *check = &context.enumtag_checks.items[check_index];
            /* Unresolved arguments are left to the compiler. */
            if (check->is_enum || check->other_type == NULL) continue;
            report_error(check->path, check->line,
                         "enumtag_of() argument has type '%s', which is not an enum "
                         "type (C cannot tell an enum from its integer type, so the "
                         "macro alone would not reject it); use typetag_of() for "
                         "other values, or typetag(T) to name an enum type",
                         check->other_type);
            context.failed = 1;
        }
    }
    enumtag_check_list_free(&context.enumtag_checks);
    if (uses != NULL) {
        size_t use_index;
        clang_visitChildren(clang_getTranslationUnitCursor(tu),
                            collect_clang_expression_type, &context);
        for (use_index = 0U; use_index < uses->count; ++use_index) {
            (void)infer_use_type_from_variable(&context, &uses->items[use_index]);
        }
    }
    source_variable_list_free(&context.variables);
    free(context.scanned_files);
    return context.failed == 0;
}

void build_bootstrap_associations(size_t template_index,
                                         const TemplateInfo *ti,
                                         const TemplateFunction *function,
                                         AssocList *assocs)
{
    size_t i;
    for (i = 0U; i < g_generic_use_count; ++i) {
        GenericUse *use = &g_generic_uses[i];
        char *symbol;
        if (!str_eq(use->name, function->name) || !use->dispatch) continue;
        symbol = join_suffix(function->name, use->type_name);
        (void)assoc_add(assocs, use->dispatch_indices,
                        (const char *const *)use->dispatch_types,
                        (const char *const *)use->dispatch_canonicals,
                        use->dispatch_count, symbol, 1);
        free(symbol);
    }
    for (i = 0U; i < g_instance_count; ++i) {
        GenInstance *instance = &g_instances[i];
        unsigned *indices;
        char **types;
        char **keys;
        char *symbol;
        size_t j;
        if (instance->template_index != template_index ||
            !instance_matches_parameters(ti, instance, function->type_parameters,
                                         function->type_parameter_count) ||
            generic_use_matches(function->name, instance->name, 1)) continue;
        indices = (unsigned *)malloc(function->dispatch_count * sizeof(*indices));
        types = (char **)calloc(function->dispatch_count, sizeof(*types));
        keys = (char **)calloc(function->dispatch_count, sizeof(*keys));
        if (indices == NULL || types == NULL || keys == NULL) die("out of memory");
        for (j = 0U; j < function->dispatch_count; ++j) {
            Buf expected;
            buf_init(&expected);
            indices[j] = function->dispatch_indices[j];
            if (function->parameters[indices[j]].application.constructor != NULL) {
                rewrite_c_type(function->parameters[indices[j]].type, ti, instance,
                               &function->parameters[indices[j]].application,
                               &expected);
            } else {
                rewrite(function->parameters[indices[j]].type, ti, instance,
                        &expected);
            }
            types[j] = normalize_spaces(expected.p != NULL ? expected.p : "");
            keys[j] = copy_string(types[j]);
            buf_free(&expected);
        }
        symbol = join_suffix(function->name, instance->name);
        (void)assoc_add(assocs, indices, (const char *const *)types,
                        (const char *const *)keys, function->dispatch_count,
                        symbol, 1);
        free(symbol);
        for (j = 0U; j < function->dispatch_count; ++j) {
            free(types[j]);
            free(keys[j]);
        }
        free(indices);
        free(types);
        free(keys);
    }
}

static int process_clang_uses(ClangUseList *uses, const TemplateInfo *infos,
                              size_t template_count, int report_incomplete)
{
    unsigned char *processed;
    size_t i;
    int okay = 1;
    if (uses->count == 0U) return 1;
    processed = (unsigned char *)calloc(uses->count, sizeof(*processed));
    if (processed == NULL) die("out of memory");
    for (i = 0U; i < uses->count; ++i) {
        if (processed[i]) continue;
        if (!process_clang_use_group(uses, i, infos, template_count, processed,
                                     report_incomplete)) okay = 0;
    }
    free(processed);
    return okay;
}

GenInstance *project_instance(const TemplateInfo *ti,
                                     const GenInstance *source,
                                     char *const *parameter_names,
                                     size_t parameter_count)
{
    size_t *indices;
    const char **spellings;
    const char **canonicals;
    size_t i;
    GenInstance *result;
    if (parameter_count == 0U) {
        return add_instance(source->template_index, NULL, NULL, NULL, 0U);
    }
    indices = (size_t *)malloc(parameter_count * sizeof(*indices));
    spellings = (const char **)malloc(parameter_count * sizeof(*spellings));
    canonicals = (const char **)malloc(parameter_count * sizeof(*canonicals));
    if (indices == NULL || spellings == NULL || canonicals == NULL) die("out of memory");
    for (i = 0U; i < parameter_count; ++i) {
        int parameter = template_parameter_index(ti, parameter_names[i]);
        int argument;
        if (parameter < 0) {
            free(indices);
            free(spellings);
            free(canonicals);
            return NULL;
        }
        argument = instance_parameter_position(source, (size_t)parameter);
        if (argument < 0) {
            free(indices);
            free(spellings);
            free(canonicals);
            return NULL;
        }
        indices[i] = (size_t)parameter;
        spellings[i] = source->arguments[argument].c_type;
        canonicals[i] = source->arguments[argument].canonical;
    }
    result = add_instance(source->template_index, indices, spellings, canonicals,
                          parameter_count);
    set_instance_origin(result, source->origin_path, source->origin_line);
    free(indices);
    free(spellings);
    free(canonicals);
    return result;
}

static int source_has_template_function_call(const SourceFile *file,
                                             const TemplateInfo *infos,
                                             size_t template_count)
{
    size_t token_index;
    for (token_index = 0U; token_index + 1U < file->token_count; ++token_index) {
        size_t template_index;
        const char *token = file->tokens[token_index].text;
        if (file->tokens[token_index].kind != C_TOKEN_IDENT ||
            !token_is(file, token_index + 1U, "(")) continue;
        for (template_index = 0U; template_index < template_count;
             ++template_index) {
            size_t function_index;
            for (function_index = 0U;
                 function_index < infos[template_index].function_count;
                 ++function_index) {
                const TemplateFunction *function =
                    &infos[template_index].functions[function_index];
                size_t base_length = strlen(function->name);
                if (str_eq(token, function->name)) return 1;
                if (function->type_parameter_count != 0U &&
                    strlen(token) > base_length &&
                    strncmp(token, function->name, base_length) == 0 &&
                    token[base_length] == '_') return 1;
            }
        }
    }
    return 0;
}

static void source_mark_template_uses(const SourceFile *file,
                                      const TemplateInfo *infos,
                                      size_t template_count,
                                      unsigned char *used)
{
    size_t token_index;
    size_t i;
    for (token_index = 0U; token_index + 1U < file->token_count; ++token_index) {
        size_t template_index;
        const char *token = file->tokens[token_index].text;
        if (file->tokens[token_index].kind != C_TOKEN_IDENT ||
            !token_is(file, token_index + 1U, "(")) continue;
        for (template_index = 0U; template_index < template_count;
             ++template_index) {
            size_t function_index;
            for (function_index = 0U;
                 function_index < infos[template_index].function_count;
                 ++function_index) {
                const TemplateFunction *function =
                    &infos[template_index].functions[function_index];
                size_t base_length = strlen(function->name);
                if (str_eq(token, function->name) ||
                    (function->type_parameter_count != 0U &&
                     strlen(token) > base_length &&
                     strncmp(token, function->name, base_length) == 0 &&
                     token[base_length] == '_')) {
                    used[template_index] = 1U;
                }
            }
        }
    }
    for (i = 0U; i < file->facts.constructor_count; ++i) {
        size_t template_index = file->facts.constructors[i].template_index;
        if (template_index < template_count) used[template_index] = 1U;
    }
    for (i = 0U; i < file->facts.pass1_uses.count; ++i) {
        size_t template_index = 0U;
        if (generic_function_template(infos, template_count,
                                      file->facts.pass1_uses.items[i].name,
                                      &template_index)) {
            used[template_index] = 1U;
        }
    }
    for (;;) {
        int changed = 0;
        for (i = 0U; i < g_function_call_dependency_count; ++i) {
            const FunctionCallDependency *dependency =
                &g_function_call_dependencies[i];
            if (dependency->caller_template_index >= template_count ||
                dependency->callee_template_index >= template_count ||
                !used[dependency->caller_template_index] ||
                used[dependency->callee_template_index]) continue;
            used[dependency->callee_template_index] = 1U;
            changed = 1;
        }
        if (!changed) break;
    }
}

static int source_call_group_equal(const ClangUse *left, const ClangUse *right)
{
    return str_eq(left->name, right->name) && str_eq(left->path, right->path) &&
           left->offset == right->offset;
}

static void source_hash_specialization(unsigned long long *hash,
                                       const TemplateInfo *ti,
                                       const TemplateFunction *function,
                                       const GenInstance *instance)
{
    Buf return_type;
    char *body;
    char *specialized_calls;
    Buf specialized_body;
    size_t i;
    signature_mix(hash, instance->key, strlen(instance->key));
    buf_init(&return_type);
    if (function->return_application.constructor != NULL) {
        rewrite_c_type(function->return_type, ti, instance,
                       &function->return_application, &return_type);
    } else {
        rewrite(function->return_type, ti, instance, &return_type);
    }
    signature_mix(hash, return_type.p != NULL ? return_type.p : "",
                  return_type.len);
    buf_free(&return_type);
    for (i = 0U; i < function->parameter_count; ++i) {
        Buf parameter_type;
        buf_init(&parameter_type);
        if (function->parameters[i].application.constructor != NULL) {
            rewrite_c_type(function->parameters[i].type, ti, instance,
                           &function->parameters[i].application, &parameter_type);
        } else {
            rewrite(function->parameters[i].type, ti, instance, &parameter_type);
        }
        signature_mix(hash, parameter_type.p != NULL ? parameter_type.p : "",
                      parameter_type.len);
        buf_free(&parameter_type);
    }
    if (function->start_offset >= function->end_offset ||
        function->end_offset > ti->body.len) return;
    body = copy_range(ti->body.p + function->start_offset,
                      (size_t)(function->end_offset - function->start_offset));
    specialized_calls = specialize_function_calls(body, ti, instance, function, 0, 0);
    buf_init(&specialized_body);
    rewrite(specialized_calls, ti, instance, &specialized_body);
    signature_mix(hash, specialized_body.p != NULL ? specialized_body.p : "",
                  specialized_body.len);
    buf_free(&specialized_body);
    free(specialized_calls);
    free(body);
}

static unsigned long long source_facts_pass2_key(const SourceFile *source,
                                                  const TemplateInfo *infos,
                                                  size_t template_count,
                                                  const Buf *full_bootstrap)
{
    unsigned long long hash = 14695981039346656037ULL;
    char numeric[64];
    size_t i, j;
    unsigned char *used;
    int use_global_bootstrap = g_dispatch_backend == GEN_DISPATCH_NONE;
    if (!source_has_template_function_call(source, infos, template_count)) {
        return source->facts.pass1_key;
    }
    (void)snprintf(numeric, sizeof(numeric), "%016llx",
                   source->facts.pass1_key);
    signature_mix(&hash, numeric, strlen(numeric));
    used = (unsigned char *)calloc(template_count, sizeof(*used));
    if (used == NULL && template_count != 0U) die("out of memory");
    source_mark_template_uses(source, infos, template_count, used);
    for (i = 0U; i < template_count; ++i) {
        if (!used[i]) continue;
        signature_mix(&hash, infos[i].path, strlen(infos[i].path));
        signature_mix(&hash, infos[i].body.p != NULL ? infos[i].body.p : "",
                      infos[i].body.len);
    }
    free(used);
    for (i = 0U; i < source->facts.dependencies.count; ++i) {
        unsigned long long hash1, hash2;
        if (!cache_file_hash(source->facts.dependencies.items[i], &hash1, &hash2)) {
            use_global_bootstrap = 1;
            break;
        }
        signature_mix(&hash, source->facts.dependencies.items[i],
                      strlen(source->facts.dependencies.items[i]));
        (void)snprintf(numeric, sizeof(numeric), "%016llx:%016llx", hash1, hash2);
        signature_mix(&hash, numeric, strlen(numeric));
    }
    for (i = 0U; i < source->facts.constructor_count; ++i) {
        const SourceConstructorFact *fact = &source->facts.constructors[i];
        signature_mix(&hash, fact->functor, strlen(fact->functor));
        for (j = 0U; j < fact->argument_count; ++j) {
            signature_mix(&hash, fact->spellings[j], strlen(fact->spellings[j]));
            signature_mix(&hash, fact->canonicals[j], strlen(fact->canonicals[j]));
        }
    }
    if (use_global_bootstrap) {
        signature_mix(&hash, full_bootstrap->p != NULL ? full_bootstrap->p : "",
                      full_bootstrap->len);
        return hash;
    }
    for (i = 0U; i < source->facts.pass1_uses.count; ++i) {
        const ClangUse *call = &source->facts.pass1_uses.items[i];
        size_t template_index = 0U;
        const TemplateFunction *function;
        const char **call_canonicals;
        unsigned char *assigned;
        int complete = 1;
        int matched = 0;
        size_t prior;
        if (!generic_function_template(infos, template_count, call->name,
                                       &template_index)) continue;
        function = find_template_function(&infos[template_index], call->name);
        if (function == NULL || function->dispatch_count == 0U) {
            use_global_bootstrap = 1;
            break;
        }
        for (prior = 0U; prior < i; ++prior) {
            if (source_call_group_equal(call,
                                        &source->facts.pass1_uses.items[prior])) break;
        }
        if (prior != i) continue;
        call_canonicals = (const char **)calloc(function->dispatch_count,
                                                sizeof(*call_canonicals));
        assigned = (unsigned char *)calloc(function->dispatch_count,
                                            sizeof(*assigned));
        if (call_canonicals == NULL || assigned == NULL) die("out of memory");
        for (j = i; j < source->facts.pass1_uses.count; ++j) {
            const ClangUse *use = &source->facts.pass1_uses.items[j];
            size_t dispatch_index;
            if (!source_call_group_equal(call, use)) continue;
            for (dispatch_index = 0U; dispatch_index < function->dispatch_count;
                 ++dispatch_index) {
                if (function->dispatch_indices[dispatch_index] != use->argument_index) {
                    continue;
                }
                if (use->canonical_spelling == NULL ||
                    use->canonical_spelling[0] == '\0') {
                    complete = 0;
                } else {
                    call_canonicals[dispatch_index] = use->canonical_spelling;
                    assigned[dispatch_index] = 1U;
                }
            }
        }
        for (j = 0U; j < function->dispatch_count; ++j) {
            if (!assigned[j]) complete = 0;
        }
        if (!complete) {
            free(call_canonicals); free(assigned);
            use_global_bootstrap = 1;
            break;
        }
        signature_mix(&hash, call->name, strlen(call->name));
        for (j = 0U; j < function->dispatch_count; ++j) {
            signature_mix(&hash, call_canonicals[j], strlen(call_canonicals[j]));
        }
        for (j = 0U; j < g_generic_use_count; ++j) {
            const GenericUse *generic = &g_generic_uses[j];
            size_t dispatch_index;
            GenInstance *instance;
            size_t *parameter_indices;
            size_t parameter_index;
            if (!generic->dispatch || !str_eq(generic->name, call->name) ||
                generic->dispatch_count != function->dispatch_count) continue;
            for (dispatch_index = 0U;
                 dispatch_index < function->dispatch_count; ++dispatch_index) {
                if (generic->dispatch_indices[dispatch_index] !=
                        function->dispatch_indices[dispatch_index] ||
                    !str_eq(generic->dispatch_canonicals[dispatch_index],
                            call_canonicals[dispatch_index])) break;
            }
            if (dispatch_index != function->dispatch_count) continue;
            matched = 1;
            signature_mix(&hash, generic->type_name, strlen(generic->type_name));
            parameter_indices = (size_t *)malloc(
                function->type_parameter_count * sizeof(*parameter_indices));
            if (parameter_indices == NULL) die("out of memory");
            for (parameter_index = 0U;
                 parameter_index < function->type_parameter_count;
                 ++parameter_index) {
                int mapped = template_parameter_index(&infos[template_index],
                    function->type_parameters[parameter_index]);
                if (mapped < 0) die("invalid pass-two cache parameter map");
                parameter_indices[parameter_index] = (size_t)mapped;
            }
            instance = find_instance(template_index, generic->type_name,
                                     parameter_indices,
                                     function->type_parameter_count);
            free(parameter_indices);
            if (instance == NULL) {
                free(call_canonicals); free(assigned);
                use_global_bootstrap = 1;
                break;
            }
            source_hash_specialization(&hash, &infos[template_index],
                                       function, instance);
        }
        free(call_canonicals); free(assigned);
        if (use_global_bootstrap) break;
        if (!matched) {
            signature_mix(&hash, "no-matching-dispatch", 20U);
        }
    }
    if (use_global_bootstrap) {
        signature_mix(&hash, full_bootstrap->p != NULL ? full_bootstrap->p : "",
                      full_bootstrap->len);
    }
    return hash;
}

static size_t source_parse_worker_count(size_t source_count)
{
#if GEN_PARALLEL_PARSE
    long online = sysconf(_SC_NPROCESSORS_ONLN);
    size_t count = online > 0 ? (size_t)online : 1U;
    if (count > 16U) count = 16U;
    if (count > source_count) count = source_count;
    return count == 0U ? 1U : count;
#else
    (void)source_count;
    return 1U;
#endif
}

#if GEN_PARALLEL_PARSE
typedef struct {
    const CXIndex *indices;
    size_t worker;
    size_t worker_count;
    const Buf *bootstrap;
    const SourceList *files;
    const size_t *parse_order;
    size_t parse_count;
    CXTranslationUnit *tus;
    enum CXErrorCode *results;
    unsigned char *done;
} ParseWorker;

static void *parse_worker_main(void *data)
{
    ParseWorker *worker = (ParseWorker *)data;
    size_t position;
    CXIndex index = worker->indices[worker->worker % worker->worker_count];
    for (position = worker->worker; position < worker->parse_count;
         position += worker->worker_count) {
        size_t source_index = worker->parse_order[position];
        SourceFile *source = &worker->files->items[source_index];
        (void)parse_source_translation_unit(index, source, worker->bootstrap,
                                            &worker->tus[source_index],
                                            &worker->results[source_index]);
        worker->done[source_index] = 1U;
    }
    return NULL;
}
#endif

static void parse_source_batch(const CXIndex *indices, size_t worker_count,
                               const Buf *bootstrap, const SourceList *files,
                               const size_t *parse_order, size_t parse_count,
                               CXTranslationUnit *tus,
                               enum CXErrorCode *results)
{
    size_t count = parse_count;
    size_t i;
#if GEN_PARALLEL_PARSE
    unsigned char *done = (unsigned char *)calloc(files->count, 1U);
    pthread_t *threads = NULL;
    ParseWorker *workers = NULL;
    size_t launched = 0U;
    size_t actual = worker_count;
    if (done == NULL && files->count != 0U) die("out of memory");
    if (count < actual) actual = count;
    if (actual > 1U) {
        threads = (pthread_t *)calloc(actual, sizeof(*threads));
        workers = (ParseWorker *)calloc(actual, sizeof(*workers));
        if (threads == NULL || workers == NULL) die("out of memory");
        for (i = 1U; i < actual; ++i) {
            workers[i].indices = indices;
            workers[i].worker = i;
            workers[i].worker_count = actual;
            workers[i].bootstrap = bootstrap;
            workers[i].files = files;
            workers[i].parse_order = parse_order;
            workers[i].parse_count = parse_count;
            workers[i].tus = tus;
            workers[i].results = results;
            workers[i].done = done;
            if (pthread_create(&threads[launched], NULL, parse_worker_main,
                               &workers[i]) != 0) break;
            ++launched;
        }
        if (actual > 1U && launched == 0U) actual = 1U;
        workers[0].indices = indices;
        workers[0].worker = 0U;
        workers[0].worker_count = actual;
        workers[0].bootstrap = bootstrap;
        workers[0].files = files;
        workers[0].parse_order = parse_order;
        workers[0].parse_count = parse_count;
        workers[0].tus = tus;
        workers[0].results = results;
        workers[0].done = done;
        (void)parse_worker_main(&workers[0]);
        for (i = 0U; i < launched; ++i) (void)pthread_join(threads[i], NULL);
    }
    for (i = 0U; i < count; ++i) {
        size_t source_index = parse_order[i];
        if (done[source_index]) continue;
        (void)parse_source_translation_unit(indices[0],
            &files->items[source_index], bootstrap, &tus[source_index],
            &results[source_index]);
        done[source_index] = 1U;
    }
    free(workers);
    free(threads);
    free(done);
#else
    (void)indices;
    (void)worker_count;
    for (i = 0U; i < count; ++i) {
        size_t source_index = parse_order[i];
        (void)parse_source_translation_unit(indices[0],
            &files->items[source_index], bootstrap, &tus[source_index],
            &results[source_index]);
    }
#endif
}

int discover_source_usages(const TemplateInfo *infos, size_t template_count,
                                  SourceList *files)
{
    CXIndex index;
    CXIndex *indices;
    Buf bootstrap;
    size_t i;
    size_t worker_count;
    size_t batch_capacity;
    size_t batch_start;
    unsigned char *need_parse;
    size_t *parse_order;
    CXTranslationUnit *tus;
    enum CXErrorCode *parse_results;
    int success = 1;
    if (!scan_source_directory(files, g_source_root)) return 0;
    /* The directory scan follows readdir order, which is filesystem
     * dependent. Generation must not depend on it, and the input signature
     * already sorts the same list. */
    if (files->count > 1U) {
        qsort(files->items, files->count, sizeof(*files->items),
              source_file_path_compare);
    }
    for (i = 0U; i < files->count; ++i) tokenize_source(&files->items[i]);
    worker_count = source_parse_worker_count(files->count);
    batch_capacity = worker_count * 4U;
    if (batch_capacity < worker_count) die("too many source files");
    if (files->count != 0U && batch_capacity > files->count) {
        batch_capacity = files->count;
    }
    indices = (CXIndex *)calloc(worker_count, sizeof(*indices));
    need_parse = (unsigned char *)calloc(files->count, 1U);
    parse_order = (size_t *)malloc(batch_capacity * sizeof(*parse_order));
    tus = (CXTranslationUnit *)calloc(files->count, sizeof(*tus));
    parse_results = (enum CXErrorCode *)calloc(files->count,
                                                sizeof(*parse_results));
    if (indices == NULL || (files->count != 0U && need_parse == NULL) ||
        parse_order == NULL || (files->count != 0U && tus == NULL) ||
        (files->count != 0U && parse_results == NULL)) {
        die("out of memory");
    }
    index = clang_createIndex(0, 0);
    if (index == NULL) die("libclang could not create an index");
    indices[0] = index;
    for (i = 1U; i < worker_count; ++i) {
        indices[i] = clang_createIndex(0, 0);
        if (indices[i] == NULL) die("libclang could not create an index");
    }
    buf_init(&bootstrap);
    emit_bootstrap_header(&bootstrap, infos, template_count, 0);
    batch_start = 0U;
    while (batch_start < files->count) {
        size_t batch_end = batch_start + batch_capacity;
        size_t parse_count = 0U;
        if (batch_end > files->count) batch_end = files->count;
        memset(need_parse + batch_start, 0, batch_end - batch_start);
        for (i = batch_start; i < batch_end; ++i) {
            SourceFile *source = &files->items[i];
            SourceFacts *facts = &source->facts;
            unsigned long long pass1_key = source_facts_key(source, &bootstrap, 1);
            if (g_tu_cache_enabled && !facts->loaded) {
                (void)source_facts_load(source, files);
            }
            if (!g_tu_cache_enabled || !facts->loaded || !facts->pass1_valid ||
                facts->pass1_key != pass1_key) {
                source_facts_reset(facts);
                facts->pass1_key = pass1_key;
                need_parse[i] = 1U;
                parse_order[parse_count++] = i;
                tus[i] = NULL;
                parse_results[i] = CXError_Failure;
            }
        }
        parse_source_batch(indices, worker_count, &bootstrap, files,
                           parse_order, parse_count, tus, parse_results);
        for (i = batch_start; i < batch_end; ++i) {
            SourceFile *source = &files->items[i];
            SourceFacts *facts = &source->facts;
            int pass1_ok = 1;
            if (need_parse[i]) {
                if (tus[i] == NULL) {
                    report_error(source->path, 0U,
                        "libclang failed to parse this source file (%d)",
                        (int)parse_results[i]);
                    pass1_ok = 0;
                } else {
                    pass1_ok = analyze_source_translation_unit(tus[i], 1, infos,
                        template_count, files, &facts->pass1_uses, facts);
                    clang_disposeTranslationUnit(tus[i]);
                    tus[i] = NULL;
                }
                facts->pass1_valid = pass1_ok;
                facts->loaded = g_tu_cache_enabled;
                if (!pass1_ok) success = 0;
            } else if (g_verbose) {
                fprintf(stderr, "tmplgen: reused source facts pass 1 for %s\n",
                        source->path);
            }
            if (!pass1_ok) continue;
            source_facts_apply(facts);
            (void)process_clang_uses(&facts->pass1_uses, infos, template_count, 0);
        }
        batch_start = batch_end;
    }
    buf_free(&bootstrap);
    if (success) {
        for (i = 0U; i < files->count; ++i) {
            collect_suffix_api_uses(&files->items[i], infos, template_count);
        }
        expand_dependencies_to_fixpoint(infos, template_count, 0);
        /* Errors are reported again (once) after the second pass. */
        (void)compute_instance_requirements();
        emit_bootstrap_header(&bootstrap, infos, template_count, 1);
        batch_start = 0U;
        while (batch_start < files->count) {
            size_t batch_end = batch_start + batch_capacity;
            size_t parse_count = 0U;
            if (batch_end > files->count) batch_end = files->count;
            memset(need_parse + batch_start, 0, batch_end - batch_start);
            for (i = batch_start; i < batch_end; ++i) {
                SourceFile *source = &files->items[i];
                SourceFacts *facts = &source->facts;
                unsigned long long pass2_key = source_facts_pass2_key(
                    source, infos, template_count, &bootstrap);
                int needs_pass2 = source_has_template_function_call(
                    source, infos, template_count);
                if (!needs_pass2) {
                    clang_use_list_free(&facts->pass2_uses);
                    facts->pass2_key = pass2_key;
                    facts->pass2_valid = 1;
                    facts->loaded = g_tu_cache_enabled;
                } else if (!g_tu_cache_enabled || !facts->loaded ||
                    !facts->pass2_valid || facts->pass2_key != pass2_key) {
                    clang_use_list_free(&facts->pass2_uses);
                    facts->pass2_key = pass2_key;
                    need_parse[i] = 1U;
                    parse_order[parse_count++] = i;
                    tus[i] = NULL;
                    parse_results[i] = CXError_Failure;
                }
            }
            parse_source_batch(indices, worker_count, &bootstrap, files,
                               parse_order, parse_count, tus, parse_results);
            for (i = batch_start; i < batch_end; ++i) {
                SourceFile *source = &files->items[i];
                SourceFacts *facts = &source->facts;
                int pass2_ok = 1;
                if (need_parse[i]) {
                    if (tus[i] == NULL) {
                        report_error(source->path, 0U,
                            "libclang failed to parse this source file (%d)",
                            (int)parse_results[i]);
                        pass2_ok = 0;
                    } else {
                        pass2_ok = analyze_source_translation_unit(tus[i], 2,
                            infos, template_count, files, &facts->pass2_uses,
                            facts);
                        clang_disposeTranslationUnit(tus[i]);
                        tus[i] = NULL;
                    }
                    facts->pass2_valid = pass2_ok;
                    facts->loaded = g_tu_cache_enabled;
                    if (!pass2_ok) success = 0;
                } else if (g_verbose && facts->pass2_valid &&
                           source_has_template_function_call(source, infos,
                                                             template_count)) {
                    fprintf(stderr, "tmplgen: reused source facts pass 2 for %s\n",
                            source->path);
                }
                if (!pass2_ok) continue;
                if (!process_clang_uses(&facts->pass2_uses, infos,
                                        template_count, 1)) {
                    /* Inference warnings do not stop source discovery. */
                }
            }
            batch_start = batch_end;
        }
        expand_dependencies_to_fixpoint(infos, template_count, 1);
        if (success && g_tu_cache_enabled) {
            for (i = 0U; i < files->count; ++i) {
                source_facts_save(&files->items[i], files);
            }
        }
    }
    buf_free(&bootstrap);
    for (i = 0U; i < worker_count; ++i) clang_disposeIndex(indices[i]);
    free(indices);
    free(need_parse);
    free(parse_order);
    free(tus);
    free(parse_results);
    return success;
}

void clear_discovery_state(void)
{
    size_t i;
    for (i = 0U; i < g_instance_count; ++i) {
        size_t j;
        free(g_instances[i].name);
        free(g_instances[i].key);
        free(g_instances[i].origin_path);
        free(g_instances[i].parameter_indices);
        for (j = 0U; j < g_instances[i].argument_count; ++j) {
            free(g_instances[i].arguments[j].c_type);
            free(g_instances[i].arguments[j].canonical);
        }
        free(g_instances[i].arguments);
    }
    free(g_instances);
    g_instances = NULL;
    g_instance_count = 0U;
    g_instance_cap = 0U;
    for (i = 0U; i < g_type_use_count; ++i) {
        free(g_type_uses[i].functor);
        free(g_type_uses[i].type_name);
    }
    free(g_type_uses);
    g_type_uses = NULL;
    g_type_use_count = 0U;
    g_type_use_cap = 0U;
    for (i = 0U; i < g_generic_use_count; ++i) {
        size_t j;
        free(g_generic_uses[i].name);
        free(g_generic_uses[i].type_name);
        free(g_generic_uses[i].origin_path);
        for (j = 0U; j < g_generic_uses[i].dispatch_count; ++j) {
            free(g_generic_uses[i].dispatch_types[j]);
            free(g_generic_uses[i].dispatch_canonicals[j]);
        }
        free(g_generic_uses[i].dispatch_indices);
        free(g_generic_uses[i].dispatch_types);
        free(g_generic_uses[i].dispatch_canonicals);
    }
    free(g_generic_uses);
    g_generic_uses = NULL;
    g_generic_use_count = 0U;
    g_generic_use_cap = 0U;
    for (i = 0U; i < g_type_variable_mismatch_count; ++i) {
        free(g_type_variable_mismatches[i].path);
        free(g_type_variable_mismatches[i].function_name);
        free(g_type_variable_mismatches[i].parameter_name);
        free(g_type_variable_mismatches[i].first_type);
        free(g_type_variable_mismatches[i].second_type);
    }
    free(g_type_variable_mismatches);
    g_type_variable_mismatches = NULL;
    g_type_variable_mismatch_count = 0U;
    g_type_variable_mismatch_cap = 0U;
    for (i = 0U; i < g_alias_count; ++i) {
        free(g_aliases[i].name);
        free(g_aliases[i].canonical);
    }
    free(g_aliases);
    g_aliases = NULL;
    g_alias_count = 0U;
    g_alias_cap = 0U;
    type_origin_list_free(g_type_origins, g_type_origin_count);
    g_type_origins = NULL;
    g_type_origin_count = 0U;
    g_type_origin_cap = 0U;
    list_free(g_instance_requirements, g_instance_requirement_count);
    g_instance_requirements = NULL;
    g_instance_requirement_count = 0U;
    for (i = 0U; i < g_function_call_dependency_count; ++i) {
        free(g_function_call_dependencies[i].caller_function);
        free(g_function_call_dependencies[i].caller_instance);
        free(g_function_call_dependencies[i].callee_function);
        free(g_function_call_dependencies[i].callee_instance);
    }
    free(g_function_call_dependencies);
    g_function_call_dependencies = NULL;
    g_function_call_dependency_count = 0U;
    g_function_call_dependency_cap = 0U;
    for (i = 0U; i < g_structural_schema_count; ++i) {
        free(g_structural_schemas[i].name);
        list_free(g_structural_schemas[i].fields,
                  g_structural_schemas[i].field_count);
    }
    free(g_structural_schemas);
    g_structural_schemas = NULL;
    g_structural_schema_count = 0U;
    g_structural_schema_cap = 0U;
}
