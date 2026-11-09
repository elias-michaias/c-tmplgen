/* Template annotation parsing and declaration analysis. */
#include "internal.h"


static void cursor_expansion_offsets(CXCursor cursor, unsigned *start_offset,
                                     unsigned *end_offset, unsigned *start_line)
{
    CXSourceRange extent = clang_getCursorExtent(cursor);
    CXFile start_file = NULL, end_file = NULL;
    unsigned end_line = 0U, column = 0U;
    clang_getExpansionLocation(clang_getRangeStart(extent), &start_file,
                               start_line, &column, start_offset);
    clang_getExpansionLocation(clang_getRangeEnd(extent), &end_file,
                               &end_line, &column, end_offset);
}

static int template_has_direct_tag(const TemplateInfo *ti, const char *tag,
                                   int want_union)
{
    SourceFile source;
    size_t brace_depth = 0U;
    size_t i;
    int in_typedef = 0;
    int found = 0;
    memset(&source, 0, sizeof(source));
    buf_init(&source.text);
    buf_puts(&source.text, ti->body.p != NULL ? ti->body.p : "");
    tokenize_source(&source);
    for (i = 0U; i < source.token_count; ++i) {
        const char *token = source.tokens[i].text;
        if (brace_depth == 0U && str_eq(token, "typedef")) in_typedef = 1;
        else if (brace_depth == 0U && (str_eq(token, "struct") ||
                                       str_eq(token, "union")) &&
                 (want_union ? str_eq(token, "union") : str_eq(token, "struct")) &&
                 !in_typedef && i + 2U < source.token_count &&
                 str_eq(source.tokens[i + 1U].text, tag) &&
                 str_eq(source.tokens[i + 2U].text, "{")) {
            found = 1;
            break;
        }
        if (str_eq(token, "{")) ++brace_depth;
        else if (str_eq(token, "}") && brace_depth != 0U) --brace_depth;
        else if (str_eq(token, ";") && brace_depth == 0U) in_typedef = 0;
    }
    source_file_free(&source);
    return found;
}

static void emit_template_analysis_functors(Buf *out, const TemplateInfo *ti)
{
    SourceFile source;
    char **emitted = NULL;
    size_t emitted_count = 0U, emitted_cap = 0U;
    size_t i;
    memset(&source, 0, sizeof(source));
    buf_init(&source.text);
    buf_puts(&source.text, ti->body.p != NULL ? ti->body.p : "");
    tokenize_source(&source);
    for (i = 0U; i < source.token_count; ++i) {
        const char *token = source.tokens[i].text;
        if ((str_eq(token, "struct") || str_eq(token, "union")) &&
            i + 1U < source.token_count &&
            source.tokens[i + 1U].kind == C_TOKEN_IDENT &&
            template_has_direct_tag(ti, source.tokens[i + 1U].text,
                                    str_eq(token, "union"))) {
            const char *tag = source.tokens[i + 1U].text;
            if (!list_contains(emitted, emitted_count, tag)) {
                list_add(&emitted, &emitted_count, &emitted_cap, tag);
                buf_puts(out, "#define ");
                buf_puts(out, tag);
                buf_puts(out, "(...) ");
                buf_puts(out, token);
                buf_putc(out, ' ');
                buf_puts(out, tag);
                buf_putc(out, '\n');
            }
        } else if (str_eq(token, "typedef")) {
            size_t j = i + 1U;
            size_t brace_depth = 0U;
            const char *alias = NULL;
            for (; j < source.token_count; ++j) {
                const char *part = source.tokens[j].text;
                if (str_eq(part, "{")) ++brace_depth;
                else if (str_eq(part, "}")) {
                    if (brace_depth != 0U) --brace_depth;
                } else if (str_eq(part, ";") && brace_depth == 0U) {
                    break;
                } else if (brace_depth == 0U &&
                           source.tokens[j].kind == C_TOKEN_IDENT) {
                    alias = part;
                }
            }
            if (alias != NULL && !list_contains(emitted, emitted_count, alias)) {
                list_add(&emitted, &emitted_count, &emitted_cap, alias);
                buf_puts(out, "#define ");
                buf_puts(out, alias);
                buf_puts(out, "(...) ");
                buf_puts(out, alias);
                buf_putc(out, '\n');
            }
            i = j;
        }
    }
    if (emitted_count != 0U) buf_putc(out, '\n');
    list_free(emitted, emitted_count);
    source_file_free(&source);
}

static void collect_type_spelling_parameters(const TemplateInfo *ti,
                                             const char *type_spelling,
                                             char ***names, size_t *count,
                                             size_t *capacity)
{
    size_t i;
    for (i = 0U; i < ti->parameter_count; ++i) {
        if (identifier_line_in_code(type_spelling, ti->parameter_names[i]) != 0U &&
            !list_contains(*names, *count, ti->parameter_names[i])) {
            list_add(names, count, capacity, ti->parameter_names[i]);
        }
    }
    for (i = 0U; i < ti->constructor_count; ++i) {
        size_t j;
        if (identifier_line_in_code(type_spelling, ti->constructors[i].name) == 0U) continue;
        for (j = 0U; j < ti->constructors[i].parameter_count; ++j) {
            const char *parameter = ti->constructors[i].parameter_names[j];
            if (!list_contains(*names, *count, parameter)) {
                list_add(names, count, capacity, parameter);
            }
        }
    }
}

static unsigned template_line_at_offset(const TemplateInfo *ti, unsigned offset)
{
    size_t i;
    unsigned line = 1U;
    size_t end = offset;
    if (end > ti->body.len) end = ti->body.len;
    for (i = 0U; i < end; ++i) {
        if (ti->body.p[i] == '\n') ++line;
    }
    return line;
}

static int cursor_find_type_application(CXCursor cursor, const TemplateInfo *ti,
                                        unsigned offset_bias,
                                        const char *stop_before,
                                        TemplateApplication *application)
{
    unsigned start = 0U, end = 0U, line = 0U;
    size_t i;
    int found = 0;
    cursor_expansion_offsets(cursor, &start, &end, &line);
    (void)line;
    if (start >= offset_bias) start -= offset_bias;
    if (end >= offset_bias) end -= offset_bias;
    if ((size_t)end > ti->body.len || start > end) return 0;
    i = (size_t)start;
    while (i < (size_t)end) {
        size_t name_start;
        size_t open;
        char *name;
        const TemplateType *constructor;
        size_t argument_end = 0U;
        if (!is_ident_start(ti->body.p[i])) {
            ++i;
            continue;
        }
        name_start = i++;
        while (i < (size_t)end && is_ident_char(ti->body.p[i])) ++i;
        name = copy_range(ti->body.p + name_start, i - name_start);
        if (stop_before != NULL && str_eq(name, stop_before)) {
            free(name);
            break;
        }
        constructor = find_template_type(ti, name);
        if (constructor == NULL) {
            int parameter = template_parameter_index(ti, name);
            if (parameter < 0 ||
                !template_parameter_is_constructor(ti, (size_t)parameter)) {
                free(name);
                continue;
            }
        }
        open = i;
        while (open < (size_t)end && isspace((unsigned char)ti->body.p[open])) ++open;
        if (open >= (size_t)end || ti->body.p[open] != '(') {
            free(name);
            continue;
        }
        template_application_free(application);
        application->constructor = name;
        found = parse_text_type_arguments(ti->body.p, open,
            &application->arguments, &application->argument_count,
            &argument_end);
        (void)argument_end;
        if (found) {
            application->argument_cap = application->argument_count;
            break;
        }
        template_application_free(application);
    }
    return found;
}

static void collect_application_parameters(const TemplateInfo *ti,
                                           const TemplateApplication *application,
                                           char ***names, size_t *count,
                                           size_t *capacity)
{
    size_t i;
    for (i = 0U; i < application->argument_count; ++i) {
        size_t parameter;
        for (parameter = 0U; parameter < ti->parameter_count; ++parameter) {
            if (identifier_line_in_code(application->arguments[i],
                                        ti->parameter_names[parameter]) != 0U &&
                !list_contains(*names, *count, ti->parameter_names[parameter])) {
                list_add(names, count, capacity, ti->parameter_names[parameter]);
            }
        }
    }
}

static TemplateAnnotation *take_template_annotation(TemplateAnalysis *analysis,
                                                     unsigned declaration_offset)
{
    TemplateInfo *ti = analysis->info;
    while (ti->next_annotation < ti->annotation_count) {
        TemplateAnnotation *annotation =
            &ti->annotations[ti->next_annotation];
        if (annotation->used) {
            ++ti->next_annotation;
            continue;
        }
        if (annotation->offset >= declaration_offset) return NULL;
        ++ti->next_annotation;
        annotation->used = 1;
        return annotation;
    }
    return NULL;
}

static int declaration_parameters_are_listed(TemplateAnalysis *analysis,
                                              const char *name, unsigned line,
                                              char **declared, size_t declared_count,
                                              char **dependencies,
                                              size_t dependency_count)
{
    size_t i;
    int valid = 1;
    for (i = 0U; i < dependency_count; ++i) {
        if (!list_contains(declared, declared_count, dependencies[i])) {
            report_error(analysis->info->path, line,
                         "declaration '%s' uses template parameter '%s' without listing it in its @template directive",
                         name, dependencies[i]);
            analysis->failed = 1;
            valid = 0;
        }
    }
    return valid;
}

static void validate_where_constraint_bindings(TemplateAnalysis *analysis,
                                               const char *declaration_name,
                                               unsigned line,
                                               const TemplateAnnotation *annotation,
                                               char **dependencies,
                                               size_t dependency_count)
{
    size_t i, j;
    const TemplateInfo *ti = analysis->info;
    if (annotation == NULL) return;
    for (i = 0U; i < annotation->constraint_count; ++i) {
        const TemplateConstraint *constraint = &annotation->constraints[i];
        if (constraint->parameter_name[0] != '\0') continue;
        for (j = 0U; j < constraint->expression_count; ++j) {
            size_t parameter_index;
            for (parameter_index = 0U; parameter_index < ti->parameter_count;
                 ++parameter_index) {
                const char *parameter = ti->parameter_names[parameter_index];
                if (identifier_line_in_code(constraint->expressions[j], parameter) == 0U) {
                    continue;
                }
                if (!list_contains(annotation->parameter_names,
                                   annotation->parameter_count, parameter)) {
                    report_error(ti->path, line,
                        "@where on '%s' refers to '%s' which is not declared in its @template block",
                        declaration_name, parameter);
                    analysis->failed = 1;
                } else if (!list_contains(dependencies, dependency_count, parameter)) {
                    report_error(ti->path, line,
                        "@where on '%s' refers to '%s', which cannot be inferred from the declaration's types",
                        declaration_name, parameter);
                    analysis->failed = 1;
                }
            }
        }
    }
}

static void copy_declared_parameters(char ***target, size_t *target_count,
                                     size_t *target_cap,
                                     const TemplateAnnotation *annotation)
{
    size_t i;
    if (annotation == NULL) return;
    for (i = 0U; i < annotation->parameter_count; ++i) {
        list_add(target, target_count, target_cap,
                 annotation->parameter_names[i]);
    }
}

static TemplateAnnotation *append_template_annotation(TemplateInfo *ti,
                                                      size_t line_number);

static TraitInfo *trait_at_offset(TemplateInfo *ti, unsigned offset)
{
    size_t i;
    for (i = 0U; i < ti->trait_count; ++i) {
        TraitInfo *trait = &ti->traits[i];
        if (offset >= trait->start_offset && offset <= trait->end_offset) {
            return trait;
        }
    }
    return NULL;
}

static TraitImpl *trait_impl_at_offset(TemplateInfo *ti, unsigned offset)
{
    size_t i;
    for (i = 0U; i < ti->trait_impl_count; ++i) {
        TraitImpl *impl = &ti->trait_impls[i];
        if (offset >= impl->start_offset && offset <= impl->end_offset) {
            return impl;
        }
    }
    return NULL;
}

static TemplateAnnotation *trait_method_annotation(TemplateInfo *ti,
                                                   const TraitInfo *trait,
                                                   size_t line_number)
{
    TemplateAnnotation *annotation = append_template_annotation(ti, line_number);
    size_t i;
    annotation->parameter_names = (char **)calloc(trait->parameter_count,
                                                  sizeof(char *));
    annotation->parameter_kinds = (TemplateParameterKind *)calloc(
        trait->parameter_count, sizeof(TemplateParameterKind));
    if (trait->parameter_count != 0U &&
        (annotation->parameter_names == NULL ||
         annotation->parameter_kinds == NULL)) {
        die("out of memory");
    }
    for (i = 0U; i < trait->parameter_count; ++i) {
        annotation->parameter_names[i] = copy_string(trait->parameters[i]);
    }
    annotation->parameter_count = trait->parameter_count;
    annotation->parameter_cap = trait->parameter_count;
    annotation->used = 1;
    return annotation;
}

/* Builds the implicit annotation for a method declared inside an `@impl`
 * block from the impl's free type variables and their bounds. */
static TemplateAnnotation *trait_impl_method_annotation(TemplateInfo *ti,
                                                        const TraitImpl *impl,
                                                        size_t line_number)
{
    TemplateAnnotation *annotation = append_template_annotation(ti, line_number);
    size_t i;
    annotation->parameter_names = (char **)calloc(impl->type_parameter_count,
                                                  sizeof(char *));
    annotation->parameter_kinds = (TemplateParameterKind *)calloc(
        impl->type_parameter_count, sizeof(TemplateParameterKind));
    if (impl->type_parameter_count != 0U &&
        (annotation->parameter_names == NULL ||
         annotation->parameter_kinds == NULL)) {
        die("out of memory");
    }
    for (i = 0U; i < impl->type_parameter_count; ++i) {
        annotation->parameter_names[i] = copy_string(impl->type_parameters[i]);
    }
    annotation->parameter_count = impl->type_parameter_count;
    annotation->parameter_cap = impl->type_parameter_count;
    copy_template_constraints(&annotation->constraints,
                              &annotation->constraint_count,
                              &annotation->constraint_cap,
                              impl->constraints, impl->constraint_count);
    annotation->used = 1;
    return annotation;
}

static char *trait_method_name(const char *trait, const char *method)
{
    size_t trait_length = strlen(trait);
    size_t method_length = strlen(method);
    char *name = (char *)malloc(trait_length + 1U + method_length + 1U);
    if (name == NULL) die("out of memory");
    memcpy(name, trait, trait_length);
    name[trait_length] = '_';
    memcpy(name + trait_length + 1U, method, method_length + 1U);
    return name;
}

