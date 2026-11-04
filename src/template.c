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
    TemplateAnnotation *annotation;
    if (ti->next_annotation >= ti->annotation_count) return NULL;
    annotation = &ti->annotations[ti->next_annotation];
    if (annotation->offset >= declaration_offset) return NULL;
    ++ti->next_annotation;
    annotation->used = 1;
    return annotation;
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
        TemplateFunction function;
        CXType function_type = clang_getCursorType(cursor);
        int argument_count = clang_getNumArgTypes(function_type);
        int i;
        char **all_parameters = NULL;
        size_t all_parameter_count = 0U, all_parameter_cap = 0U;
        TemplateAnnotation *annotation;
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
        annotation = take_template_annotation(analysis, function.start_offset);
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
            add_template_function(ti, &function);
        }
        list_free(all_parameters, all_parameter_count);
        free(function.name);
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
        list_free(function.return_parameters, function.return_parameter_count);
        template_application_free(&function.return_application);
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
    }
    list_free(source->parameter_names, source->parameter_count);
    template_parameter_kinds_free(source->parameter_kinds, source->parameter_count);
    template_constraint_array_free(source->constraints, source->constraint_count);
    memset(source, 0, sizeof(*source));
    --ti->annotation_count;
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

int parse_template(TemplateInfo *ti, const char *path)
{
    Buf source;
    size_t offset = 0U;
    size_t line_number = 1U;
    int in_block_comment = 0;
    size_t lsp_fallback_depth = 0U;
    size_t pending_annotation_index = 0U;
    int pending_annotation = 0;
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
            char *continuation = pending_annotation ?
                template_continuation_as_directive(line) : NULL;
            const char *directive_line = continuation != NULL ? continuation : line;
            directive = in_block_comment ? 0 :
                        parse_directive(ti, directive_line, line_number);
            free(continuation);
            if (directive < 0) {
                free(line);
                buf_free(&source);
                return 0;
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
            if (directive > 0) {
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