static enum CXChildVisitResult collect_template_declarations(CXCursor cursor,
                                                             CXCursor parent,
                                                             CXClientData client_data)
{
    TemplateAnalysis *analysis = (TemplateAnalysis *)client_data;
    TemplateInfo *ti = analysis->info;
    enum CXCursorKind kind = clang_getCursorKind(cursor);
    CXSourceLocation location = clang_getCursorLocation(cursor);
    CXFile file = NULL;
    char *path;
    unsigned line = 0U, column = 0U, offset = 0U;
    int own_file;
    clang_getExpansionLocation(location, &file, &line, &column, &offset);
    if (file == NULL) return CXChildVisit_Recurse;
    own_file = clang_File_isEqual(file, analysis->template_file) != 0U;
    if ((kind == CXCursor_StructDecl || kind == CXCursor_UnionDecl) &&
        clang_isCursorDefinition(cursor) != 0U) {
        if (own_file) {
            register_structural_schema(cursor, NULL);
        } else {
            CXString spelling = clang_getCursorSpelling(cursor);
            const char *record_name = clang_getCString(spelling);
            int required = record_name != NULL &&
                           template_requires_constraint(ti, record_name);
            clang_disposeString(spelling);
            if (required) register_structural_schema(cursor, NULL);
        }
    }
    if (!own_file) {
        return CXChildVisit_Recurse;
    }
    path = copy_cx_string(clang_getFileName(file));
    if (clang_getCursorKind(parent) != CXCursor_TranslationUnit) {
        free(path);
        return CXChildVisit_Recurse;
    }
    if (kind == CXCursor_TypedefDecl) {
        char *name = copy_cx_string(clang_getCursorSpelling(cursor));
        char **parameters = NULL;
        size_t parameter_count = 0U, parameter_cap = 0U;
        unsigned declaration_start = 0U, declaration_end = 0U, declaration_line = 0U;
        TemplateAnnotation *annotation;
        cursor_expansion_offsets(cursor, &declaration_start, &declaration_end,
                                 &declaration_line);
        if (declaration_start >= analysis->offset_bias) {
            declaration_start -= analysis->offset_bias;
        }
        if (declaration_end >= analysis->offset_bias) {
            declaration_end -= analysis->offset_bias;
        }
        declaration_line = template_line_at_offset(ti, declaration_start);
        if (valid_identifier(name) && template_parameter_index(ti, name) < 0) {
            annotation = take_template_annotation(analysis, declaration_start);
            collect_cursor_template_parameters(analysis->tu, cursor, ti,
                                               &parameters, &parameter_count,
                                               &parameter_cap);
            if (annotation == NULL && parameter_count != 0U) {
                report_error(ti->path, declaration_line,
                             "dependent typedef '%s' needs a preceding // @template directive",
                             name);
                analysis->failed = 1;
            }
            if (annotation != NULL) {
                (void)declaration_parameters_are_listed(analysis, name,
                    declaration_line, annotation->parameter_names,
                    annotation->parameter_count, parameters, parameter_count);
                validate_where_constraint_bindings(analysis, name,
                    declaration_line, annotation, parameters, parameter_count);
            }
            {
                size_t i;
                char **ordered_parameters = NULL;
                size_t ordered_count = 0U, ordered_cap = 0U;
                for (i = 0U; i < (annotation != NULL ?
                                  annotation->parameter_count : 0U); ++i) {
                    if (list_contains(parameters, parameter_count,
                                      annotation->parameter_names[i])) {
                        list_add(&ordered_parameters, &ordered_count, &ordered_cap,
                                 annotation->parameter_names[i]);
                    }
                }
                list_free(parameters, parameter_count);
                parameters = ordered_parameters;
                parameter_count = ordered_count;
                add_template_type(ti, name,
                                  annotation != NULL ? annotation->parameter_names : NULL,
                                  annotation != NULL ? annotation->parameter_count : 0U,
                                  annotation != NULL ? annotation->constraints : NULL,
                                  annotation != NULL ? annotation->constraint_count : 0U,
                                  parameters, parameter_count, declaration_start,
                                  declaration_end, declaration_line, 0, 0);
                list_free(parameters, parameter_count);
            }
        }
        free(name);
    } else if ((kind == CXCursor_StructDecl || kind == CXCursor_UnionDecl) &&
               clang_isCursorDefinition(cursor) != 0U) {
        char *name = copy_cx_string(clang_getCursorSpelling(cursor));
        char **parameters = NULL;
        size_t parameter_count = 0U, parameter_cap = 0U;
        unsigned declaration_start = 0U, declaration_end = 0U, declaration_line = 0U;
        TemplateAnnotation *annotation;
        if (!template_has_direct_tag(ti, name, kind == CXCursor_UnionDecl)) {
            free(name);
            free(path);
            return CXChildVisit_Recurse;
        }
        cursor_expansion_offsets(cursor, &declaration_start, &declaration_end,
                                 &declaration_line);
        if (declaration_start >= analysis->offset_bias) {
            declaration_start -= analysis->offset_bias;
        }
        if (declaration_end >= analysis->offset_bias) {
            declaration_end -= analysis->offset_bias;
        }
        declaration_line = template_line_at_offset(ti, declaration_start);
        if (valid_identifier(name) && template_parameter_index(ti, name) < 0) {
            annotation = take_template_annotation(analysis, declaration_start);
            collect_cursor_template_parameters(analysis->tu, cursor, ti,
                                               &parameters, &parameter_count,
                                               &parameter_cap);
            if (annotation == NULL && parameter_count != 0U) {
                report_error(ti->path, declaration_line,
                             "dependent tag '%s' needs a preceding // @template directive",
                             name);
                analysis->failed = 1;
            }
            if (annotation != NULL) {
                (void)declaration_parameters_are_listed(analysis, name,
                    declaration_line, annotation->parameter_names,
                    annotation->parameter_count, parameters, parameter_count);
                validate_where_constraint_bindings(analysis, name,
                    declaration_line, annotation, parameters, parameter_count);
            }
            {
                char **ordered_parameters = NULL;
                size_t ordered_count = 0U, ordered_cap = 0U;
                size_t parameter_index;
                for (parameter_index = 0U; annotation != NULL &&
                     parameter_index < annotation->parameter_count;
                     ++parameter_index) {
                    if (list_contains(parameters, parameter_count,
                                      annotation->parameter_names[parameter_index])) {
                        list_add(&ordered_parameters, &ordered_count, &ordered_cap,
                                 annotation->parameter_names[parameter_index]);
                    }
                }
                list_free(parameters, parameter_count);
                add_template_type(ti, name,
                    annotation != NULL ? annotation->parameter_names : NULL,
                    annotation != NULL ? annotation->parameter_count : 0U,
                    annotation != NULL ? annotation->constraints : NULL,
                    annotation != NULL ? annotation->constraint_count : 0U,
                    ordered_parameters, ordered_count, declaration_start,
                    declaration_end, declaration_line,
                    kind == CXCursor_StructDecl, kind == CXCursor_UnionDecl);
                list_free(ordered_parameters, ordered_count);
            }
        }
        free(name);
    } else if (kind == CXCursor_FunctionDecl) {
        if (clang_isCursorDefinition(cursor) == 0U) {
            char *name = copy_cx_string(clang_getCursorSpelling(cursor));
            char **parameters = NULL;
            size_t parameter_count = 0U, parameter_cap = 0U;
            unsigned declaration_start = 0U, declaration_end = 0U;
            unsigned declaration_line = 0U;
            TemplateAnnotation *annotation;
            cursor_expansion_offsets(cursor, &declaration_start, &declaration_end,
                                     &declaration_line);
            if (declaration_start >= analysis->offset_bias) {
                declaration_start -= analysis->offset_bias;
            }
            declaration_line = template_line_at_offset(ti, declaration_start);
            /* Trait methods are bodyless declarations; they are registered by
             * the definition path below with the trait as their annotation. */
            if (trait_at_offset(ti, declaration_start) == NULL) {
                annotation = take_template_annotation(analysis, declaration_start);
                collect_cursor_template_parameters(analysis->tu, cursor, ti,
                                                   &parameters, &parameter_count,
                                                   &parameter_cap);
                if (annotation == NULL && parameter_count != 0U) {
                    report_error(ti->path, declaration_line,
                                 "dependent function declaration '%s' needs a preceding // @template directive",
                                 name);
                    analysis->failed = 1;
                } else if (annotation != NULL) {
                    (void)declaration_parameters_are_listed(analysis, name,
                        declaration_line, annotation->parameter_names,
                        annotation->parameter_count, parameters, parameter_count);
                    validate_where_constraint_bindings(analysis, name,
                        declaration_line, annotation, parameters, parameter_count);
                }
                list_free(parameters, parameter_count);
                free(name);
                free(path);
                return CXChildVisit_Recurse;
            }
            free(name);
        }
        TemplateFunction function;
        CXType function_type = clang_getCursorType(cursor);
        int argument_count = clang_getNumArgTypes(function_type);
        int i;
        char **all_parameters = NULL;
        size_t all_parameter_count = 0U, all_parameter_cap = 0U;
        TemplateAnnotation *annotation;
        TraitInfo *trait_block;
        TraitImpl *impl_block;
        int function_moved = 0;
        memset(&function, 0, sizeof(function));
        function.name = copy_cx_string(clang_getCursorSpelling(cursor));
        function.is_static = clang_Cursor_getStorageClass(cursor) == CX_SC_Static;
        function.is_inline = clang_Cursor_isFunctionInlined(cursor) != 0U;
        cursor_expansion_offsets(cursor, &function.start_offset, &function.end_offset,
                                 &function.start_line);
        if (function.start_offset >= analysis->offset_bias) {
            function.start_offset -= analysis->offset_bias;
        }
        if (function.end_offset >= analysis->offset_bias) {
            function.end_offset -= analysis->offset_bias;
        }
        function.start_line = template_line_at_offset(ti, function.start_offset);
        trait_block = trait_at_offset(ti, function.start_offset);
        impl_block = trait_block == NULL ?
            trait_impl_at_offset(ti, function.start_offset) : NULL;
        if (trait_block != NULL) {
            function.source_name = function.name;
            function.name = trait_method_name(trait_block->name,
                                              function.source_name);
            function.trait_name = copy_string(trait_block->name);
            function.is_trait_method = 1;
            function.is_static = 1;
            function.is_inline = 1;
            annotation = trait_method_annotation(ti, trait_block,
                                                  function.start_line);
        } else if (impl_block != NULL) {
            function.source_name = copy_string(function.name);
            annotation = trait_impl_method_annotation(ti, impl_block,
                                                      function.start_line);
        } else {
            annotation = take_template_annotation(analysis, function.start_offset);
        }
        function.has_template_annotation = annotation != NULL;
        if (annotation != NULL) {
            copy_declared_parameters(&function.declared_parameters,
                                     &function.declared_parameter_count,
                                     &function.declared_parameter_cap, annotation);
            copy_template_constraints(&function.constraints,
                                      &function.constraint_count,
                                      &function.constraint_cap,
                                      annotation->constraints,
                                      annotation->constraint_count);
        }
        function.return_type = copy_cx_string(clang_getTypeSpelling(
            clang_getResultType(function_type)));
        collect_type_spelling_parameters(ti, function.return_type,
                                         &function.return_parameters,
                                         &function.return_parameter_count,
                                         &function.return_parameter_cap);
        if (cursor_find_type_application(cursor, ti, analysis->offset_bias,
                                         function.name,
                                         &function.return_application)) {
            list_free(function.return_parameters,
                      function.return_parameter_count);
            function.return_parameters = NULL;
            function.return_parameter_count = 0U;
            function.return_parameter_cap = 0U;
            collect_application_parameters(ti, &function.return_application,
                &function.return_parameters, &function.return_parameter_count,
                &function.return_parameter_cap);
            {
                const TemplateType *constructor = find_template_type(
                    ti, function.return_application.constructor);
                if (constructor != NULL && constructor->parameter_count !=
                    function.return_application.argument_count) {
                    report_error(ti->path, function.start_line,
                                 "type application %s() expects %lu arguments, got %lu",
                                 constructor->name,
                                 (unsigned long)constructor->parameter_count,
                                 (unsigned long)function.return_application.argument_count);
                    analysis->failed = 1;
                }
            }
        }
        if (argument_count < 0) argument_count = 0;
        function.parameter_count = (size_t)argument_count;
        if (function.parameter_count != 0U) {
            function.parameters = (TemplateArgument *)calloc(function.parameter_count,
                                                              sizeof(*function.parameters));
            if (function.parameters == NULL) die("out of memory");
        }
        for (i = 0; i < argument_count; ++i) {
            CXType argument_type = clang_getArgType(function_type, (unsigned)i);
            CXCursor argument_cursor = clang_Cursor_getArgument(cursor, (unsigned)i);
            TemplateArgument *argument = &function.parameters[i];
            argument->type = copy_cx_string(clang_getTypeSpelling(argument_type));
            argument->name = copy_cx_string(clang_getCursorSpelling(argument_cursor));
            {
                unsigned parameter_start = 0U, parameter_end = 0U, parameter_line = 0U;
                size_t cursor_offset;
                cursor_expansion_offsets(argument_cursor, &parameter_start,
                                         &parameter_end, &parameter_line);
                (void)parameter_line;
                (void)parameter_end;
                if (parameter_start >= analysis->offset_bias) {
                    parameter_start -= analysis->offset_bias;
                }
                cursor_offset = (size_t)parameter_start;
                if (cursor_offset <= ti->body.len) {
                    /* `typename` expands to nothing in the analysis TU, so the
                     * parameter cursor starts at the type variable itself.
                     * Look backwards for the specifier instead. */
                    size_t before = cursor_offset;
                    size_t word_end;
                    while (before > 0U &&
                           isspace((unsigned char)ti->body.p[before - 1U])) --before;
                    word_end = before;
                    while (before > 0U && is_ident_char(ti->body.p[before - 1U])) {
                        --before;
                    }
                    if (word_end - before == 8U &&
                        strncmp(ti->body.p + before, "typename", 8U) == 0) {
                        size_t name_start = cursor_offset;
                        size_t name_end = name_start;
                        while (name_end < ti->body.len &&
                               is_ident_char(ti->body.p[name_end])) ++name_end;
                        if (name_end > name_start) {
                            argument->explicit_type = 1;
                            argument->explicit_parameter = copy_range(
                                ti->body.p + name_start, name_end - name_start);
                        }
                    }
                }
            }
            if (cursor_find_type_application(argument_cursor, ti,
                                             analysis->offset_bias,
                                             NULL, &argument->application)) {
                collect_application_parameters(ti, &argument->application,
                    &argument->type_parameters, &argument->type_parameter_count,
                    &argument->type_parameter_cap);
                {
                    const TemplateType *constructor = find_template_type(
                        ti, argument->application.constructor);
                    if (constructor != NULL && constructor->parameter_count !=
                        argument->application.argument_count) {
                        report_error(ti->path, function.start_line,
                                     "type application %s() expects %lu arguments, got %lu",
                                     constructor->name,
                                     (unsigned long)constructor->parameter_count,
                                     (unsigned long)argument->application.argument_count);
                        analysis->failed = 1;
                    }
                }
            } else {
                collect_type_spelling_parameters(ti, argument->type,
                                                 &argument->type_parameters,
                                                 &argument->type_parameter_count,
                                                 &argument->type_parameter_cap);
            }
            for (size_t j = 0U; j < argument->type_parameter_count; ++j) {
                const char *parameter = argument->type_parameters[j];
                if (!list_contains(all_parameters, all_parameter_count, parameter)) {
                    list_add(&all_parameters, &all_parameter_count,
                             &all_parameter_cap, parameter);
                }
            }
        }
        for (i = 0; i < (int)function.return_parameter_count; ++i) {
            const char *parameter = function.return_parameters[i];
            if (!list_contains(all_parameters, all_parameter_count, parameter)) {
                list_add(&all_parameters, &all_parameter_count,
                         &all_parameter_cap, parameter);
            }
        }
        collect_cursor_template_parameters(analysis->tu, cursor, ti,
                                           &all_parameters, &all_parameter_count,
                                           &all_parameter_cap);
        {
            char **ordered_parameters = NULL;
            size_t ordered_count = 0U, ordered_cap = 0U;
            size_t parameter_index;
            if (annotation != NULL) {
                (void)declaration_parameters_are_listed(analysis, function.name,
                    function.start_line, annotation->parameter_names,
                    annotation->parameter_count, all_parameters,
                    all_parameter_count);
                validate_where_constraint_bindings(analysis, function.name,
                    function.start_line, annotation, all_parameters,
                    all_parameter_count);
            } else if (all_parameter_count != 0U) {
                report_error(ti->path, function.start_line,
                             "dependent function '%s' needs a preceding // @template directive",
                             function.name);
                analysis->failed = 1;
            }
            for (parameter_index = 0U; parameter_index < ti->parameter_count;
                 ++parameter_index) {
                if (list_contains(all_parameters, all_parameter_count,
                                  ti->parameter_names[parameter_index])) {
                    if (annotation != NULL &&
                        list_contains(annotation->parameter_names,
                                      annotation->parameter_count,
                                      ti->parameter_names[parameter_index])) {
                        list_add(&ordered_parameters, &ordered_count, &ordered_cap,
                                 ti->parameter_names[parameter_index]);
                    }
                }
            }
            list_free(all_parameters, all_parameter_count);
            all_parameters = ordered_parameters;
            all_parameter_count = ordered_count;
            all_parameter_cap = ordered_cap;
        }
        {
            size_t argument_index;
            char **covered_parameters = NULL;
            size_t covered_count = 0U, covered_cap = 0U;
            for (argument_index = 0U;
                 argument_index < function.parameter_count; ++argument_index) {
                size_t constructor_index;
                TemplateArgument *argument = &function.parameters[argument_index];
                int contributes = 0;
                if (argument->explicit_type) continue;
                for (constructor_index = 0U;
                     constructor_index < argument->type_parameter_count;
                     ++constructor_index) {
                    if (!list_contains(covered_parameters, covered_count,
                                       argument->type_parameters[constructor_index])) {
                        contributes = 1;
                    }
                }
                if (contributes) {
                    unsigned *next_indices;
                    size_t new_cap = function.dispatch_cap == 0U ? 4U :
                                     function.dispatch_cap * 2U;
                    if (function.dispatch_count == function.dispatch_cap) {
                        if (new_cap < function.dispatch_cap ||
                            new_cap > (size_t)-1 / sizeof(*function.dispatch_indices)) {
                            die("too many dispatch arguments in function");
                        }
                        next_indices = (unsigned *)realloc(function.dispatch_indices,
                                                           new_cap * sizeof(*function.dispatch_indices));
                        if (next_indices == NULL) die("out of memory");
                        function.dispatch_indices = next_indices;
                        function.dispatch_cap = new_cap;
                    }
                    function.dispatch_indices[function.dispatch_count++] =
                        (unsigned)argument_index;
                    for (constructor_index = 0U;
                         constructor_index < argument->type_parameter_count;
                         ++constructor_index) {
                        const char *parameter = argument->type_parameters[constructor_index];
                        if (!list_contains(covered_parameters, covered_count, parameter)) {
                            list_add(&covered_parameters, &covered_count, &covered_cap,
                                     parameter);
                        }
                    }
                }
            }
            list_free(covered_parameters, covered_count);
            if (function_has_explicit_parameters(&function)) {
                size_t explicit_count = function_explicit_count(&function);
                size_t parameter_index;
                int valid = 1;
                for (parameter_index = 0U;
                     parameter_index < function.parameter_count; ++parameter_index) {
                    const TemplateArgument *argument =
                        &function.parameters[parameter_index];
                    size_t other;
                    if (!argument->explicit_type) continue;
                    if (parameter_index >= explicit_count) {
                        report_error(ti->path, function.start_line,
                            "explicit type parameter '%s' of '%s' must precede ordinary parameters",
                            argument->explicit_parameter, function.name);
                        valid = 0;
                    }
                    if (template_parameter_index(ti,
                            argument->explicit_parameter) < 0) {
                        report_error(ti->path, function.start_line,
                            "explicit type parameter '%s' of '%s' is not a template variable",
                            argument->explicit_parameter, function.name);
                        valid = 0;
                    }
                    for (other = 0U; other < parameter_index; ++other) {
                        if (function.parameters[other].explicit_type &&
                            str_eq(function.parameters[other].explicit_parameter,
                                   argument->explicit_parameter)) {
                            report_error(ti->path, function.start_line,
                                "duplicate explicit type parameter '%s' of '%s'",
                                argument->explicit_parameter, function.name);
                            valid = 0;
                        }
                    }
                }
                for (parameter_index = 0U;
                     parameter_index < all_parameter_count; ++parameter_index) {
                    size_t explicit_index;
                    int found = 0;
                    for (explicit_index = 0U; explicit_index < explicit_count;
                         ++explicit_index) {
                        if (str_eq(function.parameters[explicit_index].explicit_parameter,
                                   all_parameters[parameter_index])) {
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        report_error(ti->path, function.start_line,
                            "function '%s' mixes explicit type parameters with inferred parameter '%s'",
                            function.name, all_parameters[parameter_index]);
                        valid = 0;
                    }
                }
                if (!valid) analysis->failed = 1;
            }
            function.type_parameters = all_parameters;
            function.type_parameter_count = all_parameter_count;
            function.type_parameter_cap = all_parameter_cap;
            all_parameters = NULL;
            all_parameter_count = 0U;
            if (impl_block != NULL) {
                add_trait_impl_method(impl_block, &function);
                function_moved = 1;
            } else {
                add_template_function(ti, &function);
                if (trait_block != NULL) {
                    add_trait_method_index(trait_block,
                                           ti->function_count - 1U);
                }
            }
        }
        list_free(all_parameters, all_parameter_count);
        if (!function_moved) {
            free(function.name);
            free(function.source_name);
            free(function.trait_name);
            list_free(function.declared_parameters,
                      function.declared_parameter_count);
            free(function.return_type);
            for (i = 0; i < (int)function.parameter_count; ++i) {
                free(function.parameters[i].type);
                free(function.parameters[i].name);
                list_free(function.parameters[i].type_parameters,
                          function.parameters[i].type_parameter_count);
                template_application_free(&function.parameters[i].application);
            }
            free(function.parameters);
            list_free(function.return_parameters,
                      function.return_parameter_count);
            template_application_free(&function.return_application);
        }
    }
    free(path);
    (void)line;
    (void)column;
    (void)offset;
    return CXChildVisit_Recurse;
}

int analyze_template(TemplateInfo *ti)
{
    CXIndex index;
    CXTranslationUnit tu = NULL;
    enum CXErrorCode result;
    const char **arguments;
    int argument_count = (int)g_clang_arg_count + 2;
    int i;
    char *path = absolute_path(ti->path);
    TemplateAnalysis analysis;
    Buf contents;
    Buf stand_in;
    char *stand_in_path = NULL;
    struct CXUnsavedFile unsaved[2];
    size_t include_index;
    size_t parameter_index;
    buf_init(&contents);
    buf_init(&stand_in);
    buf_puts(&contents, GEN_PREAMBLE);
    if (contents.len != 0U && contents.p[contents.len - 1U] != '\n') {
        buf_putc(&contents, '\n');
    }
    for (include_index = 0U; include_index < ti->include_count; ++include_index) {
        if (template_include_is_reflection(ti, ti->includes[include_index])) {
            /* The real header may not exist yet or describe other types:
             * parse against the stand-in, as an unsaved file. */
            if (stand_in_path == NULL) {
                char *reflection = reflection_header_path();
                stand_in_path = absolute_path(reflection);
                free(reflection);
                emit_reflection_bootstrap(&stand_in);
            }
            buf_puts(&contents, "#include \"");
            buf_puts(&contents, stand_in_path);
            buf_puts(&contents, "\"\n");
            continue;
        }
        buf_puts(&contents, "#include ");
        buf_puts(&contents, ti->includes[include_index]);
        buf_putc(&contents, '\n');
    }
    for (parameter_index = 0U; parameter_index < ti->parameter_count; ++parameter_index) {
        if (template_parameter_is_value(ti, parameter_index)) {
            buf_puts(&contents, "enum { ");
            buf_puts(&contents, ti->parameter_names[parameter_index]);
            buf_puts(&contents, " = 0 };\n");
        } else if (template_parameter_is_constructor(ti, parameter_index)) {
            /* A constructor parameter stands in for a one-argument type
             * constructor: F(int) collapses to the typedef name F. */
            buf_puts(&contents, "typedef int ");
            buf_puts(&contents, ti->parameter_names[parameter_index]);
            buf_puts(&contents, ";\n#define ");
            buf_puts(&contents, ti->parameter_names[parameter_index]);
            buf_puts(&contents, "(...) ");
            buf_puts(&contents, ti->parameter_names[parameter_index]);
            buf_putc(&contents, '\n');
        } else {
            buf_puts(&contents, "typedef int ");
            buf_puts(&contents, ti->parameter_names[parameter_index]);
            buf_puts(&contents, ";\n");
        }
    }
    /* `typename T` in a parameter list marks an explicit type pseudo-argument;
     * the macro makes the template parse as an unnamed T parameter. */
    buf_puts(&contents, "#define typename\n");
    emit_template_analysis_functors(&contents, ti);
    emit_line_directive(&contents, 1UL, path);
    analysis.offset_bias = (unsigned)contents.len;
    buf_putn(&contents, ti->body.p, ti->body.len);
    arguments = (const char **)malloc((size_t)argument_count * sizeof(*arguments));
    if (arguments == NULL) die("out of memory");
    arguments[0] = "-x";
    arguments[1] = "c";
    for (i = 0; i < (int)g_clang_arg_count; ++i) arguments[i + 2] = g_clang_args[i];
    index = clang_createIndex(0, 0);
    if (index == NULL) die("libclang could not create an index");
    unsaved[0].Filename = path;
    unsaved[0].Contents = contents.p != NULL ? contents.p : "";
    unsaved[0].Length = (unsigned long)contents.len;
    if (stand_in_path != NULL) {
        unsaved[1].Filename = stand_in_path;
        unsaved[1].Contents = stand_in.p != NULL ? stand_in.p : "";
        unsaved[1].Length = (unsigned long)stand_in.len;
    }
    result = clang_parseTranslationUnit2(index, path, arguments, argument_count,
                                         unsaved, stand_in_path != NULL ? 2U : 1U,
                                         CXTranslationUnit_DetailedPreprocessingRecord |
                                         CXTranslationUnit_KeepGoing, &tu);
    free(arguments);
    if (result != CXError_Success || tu == NULL) {
        report_error(ti->path, 0U, "libclang could not parse template declarations (%d)",
                     (int)result);
        buf_free(&contents);
        buf_free(&stand_in);
        free(stand_in_path);
        free(path);
        clang_disposeIndex(index);
        return 0;
    }
    clang_getInclusions(tu, cache_collect_inclusion, &g_clang_dependencies);
    analysis.info = ti;
    analysis.tu = tu;
    analysis.template_file = clang_getFile(tu, path);
    if (analysis.template_file == NULL) {
        report_error(ti->path, 0U, "libclang could not resolve the template file");
        clang_disposeTranslationUnit(tu);
        clang_disposeIndex(index);
        buf_free(&contents);
        buf_free(&stand_in);
        free(stand_in_path);
        free(path);
        return 0;
    }
    analysis.failed = 0;
    clang_visitChildren(clang_getTranslationUnitCursor(tu),
                        collect_template_declarations, &analysis);
    {
        size_t annotation_index;
        for (annotation_index = 0U; annotation_index < ti->annotation_count;
             ++annotation_index) {
            if (!ti->annotations[annotation_index].used) {
                report_error(ti->path, ti->annotations[annotation_index].line,
                             "@template directive is not attached to a top-level typedef or function");
                analysis.failed = 1;
            }
        }
    }
    {
        size_t function_index;
        for (function_index = 0U; function_index < ti->function_count;
             ++function_index) {
            TemplateFunction *function = &ti->functions[function_index];
            if (function->has_template_annotation) {
                (void)declaration_parameters_are_listed(&analysis, function->name,
                    function->start_line, function->declared_parameters,
                    function->declared_parameter_count, function->type_parameters,
                    function->type_parameter_count);
            } else if (function->type_parameter_count != 0U) {
                report_error(ti->path, function->start_line,
                             "dependent function '%s' needs a preceding // @template directive",
                             function->name);
                analysis.failed = 1;
            }
        }
    }
    if (g_verbose) {
        size_t j;
        fprintf(stderr, "tmplgen: template %s params=%lu types=%lu functions=%lu\n",
                ti->path, (unsigned long)ti->parameter_count,
                (unsigned long)ti->constructor_count,
                (unsigned long)ti->function_count);
        for (j = 0U; j < ti->constructor_count; ++j) {
            fprintf(stderr, "tmplgen:   type %s/%lu\n", ti->constructors[j].name,
                    (unsigned long)ti->constructors[j].parameter_count);
        }
        for (j = 0U; j < ti->function_count; ++j) {
            size_t k;
            fprintf(stderr, "tmplgen:   function %s/%lu dispatch=%lu\n",
                    ti->functions[j].name,
                    (unsigned long)ti->functions[j].type_parameter_count,
                    (unsigned long)ti->functions[j].dispatch_count);
            if (g_verbose) {
                for (k = 0U; k < ti->functions[j].parameter_count; ++k) {
                    const TemplateApplication *application =
                        &ti->functions[j].parameters[k].application;
                    if (application->constructor != NULL) {
                        fprintf(stderr, "tmplgen:     arg%lu application=%s/%lu\n",
                                (unsigned long)k, application->constructor,
                                (unsigned long)application->argument_count);
                    }
                }
                if (ti->functions[j].return_application.constructor != NULL) {
                    fprintf(stderr, "tmplgen:     return application=%s/%lu\n",
                            ti->functions[j].return_application.constructor,
                            (unsigned long)ti->functions[j].return_application.argument_count);
                }
            }
        }
    }
    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(index);
    buf_free(&contents);
    buf_free(&stand_in);
    free(stand_in_path);
    free(path);
    return !analysis.failed;
}

static TemplateAnnotation *append_template_annotation(TemplateInfo *ti,
                                                      size_t line_number)
{
    TemplateAnnotation *next;
    size_t new_cap;
    if (ti->annotation_count == ti->annotation_cap) {
        new_cap = ti->annotation_cap == 0U ? 8U : ti->annotation_cap * 2U;
        if (new_cap < ti->annotation_cap ||
            new_cap > (size_t)-1 / sizeof(*ti->annotations)) {
            die("too many @template annotations");
        }
        next = (TemplateAnnotation *)realloc(ti->annotations,
                                              new_cap * sizeof(*ti->annotations));
        if (next == NULL) die("out of memory");
        ti->annotations = next;
        ti->annotation_cap = new_cap;
    }
    next = &ti->annotations[ti->annotation_count++];
    memset(next, 0, sizeof(*next));
    next->line = line_number;
    return next;
}

static int scalar_type_word(const char *word, size_t length)
{
    static const char *const keywords[] = {
        "char", "short", "int", "long", "signed", "unsigned", "float",
        "double", "_Bool", "bool"
    };
    size_t i;
    for (i = 0U; i < sizeof(keywords) / sizeof(keywords[0]); ++i) {
        if (strlen(keywords[i]) == length &&
            memcmp(keywords[i], word, length) == 0) return 1;
    }
    return length > 2U && word[length - 2U] == '_' && word[length - 1U] == 't';
}

/* One entry in a constructor parameter signature: `typename` (or a bare
 * identifier) is a type parameter, anything else is a value parameter whose
 * scalar type spelling precedes an optional documentation name. */
static int parse_constructor_signature_parameter(const char *text,
                                                 TemplateParameterKind *kind)
{
    char *normalized = normalize_spaces(text);
    char *cursor = normalized;
    char *words[8];
    size_t word_count = 0U;
    memset(kind, 0, sizeof(*kind));
    while (cursor != NULL && *cursor != '\0') {
        char *start = cursor;
        while (*cursor != '\0' && !isspace((unsigned char)*cursor)) ++cursor;
        if (*cursor != '\0') *cursor++ = '\0';
        while (*cursor != '\0' && isspace((unsigned char)*cursor)) ++cursor;
        if (word_count == sizeof(words) / sizeof(words[0])) {
            free(normalized);
            return 0;
        }
        words[word_count++] = start;
    }
    if (word_count == 0U) {
        free(normalized);
        return 0;
    }
    if (str_eq(words[0], "typename")) {
        free(normalized);
        return 1;
    }
    if (word_count == 1U && !scalar_type_word(words[0], strlen(words[0]))) {
        /* A bare identifier names a type parameter, like `typename Item`. */
        free(normalized);
        return 1;
    }
    {
        size_t type_words = word_count;
        Buf type;
        size_t i;
        if (word_count > 1U &&
            !scalar_type_word(words[word_count - 1U], strlen(words[word_count - 1U]))) {
            type_words = word_count - 1U;
        }
        kind->is_value = 1;
        buf_init(&type);
        for (i = 0U; i < type_words; ++i) {
            if (i != 0U) buf_putc(&type, ' ');
            buf_puts(&type, words[i]);
        }
        kind->value_type = type.p;
    }
    free(normalized);
    return 1;
}

static int trait_add_parameter(TemplateInfo *ti, const char *name,
                               size_t line_number)
{
    size_t i;
    for (i = 0U; i < ti->parameter_count; ++i) {
        if (!str_eq(ti->parameter_names[i], name)) continue;
        if (template_parameter_is_value(ti, i) ||
            template_parameter_is_constructor(ti, i)) {
            report_error(ti->path, line_number,
                         "trait parameter '%s' conflicts with a non-type template parameter",
                         name);
            return 0;
        }
        return 1;
    }
    if (ti->parameter_count == ti->parameter_cap) {
        size_t new_cap = ti->parameter_cap == 0U ? 4U : ti->parameter_cap * 2U;
        char **next_names = (char **)realloc(ti->parameter_names,
            new_cap * sizeof(*next_names));
        TemplateParameterKind *next_kinds =
            (TemplateParameterKind *)realloc(ti->parameter_kinds,
            new_cap * sizeof(*next_kinds));
        if (next_names == NULL || next_kinds == NULL) die("out of memory");
        ti->parameter_names = next_names;
        ti->parameter_kinds = next_kinds;
        ti->parameter_cap = new_cap;
    }
    ti->parameter_names[ti->parameter_count] = copy_string(name);
    memset(&ti->parameter_kinds[ti->parameter_count], 0,
           sizeof(ti->parameter_kinds[ti->parameter_count]));
    ++ti->parameter_count;
    return 1;
}

/* Parses "Name" or "Name(item, item)" from a @trait/@impl directive. Items
 * are kept as written; @trait validates them as identifiers separately. */
static int parse_trait_header(TemplateInfo *ti, size_t line_number,
                              const char *command, const char *p,
                              char **name_out, char ***items_out,
                              size_t *count_out, size_t *cap_out)
{
    char *name = NULL;
    char **items = NULL;
    size_t count = 0U, cap = 0U;
    while (*p != '\0' && isspace((unsigned char)*p)) ++p;
    if (!is_ident_start(*p)) {
        report_error(ti->path, line_number,
                     "@%s requires a trait name", command);
        return 0;
    }
    {
        const char *start = p++;
        while (is_ident_char(*p)) ++p;
        name = copy_range(start, (size_t)(p - start));
    }
    while (*p != '\0' && isspace((unsigned char)*p)) ++p;
    if (*p == '(') {
        ++p;
        for (;;) {
            const char *start;
            const char *end;
            size_t depth = 0U;
            char quote = '\0';
            while (*p != '\0' && isspace((unsigned char)*p)) ++p;
            start = p;
            while (*p != '\0') {
                if (quote != '\0') {
                    if (*p == '\\' && p[1] != '\0') p += 2;
                    else if (*p == quote) {
                        quote = '\0';
                        ++p;
                    } else ++p;
                    continue;
                }
                if (*p == '"' || *p == '\'') {
                    quote = *p++;
                } else if (*p == '(' || *p == '[' || *p == '{') {
                    ++depth;
                    ++p;
                } else if (*p == ')' || *p == ']' || *p == '}') {
                    if (depth == 0U) break;
                    --depth;
                    ++p;
                } else if (*p == ',' && depth == 0U) {
                    break;
                } else {
                    ++p;
                }
            }
            end = p;
            while (*p != '\0' && isspace((unsigned char)*p)) ++p;
            while (start < end && isspace((unsigned char)*start)) ++start;
            while (end > start && isspace((unsigned char)end[-1])) --end;
            if (start == end) {
                report_error(ti->path, line_number,
                             "@%s has an empty argument", command);
                free(name);
                list_free(items, count);
                return 0;
            }
            list_add(&items, &count, &cap,
                     copy_range(start, (size_t)(end - start)));
            if (*p == ',') {
                ++p;
                continue;
            }
            if (*p == ')') {
                ++p;
                break;
            }
            report_error(ti->path, line_number,
                         "@%s arguments must be separated by ','", command);
            free(name);
            list_free(items, count);
            return 0;
        }
    }
    while (*p != '\0' && isspace((unsigned char)*p)) ++p;
    if (*p != '\0') {
        report_error(ti->path, line_number,
                     "unexpected text after @%s header", command);
        free(name);
        list_free(items, count);
        return 0;
    }
    *name_out = name;
    *items_out = items;
    *count_out = count;
    *cap_out = cap;
    return 1;
}

static int parse_directive(TemplateInfo *ti, const char *line_text,
                           size_t line_number)
{
    const char *p = line_text;
    const char *start;
    const char *end;
    char *command;
    while (*p != '\0' && isspace((unsigned char)*p)) ++p;
    if (p[0] != '/' || p[1] != '/' || p[2] != ' ' || p[3] != '@') return 0;
    p += 4;
    start = next_word(&p);
    if (start == NULL) {
        report_error(ti->path, line_number, "empty directive");
        return -1;
    }
    end = p;
    while (end > start && isspace((unsigned char)end[-1])) --end;
    command = word_copy(start, end);
    if (str_eq(command, "meta")) {
        free(command);
        return 0;
    }
    if (str_eq(command, "trait")) {
        char *name = NULL;
        char **parameters = NULL;
        size_t parameter_count = 0U, parameter_cap = 0U;
        size_t i;
        TraitInfo *trait;
        if (!parse_trait_header(ti, line_number, "trait", p, &name,
                                &parameters, &parameter_count,
                                &parameter_cap)) {
            free(command);
            return -1;
        }
        for (i = 0U; i < parameter_count; ++i) {
            if (!valid_identifier(parameters[i])) {
                report_error(ti->path, line_number,
                             "invalid @trait parameter '%s'", parameters[i]);
                free(name);
                list_free(parameters, parameter_count);
                free(command);
                return -1;
            }
            if (list_contains(parameters, i, parameters[i])) {
                report_error(ti->path, line_number,
                             "duplicate @trait parameter '%s'", parameters[i]);
                free(name);
                list_free(parameters, parameter_count);
                free(command);
                return -1;
            }
            if (!trait_add_parameter(ti, parameters[i], line_number)) {
                free(name);
                list_free(parameters, parameter_count);
                free(command);
                return -1;
            }
        }
        if (ti->trait_count == ti->trait_cap) {
            size_t new_cap = ti->trait_cap == 0U ? 4U : ti->trait_cap * 2U;
            TraitInfo *next = (TraitInfo *)realloc(ti->traits,
                new_cap * sizeof(*next));
            if (next == NULL) die("out of memory");
            ti->traits = next;
            ti->trait_cap = new_cap;
        }
        trait = &ti->traits[ti->trait_count++];
        memset(trait, 0, sizeof(*trait));
        trait->name = name;
        trait->parameters = parameters;
        trait->parameter_count = parameter_count;
        trait->parameter_cap = parameter_cap;
        trait->line = line_number;
        free(command);
        return 3;
    }
    if (str_eq(command, "impl")) {
        char *name = NULL;
        char **arguments = NULL;
        size_t argument_count = 0U, argument_cap = 0U;
        TraitImpl *impl;
        if (!parse_trait_header(ti, line_number, "impl", p, &name,
                                &arguments, &argument_count,
                                &argument_cap)) {
            free(command);
            return -1;
        }
        if (argument_count == 0U) {
            report_error(ti->path, line_number,
                         "@impl requires at least one trait argument");
            free(name);
            free(command);
            return -1;
        }
        if (ti->trait_impl_count == ti->trait_impl_cap) {
            size_t new_cap = ti->trait_impl_cap == 0U ? 4U :
                             ti->trait_impl_cap * 2U;
            TraitImpl *next = (TraitImpl *)realloc(ti->trait_impls,
                new_cap * sizeof(*next));
            if (next == NULL) die("out of memory");
            ti->trait_impls = next;
            ti->trait_impl_cap = new_cap;
        }
        impl = &ti->trait_impls[ti->trait_impl_count++];
        memset(impl, 0, sizeof(*impl));
        impl->trait_name = name;
        impl->arguments = arguments;
        impl->argument_count = argument_count;
        impl->argument_cap = argument_cap;
        impl->line = line_number;
        impl->template_index = ti->template_index;
        free(command);
        return 4;
    }
    if (str_eq(command, "end")) {
        while (*p != '\0' && isspace((unsigned char)*p)) ++p;
        if (*p != '\0') {
            report_error(ti->path, line_number,
                         "unexpected text after @end");
            free(command);
            return -1;
        }
        free(command);
        return 5;
    }
    if (str_eq(command, "template")) {
        int expect_name = 1;
        char **names = NULL;
        TemplateParameterKind *kinds = NULL;
        size_t kind_count = 0U, kind_cap = 0U;
        size_t name_count = 0U, name_cap = 0U;
        TemplateConstraint *constraints = NULL;
        size_t constraint_count = 0U, constraint_cap = 0U;
        while (*p != '\0') {
            const char *name_start;
            char *name;
            TemplateParameterKind kind;
            memset(&kind, 0, sizeof(kind));
            while (*p != '\0' && isspace((unsigned char)*p)) ++p;
            if (*p == '\0') break;
            if (*p == ',') {
                if (expect_name) {
                    report_error(ti->path, line_number, "expected a type parameter before ','");
                    list_free(names, name_count);
                    template_constraint_array_free(constraints, constraint_count);
                    free(command);
                    return -1;
                }
                ++p;
                expect_name = 1;
                continue;
            }
            name_start = p;
            if (!is_ident_start(*p)) {
                report_error(ti->path, line_number, "invalid @template parameter list");
                list_free(names, name_count);
                template_constraint_array_free(constraints, constraint_count);
                free(command);
                return -1;
            }
            {
                const char *first_start = p;
                size_t first_length;
                ++p;
                while (is_ident_char(*p)) ++p;
                first_length = (size_t)(p - first_start);
                if (first_length == 8U &&
                    strncmp(first_start, "template", 8U) == 0) {
                    const char *q = p;
                    while (*q != '\0' && isspace((unsigned char)*q)) ++q;
                    if (*q == '(') {
                        char **signature_arguments = NULL;
                        size_t signature_count = 0U, signature_end = 0U;
                        size_t signature_index;
                        int parsed = parse_text_type_arguments(q, 0U,
                            &signature_arguments, &signature_count, &signature_end);
                        int valid = parsed || signature_end != 0U;
                        if (!valid) {
                            report_error(ti->path, line_number,
                                         "malformed constructor parameter signature");
                        }
                        kind.is_constructor = 1;
                        kind.signature_count = signature_count;
                        if (valid && signature_count != 0U) {
                            kind.signature = (TemplateParameterKind *)calloc(
                                signature_count, sizeof(*kind.signature));
                            if (kind.signature == NULL) die("out of memory");
                            for (signature_index = 0U;
                                 signature_index < signature_count;
                                 ++signature_index) {
                                if (!parse_constructor_signature_parameter(
                                        signature_arguments[signature_index],
                                        &kind.signature[signature_index])) {
                                    report_error(ti->path, line_number,
                                        "invalid constructor signature parameter '%s'",
                                        signature_arguments[signature_index]);
                                    valid = 0;
                                    break;
                                }
                            }
                        }
                        list_free(signature_arguments, signature_count);
                        if (!valid) {
                            template_parameter_kind_clear(&kind);
                            list_free(names, name_count);
                            template_parameter_kinds_free(kinds, kind_count);
                            template_constraint_array_free(constraints, constraint_count);
                            free(command);
                            return -1;
                        }
                        p = q + signature_end;
                        while (*p != '\0' && isspace((unsigned char)*p)) ++p;
                        name_start = p;
                        if (!is_ident_start(*p)) {
                            report_error(ti->path, line_number,
                                "expected a constructor parameter name after 'template(...)'");
                            template_parameter_kind_clear(&kind);
                            list_free(names, name_count);
                            template_parameter_kinds_free(kinds, kind_count);
                            template_constraint_array_free(constraints, constraint_count);
                            free(command);
                            return -1;
                        }
                        while (is_ident_char(*p)) ++p;
                    }
                } else if (first_length == 8U &&
                    strncmp(first_start, "typename", 8U) == 0) {
                    /* `typename` before a parameter name is an explicit
                     * type parameter specifier; a parameter actually named
                     * `typename` (not followed by another identifier) is
                     * still accepted. */
                    const char *q = p;
                    while (*q != '\0' && isspace((unsigned char)*q)) ++q;
                    if (is_ident_start(*q)) {
                        p = q;
                        name_start = p;
                        ++p;
                        while (is_ident_char(*p)) ++p;
                    }
                } else {
                    const char *q = p;
                    while (*q != '\0' && isspace((unsigned char)*q)) ++q;
                    if (is_ident_start(*q)) {
                        /* A type spelling before the name declares a value
                         * parameter: `size_t N`, `unsigned long N`. */
                        const char *type_end = p;
                        const char *cursor = q;
                        const char *word_start;
                        for (;;) {
                            const char *r;
                            word_start = cursor;
                            ++cursor;
                            while (is_ident_char(*cursor)) ++cursor;
                            r = cursor;
                            while (*r != '\0' && isspace((unsigned char)*r)) ++r;
                            if (!is_ident_start(*r)) break;
                            type_end = cursor;
                            cursor = r;
                        }
                        kind.is_value = 1;
                        kind.value_type = copy_range(first_start,
                                                     (size_t)(type_end - first_start));
                        name_start = word_start;
                        p = cursor;
                    }
                }
            }
            name = copy_range(name_start, (size_t)(p - name_start));
            if (list_contains(names, name_count, name)) {
                report_error(ti->path, line_number, "duplicate @template parameter '%s'", name);
                free(name);
                free(kind.value_type);
                list_free(names, name_count);
                template_constraint_array_free(constraints, constraint_count);
                free(command);
                return -1;
            }
            list_add(&names, &name_count, &name_cap, name);
            if (kind_count == kind_cap) {
                size_t new_cap = kind_cap == 0U ? 4U : kind_cap * 2U;
                TemplateParameterKind *next = (TemplateParameterKind *)realloc(
                    kinds, new_cap * sizeof(*kinds));
                if (next == NULL) die("out of memory");
                kinds = next;
                kind_cap = new_cap;
            }
            kinds[kind_count++] = kind;
            expect_name = 0;
            while (*p != '\0' && isspace((unsigned char)*p)) ++p;
            if (*p == '<') {
                if (kind.is_value || kind.is_constructor) {
                    report_error(ti->path, line_number,
                                 "%s parameter '%s' cannot have a structural constraint",
                                 kind.is_value ? "value" : "constructor", name);
                    free(name);
                    list_free(names, name_count);
                    template_parameter_kinds_free(kinds, kind_count);
                    template_constraint_array_free(constraints, constraint_count);
                    free(command);
                    return -1;
                }
                if (p[1] != ':') {
                    report_error(ti->path, line_number,
                                 "expected '<:' before the structural constraint");
                    free(name);
                    list_free(names, name_count);
                    template_constraint_array_free(constraints, constraint_count);
                    free(command);
                    return -1;
                }
                p += 2;
                for (;;) {
                    const char *constraint_start;
                    char *constraint_name;
                    while (*p != '\0' && isspace((unsigned char)*p)) ++p;
                    if (!is_ident_start(*p)) {
                        report_error(ti->path, line_number,
                                     "expected a named struct/union after '<:' or '+'");
                        free(name);
                        list_free(names, name_count);
                        template_constraint_array_free(constraints,
                                                       constraint_count);
                        free(command);
                        return -1;
                    }
                    constraint_start = p++;
                    while (is_ident_char(*p)) ++p;
                    constraint_name = copy_range(constraint_start,
                        (size_t)(p - constraint_start));
                    template_constraint_add(&constraints, &constraint_count,
                        &constraint_cap, name, constraint_name, line_number);
                    free(constraint_name);
                    while (*p != '\0' && isspace((unsigned char)*p)) ++p;
                    if (*p != '+') break;
                    ++p;
                }
                while (*p != '\0' && isspace((unsigned char)*p)) ++p;
            } else if (*p == '|') {
                if (kind.is_value || kind.is_constructor) {
                    report_error(ti->path, line_number,
                                 "%s parameter '%s' cannot have a constraint expression",
                                 kind.is_value ? "value" : "constructor", name);
                    free(name);
                    list_free(names, name_count);
                    template_parameter_kinds_free(kinds, kind_count);
                    template_constraint_array_free(constraints, constraint_count);
                    free(command);
                    return -1;
                }
                if (p[1] != '=') {
                    report_error(ti->path, line_number,
                                 "expected '|=' before the constraint expression");
                    free(name);
                    list_free(names, name_count);
                    template_constraint_array_free(constraints, constraint_count);
                    free(command);
                    return -1;
                }
                p += 2;
                for (;;) {
                    const char *expression_start;
                    const char *expression_end;
                    size_t parens = 0U, brackets = 0U, braces = 0U;
                    char quote = '\0';
                    while (*p != '\0' && isspace((unsigned char)*p)) ++p;
                    expression_start = p;
                    while (*p != '\0') {
                        if (quote != '\0') {
                            if (*p == '\\' && p[1] != '\0') p += 2;
                            else if (*p == quote) {
                                quote = '\0';
                                ++p;
                            } else ++p;
                            continue;
                        }
                        if ((*p == '"' || *p == '\'') ) {
                            quote = *p++;
                        } else if (p[0] == '/' && p[1] == '*') {
                            p += 2;
                            while (*p != '\0' && !(p[0] == '*' && p[1] == '/')) ++p;
                            if (*p != '\0') p += 2;
                        } else if (p[0] == '/' && p[1] == '/') {
                            while (*p != '\0') ++p;
                        } else if (*p == '(') {
                            ++parens;
                            ++p;
                        } else if (*p == ')') {
                            if (parens == 0U) break;
                            --parens;
                            ++p;
                        } else if (*p == '[') {
                            ++brackets;
                            ++p;
                        } else if (*p == ']') {
                            if (brackets == 0U) break;
                            --brackets;
                            ++p;
                        } else if (*p == '{') {
                            ++braces;
                            ++p;
                        } else if (*p == '}') {
                            if (braces == 0U) break;
                            --braces;
                            ++p;
                        } else if (parens == 0U && brackets == 0U && braces == 0U &&
                                   (*p == '+' || *p == ',')) {
                            break;
                        } else {
                            ++p;
                        }
                    }
                    expression_end = p;
                    while (expression_start < expression_end &&
                           isspace((unsigned char)*expression_start)) ++expression_start;
                    while (expression_end > expression_start &&
                           isspace((unsigned char)expression_end[-1])) --expression_end;
                    if (expression_start == expression_end) {
                        report_error(ti->path, line_number,
                                     "expected an expression after '|=' or '+'");
                        free(name);
                        list_free(names, name_count);
                        template_constraint_array_free(constraints,
                                                       constraint_count);
                        free(command);
                        return -1;
                    }
                    {
                        char *expression = copy_range(expression_start,
                            (size_t)(expression_end - expression_start));
                        template_constraint_add_expression(&constraints,
                            &constraint_count, &constraint_cap, name, expression,
                            line_number);
                        free(expression);
                    }
                    while (*p != '\0' && isspace((unsigned char)*p)) ++p;
                    if (*p != '+') break;
                    ++p;
                }
                while (*p != '\0' && isspace((unsigned char)*p)) ++p;
            } else if (*p == ':') {
                if (kind.is_value || kind.is_constructor) {
                    report_error(ti->path, line_number,
                                 "%s parameter '%s' cannot have a trait bound",
                                 kind.is_value ? "value" : "constructor", name);
                    free(name);
                    list_free(names, name_count);
                    template_parameter_kinds_free(kinds, kind_count);
                    template_constraint_array_free(constraints, constraint_count);
                    free(command);
                    return -1;
                }
                ++p;
                for (;;) {
                    const char *trait_start;
                    char *trait_name;
                    while (*p != '\0' && isspace((unsigned char)*p)) ++p;
                    if (!is_ident_start(*p)) {
                        report_error(ti->path, line_number,
                                     "expected a trait name after ':' or '+'");
                        free(name);
                        list_free(names, name_count);
                        template_parameter_kinds_free(kinds, kind_count);
                        template_constraint_array_free(constraints,
                                                       constraint_count);
                        free(command);
                        return -1;
                    }
                    trait_start = p++;
                    while (is_ident_char(*p)) ++p;
                    trait_name = copy_range(trait_start,
                                            (size_t)(p - trait_start));
                    template_constraint_add_bound(&constraints,
                        &constraint_count, &constraint_cap, name, trait_name,
                        line_number);
                    free(trait_name);
                    while (*p != '\0' && isspace((unsigned char)*p)) ++p;
                    if (*p != '+') break;
                    ++p;
                }
                while (*p != '\0' && isspace((unsigned char)*p)) ++p;
            }
            free(name);
            if (*p != '\0' && *p != ',') {
                report_error(ti->path, line_number,
                             "expected ',' between @template parameters");
                list_free(names, name_count);
                template_constraint_array_free(constraints, constraint_count);
                free(command);
                return -1;
            }
        }
        if (expect_name && name_count != 0U) {
            report_error(ti->path, line_number, "expected a type parameter after ','");
            list_free(names, name_count);
            template_constraint_array_free(constraints, constraint_count);
            free(command);
            return -1;
        }
        {
            size_t i;
            TemplateAnnotation *annotation = append_template_annotation(
                ti, line_number);
            annotation->parameter_names = names;
            annotation->parameter_kinds = kinds;
            annotation->parameter_count = name_count;
            annotation->parameter_cap = name_cap;
            annotation->constraints = constraints;
            annotation->constraint_count = constraint_count;
            annotation->constraint_cap = constraint_cap;
            for (i = 0U; i < name_count; ++i) {
                int existing = -1;
                size_t j;
                for (j = 0U; j < ti->parameter_count; ++j) {
                    if (str_eq(ti->parameter_names[j], names[i])) {
                        existing = (int)j;
                        break;
                    }
                }
                if (existing >= 0) {
                    if (!template_parameter_kind_equal(&ti->parameter_kinds[existing],
                                                       &kinds[i])) {
                        report_error(ti->path, line_number,
                                     "@template parameter '%s' is declared with conflicting kinds",
                                     names[i]);
                        return -1;
                    }
                    continue;
                }
                if (ti->parameter_count == ti->parameter_cap) {
                    size_t new_cap = ti->parameter_cap == 0U ? 4U :
                                     ti->parameter_cap * 2U;
                    char **next_names = (char **)realloc(ti->parameter_names,
                        new_cap * sizeof(*ti->parameter_names));
                    TemplateParameterKind *next_kinds =
                        (TemplateParameterKind *)realloc(ti->parameter_kinds,
                        new_cap * sizeof(*ti->parameter_kinds));
                    if (next_names == NULL || next_kinds == NULL) {
                        die("out of memory");
                    }
                    ti->parameter_names = next_names;
                    ti->parameter_kinds = next_kinds;
                    ti->parameter_cap = new_cap;
                }
                ti->parameter_names[ti->parameter_count] = copy_string(names[i]);
                template_parameter_kind_copy(&ti->parameter_kinds[ti->parameter_count],
                                             &kinds[i]);
                ++ti->parameter_count;
            }
        }
    } else if (str_eq(command, "where")) {
        TemplateConstraint *constraints = NULL;
        size_t constraint_count = 0U, constraint_cap = 0U;
        int saw_expression = 0;
        for (;;) {
            const char *expression_start;
            const char *expression_end;
            size_t parens = 0U, brackets = 0U, braces = 0U;
            char quote = '\0';
            while (*p != '\0' && isspace((unsigned char)*p)) ++p;
            expression_start = p;
            while (*p != '\0') {
                if (quote != '\0') {
                    if (*p == '\\' && p[1] != '\0') p += 2;
                    else if (*p == quote) {
                        quote = '\0';
                        ++p;
                    } else ++p;
                    continue;
                }
                if (*p == '"' || *p == '\'') {
                    quote = *p++;
                } else if (p[0] == '/' && p[1] == '*') {
                    p += 2;
                    while (*p != '\0' && !(p[0] == '*' && p[1] == '/')) ++p;
                    if (*p != '\0') p += 2;
                } else if (p[0] == '/' && p[1] == '/') {
                    while (*p != '\0') ++p;
                } else if (*p == '(') {
                    ++parens;
                    ++p;
                } else if (*p == ')') {
                    if (parens == 0U) break;
                    --parens;
                    ++p;
                } else if (*p == '[') {
                    ++brackets;
                    ++p;
                } else if (*p == ']') {
                    if (brackets == 0U) break;
                    --brackets;
                    ++p;
                } else if (*p == '{') {
                    ++braces;
                    ++p;
                } else if (*p == '}') {
                    if (braces == 0U) break;
                    --braces;
                    ++p;
                } else if (parens == 0U && brackets == 0U && braces == 0U &&
                           (*p == '+' || *p == ',')) {
                    break;
                } else {
                    ++p;
                }
            }
            expression_end = p;
            while (expression_start < expression_end &&
                   isspace((unsigned char)*expression_start)) ++expression_start;
            while (expression_end > expression_start &&
                   isspace((unsigned char)expression_end[-1])) --expression_end;
            if (expression_start == expression_end) {
                report_error(ti->path, line_number,
                             "@where requires a non-empty expression after each '+'");
                template_constraint_array_free(constraints, constraint_count);
                free(command);
                return -1;
            }
            {
                char *expression = copy_range(expression_start,
                    (size_t)(expression_end - expression_start));
                template_constraint_add_expression(&constraints,
                    &constraint_count, &constraint_cap, "", expression,
                    line_number);
                free(expression);
            }
            saw_expression = 1;
            while (*p != '\0' && isspace((unsigned char)*p)) ++p;
            if (*p == '+') {
                ++p;
                continue;
            }
            if (*p == ',') {
                report_error(ti->path, line_number,
                             "commas in @where expressions must be inside parentheses");
                template_constraint_array_free(constraints, constraint_count);
                free(command);
                return -1;
            }
            break;
        }
        if (!saw_expression) {
            report_error(ti->path, line_number, "@where requires an expression");
            template_constraint_array_free(constraints, constraint_count);
            free(command);
            return -1;
        }
        {
            TemplateAnnotation *annotation = append_template_annotation(ti,
                                                                         line_number);
            annotation->constraints = constraints;
            annotation->constraint_count = constraint_count;
            annotation->constraint_cap = constraint_cap;
        }
        free(command);
        return 2;
    } else {
        report_error(ti->path, line_number, "unknown directive '@%s'", command);
        free(command);
        return -1;
    }
    free(command);
    return 1;
}

static char *template_continuation_as_directive(const char *line)
{
    const char *p = line;
    Buf directive;
    while (*p != '\0' && isspace((unsigned char)*p)) ++p;
    if (p[0] != '/' || p[1] != '/' || p[2] != ' ' || p[3] != '@' ||
        !isspace((unsigned char)p[4])) return NULL;
    p += 4;
    buf_init(&directive);
    buf_puts(&directive, "// @template");
    buf_puts(&directive, p);
    return directive.p != NULL ? directive.p : copy_string("// @template");
}

static void merge_template_annotations(TemplateInfo *ti, size_t target_index,
                                       size_t source_index)
{
    TemplateAnnotation *target = &ti->annotations[target_index];
    TemplateAnnotation *source = &ti->annotations[source_index];
    size_t i, j;
    for (i = 0U; i < source->parameter_count; ++i) {
        if (!list_contains(target->parameter_names, target->parameter_count,
                           source->parameter_names[i])) {
            TemplateParameterKind kind;
            memset(&kind, 0, sizeof(kind));
            if (source->parameter_kinds != NULL) {
                template_parameter_kind_copy(&kind, &source->parameter_kinds[i]);
            }
            list_add(&target->parameter_names, &target->parameter_count,
                     &target->parameter_cap, source->parameter_names[i]);
            {
                size_t new_cap = target->parameter_cap;
                TemplateParameterKind *next = (TemplateParameterKind *)realloc(
                    target->parameter_kinds, new_cap * sizeof(*target->parameter_kinds));
                if (next == NULL) die("out of memory");
                target->parameter_kinds = next;
                target->parameter_kinds[target->parameter_count - 1U] = kind;
            }
        }
    }
    for (i = 0U; i < source->constraint_count; ++i) {
        const TemplateConstraint *constraint = &source->constraints[i];
        for (j = 0U; j < constraint->required_type_count; ++j) {
            template_constraint_add(&target->constraints, &target->constraint_count,
                &target->constraint_cap, constraint->parameter_name,
                constraint->required_types[j], constraint->line);
        }
        for (j = 0U; j < constraint->expression_count; ++j) {
            template_constraint_add_expression(&target->constraints,
                &target->constraint_count, &target->constraint_cap,
                constraint->parameter_name, constraint->expressions[j],
                constraint->line);
        }
        for (j = 0U; j < constraint->bound_trait_count; ++j) {
            template_constraint_add_bound(&target->constraints,
                &target->constraint_count, &target->constraint_cap,
                constraint->parameter_name, constraint->bound_traits[j],
                constraint->line);
        }
    }
    list_free(source->parameter_names, source->parameter_count);
    template_parameter_kinds_free(source->parameter_kinds, source->parameter_count);
    template_constraint_array_free(source->constraints, source->constraint_count);
    memset(source, 0, sizeof(*source));
    --ti->annotation_count;
}

/* Copies a `@template` annotation into an `@impl` block, turning its
 * declared parameters into free type variables and its bounds into impl
 * constraints. The annotation itself is left in place to be freed normally. */
static void trait_impl_take_annotation(TraitImpl *impl,
                                       TemplateAnnotation *annotation)
{
    size_t i;
    annotation->used = 1;
    for (i = 0U; i < annotation->parameter_count; ++i) {
        if (!list_contains(impl->type_parameters, impl->type_parameter_count,
                           annotation->parameter_names[i])) {
            list_add(&impl->type_parameters, &impl->type_parameter_count,
                     &impl->type_parameter_cap,
                     copy_string(annotation->parameter_names[i]));
        }
    }
    for (i = 0U; i < annotation->constraint_count; ++i) {
        const TemplateConstraint *constraint = &annotation->constraints[i];
        size_t j;
        for (j = 0U; j < constraint->required_type_count; ++j) {
            template_constraint_add(&impl->constraints,
                &impl->constraint_count, &impl->constraint_cap,
                constraint->parameter_name, constraint->required_types[j],
                constraint->line);
        }
        for (j = 0U; j < constraint->expression_count; ++j) {
            template_constraint_add_expression(&impl->constraints,
                &impl->constraint_count, &impl->constraint_cap,
                constraint->parameter_name, constraint->expressions[j],
                constraint->line);
        }
        for (j = 0U; j < constraint->bound_trait_count; ++j) {
            template_constraint_add_bound(&impl->constraints,
                &impl->constraint_count, &impl->constraint_cap,
                constraint->parameter_name, constraint->bound_traits[j],
                constraint->line);
        }
    }
}

size_t identifier_line_in_code(const char *src, const char *name)
{
    size_t i = 0U;
    size_t line = 1U;
    size_t name_len = strlen(name);
    while (src[i] != '\0') {
        if (src[i] == '/' && src[i + 1U] == '/') {
            i += 2U;
            while (src[i] != '\0' && src[i] != '\n') ++i;
            if (src[i] == '\n') {
                ++line;
                ++i;
            }
        } else if (src[i] == '/' && src[i + 1U] == '*') {
            i += 2U;
            while (src[i] != '\0' && !(src[i] == '*' && src[i + 1U] == '/')) {
                if (src[i] == '\n') ++line;
                ++i;
            }
            if (src[i] != '\0') i += 2U;
        } else if (src[i] == '"' || src[i] == '\'') {
            char quote = src[i++];
            while (src[i] != '\0' && src[i] != quote) {
                if (src[i] == '\\' && src[i + 1U] != '\0') {
                    if (src[i + 1U] == '\n') ++line;
                    i += 2U;
                } else {
                    if (src[i] == '\n') ++line;
                    ++i;
                }
            }
            if (src[i] != '\0') ++i;
        } else if (is_ident_start(src[i])) {
            size_t start = i++;
            while (is_ident_char(src[i])) ++i;
            if (i - start == name_len && memcmp(src + start, name, name_len) == 0) return line;
        } else {
            if (src[i] == '\n') ++line;
            ++i;
        }
    }
    return 0;
}

static const char *preprocessor_keyword(const char *line, const char *keyword)
{
    const char *p = line;
    size_t length = strlen(keyword);
    while (*p != '\0' && isspace((unsigned char)*p)) ++p;
    if (*p++ != '#') return NULL;
    while (*p != '\0' && isspace((unsigned char)*p)) ++p;
    if (strncmp(p, keyword, length) != 0) return NULL;
    if (is_ident_char(p[length])) return NULL;
    return p + length;
}

static int is_preprocessor_open(const char *line)
{
    return preprocessor_keyword(line, "if") != NULL ||
           preprocessor_keyword(line, "ifdef") != NULL ||
           preprocessor_keyword(line, "ifndef") != NULL;
}

static int is_lsp_fallback_start(const char *line)
{
    const char *p = preprocessor_keyword(line, "ifndef");
    size_t guard_length = strlen(GEN_LSP_GUARD);
    if (p == NULL) return 0;
    while (*p != '\0' && isspace((unsigned char)*p)) ++p;
    if (strncmp(p, GEN_LSP_GUARD, guard_length) != 0 ||
        is_ident_char(p[guard_length])) return 0;
    p += guard_length;
    while (*p != '\0' && isspace((unsigned char)*p)) ++p;
    return *p == '\0';
}

static int collect_include_line(TemplateInfo *ti, const char *line, size_t line_number)
{
    const char *p = preprocessor_keyword(line, "include");
    const char *end;
    const char *rest;
    char closing;
    char *header;
    if (p == NULL) return 0;
    while (*p != '\0' && isspace((unsigned char)*p)) ++p;
    if (*p != '<' && *p != '"') return 0;
    closing = *p == '<' ? '>' : '"';
    end = strchr(p + 1, closing);
    if (end == NULL) {
        report_error(ti->path, line_number, "unterminated #include header");
        return -1;
    }
    rest = end + 1;
    while (*rest != '\0' && isspace((unsigned char)*rest)) ++rest;
    if (*rest != '\0' && !(rest[0] == '/' && rest[1] == '/')) return 0;
    header = copy_range(p, (size_t)(end - p + 1));
    if (!list_contains(ti->includes, ti->include_count, header)) {
        list_add(&ti->includes, &ti->include_count, &ti->include_cap, header);
    }
    free(header);
    return 1;
}

static void buf_put_blanked_source(Buf *out, const char *source,
                                   size_t start, size_t end)
{
    size_t i;
    for (i = start; i < end; ++i) {
        buf_putc(out, source[i] == '\n' || source[i] == '\r' ? source[i] : ' ');
    }
}

static void update_block_comment_state(const char *line, int *in_block_comment)
{
    size_t i = 0U;
    while (line[i] != '\0') {
        if (*in_block_comment) {
            if (line[i] == '*' && line[i + 1U] == '/') {
                *in_block_comment = 0;
                i += 2U;
            } else {
                ++i;
            }
        } else if (line[i] == '/' && line[i + 1U] == '/') {
            break;
        } else if (line[i] == '/' && line[i + 1U] == '*') {
            *in_block_comment = 1;
            i += 2U;
        } else if (line[i] == '"' || line[i] == '\'') {
            char quote = line[i++];
            while (line[i] != '\0' && line[i] != quote) {
                if (line[i] == '\\' && line[i + 1U] != '\0') i += 2U;
                else ++i;
            }
            if (line[i] != '\0') ++i;
        } else {
            ++i;
        }
    }
}

static int line_is_blank(const char *line)
{
    size_t i;
    for (i = 0U; line[i] != '\0'; ++i) {
        if (!isspace((unsigned char)line[i])) return 0;
    }
    return 1;
}

int parse_template(TemplateInfo *ti, const char *path)
{
    Buf source;
    size_t offset = 0U;
    size_t line_number = 1U;
    int in_block_comment = 0;
    size_t lsp_fallback_depth = 0U;
    size_t pending_annotation_index = 0U;
    int pending_annotation = 0;
    int block_kind = 0;
    size_t block_index = 0U;
    buf_init(&source);
    ti->path = copy_string(path);
    if (!read_file(path, &source)) {
        buf_free(&source);
        return 0;
    }
#if defined(CLANGPLUGIN)
    /* The editor's unsaved buffer of this template, if it is the main file. */
    if (g_unsaved_source_path != NULL) {
        char *absolute = absolute_path(path);
        if (str_eq(absolute, g_unsaved_source_path)) {
            buf_free(&source);
            buf_init(&source);
            buf_putn(&source, g_unsaved_source_contents, g_unsaved_source_length);
        }
        free(absolute);
    }
#endif
    while (offset < source.len) {
        size_t end = offset;
        size_t content_end;
        size_t next;
        char *line;
        int directive;
        while (end < source.len && source.p[end] != '\n') ++end;
        next = end < source.len ? end + 1U : end;
        content_end = end;
        if (content_end > offset && source.p[content_end - 1U] == '\r') --content_end;
        line = copy_range(source.p + offset, content_end - offset);
        if (lsp_fallback_depth != 0U) {
            pending_annotation = 0;
            if (is_preprocessor_open(line)) ++lsp_fallback_depth;
            else if (preprocessor_keyword(line, "endif") != NULL) --lsp_fallback_depth;
            buf_put_blanked_source(&ti->body, source.p, offset, next);
        } else if (!in_block_comment && is_lsp_fallback_start(line)) {
            pending_annotation = 0;
            lsp_fallback_depth = 1U;
            buf_put_blanked_source(&ti->body, source.p, offset, next);
        } else {
            size_t body_offset = ti->body.len;
            char *continuation;
            const char *directive_line;
            if (block_kind != 0 && line_is_blank(line)) {
                if (block_kind == 1) {
                    ti->traits[block_index].end_offset = (unsigned)body_offset;
                } else {
                    ti->trait_impls[block_index].end_offset =
                        (unsigned)body_offset;
                }
                block_kind = 0;
            }
            continuation = pending_annotation ?
                template_continuation_as_directive(line) : NULL;
            directive_line = continuation != NULL ? continuation : line;
            directive = in_block_comment ? 0 :
                        parse_directive(ti, directive_line, line_number);
            free(continuation);
            if (directive < 0) {
                free(line);
                buf_free(&source);
                return 0;
            }
            if ((directive == 3 || directive == 4) && block_kind != 0) {
                report_error(ti->path, line_number,
                             "@trait and @impl blocks cannot be nested");
                free(line);
                buf_free(&source);
                return 0;
            }
            if ((directive == 1 || directive == 2) && block_kind != 0) {
                report_error(ti->path, line_number,
                             "@template and @where are not allowed inside a trait or impl block");
                free(line);
                buf_free(&source);
                return 0;
            }
            if (directive == 5) {
                if (block_kind == 0) {
                    report_error(ti->path, line_number,
                                 "@end without an open @trait or @impl block");
                    free(line);
                    buf_free(&source);
                    return 0;
                }
                if (block_kind == 1) {
                    ti->traits[block_index].end_offset = (unsigned)body_offset;
                } else {
                    ti->trait_impls[block_index].end_offset =
                        (unsigned)body_offset;
                }
                block_kind = 0;
            }
            if (directive == 3 || directive == 4) {
                block_kind = directive == 3 ? 1 : 2;
                block_index = directive == 3 ? ti->trait_count - 1U :
                                               ti->trait_impl_count - 1U;
                if (block_kind == 1) {
                    ti->traits[block_index].start_offset = (unsigned)body_offset;
                } else {
                    if (pending_annotation) {
                        trait_impl_take_annotation(
                            &ti->trait_impls[block_index],
                            &ti->annotations[pending_annotation_index]);
                    }
                    ti->trait_impls[block_index].start_offset =
                        (unsigned)body_offset;
                }
                pending_annotation = 0;
            }
            if (directive == 2 && !pending_annotation) {
                TemplateAnnotation *orphan =
                    &ti->annotations[ti->annotation_count - 1U];
                report_error(ti->path, line_number,
                             "@where must be inside a preceding @template block");
                list_free(orphan->parameter_names, orphan->parameter_count);
                template_constraint_array_free(orphan->constraints,
                                               orphan->constraint_count);
                memset(orphan, 0, sizeof(*orphan));
                --ti->annotation_count;
                free(line);
                buf_free(&source);
                return 0;
            }
            if (directive == 1 || directive == 2) {
                size_t added_index = ti->annotation_count - 1U;
                ti->annotations[added_index].offset = (unsigned)body_offset;
                if (pending_annotation) {
                    merge_template_annotations(ti, pending_annotation_index,
                                               added_index);
                } else {
                    pending_annotation_index = added_index;
                    pending_annotation = 1;
                }
            }
            if (directive == 0) {
                pending_annotation = 0;
                int include_line = in_block_comment ? 0 :
                                   collect_include_line(ti, line, line_number);
                if (include_line < 0) {
                    free(line);
                    buf_free(&source);
                    return 0;
                }
                if (include_line == 0) {
                    update_block_comment_state(line, &in_block_comment);
                    buf_putn(&ti->body, source.p + offset, next - offset);
                } else {
                    buf_put_blanked_source(&ti->body, source.p, offset, next);
                }
            } else {
                buf_put_blanked_source(&ti->body, source.p, offset, next);
            }
        }
        free(line);
        offset = next;
        ++line_number;
    }
    if (block_kind != 0) {
        if (ti->body.p == NULL && ti->body.len == 0U) {
            /* keep the empty body initialized below */
        }
        if (block_kind == 1) {
            ti->traits[block_index].end_offset = (unsigned)ti->body.len;
        } else {
            ti->trait_impls[block_index].end_offset = (unsigned)ti->body.len;
        }
    }
    if (lsp_fallback_depth != 0U) {
        report_error(ti->path, line_number, "unterminated LSP fallback block");
        buf_free(&source);
        return 0;
    }
    if (ti->body.p == NULL) {
        ti->body.p = copy_string("");
        ti->body.cap = 1U;
    }
    buf_free(&source);
    return 1;
}

static int template_has_function(const TemplateInfo *ti, const char *name)
{
    return find_template_function(ti, name) != NULL;
}

/* ======================= TOKEN REWRITER ==================== */

int instance_parameter_position(const GenInstance *instance,
                                      size_t parameter_index)
{
    size_t i;
    for (i = 0U; i < instance->argument_count; ++i) {
        if (instance->parameter_indices[i] == parameter_index) return (int)i;
    }
    return -1;
}

int validate_template_info(const TemplateInfo *infos, size_t count)
{
    StringSet function_names;
    StringSet constructor_names;
    size_t i;
    memset(&function_names, 0, sizeof(function_names));
    memset(&constructor_names, 0, sizeof(constructor_names));
    for (i = 0U; i < count; ++i) {
        size_t j;
        for (j = 0U; j < infos[i].constructor_count; ++j) {
            const char *name = infos[i].constructors[j].name;
            if (template_has_function(&infos[i], name) ||
                string_set_has(&function_names, name)) {
                report_error(infos[i].path, 0U,
                             "declaration '%s' is both a function and a type constructor",
                             name);
                string_set_free(&function_names);
                string_set_free(&constructor_names);
                return 0;
            }
            if (string_set_has(&constructor_names, name)) {
                report_error(infos[i].path, 0U,
                             "type constructor '%s' is declared by more than one template",
                             name);
                string_set_free(&function_names);
                string_set_free(&constructor_names);
                return 0;
            }
            string_set_add(&constructor_names, name);
        }
        for (j = 0U; j < infos[i].function_count; ++j) {
            const char *name = infos[i].functions[j].name;
            if (string_set_has(&function_names, name) ||
                string_set_has(&constructor_names, name)) {
                report_error(infos[i].path, 0U,
                             "generic function '%s' is declared by more than one template",
                             name);
                string_set_free(&function_names);
                string_set_free(&constructor_names);
                return 0;
            }
            string_set_add(&function_names, name);
        }
    }
    string_set_free(&function_names);
    string_set_free(&constructor_names);
    return 1;
}

static void trait_instance_init(GenInstance *instance, size_t template_index,
                                const TraitInfo *trait,
                                const TemplateInfo *trait_ti,
                                const TraitImpl *impl)
{
    size_t i;
    memset(instance, 0, sizeof(*instance));
    instance->template_index = template_index;
    instance->argument_count = impl->argument_count;
    instance->parameter_indices = (size_t *)malloc(
        impl->argument_count * sizeof(*instance->parameter_indices));
    instance->arguments = (TypeArgument *)calloc(
        impl->argument_count, sizeof(*instance->arguments));
    if (impl->argument_count != 0U &&
        (instance->parameter_indices == NULL ||
         instance->arguments == NULL)) {
        die("out of memory");
    }
    for (i = 0U; i < impl->argument_count; ++i) {
        int parameter = template_parameter_index(trait_ti,
                                                 trait->parameters[i]);
        instance->parameter_indices[i] = parameter < 0 ? 0U : (size_t)parameter;
        instance->arguments[i].c_type = impl->arguments[i];
    }
}

static void trait_instance_free(GenInstance *instance)
{
    free(instance->parameter_indices);
    free(instance->arguments);
    memset(instance, 0, sizeof(*instance));
}

static char *trait_rewrite_type(const char *type, const TemplateInfo *ti,
                                const GenInstance *instance)
{
    Buf rewritten;
    char *normalized;
    buf_init(&rewritten);
    rewrite_c_type(type, ti, instance, NULL, &rewritten);
    normalized = normalize_spaces(rewritten.p != NULL ? rewritten.p : "");
    buf_free(&rewritten);
    return normalized;
}

static const TemplateFunction *trait_impl_method_named(const TraitImpl *impl,
                                                       const char *name)
{
    size_t i;
    for (i = 0U; i < impl->method_count; ++i) {
        const TemplateFunction *method = &impl->methods[i];
        const char *source = method->source_name != NULL ?
            method->source_name : method->name;
        if (str_eq(source, name)) return method;
    }
    return NULL;
}

static char *trait_impl_type_text(const char *type,
                                  const TemplateApplication *application)
{
    Buf rendered;
    char *normalized;
    buf_init(&rendered);
    emit_template_type_pattern(&rendered, type != NULL ? type : "",
                               application);
    normalized = normalize_spaces(rendered.p != NULL ? rendered.p : "");
    buf_free(&rendered);
    return normalized;
}

static char *trait_expected_type_text(const char *type,
                                      const TemplateApplication *application,
                                      const TemplateInfo *ti,
                                      const GenInstance *instance)
{
    Buf pattern;
    char *rewritten;
    buf_init(&pattern);
    emit_template_type_pattern(&pattern, type != NULL ? type : "",
                               application);
    rewritten = trait_rewrite_type(pattern.p != NULL ? pattern.p : "", ti,
                                   instance);
    buf_free(&pattern);
    return rewritten;
}

static int trait_method_signature_matches(const TemplateFunction *trait_method,
                                          const TemplateFunction *impl_method,
                                          const TemplateInfo *trait_ti,
                                          const GenInstance *instance)
{
    size_t i;
    char *expected;
    char *actual;
    int matches = 1;
    expected = trait_expected_type_text(trait_method->return_type,
                                        &trait_method->return_application,
                                        trait_ti, instance);
    actual = trait_impl_type_text(impl_method->return_type,
                                  &impl_method->return_application);
    if (!str_eq(expected, actual)) matches = 0;
    free(expected);
    free(actual);
    if (trait_method->parameter_count != impl_method->parameter_count) {
        return 0;
    }
    for (i = 0U; i < trait_method->parameter_count && matches; ++i) {
        expected = trait_expected_type_text(
            trait_method->parameters[i].type,
            &trait_method->parameters[i].application, trait_ti, instance);
        actual = trait_impl_type_text(impl_method->parameters[i].type,
                                      &impl_method->parameters[i].application);
        if (!str_eq(expected, actual)) matches = 0;
        free(expected);
        free(actual);
    }
    return matches;
}

/* ================= IMPL PATTERN MATCHING ================== */

/* A parsed impl argument: either a bare name (possibly a free type variable)
 * or a type-constructor application such as `Vec(T)`. */
typedef struct {
    int is_application;
    char *name;
    char **arguments;
    size_t argument_count;
} TraitPattern;

static const char *skip_type_qualifiers(const char *text)
{
    static const char *const qualifiers[] = {"const", "volatile", "struct",
                                             "union"};
    for (;;) {
        size_t i;
        int matched = 0;
        while (*text != '\0' && isspace((unsigned char)*text)) ++text;
        for (i = 0U; i < sizeof(qualifiers) / sizeof(qualifiers[0]); ++i) {
            size_t length = strlen(qualifiers[i]);
            if (strncmp(text, qualifiers[i], length) == 0 &&
                !is_ident_char(text[length])) {
                text += length;
                matched = 1;
                break;
            }
        }
        if (!matched) return text;
    }
}

static void trait_pattern_free(TraitPattern *pattern)
{
    list_free(pattern->arguments, pattern->argument_count);
    free(pattern->name);
    memset(pattern, 0, sizeof(*pattern));
}

static void trait_pattern_parse(const char *text, TraitPattern *pattern)
{
    char *normalized = normalize_spaces(skip_type_qualifiers(
        text != NULL ? text : ""));
    size_t length = strlen(normalized);
    size_t i = 0U;
    memset(pattern, 0, sizeof(*pattern));
    if (!is_ident_start(normalized[0])) {
        pattern->name = normalized;
        return;
    }
    while (is_ident_char(normalized[i])) ++i;
    pattern->name = copy_range(normalized, i);
    while (i < length && isspace((unsigned char)normalized[i])) ++i;
    if (normalized[i] == '(') {
        char **arguments = NULL;
        size_t count = 0U, end = 0U;
        if (parse_text_type_arguments(normalized, i, &arguments, &count, &end) &&
            end == length) {
            pattern->is_application = 1;
            pattern->arguments = arguments;
            pattern->argument_count = count;
            free(normalized);
            return;
        }
        list_free(arguments, count);
    }
    free(pattern->name);
    pattern->name = normalized;
}

static int trait_binding_lookup(const TraitBinding *bindings, size_t count,
                                const char *name, const char **type_out)
{
    size_t i;
    for (i = 0U; i < count; ++i) {
        if (str_eq(bindings[i].name, name)) {
            if (type_out != NULL) *type_out = bindings[i].type;
            return 1;
        }
    }
    return 0;
}

static void trait_binding_add(TraitBinding **bindings, size_t *count,
                              size_t *capacity, const char *name,
                              const char *type)
{
    if (*count == *capacity) {
        size_t new_cap = *capacity == 0U ? 4U : *capacity * 2U;
        TraitBinding *next = (TraitBinding *)realloc(*bindings,
            new_cap * sizeof(*next));
        if (next == NULL) die("out of memory");
        *bindings = next;
        *capacity = new_cap;
    }
    (*bindings)[*count].name = copy_string(name);
    (*bindings)[*count].type = copy_string(type);
    ++*count;
}

void trait_bindings_free(TraitBinding *bindings, size_t count)
{
    size_t i;
    for (i = 0U; i < count; ++i) {
        free(bindings[i].name);
        free(bindings[i].type);
    }
    free(bindings);
}

static int trait_pattern_match(const char *pattern_text,
                               const TraitImpl *impl,
                               const char *concrete_text,
                               TraitBinding **bindings, size_t *binding_count,
                               size_t *binding_capacity)
{
    TraitPattern pattern;
    char *concrete;
    int matched = 0;
    trait_pattern_parse(pattern_text, &pattern);
    if (!pattern.is_application &&
        list_contains(impl->type_parameters, impl->type_parameter_count,
                      pattern.name)) {
        const char *existing = NULL;
        concrete = normalize_spaces(skip_type_qualifiers(concrete_text));
        if (trait_binding_lookup(*bindings, *binding_count, pattern.name,
                                 &existing)) {
            matched = str_eq(existing, concrete);
        } else {
            trait_binding_add(bindings, binding_count, binding_capacity,
                              pattern.name, concrete);
            matched = 1;
        }
        free(concrete);
        trait_pattern_free(&pattern);
        return matched;
    }
    concrete = normalize_spaces(skip_type_qualifiers(concrete_text));
    if (!pattern.is_application) {
        matched = str_eq(pattern.name, concrete);
        free(concrete);
        trait_pattern_free(&pattern);
        return matched;
    }
    {
        size_t constructor_index = 0U;
        const TemplateType *constructor = find_template_constructor_any(
            g_all_template_infos, g_all_template_info_count, pattern.name,
            &constructor_index);
        if (constructor != NULL) {
            size_t i;
            for (i = 0U; i < g_instance_count && !matched; ++i) {
                const GenInstance *instance = &g_instances[i];
                char *spelling;
                size_t j;
                if (instance->template_index != constructor_index) continue;
                spelling = join_suffix(constructor->name, instance->name);
                if (!str_eq(spelling, concrete) ||
                    instance->argument_count != pattern.argument_count) {
                    free(spelling);
                    continue;
                }
                free(spelling);
                matched = 1;
                for (j = 0U; j < pattern.argument_count; ++j) {
                    if (!trait_pattern_match(pattern.arguments[j], impl,
                                             instance->arguments[j].c_type,
                                             bindings, binding_count,
                                             binding_capacity)) {
                        matched = 0;
                        break;
                    }
                }
            }
        }
        if (!matched) {
            TraitPattern concrete_pattern;
            trait_pattern_parse(concrete, &concrete_pattern);
            if (concrete_pattern.is_application &&
                str_eq(concrete_pattern.name, pattern.name) &&
                concrete_pattern.argument_count == pattern.argument_count) {
                size_t j;
                matched = 1;
                for (j = 0U; j < pattern.argument_count; ++j) {
                    if (!trait_pattern_match(pattern.arguments[j], impl,
                                             concrete_pattern.arguments[j],
                                             bindings, binding_count,
                                             binding_capacity)) {
                        matched = 0;
                        break;
                    }
                }
            }
            trait_pattern_free(&concrete_pattern);
        }
    }
    free(concrete);
    trait_pattern_free(&pattern);
    return matched;
}

static int trait_impl_bind_patterns(const TraitImpl *impl,
                                    const char *const *concretes,
                                    size_t concrete_count,
                                    TraitBinding **bindings_out,
                                    size_t *binding_count_out)
{
    TraitBinding *bindings = NULL;
    size_t count = 0U, capacity = 0U;
    size_t i;
    if (concrete_count != impl->argument_count) return 0;
    for (i = 0U; i < impl->argument_count; ++i) {
        if (concretes[i] == NULL) continue;
        if (!trait_pattern_match(impl->arguments[i], impl, concretes[i],
                                 &bindings, &count, &capacity)) {
            trait_bindings_free(bindings, count);
            return 0;
        }
    }
    *bindings_out = bindings;
    *binding_count_out = count;
    return 1;
}

int trait_impl_bind_instance(const TraitImpl *impl, const TraitInfo *trait,
                             const TemplateInfo *trait_ti,
                             const GenInstance *instance,
                             TraitBinding **bindings_out,
                             size_t *binding_count_out)
{
    const char **concretes;
    size_t i;
    int matched;
    if (impl->argument_count == 0U) {
        *bindings_out = NULL;
        *binding_count_out = 0U;
        return 1;
    }
    concretes = (const char **)calloc(impl->argument_count, sizeof(*concretes));
    if (concretes == NULL) die("out of memory");
    for (i = 0U; i < impl->argument_count && i < trait->parameter_count; ++i) {
        int parameter = template_parameter_index(trait_ti,
                                                 trait->parameters[i]);
        int position = parameter < 0 ? -1 :
            instance_parameter_position(instance, (size_t)parameter);
        if (position >= 0) {
            concretes[i] = instance->arguments[position].c_type;
        }
    }
    matched = trait_impl_bind_patterns(impl, concretes, impl->argument_count,
                                       bindings_out, binding_count_out);
    free(concretes);
    return matched;
}

static int trait_pattern_at_least_as_specific(const char *left_text,
                                              const TraitImpl *left_impl,
                                              const char *right_text,
                                              const TraitImpl *right_impl)
{
    TraitPattern left;
    TraitPattern right;
    int result = 0;
    trait_pattern_parse(left_text, &left);
    trait_pattern_parse(right_text, &right);
    if (!right.is_application &&
        list_contains(right_impl->type_parameters,
                      right_impl->type_parameter_count, right.name)) {
        result = 1;
    } else if (!left.is_application &&
               list_contains(left_impl->type_parameters,
                             left_impl->type_parameter_count, left.name)) {
        result = !right.is_application && str_eq(left.name, right.name);
    } else if (left.is_application && right.is_application) {
        if (str_eq(left.name, right.name) &&
            left.argument_count == right.argument_count) {
            size_t i;
            result = 1;
            for (i = 0U; i < left.argument_count; ++i) {
                if (!trait_pattern_at_least_as_specific(
                        left.arguments[i], left_impl, right.arguments[i],
                        right_impl)) {
                    result = 0;
                    break;
                }
            }
        }
    } else if (!left.is_application && !right.is_application) {
        result = str_eq(left.name, right.name);
    }
    trait_pattern_free(&left);
    trait_pattern_free(&right);
    return result;
}

int trait_impl_more_specific(const TraitImpl *left, const TraitImpl *right,
                             const TraitInfo *trait)
{
    size_t count = left->argument_count < right->argument_count ?
        left->argument_count : right->argument_count;
    size_t i;
    int left_at_least = 1;
    int right_at_least = 1;
    if (count > trait->parameter_count) count = trait->parameter_count;
    for (i = 0U; i < count; ++i) {
        if (!trait_pattern_at_least_as_specific(left->arguments[i], left,
                                                right->arguments[i], right)) {
            left_at_least = 0;
        }
        if (!trait_pattern_at_least_as_specific(right->arguments[i], right,
                                                left->arguments[i], left)) {
            right_at_least = 0;
        }
    }
    return left_at_least && !right_at_least;
}

const TraitImpl *trait_select_impl(const TraitImpl *const *candidates,
                                   size_t count, const TraitInfo *trait,
                                   int *ambiguous)
{
    size_t i;
    const TraitImpl *selected = NULL;
    size_t selected_count = 0U;
    if (ambiguous != NULL) *ambiguous = 0;
    for (i = 0U; i < count; ++i) {
        size_t j;
        int dominated = 0;
        for (j = 0U; j < count; ++j) {
            if (i == j) continue;
            if (trait_impl_more_specific(candidates[j], candidates[i], trait)) {
                dominated = 1;
                break;
            }
        }
        if (!dominated) {
            selected = candidates[i];
            ++selected_count;
        }
    }
    if (selected_count == 1U) return selected;
    if (selected_count > 1U && ambiguous != NULL) *ambiguous = 1;
    return NULL;
}

static char *trait_impl_key(const TraitImpl *impl)
{
    Buf key;
    size_t i;
    buf_init(&key);
    buf_puts(&key, impl->trait_name);
    buf_putc(&key, '(');
    for (i = 0U; i < impl->argument_count; ++i) {
        char *argument = normalize_spaces(impl->arguments[i]);
        if (i != 0U) buf_putc(&key, ',');
        buf_puts(&key, argument);
        free(argument);
    }
    buf_putc(&key, ')');
    return key.p != NULL ? key.p : copy_string("");
}

int validate_trait_info(const TemplateInfo *infos, size_t count)
{
    StringSet impl_keys;
    size_t i;
    int okay = 1;
    memset(&impl_keys, 0, sizeof(impl_keys));
    for (i = 0U; i < count; ++i) {
        size_t j;
        for (j = 0U; j < infos[i].trait_count; ++j) {
            const TraitInfo *trait = &infos[i].traits[j];
            size_t owner = 0U;
            size_t k;
            if (find_trait_named(infos, count, trait->name, &owner) != trait) {
                report_error(infos[i].path, trait->line,
                             "trait '%s' is declared by more than one template",
                             trait->name);
                okay = 0;
            }
            if (trait->method_count == 0U) {
                report_error(infos[i].path, trait->line,
                             "trait '%s' declares no methods", trait->name);
                okay = 0;
            }
            for (k = 0U; k < trait->method_count; ++k) {
                const TemplateFunction *method =
                    &infos[i].functions[trait->method_indices[k]];
                size_t other;
                for (other = 0U; other < k; ++other) {
                    const TemplateFunction *previous =
                        &infos[i].functions[trait->method_indices[other]];
                    if (str_eq(method->source_name, previous->source_name)) {
                        report_error(infos[i].path, method->start_line,
                                     "trait '%s' declares method '%s' more than once",
                                     trait->name, method->source_name);
                        okay = 0;
                    }
                }
            }
        }
        for (j = 0U; j < infos[i].trait_impl_count; ++j) {
            const TraitImpl *impl = &infos[i].trait_impls[j];
            const TraitInfo *trait;
            size_t trait_owner = 0U;
            size_t k;
            char *key;
            trait = find_trait_named(infos, count, impl->trait_name,
                                     &trait_owner);
            if (trait == NULL) {
                report_error(infos[i].path, impl->line,
                             "@impl names unknown trait '%s'",
                             impl->trait_name);
                okay = 0;
                continue;
            }
            if (impl->argument_count != trait->parameter_count) {
                report_error(infos[i].path, impl->line,
                             trait->parameter_count == 1U ?
                             "@impl %s expects 1 argument, got %lu" :
                             "@impl %s expects %lu arguments, got %lu",
                             impl->trait_name,
                             (unsigned long)trait->parameter_count,
                             (unsigned long)impl->argument_count);
                okay = 0;
                continue;
            }
            key = trait_impl_key(impl);
            if (string_set_has(&impl_keys, key)) {
                report_error(infos[i].path, impl->line,
                             "duplicate @impl for %s", key);
                free(key);
                okay = 0;
                continue;
            }
            string_set_add(&impl_keys, key);
            {
                GenInstance instance;
                trait_instance_init(&instance, trait_owner, trait,
                                    &infos[trait_owner], impl);
                for (k = 0U; k < trait->method_count; ++k) {
                    const TemplateFunction *method =
                        &infos[trait_owner].functions[trait->method_indices[k]];
                    const TemplateFunction *definition =
                        trait_impl_method_named(impl, method->source_name);
                    if (definition == NULL) {
                        report_error(infos[i].path, impl->line,
                            "@impl %s is missing method '%s'",
                            impl->trait_name, method->source_name);
                        okay = 0;
                    } else if (!trait_method_signature_matches(method, definition,
                                                               &infos[trait_owner],
                                                               &instance)) {
                        report_error(infos[i].path, definition->start_line,
                            "method '%s' in @impl %s does not match the trait signature",
                            method->source_name, impl->trait_name);
                        okay = 0;
                    }
                }
                for (k = 0U; k < impl->method_count; ++k) {
                    const TemplateFunction *definition = &impl->methods[k];
                    const char *source = definition->source_name != NULL ?
                        definition->source_name : definition->name;
                    size_t other;
                    if (trait_impl_method_named(impl, source) != definition) {
                        continue;
                    }
                    for (other = 0U; other < k; ++other) {
                        const TemplateFunction *previous = &impl->methods[other];
                        const char *previous_source =
                            previous->source_name != NULL ?
                            previous->source_name : previous->name;
                        if (str_eq(source, previous_source)) {
                            report_error(infos[i].path,
                                         definition->start_line,
                                "@impl %s declares method '%s' more than once",
                                impl->trait_name, source);
                            okay = 0;
                        }
                    }
                    {
                        const TemplateFunction *trait_method = NULL;
                        size_t trait_index;
                        for (trait_index = 0U;
                             trait_index < trait->method_count; ++trait_index) {
                            const TemplateFunction *candidate =
                                &infos[trait_owner].functions[
                                    trait->method_indices[trait_index]];
                            if (str_eq(candidate->source_name, source)) {
                                trait_method = candidate;
                                break;
                            }
                        }
                        if (trait_method == NULL) {
                            report_error(infos[i].path,
                                         definition->start_line,
                                "@impl %s declares unknown method '%s'",
                                impl->trait_name, source);
                            okay = 0;
                        }
                    }
                }
                trait_instance_free(&instance);
            }
        }
        for (j = 0U; j < infos[i].function_count; ++j) {
            const TemplateFunction *function = &infos[i].functions[j];
            size_t k;
            for (k = 0U; k < function->constraint_count; ++k) {
                const TemplateConstraint *constraint =
                    &function->constraints[k];
                size_t b;
                for (b = 0U; b < constraint->bound_trait_count; ++b) {
                    if (find_trait_named(infos, count,
                                         constraint->bound_traits[b],
                                         NULL) == NULL) {
                        report_error(infos[i].path, constraint->line,
                                     "bound on '%s' names unknown trait '%s'",
                                     constraint->parameter_name,
                                     constraint->bound_traits[b]);
                        okay = 0;
                    }
                }
            }
        }
    }
    string_set_free(&impl_keys);
    return okay;
}

/* Whether the trait bound `trait_name` is satisfied for `concrete`, by
 * finding a matching impl (possibly generic itself) and discharging its own
 * bounds recursively. Returns 1 when undecidable so we do not report false
 * errors for traits we cannot reason about. */
static int trait_bound_satisfied(const char *trait_name, const char *concrete,
                                 int depth)
{
    const TraitInfo *trait;
    size_t i;
    if (depth >= 32) return 0;
    trait = find_trait_named(g_all_template_infos, g_all_template_info_count,
                             trait_name, NULL);
    if (trait == NULL || trait->parameter_count != 1U) return 1;
    for (i = 0U; i < g_all_template_info_count; ++i) {
        const TemplateInfo *impl_ti = &g_all_template_infos[i];
        size_t j;
        for (j = 0U; j < impl_ti->trait_impl_count; ++j) {
            const TraitImpl *impl = &impl_ti->trait_impls[j];
            const char *concretes[1];
            TraitBinding *bindings = NULL;
            size_t binding_count = 0U;
            size_t k;
            int satisfied = 1;
            if (!str_eq(impl->trait_name, trait_name)) continue;
            if (impl->argument_count != 1U) continue;
            concretes[0] = concrete;
            if (!trait_impl_bind_patterns(impl, concretes, 1U, &bindings,
                                          &binding_count)) {
                continue;
            }
            for (k = 0U; k < impl->constraint_count && satisfied; ++k) {
                const TemplateConstraint *constraint = &impl->constraints[k];
                const char *bound_type = NULL;
                size_t b;
                if (!trait_binding_lookup(bindings, binding_count,
                                          constraint->parameter_name,
                                          &bound_type)) {
                    continue;
                }
                for (b = 0U; b < constraint->bound_trait_count; ++b) {
                    if (!trait_bound_satisfied(constraint->bound_traits[b],
                                               bound_type, depth + 1)) {
                        satisfied = 0;
                        break;
                    }
                }
            }
            trait_bindings_free(bindings, binding_count);
            if (satisfied) return 1;
        }
    }
    return 0;
}

static void trait_bound_failure_key(Buf *key, const char *path, size_t line,
                                    const char *trait, const char *type)
{
    char line_text[32];
    buf_init(key);
    buf_puts(key, path != NULL ? path : "");
    buf_putc(key, '\n');
    (void)snprintf(line_text, sizeof(line_text), "%lu", (unsigned long)line);
    buf_puts(key, line_text);
    buf_putc(key, '\n');
    buf_puts(key, trait);
    buf_putc(key, '\n');
    buf_puts(key, type);
}

int validate_trait_instances(const TemplateInfo *infos, size_t count)
{
    typedef struct {
        const char *path;
        size_t line;
        const TraitInfo *trait;
        const TemplateFunction *function;
        size_t instance_index;
    } PendingMissingImpl;
    StringSet failed_bounds;
    PendingMissingImpl *pending = NULL;
    size_t pending_count = 0U, pending_cap = 0U;
    size_t i;
    int okay = 1;
    memset(&failed_bounds, 0, sizeof(failed_bounds));
    for (i = 0U; i < count; ++i) {
        size_t trait_index;
        for (trait_index = 0U; trait_index < infos[i].trait_count;
             ++trait_index) {
            const TraitInfo *trait = &infos[i].traits[trait_index];
            size_t method_index;
            for (method_index = 0U; method_index < trait->method_count;
                 ++method_index) {
                const TemplateFunction *function =
                    &infos[i].functions[trait->method_indices[method_index]];
                size_t instance_index;
                for (instance_index = 0U; instance_index < g_instance_count;
                     ++instance_index) {
                    const GenInstance *instance = &g_instances[instance_index];
                    const TraitImpl **candidates = NULL;
                    size_t candidate_count = 0U, candidate_cap = 0U;
                    const TraitImpl *winner;
                    int ambiguous = 0;
                    Buf arguments;
                    size_t j, k;
                    if (instance->template_index != i ||
                        !instance_matches_parameters(&infos[i], instance,
                            function->type_parameters,
                            function->type_parameter_count) ||
                        !generic_use_matches(function->name, instance->name, 0)) {
                        continue;
                    }
                    buf_init(&arguments);
                    for (k = 0U; k < instance->argument_count; ++k) {
                        if (k != 0U) buf_puts(&arguments, ", ");
                        buf_puts(&arguments, instance->arguments[k].c_type);
                    }
                    for (j = 0U; j < count; ++j) {
                        for (k = 0U; k < infos[j].trait_impl_count; ++k) {
                            const TraitImpl *impl = &infos[j].trait_impls[k];
                            TraitBinding *probe = NULL;
                            size_t probe_count = 0U;
                            if (!str_eq(impl->trait_name,
                                        function->trait_name)) {
                                continue;
                            }
                            if (!trait_impl_bind_instance(impl, trait,
                                    &infos[i], instance, &probe, &probe_count)) {
                                continue;
                            }
                            trait_bindings_free(probe, probe_count);
                            if (candidate_count == candidate_cap) {
                                size_t new_cap = candidate_cap == 0U ? 4U :
                                                 candidate_cap * 2U;
                                const TraitImpl **next =
                                    (const TraitImpl **)realloc(candidates,
                                        new_cap * sizeof(*next));
                                if (next == NULL) die("out of memory");
                                candidates = next;
                                candidate_cap = new_cap;
                            }
                            candidates[candidate_count++] = impl;
                        }
                    }
                    winner = candidate_count != 0U ?
                        trait_select_impl(candidates, candidate_count, trait,
                                          &ambiguous) : NULL;
                    if (candidate_count == 0U) {
                        if (pending_count == pending_cap) {
                            size_t new_cap = pending_cap == 0U ? 4U :
                                             pending_cap * 2U;
                            PendingMissingImpl *next =
                                (PendingMissingImpl *)realloc(pending,
                                    new_cap * sizeof(*next));
                            if (next == NULL) die("out of memory");
                            pending = next;
                            pending_cap = new_cap;
                        }
                        pending[pending_count].path = instance->origin_path;
                        pending[pending_count].line = instance->origin_line;
                        pending[pending_count].trait = trait;
                        pending[pending_count].function = function;
                        pending[pending_count].instance_index = instance_index;
                        ++pending_count;
                        okay = 0;
                    } else if (ambiguous) {
                        Buf listing;
                        buf_init(&listing);
                        for (k = 0U; k < candidate_count; ++k) {
                            char *key = trait_impl_key(candidates[k]);
                            if (k != 0U) buf_puts(&listing, ", ");
                            buf_puts(&listing, key);
                            free(key);
                        }
                        report_error(instance->origin_path,
                                     instance->origin_line,
                                     "ambiguous @impl for %s(%s); candidates: %s",
                                     function->trait_name,
                                     arguments.p != NULL ? arguments.p : "",
                                     listing.p != NULL ? listing.p : "");
                        buf_free(&listing);
                        okay = 0;
                    } else if (winner != NULL) {
                        TraitBinding *bindings = NULL;
                        size_t binding_count = 0U;
                        (void)trait_impl_bind_instance(winner, trait, &infos[i],
                                                       instance, &bindings,
                                                       &binding_count);
                        for (k = 0U; k < winner->type_parameter_count; ++k) {
                            if (!trait_binding_lookup(bindings, binding_count,
                                    winner->type_parameters[k], NULL)) {
                                report_error(instance->origin_path,
                                    instance->origin_line,
                                    "cannot infer type variable '%s' for @impl %s(%s)",
                                    winner->type_parameters[k],
                                    winner->trait_name,
                                    arguments.p != NULL ? arguments.p : "");
                                okay = 0;
                            }
                        }
                        for (k = 0U; k < winner->constraint_count; ++k) {
                            const TemplateConstraint *constraint =
                                &winner->constraints[k];
                            const char *bound_type = NULL;
                            size_t b;
                            if (!trait_binding_lookup(bindings, binding_count,
                                    constraint->parameter_name, &bound_type)) {
                                continue;
                            }
                            for (b = 0U; b < constraint->bound_trait_count;
                                 ++b) {
                                char *impl_key;
                                if (trait_bound_satisfied(
                                        constraint->bound_traits[b],
                                        bound_type, 0)) {
                                    continue;
                                }
                                Buf failure_key;
                                impl_key = trait_impl_key(winner);
                                report_error(instance->origin_path,
                                    instance->origin_line,
                                    "type '%s' does not implement trait '%s' (required by @impl %s)",
                                    bound_type,
                                    constraint->bound_traits[b], impl_key);
                                trait_bound_failure_key(&failure_key,
                                    instance->origin_path,
                                    instance->origin_line,
                                    constraint->bound_traits[b], bound_type);
                                string_set_add(&failed_bounds,
                                    failure_key.p != NULL ?
                                    failure_key.p : "");
                                buf_free(&failure_key);
                                free(impl_key);
                                okay = 0;
                            }
                        }
                        trait_bindings_free(bindings, binding_count);
                    }
                    free(candidates);
                    buf_free(&arguments);
                }
            }
        }
    }
    for (i = 0U; i < pending_count; ++i) {
        const GenInstance *instance = &g_instances[pending[i].instance_index];
        Buf arguments;
        size_t k;
        int covered = 0;
        if (instance->argument_count == 1U) {
            Buf key;
            trait_bound_failure_key(&key, pending[i].path, pending[i].line,
                                    pending[i].trait->name,
                                    instance->arguments[0].c_type);
            covered = string_set_has(&failed_bounds,
                                     key.p != NULL ? key.p : "");
            buf_free(&key);
        }
        if (covered) continue;
        buf_init(&arguments);
        for (k = 0U; k < instance->argument_count; ++k) {
            if (k != 0U) buf_puts(&arguments, ", ");
            buf_puts(&arguments, instance->arguments[k].c_type);
        }
        report_error(pending[i].path, pending[i].line,
                     instance->argument_count == 1U ?
                     "type '%s' does not implement trait '%s' (needed by '%s')" :
                     "types '%s' do not implement trait '%s' (needed by '%s')",
                     arguments.p != NULL ? arguments.p : "",
                     pending[i].function->trait_name,
                     pending[i].function->name);
        buf_free(&arguments);
    }
    free(pending);
    string_set_free(&failed_bounds);
    return okay;
}

int validate_structural_constraints(const TemplateInfo *infos,
                                           size_t template_count)
{
    size_t i;
    int okay = 1;
    for (i = 0U; i < template_count; ++i) {
        size_t j;
        for (j = 0U; j < infos[i].constructor_count; ++j) {
            const TemplateType *type = &infos[i].constructors[j];
            size_t constraint_index;
            for (constraint_index = 0U; constraint_index < type->constraint_count;
                 ++constraint_index) {
                const TemplateConstraint *constraint = &type->constraints[constraint_index];
                size_t required_index;
                for (required_index = 0U;
                     required_index < constraint->required_type_count;
                     ++required_index) {
                    int ambiguous;
                    if (find_structural_schema(
                            constraint->required_types[required_index],
                            &ambiguous) == NULL) {
                        report_error(infos[i].path, constraint->line,
                            ambiguous ? "structural schema '%s' is ambiguous between struct and union tags" :
                                        "structural schema '%s' is not a visible named struct or union",
                            constraint->required_types[required_index]);
                        okay = 0;
                    }
                }
            }
        }
        for (j = 0U; j < infos[i].function_count; ++j) {
            const TemplateFunction *function = &infos[i].functions[j];
            size_t constraint_index;
            for (constraint_index = 0U; constraint_index < function->constraint_count;
                 ++constraint_index) {
                const TemplateConstraint *constraint =
                    &function->constraints[constraint_index];
                size_t required_index;
                for (required_index = 0U;
                     required_index < constraint->required_type_count;
                     ++required_index) {
                    int ambiguous;
                    if (find_structural_schema(
                            constraint->required_types[required_index],
                            &ambiguous) == NULL) {
                        report_error(infos[i].path, constraint->line,
                            ambiguous ? "structural schema '%s' is ambiguous between struct and union tags" :
                                        "structural schema '%s' is not a visible named struct or union",
                            constraint->required_types[required_index]);
                        okay = 0;
                    }
                }
            }
        }
    }
    return okay;
}

void source_variable_add(SourceVariableList *variables, const char *name,
                                size_t file_index, const char *type_spelling,
                                const char *canonical_spelling, unsigned offset)
{
    SourceVariable *next;
    size_t new_cap;
    if (variables->count == variables->cap) {
        new_cap = variables->cap == 0U ? 16U : variables->cap * 2U;
        if (new_cap < variables->cap ||
            new_cap > (size_t)-1 / sizeof(*variables->items)) {
            die("too many source variables");
        }
        next = (SourceVariable *)realloc(variables->items,
                                         new_cap * sizeof(*variables->items));
        if (next == NULL) die("out of memory");
        variables->items = next;
        variables->cap = new_cap;
    }
    variables->items[variables->count].name = copy_string(name);
    variables->items[variables->count].file_index = file_index;
    variables->items[variables->count].type_spelling = copy_string(type_spelling);
    variables->items[variables->count].canonical_spelling =
        copy_string(canonical_spelling);
    variables->items[variables->count].offset = offset;
    ++variables->count;
}
