/* Specialization, bootstrap, and dispatch emission. */
#include "internal.h"

/* Module-private forward declarations. */
static void emit_dispatch_section(Buf *out, const TemplateInfo *ti,
                                  const char *requirement, int hooks,
                                  const char *output_path);
static void emit_type_functors(Buf *out, const TemplateInfo *ti);
static void emit_explicit_function_macros(Buf *out, const TemplateInfo *ti);


void string_set_add(StringSet *set, const char *s)
{
    if (!string_set_has(set, s)) list_add(&set->items, &set->count, &set->cap, s);
}

void string_set_free(StringSet *set)
{
    list_free(set->items, set->count);
    memset(set, 0, sizeof(*set));
}

void emit_include(Buf *out, const char *header)
{
    buf_puts(out, "#include ");
    if (header[0] == '<' || header[0] == '"') buf_puts(out, header);
    else {
        buf_putc(out, '"');
        buf_puts(out, header);
        buf_putc(out, '"');
    }
    buf_putc(out, '\n');
}

char *include_key(const char *header)
{
    Buf key;
    buf_init(&key);
    if (header[0] == '<' || header[0] == '"') {
        buf_puts(&key, header);
    } else {
        buf_putc(&key, '"');
        buf_puts(&key, header);
        buf_putc(&key, '"');
    }
    return key.p;
}

void collect_preamble_includes(StringSet *includes, const char *preamble)
{
    size_t offset = 0U;
    size_t length = strlen(preamble);
    while (offset < length) {
        size_t end = offset;
        const char *p;
        while (end < length && preamble[end] != '\n') ++end;
        p = preamble + offset;
        while (p < preamble + end && isspace((unsigned char)*p)) ++p;
        if ((size_t)((preamble + end) - p) >= 8U &&
            memcmp(p, "#include", 8U) == 0) {
            const char *start = p + 8;
            const char *close;
            while (start < preamble + end && isspace((unsigned char)*start)) ++start;
            if (start < preamble + end && (*start == '<' || *start == '"')) {
                char closing = *start == '<' ? '>' : '"';
                close = start + 1;
                while (close < preamble + end && *close != closing) ++close;
                if (close < preamble + end) {
                    char *key = copy_range(start, (size_t)(close - start + 1));
                    string_set_add(includes, key);
                    free(key);
                }
            }
        }
        offset = end < length ? end + 1U : end;
    }
}

static char *line_directive_path(const char *path)
{
    char *absolute;
    const char *relative;
    char *result;
    if (g_line_path_base == NULL || g_absolute_template_dir == NULL) {
        return copy_string(path);
    }
    absolute = absolute_path(path);
    if (!path_is_within_directory(absolute, g_absolute_template_dir) ||
        !path_is_within_directory(absolute, g_line_path_base)) {
        free(absolute);
        return copy_string(path);
    }
    relative = absolute + strlen(g_line_path_base);
    while (*relative == '/' || *relative == '\\') ++relative;
    result = copy_string(relative);
    free(absolute);
    return result;
}

void emit_line_directive(Buf *out, unsigned long line,
                                const char *original_path)
{
    size_t i;
    char *path = line_directive_path(original_path);
    buf_puts(out, "#line ");
    buf_put_uint(out, line);
    buf_puts(out, " \"");
    for (i = 0U; path[i] != '\0'; ++i) {
        if (path[i] == '\\' || path[i] == '"') buf_putc(out, '\\');
        buf_putc(out, path[i]);
    }
    buf_puts(out, "\"\n");
    free(path);
}

static void emit_origin_mapped_fragment(Buf *out, const char *text,
                                        size_t length, unsigned line,
                                        const char *path)
{
    size_t offset = 0U;
    int in_block_comment = 0;
    int in_line_comment = 0;
    int map_next_line = 0;
    char quote = '\0';
    emit_line_directive(out, line, path);
    while (offset < length) {
        char c = text[offset];
        if (map_next_line && !in_block_comment && quote == '\0') {
            emit_line_directive(out, line, path);
            map_next_line = 0;
        }
        if (c == '\\' && offset + 1U < length &&
            text[offset + 1U] == '\n') {
            buf_putn(out, text + offset, 2U);
            offset += 2U;
            map_next_line = 0;
            continue;
        }
        if (quote != '\0') {
            buf_putc(out, c);
            ++offset;
            if (c == quote) quote = '\0';
            if (c == '\n') map_next_line = 0;
            continue;
        }
        if (in_block_comment) {
            if (c == '*' && offset + 1U < length && text[offset + 1U] == '/') {
                buf_putn(out, text + offset, 2U);
                offset += 2U;
                in_block_comment = 0;
                continue;
            }
            buf_putc(out, c);
            ++offset;
            if (c == '\n') map_next_line = 0;
            continue;
        }
        if (in_line_comment) {
            buf_putc(out, c);
            ++offset;
            if (c == '\n') {
                in_line_comment = 0;
                map_next_line = 1;
            }
            continue;
        }
        if (c == '/' && offset + 1U < length && text[offset + 1U] == '/') {
            buf_putn(out, text + offset, 2U);
            offset += 2U;
            in_line_comment = 1;
        } else if (c == '/' && offset + 1U < length && text[offset + 1U] == '*') {
            buf_putn(out, text + offset, 2U);
            offset += 2U;
            in_block_comment = 1;
        } else {
            buf_putc(out, c);
            ++offset;
            if (c == '"' || c == '\'') quote = c;
            if (c == '\n') map_next_line = 1;
        }
    }
}

static char *substitute_template_identifier(const char *source,
                                            const char *parameter,
                                            const char *concrete_type)
{
    Buf output;
    size_t i = 0U;
    buf_init(&output);
    while (source[i] != '\0') {
        if (source[i] == '/' && source[i + 1U] == '/') {
            size_t start = i;
            while (source[i] != '\0' && source[i] != '\n') ++i;
            buf_putn(&output, source + start, i - start);
        } else if (source[i] == '/' && source[i + 1U] == '*') {
            size_t start = i;
            i += 2U;
            while (source[i] != '\0' &&
                   !(source[i] == '*' && source[i + 1U] == '/')) ++i;
            if (source[i] != '\0') i += 2U;
            buf_putn(&output, source + start, i - start);
        } else if (source[i] == '"' || source[i] == '\'') {
            char quote = source[i];
            size_t start = i++;
            while (source[i] != '\0' && source[i] != quote) {
                if (source[i] == '\\' && source[i + 1U] != '\0') i += 2U;
                else ++i;
            }
            if (source[i] != '\0') ++i;
            buf_putn(&output, source + start, i - start);
        } else if (is_ident_start(source[i])) {
            size_t start = i++;
            char *identifier;
            while (is_ident_char(source[i])) ++i;
            identifier = copy_range(source + start, i - start);
            buf_puts(&output, str_eq(identifier, parameter) ? concrete_type : identifier);
            free(identifier);
        } else {
            buf_putc(&output, source[i++]);
        }
    }
    return output.p != NULL ? output.p : copy_string("");
}

static char *substitute_instance_parameters(const char *source,
                                            const TemplateInfo *ti,
                                            const GenInstance *instance)
{
    Buf output;
    size_t i = 0U;
    buf_init(&output);
    while (source[i] != '\0') {
        if (source[i] == '/' && source[i + 1U] == '/') {
            size_t start = i;
            while (source[i] != '\0' && source[i] != '\n') ++i;
            buf_putn(&output, source + start, i - start);
        } else if (source[i] == '/' && source[i + 1U] == '*') {
            size_t start = i;
            i += 2U;
            while (source[i] != '\0' &&
                   !(source[i] == '*' && source[i + 1U] == '/')) ++i;
            if (source[i] != '\0') i += 2U;
            buf_putn(&output, source + start, i - start);
        } else if (source[i] == '"' || source[i] == '\'') {
            char quote = source[i];
            size_t start = i++;
            while (source[i] != '\0' && source[i] != quote) {
                if (source[i] == '\\' && source[i + 1U] != '\0') i += 2U;
                else ++i;
            }
            if (source[i] != '\0') ++i;
            buf_putn(&output, source + start, i - start);
        } else if (is_ident_start(source[i])) {
            size_t start = i++;
            char *identifier;
            int parameter;
            int argument;
            while (is_ident_char(source[i])) ++i;
            identifier = copy_range(source + start, i - start);
            parameter = template_parameter_index(ti, identifier);
            argument = parameter >= 0 ? instance_parameter_position(
                instance, (size_t)parameter) : -1;
            if (argument >= 0) buf_puts(&output, instance->arguments[argument].c_type);
            else buf_puts(&output, identifier);
            free(identifier);
        } else {
            buf_putc(&output, source[i++]);
        }
    }
    return output.p != NULL ? output.p : copy_string("");
}

void emit_escaped_c_string(Buf *out, const char *text)
{
    size_t i;
    for (i = 0U; text[i] != '\0'; ++i) {
        if (text[i] == '\\' || text[i] == '"') buf_putc(out, '\\');
        if (text[i] == '\n') buf_puts(out, "\\n");
        else if (text[i] == '\r') buf_puts(out, "\\r");
        else buf_putc(out, text[i]);
    }
}

static void emit_template_static_assert(Buf *out, const char *expression,
                                        const char *message,
                                        const TemplateInfo *ti,
                                        unsigned source_line,
                                        const char *source_path)
{
    if (source_path == NULL) source_path = ti->path;
    buf_puts(out, "#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L\n");
    emit_line_directive(out, source_line, source_path);
    buf_puts(out, "_Static_assert((");
    buf_puts(out, expression);
    buf_puts(out, "), \"");
    emit_escaped_c_string(out, message);
    buf_puts(out, "\");\n#elif defined(__GNUC__) || defined(__clang__)\n");
    emit_line_directive(out, source_line, source_path);
    buf_puts(out, "__extension__ _Static_assert((");
    buf_puts(out, expression);
    buf_puts(out, "), \"");
    emit_escaped_c_string(out, message);
    buf_puts(out, "\");\n#endif\n");
}

static int emit_structural_assertions(Buf *out, const TemplateInfo *ti,
                                      const GenInstance *instance,
                                      const char *symbol, const char *output_path,
                                      unsigned declaration_line,
                                      const char *origin_path,
                                      size_t origin_line)
{
    const TemplateConstraint *constraints = NULL;
    size_t constraint_count = 0U;
    const TemplateFunction *function = find_template_function(ti, symbol);
    const TemplateType *type = function == NULL ? find_template_type(ti, symbol) : NULL;
    size_t constraint_index;
    int emitted = 0;
    const char *source_path = origin_path != NULL ? origin_path :
        (instance->origin_path != NULL ? instance->origin_path : ti->path);
    unsigned source_line = origin_line != 0U ? (unsigned)origin_line :
        (instance->origin_line != 0U ? (unsigned)instance->origin_line :
                                       declaration_line);
    if (function != NULL) {
        constraints = function->constraints;
        constraint_count = function->constraint_count;
    } else if (type != NULL) {
        constraints = type->constraints;
        constraint_count = type->constraint_count;
    }
    for (constraint_index = 0U; constraint_index < constraint_count;
         ++constraint_index) {
        const TemplateConstraint *constraint = &constraints[constraint_index];
        int where_constraint = constraint->parameter_name[0] == '\0';
        int parameter = where_constraint ? -1 :
            template_parameter_index(ti, constraint->parameter_name);
        int argument = parameter >= 0 ? instance_parameter_position(
            instance, (size_t)parameter) : -1;
        size_t required_index;
        if (!where_constraint && argument < 0) continue;
        if (constraint->required_type_count != 0U) emitted = 1;
        for (required_index = 0U; required_index < constraint->required_type_count;
             ++required_index) {
            int ambiguous;
            const StructuralSchema *schema = find_structural_schema(
                constraint->required_types[required_index], &ambiguous);
            size_t field_index;
            if (schema == NULL) continue;
            for (field_index = 0U; field_index < schema->field_count; ++field_index) {
                const char *field = schema->fields[field_index];
                buf_puts(out, "#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L\n");
                emit_line_directive(out, source_line, source_path);
                buf_puts(out, "_Static_assert(__builtin_types_compatible_p(__typeof__(((");
                buf_puts(out, instance->arguments[argument].c_type);
                buf_puts(out, " *)0)->");
                buf_puts(out, field);
                buf_puts(out, "), __typeof__(((");
                buf_puts(out, schema->is_union ? "union " : "struct ");
                buf_puts(out, schema->name);
                buf_puts(out, " *)0)->");
                buf_puts(out, field);
                buf_puts(out, ")), \"tmplgen: ");
                buf_puts(out, constraint->parameter_name);
                buf_puts(out, " is not a subtype of ");
                buf_puts(out, constraint->required_types[required_index]);
                buf_puts(out, " (incompatible field ");
                buf_puts(out, field);
                buf_puts(out, ")\");");
                buf_puts(out, "\n#elif defined(__GNUC__) || defined(__clang__)\n");
                emit_line_directive(out, source_line, source_path);
                buf_puts(out, "__extension__ _Static_assert(__builtin_types_compatible_p(__typeof__(((");
                buf_puts(out, instance->arguments[argument].c_type);
                buf_puts(out, " *)0)->");
                buf_puts(out, field);
                buf_puts(out, "), __typeof__(((");
                buf_puts(out, schema->is_union ? "union " : "struct ");
                buf_puts(out, schema->name);
                buf_puts(out, " *)0)->");
                buf_puts(out, field);
                buf_puts(out, ")), \"tmplgen: ");
                buf_puts(out, constraint->parameter_name);
                buf_puts(out, " is not a subtype of ");
                buf_puts(out, constraint->required_types[required_index]);
                buf_puts(out, " (incompatible field ");
                buf_puts(out, field);
                buf_puts(out, ")\");");
                buf_puts(out, "\n#endif\n");
            }
        }
    }
    for (constraint_index = 0U; constraint_index < constraint_count;
         ++constraint_index) {
        const TemplateConstraint *constraint = &constraints[constraint_index];
        int where_constraint = constraint->parameter_name[0] == '\0';
        int parameter = where_constraint ? -1 :
            template_parameter_index(ti, constraint->parameter_name);
        int argument = parameter >= 0 ? instance_parameter_position(
            instance, (size_t)parameter) : -1;
        size_t expression_index;
        if (!where_constraint && argument < 0) continue;
        for (expression_index = 0U;
             expression_index < constraint->expression_count;
             ++expression_index) {
            char *expanded = NULL;
            char *expression;
            if (where_constraint) {
                expression = substitute_instance_parameters(
                    constraint->expressions[expression_index], ti, instance);
            } else {
                expanded = expand_short_constraint_call(
                    constraint->expressions[expression_index],
                    constraint->parameter_name,
                    instance->arguments[argument].c_type);
                expression = substitute_template_identifier(
                    expanded != NULL ? expanded :
                        constraint->expressions[expression_index],
                    constraint->parameter_name,
                    instance->arguments[argument].c_type);
            }
            Buf message;
            emitted = 1;
            buf_init(&message);
            if (where_constraint) {
                buf_puts(&message, "tmplgen: @where failed to satisfy ");
            } else {
                buf_puts(&message, "tmplgen: ");
                buf_puts(&message, constraint->parameter_name);
                buf_puts(&message, " failed to satisfy ");
            }
            buf_puts(&message, constraint->expressions[expression_index]);
            emit_template_static_assert(out, expression, message.p, ti,
                                        source_line, source_path);
            buf_free(&message);
            free(expanded);
            free(expression);
        }
    }
    if (emitted) {
        unsigned long resume_line = (unsigned long)out->newlines + 2UL;
        emit_line_directive(out, resume_line, output_path);
    }
    return emitted;
}

size_t skip_c_trivia(const char *source, size_t offset)
{
    for (;;) {
        while (isspace((unsigned char)source[offset])) ++offset;
        if (source[offset] == '/' && source[offset + 1U] == '/') {
            while (source[offset] != '\0' && source[offset] != '\n') ++offset;
        } else if (source[offset] == '/' && source[offset + 1U] == '*') {
            offset += 2U;
            while (source[offset] != '\0' &&
                   !(source[offset] == '*' && source[offset + 1U] == '/')) ++offset;
            if (source[offset] != '\0') offset += 2U;
        } else {
            return offset;
        }
    }
}

size_t skip_c_literal(const char *source, size_t offset)
{
    char quote = source[offset++];
    while (source[offset] != '\0' && source[offset] != quote) {
        if (source[offset] == '\\' && source[offset + 1U] != '\0') offset += 2U;
        else ++offset;
    }
    if (source[offset] != '\0') ++offset;
    return offset;
}

static int externalize_function_definition(Buf *definition,
                                           const char *function_name)
{
    const char *source = definition->p != NULL ? definition->p : "";
    size_t name_offset = (size_t)-1;
    size_t offset = 0U;
    size_t name_length = strlen(function_name);
    while (source[offset] != '\0') {
        if (source[offset] == '/' &&
            (source[offset + 1U] == '/' || source[offset + 1U] == '*')) {
            offset = skip_c_trivia(source, offset);
        } else if (source[offset] == '"' || source[offset] == '\'') {
            offset = skip_c_literal(source, offset);
        } else if (is_ident_start(source[offset])) {
            size_t start = offset++;
            size_t end;
            size_t after;
            while (is_ident_char(source[offset])) ++offset;
            end = offset;
            if (end - start != name_length ||
                memcmp(source + start, function_name, name_length) != 0) continue;
            after = skip_c_trivia(source, end);
            if (source[after] == '(') {
                name_offset = start;
                break;
            }
        } else {
            ++offset;
        }
    }
    if (name_offset == (size_t)-1) return 0;

    offset = 0U;
    while (offset < name_offset) {
        if (source[offset] == '/' &&
            (source[offset + 1U] == '/' || source[offset + 1U] == '*')) {
            offset = skip_c_trivia(source, offset);
        } else if (source[offset] == '"' || source[offset] == '\'') {
            offset = skip_c_literal(source, offset);
        } else if (is_ident_start(source[offset])) {
            size_t start = offset++;
            size_t length;
            while (is_ident_char(source[offset])) ++offset;
            length = offset - start;
            if ((length == 6U && memcmp(source + start, "static", 6U) == 0) ||
                (length == 6U && memcmp(source + start, "inline", 6U) == 0)) {
                memset(definition->p + start, ' ', length);
            }
        } else {
            ++offset;
        }
    }
    return 1;
}

static void emit_instance(Buf *out, const TemplateInfo *ti, const GenInstance *instance,
                          const char *output_path, const char *stem, const char *symbol,
                          unsigned start_offset, unsigned end_offset,
                          unsigned start_line, int external_linkage)
{
    char *fragment;
    char *specialized_calls = NULL;
    const TemplateFunction *matched_function = NULL;
    const char *rewrite_source;
    Buf rewritten;
    Buf path;
    size_t function_index;
    const char *origin_path = instance->origin_path;
    size_t origin_line = instance->origin_line;
    int map_body_to_origin;
    if (end_offset > ti->body.len || start_offset >= end_offset) return;
    fragment = copy_range(ti->body.p + start_offset,
                          (size_t)(end_offset - start_offset));
    rewrite_source = fragment;
    for (function_index = 0U; function_index < ti->function_count;
         ++function_index) {
        const TemplateFunction *function = &ti->functions[function_index];
        if (function->start_offset == start_offset &&
            function->end_offset == end_offset &&
            function->type_parameter_count != 0U) {
            matched_function = function;
            specialized_calls = specialize_function_calls(fragment, ti, instance,
                                                            function, 0, 0);
            rewrite_source = specialized_calls;
            break;
        }
    }
    if (matched_function != NULL) {
        size_t use_index;
        for (use_index = 0U; use_index < g_generic_use_count; ++use_index) {
            const GenericUse *use = &g_generic_uses[use_index];
            if (str_eq(use->name, matched_function->name) &&
                str_eq(use->type_name, instance->name) &&
                use->origin_path != NULL) {
                origin_path = use->origin_path;
                origin_line = use->origin_line;
                break;
            }
        }
    }
    map_body_to_origin = !g_emit_line && matched_function != NULL &&
        matched_function->constraint_count != 0U && origin_path != NULL &&
        origin_line != 0U;
    buf_init(&rewritten);
    buf_init(&path);
    rewrite(rewrite_source, ti, instance, &rewritten);
    if (external_linkage) {
        char *external_name;
        int externalized;
        if (matched_function == NULL) die("missing function for generated symbol '%s'", symbol);
        external_name = join_suffix(matched_function->name, instance->name);
        externalized = externalize_function_definition(&rewritten, external_name);
        free(external_name);
        if (!externalized) die("cannot externalize generated function '%s'", symbol);
    }
    buf_puts(out, "\n/* ---- ");
    buf_puts(out, stem);
    buf_puts(out, ": ");
    buf_puts(out, symbol);
    if (instance->name != NULL && instance->name[0] != '\0') {
        buf_putc(out, '_');
        buf_puts(out, instance->name);
    }
    buf_puts(out, " ---- */\n");
    (void)emit_structural_assertions(out, ti, instance, symbol,
                                     output_path, start_line,
                                     origin_path, origin_line);
    if (g_emit_line) {
        buf_puts(&path, g_template_dir);
        buf_putc(&path, '/');
        buf_puts(&path, stem);
        buf_puts(&path, GEN_TMPL_SUFFIX);
        emit_line_directive(out, (unsigned long)start_line, path.p);
    }
    if (map_body_to_origin) {
        emit_origin_mapped_fragment(out, rewritten.p, rewritten.len,
                                    (unsigned)origin_line, origin_path);
    } else {
        buf_putn(out, rewritten.p, rewritten.len);
    }
    if (out->len == 0U || out->p[out->len - 1U] != '\n') buf_putc(out, '\n');
    if (g_emit_line || map_body_to_origin) {
        unsigned long resume_line = (unsigned long)out->newlines + 2UL;
        emit_line_directive(out, resume_line, output_path);
    }
    free(fragment);
    free(specialized_calls);
    buf_free(&rewritten);
    buf_free(&path);
}

static unsigned highest_function_argument(const TemplateInfo *infos, size_t count)
{
    unsigned maximum = 0U;
    size_t i;
    for (i = 0U; i < count; ++i) {
        size_t j;
        for (j = 0U; j < infos[i].function_count; ++j) {
            if (infos[i].functions[j].parameter_count != 0U &&
                infos[i].functions[j].parameter_count - 1U > (size_t)maximum) {
                maximum = (unsigned)(infos[i].functions[j].parameter_count - 1U);
            }
        }
    }
    return maximum;
}

static void emit_arg_helpers(Buf *out, unsigned maximum)
{
    unsigned i;
    for (i = 0U; i <= maximum; ++i) {
        unsigned j;
        buf_puts(out, "#ifndef GEN_ARG");
        buf_put_uint(out, (unsigned long)i);
        buf_putc(out, '\n');
        buf_puts(out, "#define GEN_ARG");
        buf_put_uint(out, (unsigned long)i);
        buf_puts(out, "(...) GEN_ARG");
        buf_put_uint(out, (unsigned long)i);
        buf_puts(out, "_(");
        buf_puts(out, "__VA_ARGS__");
        for (j = 0U; j <= i; ++j) buf_puts(out, ", 0");
        buf_puts(out, ")\n#define GEN_ARG");
        buf_put_uint(out, (unsigned long)i);
        buf_puts(out, "_(");
        for (j = 0U; j <= i; ++j) {
            if (j != 0U) buf_puts(out, ", ");
            buf_puts(out, "arg");
            buf_put_uint(out, (unsigned long)j);
        }
        buf_puts(out, ", ...) ");
        buf_puts(out, "arg");
        buf_put_uint(out, (unsigned long)i);
        buf_puts(out, "\n#endif\n");
    }
    buf_putc(out, '\n');
}

static void assoc_list_free(AssocList *list)
{
    size_t i;
    for (i = 0U; i < list->count; ++i) {
        size_t j;
        for (j = 0U; j < list->items[i].count; ++j) {
            free(list->items[i].types[j]);
            free(list->items[i].keys[j]);
        }
        free(list->items[i].indices);
        free(list->items[i].types);
        free(list->items[i].keys);
        free(list->items[i].function_name);
    }
    free(list->items);
    memset(list, 0, sizeof(*list));
}

char *join_suffix(const char *base, const char *suffix)
{
    Buf b;
    buf_init(&b);
    buf_puts(&b, base);
    buf_putc(&b, '_');
    buf_puts(&b, suffix);
    return b.p;
}

int assoc_add(AssocList *list, const unsigned *indices,
                     const char *const *types, const char *const *keys,
                     size_t association_count, const char *function_name,
                     int deduplicate)
{
    size_t i;
    DispatchAssoc *next;
    size_t new_cap;
    if (deduplicate) {
        for (i = 0U; i < list->count; ++i) {
            size_t j;
            if (list->items[i].count != association_count) continue;
            for (j = 0U; j < association_count; ++j) {
                if (list->items[i].indices[j] != indices[j] ||
                    !str_eq(list->items[i].keys[j], keys[j])) break;
            }
            if (j == association_count) return 1;
        }
    }
    if (list->count == list->cap) {
        new_cap = list->cap == 0U ? 4U : list->cap * 2U;
        if (new_cap < list->cap || new_cap > (size_t)-1 / sizeof(*list->items)) {
            die("too many dispatch associations");
        }
        next = (DispatchAssoc *)realloc(list->items, new_cap * sizeof(*list->items));
        if (next == NULL) die("out of memory");
        list->items = next;
        list->cap = new_cap;
    }
    list->items[list->count].count = association_count;
    list->items[list->count].indices = NULL;
    list->items[list->count].types = NULL;
    list->items[list->count].keys = NULL;
    if (association_count != 0U) {
        size_t j;
        list->items[list->count].indices =
            (unsigned *)malloc(association_count * sizeof(*list->items[list->count].indices));
        list->items[list->count].types =
            (char **)calloc(association_count, sizeof(*list->items[list->count].types));
        list->items[list->count].keys =
            (char **)calloc(association_count, sizeof(*list->items[list->count].keys));
        if (list->items[list->count].indices == NULL ||
            list->items[list->count].types == NULL ||
            list->items[list->count].keys == NULL) die("out of memory");
        for (j = 0U; j < association_count; ++j) {
            list->items[list->count].indices[j] = indices[j];
            list->items[list->count].types[j] = copy_string(types[j]);
            list->items[list->count].keys[j] = copy_string(keys[j]);
        }
    }
    list->items[list->count].function_name = copy_string(function_name);
    ++list->count;
    return 1;
}

static void build_associations(const char *generic_name, AssocList *assocs)
{
    size_t i;
    for (i = 0U; i < g_generic_use_count; ++i) {
        char *function_name;
        GenericUse *use = &g_generic_uses[i];
        if (!str_eq(use->name, generic_name) || !use->dispatch) continue;
        function_name = join_suffix(generic_name, use->type_name);
        (void)assoc_add(assocs, use->dispatch_indices,
                        (const char *const *)use->dispatch_types,
                        (const char *const *)use->dispatch_canonicals,
                        use->dispatch_count, function_name,
                        g_dispatch_backend != GEN_DISPATCH_NONE);
        free(function_name);
    }
}

static GenInstance *find_function_instance_for_symbol(const TemplateInfo *ti,
                                                       const TemplateFunction *function,
                                                       const char *symbol)
{
    size_t name_length = strlen(function->name);
    size_t i;
    size_t *indices;
    GenInstance *instance;
    if (strlen(symbol) <= name_length ||
        strncmp(symbol, function->name, name_length) != 0 ||
        symbol[name_length] != '_') return NULL;
    indices = (size_t *)malloc(function->type_parameter_count * sizeof(*indices));
    if (indices == NULL) die("out of memory");
    for (i = 0U; i < function->type_parameter_count; ++i) {
        int parameter = template_parameter_index(ti, function->type_parameters[i]);
        if (parameter < 0) die("invalid function specialization parameter map");
        indices[i] = (size_t)parameter;
    }
    instance = find_instance(ti->template_index, symbol + name_length + 1U,
                             indices, function->type_parameter_count);
    free(indices);
    return instance;
}

static void emit_function_signature(Buf *out, const TemplateInfo *ti,
                                    const TemplateFunction *function,
                                    const GenInstance *instance,
                                    const char *name, int documentation)
{
    Buf return_type;
    size_t i;
    int external = g_compiled_output && !documentation &&
                   function->type_parameter_count != 0U;
    buf_init(&return_type);
    if (instance != NULL) {
        rewrite_c_type(function->return_type, ti, instance,
                        &function->return_application, &return_type);
    } else {
        emit_template_type_pattern(&return_type, function->return_type,
                                   &function->return_application);
    }
    if (documentation) buf_puts(out, "/// ");
    if (!external && function->is_static) buf_puts(out, "static ");
    if (!external && function->is_inline) buf_puts(out, "inline ");
    buf_puts(out, return_type.p != NULL ? return_type.p : "void");
    buf_putc(out, ' ');
    buf_puts(out, name);
    buf_putc(out, '(');
    {
        size_t emitted_parameters = 0U;
    for (i = 0U; i < function->parameter_count; ++i) {
        Buf parameter_type;
        if (function->parameters[i].explicit_type) {
            if (!documentation) continue;
            if (emitted_parameters++ != 0U) buf_puts(out, ", ");
            buf_puts(out, "typename ");
            buf_puts(out, function->parameters[i].explicit_parameter);
            continue;
        }
        if (emitted_parameters++ != 0U) buf_puts(out, ", ");
        buf_init(&parameter_type);
        if (instance != NULL) {
            rewrite_c_type(function->parameters[i].type, ti, instance,
                           &function->parameters[i].application, &parameter_type);
        } else {
            emit_template_type_pattern(&parameter_type,
                function->parameters[i].type,
                &function->parameters[i].application);
        }
        buf_puts(out, parameter_type.p != NULL ? parameter_type.p : "int");
        if (function->parameters[i].name[0] != '\0') {
            buf_putc(out, ' ');
            buf_puts(out, function->parameters[i].name);
        }
        buf_free(&parameter_type);
    }
        if (emitted_parameters == 0U) buf_puts(out, "void");
    }
    buf_putc(out, ')');
    if (!documentation) buf_puts(out, ";\n");
    else buf_putc(out, '\n');
    buf_free(&return_type);
}

int instance_matches_parameters(const TemplateInfo *ti,
                                       const GenInstance *instance,
                                       char *const *parameter_names,
                                       size_t parameter_count)
{
    size_t i;
    if (instance->template_index >= gen_template_count() ||
        instance->argument_count != parameter_count) return 0;
    for (i = 0U; i < parameter_count; ++i) {
        int parameter = template_parameter_index(ti, parameter_names[i]);
        if (parameter < 0 || instance->parameter_indices[i] != (size_t)parameter) return 0;
    }
    return 1;
}

static DispatchHook *dispatch_plan_hook(DispatchPlan *plan, const char *name,
                                        const char *requirement, int gnu)
{
    size_t i;
    DispatchHook *hook;
    for (i = 0U; i < plan->hook_count; ++i) {
        if (str_eq(plan->hooks[i]->name, name)) return plan->hooks[i];
    }
    if (plan->hook_count == plan->hook_cap) {
        size_t new_cap = plan->hook_cap == 0U ? 4U : plan->hook_cap * 2U;
        DispatchHook **next;
        if (new_cap < plan->hook_cap ||
            new_cap > (size_t)-1 / sizeof(*plan->hooks)) die("too many dispatch hooks");
        next = (DispatchHook **)realloc(plan->hooks, new_cap * sizeof(*plan->hooks));
        if (next == NULL) die("out of memory");
        plan->hooks = next;
        plan->hook_cap = new_cap;
    }
    hook = (DispatchHook *)calloc(1U, sizeof(*hook));
    if (hook == NULL) die("out of memory");
    hook->name = copy_string(name);
    hook->requirement = copy_string(requirement);
    hook->gnu = gnu;
    buf_init(&hook->body);
    plan->hooks[plan->hook_count++] = hook;
    return hook;
}

static void dispatch_plan_free(DispatchPlan *plan)
{
    size_t i;
    for (i = 0U; i < plan->hook_count; ++i) {
        free(plan->hooks[i]->name);
        free(plan->hooks[i]->requirement);
        buf_free(&plan->hooks[i]->body);
        free(plan->hooks[i]);
    }
    free(plan->hooks);
    if (plan->requirements != NULL) {
        list_free(plan->requirements, plan->assocs->count);
    }
    memset(plan, 0, sizeof(*plan));
}

static void emit_planned_generic_node(DispatchPlan *plan, Buf *out,
                                      const size_t *candidates,
                                      size_t candidate_count, size_t depth,
                                      unsigned indent, const char *context)
{
    const AssocList *assocs = plan->assocs;
    const DispatchAssoc *first = &assocs->items[candidates[0]];
    unsigned node = plan->next_node++;
    StringSet node_hooks;
    size_t i;
    memset(&node_hooks, 0, sizeof(node_hooks));
    emit_spaces(out, indent);
    buf_puts(out, "_Generic((GEN_ARG");
    buf_put_uint(out, (unsigned long)first->indices[depth]);
    buf_puts(out, "(__VA_ARGS__)), \\\n");
    for (i = 0U; i < candidate_count; ++i) {
        const DispatchAssoc *association = &assocs->items[candidates[i]];
        size_t previous, j;
        int seen = 0;
        size_t subset_count = 0U;
        size_t *subset;
        char *label_requirement;
        Buf *target = out;
        for (previous = 0U; previous < i; ++previous) {
            if (str_eq(assocs->items[candidates[previous]].keys[depth],
                       association->keys[depth])) {
                seen = 1;
                break;
            }
        }
        if (seen) continue;
        subset = (size_t *)malloc(candidate_count * sizeof(*subset));
        if (subset == NULL) die("out of memory");
        label_requirement = copy_string(plan->requirements[candidates[i]]);
        for (j = i; j < candidate_count; ++j) {
            if (str_eq(assocs->items[candidates[j]].keys[depth],
                       association->keys[depth])) {
                char *common = requirement_intersection(
                    label_requirement, plan->requirements[candidates[j]]);
                free(label_requirement);
                label_requirement = common;
                subset[subset_count++] = candidates[j];
            }
        }
        if (!str_eq(label_requirement, context)) {
            Buf hook_name;
            DispatchHook *hook;
            buf_init(&hook_name);
            buf_puts(&hook_name, "TMPLGEN_GENERIC_");
            buf_puts(&hook_name, plan->name);
            buf_puts(&hook_name, "_N");
            buf_put_uint(&hook_name, (unsigned long)node);
            buf_putc(&hook_name, '_');
            buf_put_requirement_id(&hook_name, label_requirement);
            hook = dispatch_plan_hook(plan, hook_name.p, label_requirement, 0);
            string_set_add(&node_hooks, hook_name.p);
            target = &hook->body;
            buf_free(&hook_name);
        }
        emit_spaces(target, indent + 4U);
        buf_puts(target, association->types[depth]);
        buf_puts(target, ": ");
        if (depth + 1U == association->count) {
            buf_puts(target, association->function_name);
        } else {
            /* Hooks are heap-allocated, so `target` stays valid while the
             * subtree creates further hooks. */
            Buf subtree;
            buf_init(&subtree);
            emit_planned_generic_node(plan, &subtree, subset, subset_count,
                                      depth + 1U, indent + 4U, label_requirement);
            buf_puts(target, subtree.p != NULL ? subtree.p : "");
            buf_free(&subtree);
        }
        buf_puts(target, ", \\\n");
        free(label_requirement);
        free(subset);
    }
    for (i = 0U; i < node_hooks.count; ++i) {
        emit_spaces(out, indent + 4U);
        buf_puts(out, node_hooks.items[i]);
        buf_puts(out, "(__VA_ARGS__) \\\n");
    }
    emit_spaces(out, indent + 4U);
    buf_puts(out, "default: ");
    buf_puts(out, plan->name);
    buf_puts(out, "__no_match_for_argument_type)");
    string_set_free(&node_hooks);
}

static void emit_planned_gnu_chain(Buf *out, const AssocList *assocs,
                                   const size_t *indices, size_t count,
                                   const char *tail)
{
    size_t i, depth;
    for (i = 0U; i < count; ++i) {
        const DispatchAssoc *association = &assocs->items[indices[i]];
        emit_spaces(out, (unsigned)(4U + i * 4U));
        buf_puts(out, "__builtin_choose_expr( \\\n");
        emit_spaces(out, (unsigned)(8U + i * 4U));
        buf_putc(out, '(');
        for (depth = 0U; depth < association->count; ++depth) {
            if (depth != 0U) buf_puts(out, " && ");
            buf_puts(out, "__builtin_types_compatible_p(__typeof__(GEN_ARG");
            buf_put_uint(out, (unsigned long)association->indices[depth]);
            buf_puts(out, "(__VA_ARGS__)), ");
            buf_puts(out, association->types[depth]);
            buf_putc(out, ')');
        }
        buf_puts(out, "), \\\n");
        emit_spaces(out, (unsigned)(8U + i * 4U));
        buf_puts(out, association->function_name);
        buf_puts(out, ", \\\n");
    }
    emit_spaces(out, (unsigned)(4U + count * 4U));
    buf_puts(out, tail);
    for (i = 0U; i < count; ++i) buf_putc(out, ')');
}

/* Builds the core dispatch expression for both backends (to be followed by
 * the call's argument list) and every hook body. */
static void build_dispatch_plan(DispatchPlan *plan, const TemplateInfo *ti,
                                const TemplateFunction *function,
                                const AssocList *assocs, Buf *generic_macro,
                                Buf *gnu_macro)
{
    size_t i;
    StringSet requirements;
    size_t *indices;
    size_t count;
    Buf tail;
    memset(plan, 0, sizeof(*plan));
    plan->name = function->name;
    plan->assocs = assocs;
    plan->requirements = (char **)calloc(assocs->count + 1U,
                                         sizeof(*plan->requirements));
    if (plan->requirements == NULL) die("out of memory");
    memset(&requirements, 0, sizeof(requirements));
    for (i = 0U; i < assocs->count; ++i) {
        const GenInstance *instance = find_function_instance_for_symbol(
            ti, function, assocs->items[i].function_name);
        plan->requirements[i] = copy_string(instance_requirement(instance));
        if (plan->requirements[i][0] != '\0') {
            string_set_add(&requirements, plan->requirements[i]);
        }
    }
    sort_requirements(&requirements);

    /* _Generic tree */
    if (g_dispatch_backend == GEN_DISPATCH_GENERIC ||
        g_dispatch_backend == GEN_DISPATCH_AUTO) {
        indices = (size_t *)malloc((assocs->count + 1U) * sizeof(*indices));
        if (indices == NULL) die("out of memory");
        for (i = 0U; i < assocs->count; ++i) indices[i] = i;
        emit_planned_generic_node(plan, generic_macro, indices, assocs->count,
                                  0U, 0U, "");
        free(indices);
    }

    /* GNU chain: unconditional associations first, then one pass-through
     * hook per requirement, nested so each may append its own tests. */
    if (g_dispatch_backend == GEN_DISPATCH_GNU ||
        g_dispatch_backend == GEN_DISPATCH_AUTO) {
        size_t r;
        indices = (size_t *)malloc((assocs->count + 1U) * sizeof(*indices));
        if (indices == NULL) die("out of memory");
        buf_init(&tail);
        for (r = 0U; r < requirements.count; ++r) {
            Buf hook_name;
            DispatchHook *hook;
            size_t hook_count = 0U;
            buf_init(&hook_name);
            buf_puts(&hook_name, "TMPLGEN_GNU_");
            buf_puts(&hook_name, function->name);
            buf_putc(&hook_name, '_');
            buf_put_requirement_id(&hook_name, requirements.items[r]);
            hook = dispatch_plan_hook(plan, hook_name.p, requirements.items[r], 1);
            for (i = 0U; i < assocs->count; ++i) {
                if (str_eq(plan->requirements[i], requirements.items[r])) {
                    indices[hook_count++] = i;
                }
            }
            emit_planned_gnu_chain(&hook->body, assocs, indices, hook_count, "next");
            buf_puts(&hook->body, " \\\n");
            buf_puts(&tail, hook_name.p);
            buf_putc(&tail, '(');
            buf_free(&hook_name);
        }
        buf_puts(&tail, function->name);
        buf_puts(&tail, "__no_match_for_argument_type");
        for (r = 0U; r < requirements.count; ++r) buf_puts(&tail, ", __VA_ARGS__)");
        count = 0U;
        for (i = 0U; i < assocs->count; ++i) {
            if (plan->requirements[i][0] == '\0') indices[count++] = i;
        }
        emit_planned_gnu_chain(gnu_macro, assocs, indices, count,
                               tail.p != NULL ? tail.p : "0");
        buf_free(&tail);
        free(indices);
    }
    string_set_free(&requirements);
}

static void emit_hook_default(Buf *out, const DispatchHook *hook)
{
    buf_puts(out, "#ifndef ");
    buf_puts(out, hook->name);
    buf_puts(out, "\n#define ");
    buf_puts(out, hook->name);
    buf_puts(out, hook->gnu ? "(next, ...) next\n" : "(...)\n");
    buf_puts(out, "#endif\n");
}

static void emit_hook_definition(Buf *out, const DispatchHook *hook)
{
    buf_puts(out, "#undef ");
    buf_puts(out, hook->name);
    buf_puts(out, "\n#define ");
    buf_puts(out, hook->name);
    buf_puts(out, hook->gnu ? "(next, ...) \\\n" : "(...) \\\n");
    buf_puts(out, hook->body.p != NULL ? hook->body.p : "");
    buf_puts(out, "\n");
}

static void emit_dispatch_define(Buf *out, const char *name,
                                 const Buf *expression, int gnu)
{
    buf_puts(out, "#define ");
    buf_puts(out, name);
    buf_puts(out, gnu ? "(...) \\\n" : "(...) ");
    buf_puts(out, expression->p != NULL ? expression->p : "");
    buf_puts(out, "(__VA_ARGS__)\n\n");
}

static void emit_dispatch_macro(Buf *out, const char *name,
                                const TemplateInfo *ti,
                                const TemplateFunction *function,
                                const AssocList *assocs)
{
    DispatchPlan plan;
    Buf generic_macro, gnu_macro;
    size_t i;
    buf_init(&generic_macro);
    buf_init(&gnu_macro);
    build_dispatch_plan(&plan, ti, function, assocs, &generic_macro, &gnu_macro);
    emit_no_match_declaration(out, name);
    for (i = 0U; i < plan.hook_count; ++i) emit_hook_default(out, plan.hooks[i]);
    buf_puts(out, "#undef ");
    buf_puts(out, name);
    buf_putc(out, '\n');
    if (g_dispatch_backend == GEN_DISPATCH_AUTO) {
        buf_puts(out, "#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L\n");
        emit_function_signature(out, ti, function, NULL, name, 1);
        emit_line_directive(out, (unsigned long)function->start_line, ti->path);
        emit_dispatch_define(out, name, &generic_macro, 0);
        buf_puts(out, "#elif defined(__GNUC__) || defined(__clang__)\n");
        emit_function_signature(out, ti, function, NULL, name, 1);
        emit_line_directive(out, (unsigned long)function->start_line, ti->path);
        emit_dispatch_define(out, name, &gnu_macro, 1);
        buf_puts(out, "#else\n#error \"GEN_DISPATCH_AUTO requires C11 or GNU-compatible C\"\n#endif\n\n");
    } else if (g_dispatch_backend == GEN_DISPATCH_GENERIC ||
               g_dispatch_backend == GEN_DISPATCH_GNU) {
        int gnu = g_dispatch_backend == GEN_DISPATCH_GNU;
        emit_function_signature(out, ti, function, NULL, name, 1);
        emit_line_directive(out, (unsigned long)function->start_line, ti->path);
        emit_dispatch_define(out, name, gnu ? &gnu_macro : &generic_macro, gnu);
    }
    buf_free(&generic_macro);
    buf_free(&gnu_macro);
    dispatch_plan_free(&plan);
}

static int function_has_type_variable_mismatch(const char *function_name)
{
    size_t i;
    for (i = 0U; i < g_type_variable_mismatch_count; ++i) {
        if (str_eq(g_type_variable_mismatches[i].function_name, function_name)) {
            return 1;
        }
    }
    return 0;
}

static void emit_mismatch_only_macro(Buf *out, const TemplateFunction *function)
{
    char *return_type = normalize_spaces(function->return_type);
    buf_puts(out, "#undef ");
    buf_puts(out, function->name);
    buf_puts(out, "\n#define ");
    buf_puts(out, function->name);
    buf_puts(out, "(...) ");
    buf_puts(out, str_eq(return_type, "void") ? "((void)0)\n\n" : "(0)\n\n");
    free(return_type);
}

static void emit_dispatch(Buf *out, const TemplateInfo *ti)
{
    size_t i;
    if (g_dispatch_backend == GEN_DISPATCH_NONE) return;
    for (i = 0U; i < ti->function_count; ++i) {
        AssocList assocs;
        memset(&assocs, 0, sizeof(assocs));
        build_associations(ti->functions[i].name, &assocs);
        if (assocs.count != 0U) {
            size_t j;
            emit_line_directive(out,
                (unsigned long)ti->functions[i].start_line, ti->path);
            for (j = 0U; j < assocs.count; ++j) {
                GenInstance *instance = find_function_instance_for_symbol(
                    ti, &ti->functions[i], assocs.items[j].function_name);
                /* Specializations that need project types are declared in
                 * their requirement's section (emit_dispatch_section). */
                if (instance != NULL && instance_requirement(instance)[0] == '\0') {
                    emit_function_signature(out, ti, &ti->functions[i], instance,
                                            assocs.items[j].function_name, 1);
                    emit_function_signature(out, ti, &ti->functions[i], instance,
                                            assocs.items[j].function_name, 0);
                }
            }
            emit_dispatch_macro(out, ti->functions[i].name, ti,
                                &ti->functions[i], &assocs);
        } else if (function_has_type_variable_mismatch(
                       ti->functions[i].name)) {
            emit_line_directive(out,
                (unsigned long)ti->functions[i].start_line, ti->path);
            emit_mismatch_only_macro(out, &ti->functions[i]);
        }
        assoc_list_free(&assocs);
    }
}

/* The parts of a template's dispatch that need `requirement`: navigation
 * prototypes of its specializations and, when `hooks` is set, the hook
 * definitions that add them to the dispatch macros. */
static void emit_dispatch_section(Buf *out, const TemplateInfo *ti,
                                  const char *requirement, int hooks,
                                  const char *output_path)
{
    size_t i;
    if (g_dispatch_backend == GEN_DISPATCH_NONE) return;
    for (i = 0U; i < ti->function_count; ++i) {
        AssocList assocs;
        size_t j;
        int emitted_line = 0;
        memset(&assocs, 0, sizeof(assocs));
        build_associations(ti->functions[i].name, &assocs);
        for (j = 0U; j < assocs.count; ++j) {
            GenInstance *instance = find_function_instance_for_symbol(
                ti, &ti->functions[i], assocs.items[j].function_name);
            if (instance == NULL ||
                !str_eq(instance_requirement(instance), requirement)) continue;
            if (!emitted_line) {
                emit_line_directive(out,
                    (unsigned long)ti->functions[i].start_line, ti->path);
                emitted_line = 1;
            }
            emit_function_signature(out, ti, &ti->functions[i], instance,
                                    assocs.items[j].function_name, 1);
            emit_function_signature(out, ti, &ti->functions[i], instance,
                                    assocs.items[j].function_name, 0);
        }
        if (emitted_line) {
            emit_line_directive(out, (unsigned long)out->newlines + 2UL, output_path);
        }
        if (hooks && assocs.count != 0U) {
            DispatchPlan plan;
            Buf generic_macro, gnu_macro;
            buf_init(&generic_macro);
            buf_init(&gnu_macro);
            build_dispatch_plan(&plan, ti, &ti->functions[i], &assocs,
                                &generic_macro, &gnu_macro);
            for (j = 0U; j < plan.hook_count; ++j) {
                if (str_eq(plan.hooks[j]->requirement, requirement)) {
                    emit_hook_definition(out, plan.hooks[j]);
                }
            }
            buf_free(&generic_macro);
            buf_free(&gnu_macro);
            dispatch_plan_free(&plan);
        }
        assoc_list_free(&assocs);
    }
}

static void emit_type_variable_mismatch_assertions(Buf *out)
{
    size_t i;
    if (g_type_variable_mismatch_count == 0U) return;
    for (i = 0U; i < g_type_variable_mismatch_count; ++i) {
        const TypeVariableMismatch *mismatch = &g_type_variable_mismatches[i];
        Buf message;
        buf_init(&message);
        buf_puts(&message, "tmplgen: ");
        buf_puts(&message, mismatch->function_name);
        buf_puts(&message, " type variable ");
        buf_puts(&message, mismatch->parameter_name);
        buf_puts(&message,
                 " must be consistent across arguments (got ");
        buf_puts(&message, mismatch->first_type);
        buf_puts(&message, " vs ");
        buf_puts(&message, mismatch->second_type);
        buf_putc(&message, ')');
        buf_puts(out, "#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L\n");
        emit_line_directive(out, (unsigned long)mismatch->line,
                            mismatch->path);
        buf_puts(out, "_Static_assert(0, \"");
        emit_escaped_c_string(out, message.p != NULL ? message.p : "");
        buf_puts(out, "\");\n#elif defined(__GNUC__) || defined(__clang__)\n");
        emit_line_directive(out, (unsigned long)mismatch->line,
                            mismatch->path);
        buf_puts(out, "__extension__ _Static_assert(0, \"");
        emit_escaped_c_string(out, message.p != NULL ? message.p : "");
        buf_puts(out, "\");\n#else\n#error \"tmplgen type-variable mismatch requires C11 or GNU C\"\n#endif\n");
        buf_free(&message);
    }
    emit_line_directive(out, (unsigned long)out->newlines + 2UL, g_output_path);
}

static size_t template_dispatch_count(const TemplateInfo *ti)
{
    size_t i;
    size_t count = 0U;
    for (i = 0U; i < ti->function_count; ++i) {
        size_t j;
        int found = 0;
        for (j = 0U; j < g_generic_use_count; ++j) {
            if (str_eq(g_generic_uses[j].name, ti->functions[i].name) &&
                g_generic_uses[j].dispatch) {
                found = 1;
                break;
            }
        }
        if (found || function_has_type_variable_mismatch(ti->functions[i].name)) {
            ++count;
        }
    }
    return count;
}

static void emit_function_forward_declarations(Buf *out, size_t template_index,
                                              const TemplateInfo *ti,
                                              const char *requirement)
{
    size_t i;
    for (i = 0U; i < ti->function_count; ++i) {
        const TemplateFunction *function = &ti->functions[i];
        if (function->type_parameter_count == 0U) {
            if (requirement[0] == '\0') {
                emit_function_signature(out, ti, function, NULL, function->name, 0);
            }
        } else {
            size_t j;
            for (j = 0U; j < g_instance_count; ++j) {
                const GenInstance *instance = &g_instances[j];
                char *symbol;
                if (instance->template_index != template_index ||
                    !instance_matches_parameters(ti, instance,
                        function->type_parameters,
                        function->type_parameter_count) ||
                    !generic_use_matches(function->name, instance->name, 0) ||
                    !str_eq(instance_requirement(instance), requirement)) continue;
                symbol = join_suffix(function->name, instance->name);
                emit_function_signature(out, ti, function, instance, symbol, 0);
                free(symbol);
            }
        }
    }
    for (i = 0U; i < g_function_call_dependency_count; ++i) {
        const FunctionCallDependency *dependency =
            &g_function_call_dependencies[i];
        const TemplateInfo *callee_ti;
        const TemplateFunction *callee;
        size_t *indices;
        size_t j;
        GenInstance *instance;
        char *symbol;
        if (dependency->caller_template_index != template_index ||
            dependency->callee_template_index == template_index ||
            dependency->callee_template_index >= g_all_template_info_count) continue;
        callee_ti = &g_all_template_infos[dependency->callee_template_index];
        callee = find_template_function(callee_ti, dependency->callee_function);
        if (callee == NULL) continue;
        indices = (size_t *)malloc((callee->type_parameter_count + 1U) * sizeof(*indices));
        if (indices == NULL) die("out of memory");
        for (j = 0U; j < callee->type_parameter_count; ++j) {
            int parameter = template_parameter_index(callee_ti,
                callee->type_parameters[j]);
            if (parameter < 0) die("invalid cross-template prototype parameter map");
            indices[j] = (size_t)parameter;
        }
        instance = find_instance(dependency->callee_template_index,
            dependency->callee_instance, indices, callee->type_parameter_count);
        free(indices);
        if (instance == NULL ||
            !str_eq(instance_requirement(instance), requirement)) continue;
        symbol = join_suffix(callee->name, instance->name);
        emit_function_signature(out, callee_ti, callee, instance, symbol, 0);
        free(symbol);
    }
    buf_putc(out, '\n');
}

/* Requirements of everything a template module emits, sorted so that every
 * requirement follows its subsets. */
static void collect_module_requirements(size_t template_index, StringSet *out)
{
    size_t i;
    for (i = 0U; i < g_instance_count; ++i) {
        const char *requirement = instance_requirement(&g_instances[i]);
        if (g_instances[i].template_index == template_index && requirement[0] != '\0') {
            string_set_add(out, requirement);
        }
    }
    for (i = 0U; i < g_function_call_dependency_count; ++i) {
        const FunctionCallDependency *dependency = &g_function_call_dependencies[i];
        size_t j;
        if (dependency->caller_template_index != template_index ||
            dependency->callee_template_index == template_index) continue;
        for (j = 0U; j < g_instance_count; ++j) {
            const char *requirement = instance_requirement(&g_instances[j]);
            if (g_instances[j].template_index == dependency->callee_template_index &&
                str_eq(g_instances[j].name, dependency->callee_instance) &&
                requirement[0] != '\0') {
                string_set_add(out, requirement);
            }
        }
    }
    sort_requirements(out);
}

/* Whether `spelling` names the whole identifier `name`. */
static int spelling_names_type(const char *spelling, const char *name)
{
    size_t length = strlen(name);
    const char *p = spelling;
    while (*p != '\0') {
        if (is_ident_start(*p)) {
            const char *start = p;
            while (is_ident_char(*p)) ++p;
            if ((size_t)(p - start) == length && strncmp(start, name, length) == 0) {
                return 1;
            }
        } else {
            ++p;
        }
    }
    return 0;
}

static void emit_type_instance_ref(Buf *out, const TemplateInfo *ti,
                                   const TypeInstanceRef *ref, const char *stem,
                                   const char *output_path)
{
    const TemplateType *constructor = &ti->constructors[ref->constructor];
    emit_instance(out, ti, &g_instances[ref->instance], output_path, stem,
                  constructor->name, constructor->start_offset,
                  constructor->end_offset, constructor->start_line, 0);
}

/* Emit the parameterized specializations of the type constructors in this
 * requirement section, dependencies first. A specialization such as
 * Vec(Vec_Foo) names Vec_Foo in one of its arguments, so its definition must
 * follow the definition of Vec_Foo regardless of the order the source files
 * using them were scanned in. */
static void emit_type_instances(Buf *out, size_t template_index,
                                const TemplateInfo *ti, const char *stem,
                                const char *output_path, const char *requirement)
{
    TypeInstanceRef *refs = NULL;
    char **generated = NULL;
    size_t count = 0U, cap = 0U;
    size_t constructor_index, i;
    for (constructor_index = 0U; constructor_index < ti->constructor_count;
         ++constructor_index) {
        const TemplateType *constructor = &ti->constructors[constructor_index];
        size_t instance_index;
        if (constructor->parameter_count == 0U) continue;
        for (instance_index = 0U; instance_index < g_instance_count; ++instance_index) {
            GenInstance *instance = &g_instances[instance_index];
            if (instance->template_index != template_index ||
                !instance_matches_parameters(ti, instance,
                    constructor->parameter_names, constructor->parameter_count) ||
                !type_use_matches(constructor->name, instance->name) ||
                !str_eq(instance_requirement(instance), requirement)) continue;
            if (count == cap) {
                size_t new_cap = cap == 0U ? 8U : cap * 2U;
                TypeInstanceRef *next_refs;
                char **next_generated;
                if (new_cap < cap || new_cap > (size_t)-1 / sizeof(*refs)) {
                    die("too many type specializations");
                }
                next_refs = (TypeInstanceRef *)realloc(refs, new_cap * sizeof(*refs));
                next_generated = (char **)realloc(generated, new_cap * sizeof(*generated));
                if (next_refs == NULL || next_generated == NULL) die("out of memory");
                refs = next_refs;
                generated = next_generated;
                cap = new_cap;
            }
            refs[count].instance = instance_index;
            refs[count].constructor = constructor_index;
            generated[count] = join_suffix(constructor->name, instance->name);
            ++count;
        }
    }
    if (count != 0U) {
        size_t *remaining = (size_t *)calloc(count, sizeof(*remaining));
        size_t **dependents = (size_t **)calloc(count, sizeof(*dependents));
        size_t *dependent_count = (size_t *)calloc(count, sizeof(*dependent_count));
        size_t *dependent_cap = (size_t *)calloc(count, sizeof(*dependent_cap));
        size_t *ready = (size_t *)malloc(count * sizeof(*ready));
        unsigned char *emitted = (unsigned char *)calloc(count, sizeof(*emitted));
        size_t head = 0U, tail = 0U;
        size_t candidate;
        if (remaining == NULL || dependents == NULL || dependent_count == NULL ||
            dependent_cap == NULL || ready == NULL || emitted == NULL) {
            die("out of memory");
        }
        for (candidate = 0U; candidate < count; ++candidate) {
            size_t other;
            for (other = 0U; other < count; ++other) {
                size_t argument;
                if (other == candidate) continue;
                for (argument = 0U;
                     argument < g_instances[refs[candidate].instance].argument_count;
                     ++argument) {
                    const char *spelling =
                        g_instances[refs[candidate].instance].arguments[argument].c_type;
                    if (!spelling_names_type(spelling, generated[other])) continue;
                    if (dependent_count[other] == dependent_cap[other]) {
                        size_t new_cap = dependent_cap[other] == 0U ? 4U :
                            dependent_cap[other] * 2U;
                        size_t *next;
                        if (new_cap < dependent_cap[other] ||
                            new_cap > (size_t)-1 / sizeof(*dependents[other])) {
                            die("too many type dependencies");
                        }
                        next = (size_t *)realloc(dependents[other],
                                                 new_cap * sizeof(*dependents[other]));
                        if (next == NULL) die("out of memory");
                        dependents[other] = next;
                        dependent_cap[other] = new_cap;
                    }
                    dependents[other][dependent_count[other]++] = candidate;
                    ++remaining[candidate];
                    break;
                }
            }
        }
        for (candidate = 0U; candidate < count; ++candidate) {
            if (remaining[candidate] == 0U) ready[tail++] = candidate;
        }
        while (head < tail) {
            size_t pick = ready[head++];
            size_t dependent;
            emitted[pick] = 1U;
            emit_type_instance_ref(out, ti, &refs[pick], stem, output_path);
            for (dependent = 0U; dependent < dependent_count[pick]; ++dependent) {
                size_t next = dependents[pick][dependent];
                if (--remaining[next] == 0U) ready[tail++] = next;
            }
        }
        /* A dependency cycle cannot be ordered; keep the registration order. */
        for (candidate = 0U; candidate < count; ++candidate) {
            if (emitted[candidate]) continue;
            emit_type_instance_ref(out, ti, &refs[candidate], stem, output_path);
        }
        for (i = 0U; i < count; ++i) free(dependents[i]);
        free(dependent_cap);
        free(dependent_count);
        free(dependents);
        free(remaining);
        free(ready);
        free(emitted);
    }
    for (i = 0U; i < count; ++i) free(generated[i]);
    free(generated);
    free(refs);
}

/* Specializations (types, prototypes and bodies) of one template that need
 * exactly `requirement` ("" for those that need no project type). */
static void emit_module_part(Buf *out, size_t template_index,
                             const TemplateInfo *ti, const char *stem,
                             const char *output_path, Buf *implementation,
                             const char *requirement)
{
    GenInstance empty_instance;
    size_t i;
    int core = requirement[0] == '\0';
    memset(&empty_instance, 0, sizeof(empty_instance));
    empty_instance.name = "";
    empty_instance.template_index = template_index;
    for (i = 0U; i < ti->constructor_count; ++i) {
        const TemplateType *constructor = &ti->constructors[i];
        if (constructor->parameter_count != 0U || !core) continue;
        emit_instance(out, ti, &empty_instance, output_path, stem,
                      constructor->name, constructor->start_offset,
                      constructor->end_offset, constructor->start_line, 0);
    }
    emit_type_instances(out, template_index, ti, stem, output_path, requirement);
    emit_function_forward_declarations(out, template_index, ti, requirement);
    for (i = 0U; i < ti->function_count; ++i) {
        const TemplateFunction *function = &ti->functions[i];
        size_t j;
        if (function->type_parameter_count == 0U) {
            if (core) {
                emit_instance(out, ti, &empty_instance, output_path, stem,
                              function->name, function->start_offset,
                              function->end_offset, function->start_line, 0);
            }
            continue;
        }
        for (j = 0U; j < g_instance_count; ++j) {
            GenInstance *instance = &g_instances[j];
            if (instance->template_index != template_index ||
                !instance_matches_parameters(ti, instance,
                    function->type_parameters, function->type_parameter_count) ||
                !generic_use_matches(function->name, instance->name, 0) ||
                !str_eq(instance_requirement(instance), requirement)) continue;
            if (implementation != NULL) {
                char *implementation_path = generated_module_source_path(stem);
                emit_instance(implementation, ti, instance, implementation_path,
                              stem, function->name, function->start_offset,
                              function->end_offset, function->start_line, 1);
                free(implementation_path);
            } else {
                emit_instance(out, ti, instance, output_path, stem, function->name,
                              function->start_offset, function->end_offset,
                              function->start_line, 0);
            }
        }
    }
}

void buf_put_section_open(Buf *out, const char *have_prefix,
                                 const char *requirement)
{
    buf_puts(out, "#if ");
    buf_put_requirement_condition(out, requirement);
    buf_puts(out, " && !defined(");
    buf_puts(out, have_prefix);
    buf_put_requirement_id(out, requirement);
    buf_puts(out, ")\n#define ");
    buf_puts(out, have_prefix);
    buf_put_requirement_id(out, requirement);
    buf_putc(out, '\n');
}

/* Relative path from directory `from` to file `to` (both absolute). */
char *relative_path_between(const char *from, const char *to)
{
    size_t common = 0U;
    size_t i;
    Buf path;
    while (from[common] != '\0' && from[common] == to[common]) ++common;
    if (!((from[common] == '\0' && (to[common] == '/' || to[common] == '\\')) ||
          (common != 0U && (from[common - 1U] == '/' || from[common - 1U] == '\\')))) {
        while (common != 0U && from[common - 1U] != '/' && from[common - 1U] != '\\') {
            --common;
        }
    }
    buf_init(&path);
    for (i = common; from[i] != '\0'; ++i) {
        if ((i == common && from[i] != '/' && from[i] != '\\') ||
            ((from[i] == '/' || from[i] == '\\') && from[i + 1U] != '\0')) {
            buf_puts(&path, "../");
        }
    }
    while (to[common] == '/' || to[common] == '\\') ++common;
    buf_puts(&path, to + common);
    return path.p != NULL ? path.p : copy_string(to);
}

/* Whether a requirement names module `guard`: its include guard or one of
 * its section markers. */
static int requirement_names_module(const char *requirement, const char *guard)
{
    size_t guard_length = strlen(guard);
    while (*requirement != '\0') {
        const char *comma = strchr(requirement, ',');
        size_t length = comma != NULL ? (size_t)(comma - requirement) : strlen(requirement);
        if (length == guard_length && strncmp(requirement, guard, length) == 0) return 1;
        if (length > 13U + guard_length + 1U &&
            strncmp(requirement, "TMPLGEN_HAVE_", 13U) == 0 &&
            strncmp(requirement + 13U, guard, guard_length) == 0 &&
            requirement[13U + guard_length] == '_') return 1;
        requirement += length;
        if (*requirement == ',') ++requirement;
    }
    return 0;
}

/* Before the sections: the modules this one depends on, included again so
 * that their sections (which the sections below may need) run first. A
 * module is skipped while it is being included from here, so modules that
 * depend on each other do not recurse. */
static void emit_module_dependency_includes(Buf *out, size_t template_index,
                                            const TemplateInfo *ti, const char *stem,
                                            const char *guard,
                                            const StringSet *requirements)
{
    StringSet stems;
    size_t i;
    memset(&stems, 0, sizeof(stems));
    for (i = 0U; i < ti->include_count; ++i) {
        char *target_stem = template_include_target(ti->includes[i], stem);
        if (target_stem != NULL) string_set_add(&stems, target_stem);
        free(target_stem);
    }
    for (i = 0U; i < gen_template_count(); ++i) {
        char *other_guard;
        size_t r;
        if (i == template_index) continue;
        other_guard = generated_module_guard(g_templates[i].stem);
        for (r = 0U; r < requirements->count; ++r) {
            if (requirement_names_module(requirements->items[r], other_guard)) {
                string_set_add(&stems, g_templates[i].stem);
                break;
            }
        }
        free(other_guard);
    }
    if (stems.count != 0U) {
        buf_puts(out, "#ifndef TMPLGEN_INCLUDING_");
        buf_puts(out, guard);
        buf_puts(out, "\n#define TMPLGEN_INCLUDING_");
        buf_puts(out, guard);
        buf_putc(out, '\n');
        for (i = 0U; i < stems.count; ++i) {
            char *include_path = relative_module_include(stem, stems.items[i]);
            emit_include(out, include_path);
            free(include_path);
        }
        buf_puts(out, "#undef TMPLGEN_INCLUDING_");
        buf_puts(out, guard);
        buf_puts(out, "\n#endif\n");
    }
    string_set_free(&stems);
}

void emit_template_module(Buf *out, size_t template_index,
                                 const TemplateInfo *ti, const char *stem,
                                 const char *output_path,
                                 const char *guard, Buf *implementation)
{
    StringSet includes;
    StringSet requirements;
    Buf have_prefix;
    size_t i;
    memset(&includes, 0, sizeof(includes));
    memset(&requirements, 0, sizeof(requirements));
    collect_module_requirements(template_index, &requirements);
    if (implementation != NULL) {
        Buf header_name;
        StringSet headers;
        memset(&headers, 0, sizeof(headers));
        /* The implementation file is one translation unit, so it can include
         * the declaring header of every argument type up front. */
        for (i = 0U; i < requirements.count; ++i) {
            const char *requirement = requirements.items[i];
            while (*requirement != '\0') {
                const char *comma = strchr(requirement, ',');
                size_t length = comma != NULL ? (size_t)(comma - requirement) :
                                                strlen(requirement);
                char *guard_name = copy_range(requirement, length);
                const char *header = header_path_for_guard(guard_name);
                if (header != NULL && !string_set_has(&headers, header)) {
                    char *relative = relative_path_between(g_generated_dir, header);
                    string_set_add(&headers, header);
                    emit_include(implementation, relative);
                    free(relative);
                }
                free(guard_name);
                requirement += length;
                if (*requirement == ',') ++requirement;
            }
        }
        string_set_free(&headers);
        /* Bodies that use reflection need its API and its typetag_of entries
         * for the argument types, whose headers are all included above. */
        if (template_uses_reflection(ti) && g_reflect_mode != REFLECT_NONE) {
            emit_include(implementation, "reflection.h");
        }
        buf_init(&header_name);
        buf_puts(&header_name, stem);
        buf_puts(&header_name, ".gen.h");
        buf_puts(implementation, "#define TMPLGEN_AGGREGATING\n");
        emit_include(implementation, header_name.p);
        buf_puts(implementation, "#undef TMPLGEN_AGGREGATING\n\n");
        buf_free(&header_name);
    }
    emit_preamble(out, guard, ti, 1U, stem, &includes);
    emit_module_part(out, template_index, ti, stem, output_path, implementation, "");
    buf_puts(out, "#ifndef TMPLGEN_AGGREGATING\n");
    emit_type_functors(out, ti);
    emit_explicit_function_macros(out, ti);
    if (template_dispatch_count(ti) != 0U &&
        g_dispatch_backend != GEN_DISPATCH_NONE) {
        emit_arg_helpers(out, highest_function_argument(ti, 1U));
        emit_dispatch(out, ti);
    }
    buf_puts(out, "#endif /* TMPLGEN_AGGREGATING */\n");
    buf_puts(out, "#endif /* ");
    buf_puts(out, guard);
    buf_puts(out, " */\n");
    if (requirements.count != 0U) {
        buf_puts(out,
            "\n/* Specializations that name project types. Each section is\n"
            "   compiled once the header declaring those types (identified by its\n"
            "   include guard) has been included; include this header after them,\n"
            "   or again. */\n");
        emit_module_dependency_includes(out, template_index, ti, stem, guard,
                                        &requirements);
        buf_init(&have_prefix);
        buf_puts(&have_prefix, "TMPLGEN_HAVE_");
        buf_puts(&have_prefix, guard);
        buf_putc(&have_prefix, '_');
        for (i = 0U; i < requirements.count; ++i) {
            buf_put_section_open(out, have_prefix.p, requirements.items[i]);
            emit_module_part(out, template_index, ti, stem, output_path,
                             implementation, requirements.items[i]);
            emit_dispatch_section(out, ti, requirements.items[i], 1, output_path);
            buf_puts(out, "#endif\n");
        }
        buf_free(&have_prefix);
    }
    string_set_free(&requirements);
    string_set_free(&includes);
}

void emit_aggregate_header(Buf *out, const TemplateInfo *infos,
                                  size_t template_count)
{
    size_t i;
    size_t dispatch_count = 0U;
    StringSet requirements;
    buf_puts(out, "/* AUTO-GENERATED aggregate. DO NOT EDIT.\n"
                  "   Include it after the headers that declare the types you use\n"
                  "   as template arguments; including it again later is fine. */\n");
    buf_puts(out, "#ifndef TMPLGEN_AGGREGATING\n#define TMPLGEN_AGGREGATING\n");
    buf_puts(out, "#define TMPLGEN_AGGREGATING_LOCAL\n#endif\n");
    for (i = 0U; i < template_count; ++i) {
        Buf relative;
        buf_init(&relative);
        buf_puts(&relative, g_templates[i].stem);
        buf_puts(&relative, ".gen.h");
        emit_include(out, relative.p);
        buf_free(&relative);
    }
    buf_puts(out, "#ifdef TMPLGEN_AGGREGATING_LOCAL\n#undef TMPLGEN_AGGREGATING_LOCAL\n");
    buf_puts(out, "#undef TMPLGEN_AGGREGATING\n#endif\n\n");
    buf_puts(out, "#ifndef " GEN_GUARD "\n#define " GEN_GUARD "\n");
    for (i = 0U; i < template_count; ++i) {
        emit_type_functors(out, &infos[i]);
        emit_explicit_function_macros(out, &infos[i]);
    }
    if (g_dispatch_backend != GEN_DISPATCH_NONE) {
        for (i = 0U; i < template_count; ++i) {
            dispatch_count += template_dispatch_count(&infos[i]);
        }
        if (dispatch_count != 0U) {
            buf_puts(out, "/* Dispatch macros for all generated template functions. */\n");
            emit_arg_helpers(out,
                highest_function_argument(infos, template_count));
            for (i = 0U; i < template_count; ++i) {
                emit_dispatch(out, &infos[i]);
            }
        }
    }
    emit_type_variable_mismatch_assertions(out);
    buf_puts(out, "\n#endif /* " GEN_GUARD " */\n");
    memset(&requirements, 0, sizeof(requirements));
    if (g_dispatch_backend != GEN_DISPATCH_NONE && dispatch_count != 0U) {
        /* Absolute like the module paths, so the text does not depend on
         * how --output was spelled (standalone vs. plugins). */
        char *aggregate_path = absolute_path(g_output_path);
        for (i = 0U; i < template_count; ++i) {
            collect_module_requirements(i, &requirements);
        }
        sort_requirements(&requirements);
        for (i = 0U; i < requirements.count; ++i) {
            size_t t;
            buf_put_section_open(out, "TMPLGEN_HAVE_" GEN_GUARD "_",
                                 requirements.items[i]);
            for (t = 0U; t < template_count; ++t) {
                emit_dispatch_section(out, &infos[t], requirements.items[i], 0,
                                      aggregate_path);
            }
            buf_puts(out, "#endif\n");
        }
        free(aggregate_path);
    }
    string_set_free(&requirements);
}

char *generated_module_path(const char *stem)
{
    Buf relative;
    char *path;
    buf_init(&relative);
    buf_puts(&relative, stem);
    buf_puts(&relative, ".gen.h");
    path = join_path(g_generated_dir, relative.p);
    buf_free(&relative);
    return path;
}

static void emit_type_functors(Buf *out, const TemplateInfo *ti)
{
    StringSet emitted;
    size_t i;
    memset(&emitted, 0, sizeof(emitted));
    for (i = 0U; i < ti->constructor_count; ++i) {
        const char *name = ti->constructors[i].name;
        const TemplateType *constructor = &ti->constructors[i];
        size_t k;
        if (constructor->parameter_count == 0U) continue;
        if (!string_set_has(&emitted, name)) {
            string_set_add(&emitted, name);
            emit_line_directive(out, (unsigned long)template_type_name_line(
                                          ti, constructor), ti->path);
            buf_puts(out, "#undef ");
            buf_puts(out, name);
            buf_putc(out, '\n');
            buf_puts(out, "#define ");
            buf_puts(out, name);
            buf_putc(out, '(');
            for (k = 0U; k < constructor->parameter_count; ++k) {
                if (k != 0U) buf_puts(out, ", ");
                buf_puts(out, "type");
                buf_put_uint(out, (unsigned long)k);
            }
            buf_puts(out, ") ");
            if (constructor->is_struct_tag) buf_puts(out, "struct ");
            else if (constructor->is_union_tag) buf_puts(out, "union ");
            buf_puts(out, name);
            for (k = 0U; k < constructor->parameter_count; ++k) {
                if (k == 0U) buf_puts(out, "_##type0");
                else buf_puts(out, "##_##type");
                if (k != 0U) buf_put_uint(out, (unsigned long)k);
            }
            buf_putc(out, '\n');
        }
    }
    if (emitted.count != 0U) buf_putc(out, '\n');
    string_set_free(&emitted);
}

static void emit_explicit_function_macros(Buf *out, const TemplateInfo *ti)
{
    size_t i;
    for (i = 0U; i < ti->function_count; ++i) {
        const TemplateFunction *function = &ti->functions[i];
        size_t explicit_count = function_explicit_count(function);
        size_t ordinary_count;
        size_t k;
        if (explicit_count == 0U) continue;
        ordinary_count = function->parameter_count - explicit_count;
        emit_line_directive(out, (unsigned long)function->start_line, ti->path);
        buf_puts(out, "#undef ");
        buf_puts(out, function->name);
        buf_puts(out, "\n#define ");
        buf_puts(out, function->name);
        buf_putc(out, '(');
        for (k = 0U; k < explicit_count; ++k) {
            if (k != 0U) buf_puts(out, ", ");
            buf_puts(out, "type");
            buf_put_uint(out, (unsigned long)k);
        }
        buf_puts(out, ordinary_count != 0U ? ", ...) " : ") ");
        buf_puts(out, function->name);
        for (k = 0U; k < function->type_parameter_count; ++k) {
            int explicit_index = function_explicit_index(function,
                function->type_parameters[k]);
            if (explicit_index < 0) break;
            buf_puts(out, k == 0U ? "_##type" : "##_##type");
            buf_put_uint(out, (unsigned long)explicit_index);
        }
        buf_puts(out, ordinary_count != 0U ? "(__VA_ARGS__)\n" : "()\n");
    }
}

int macro_type_arguments(CXTranslationUnit tu, CXCursor cursor,
                                char ***arguments, size_t **token_counts,
                                size_t *argument_count)
{
    CXToken *tokens = NULL;
    unsigned count = 0U;
    unsigned i;
    unsigned open = 0U;
    unsigned start;
    size_t parens = 0U, brackets = 0U, braces = 0U;
    size_t capacity = 0U, count_capacity = 0U;
    size_t *counts = NULL;
    int found_open = 0;
    int okay = 1;
    clang_tokenize(tu, clang_getCursorExtent(cursor), &tokens, &count);
    for (i = 0U; i < count; ++i) {
        char *text = copy_cx_string(clang_getTokenSpelling(tu, tokens[i]));
        if (str_eq(text, "(")) {
            open = i;
            found_open = 1;
            free(text);
            break;
        }
        free(text);
    }
    if (!found_open) okay = 0;
    start = open + 1U;
    for (i = start; okay && i < count; ++i) {
        char *text = copy_cx_string(clang_getTokenSpelling(tu, tokens[i]));
        int boundary = 0;
        int closing = str_eq(text, ")");
        if (str_eq(text, "(")) ++parens;
        else if (closing) {
            if (parens == 0U && brackets == 0U && braces == 0U) boundary = 1;
            else if (parens != 0U) --parens;
        } else if (str_eq(text, "[")) ++brackets;
        else if (str_eq(text, "]")) {
            if (brackets != 0U) --brackets;
        } else if (str_eq(text, "{")) ++braces;
        else if (str_eq(text, "}")) {
            if (braces != 0U) --braces;
        } else if (str_eq(text, ",") && parens == 0U && brackets == 0U && braces == 0U) {
            boundary = 1;
        }
        free(text);
        if (!boundary) continue;
        if (start == i && closing && *argument_count == 0U) break;
        if (start >= i) {
            okay = 0;
            break;
        }
        {
            Buf joined;
            unsigned k;
            char *argument;
            buf_init(&joined);
            for (k = start; k < i; ++k) {
                char *piece = copy_cx_string(
                    clang_getTokenSpelling(tu, tokens[k]));
                if (k != start) buf_putc(&joined, ' ');
                buf_puts(&joined, piece);
                free(piece);
            }
            argument = joined.p != NULL ? joined.p : copy_string("");
            list_add(arguments, argument_count, &capacity, argument);
            if (*argument_count > count_capacity) {
                size_t new_cap = count_capacity == 0U ? 4U : count_capacity * 2U;
                size_t *next = (size_t *)realloc(counts, new_cap * sizeof(*counts));
                if (next == NULL) die("out of memory");
                counts = next;
                count_capacity = new_cap;
            }
            counts[*argument_count - 1U] = (size_t)(i - start);
            free(argument);
        }
        if (closing) break;
        start = i + 1U;
    }
    if (*argument_count == 0U) okay = 0;
    clang_disposeTokens(tu, tokens, count);
    if (!okay) {
        list_free(*arguments, *argument_count);
        *argument_count = 0U;
        free(counts);
        counts = NULL;
    }
    *token_counts = counts;
    return okay;
}

static void emit_bootstrap_function_prototypes(Buf *out,
                                               const TemplateInfo *infos,
                                               size_t template_count,
                                               const char *requirement)
{
    size_t i;
    for (i = 0U; i < template_count; ++i) {
        size_t j;
        for (j = 0U; j < infos[i].function_count; ++j) {
            const TemplateFunction *function = &infos[i].functions[j];
            size_t k;
            if ((!function_has_explicit_parameters(function) &&
                 function->dispatch_count == 0U) ||
                function->type_parameter_count == 0U) continue;
            for (k = 0U; k < g_instance_count; ++k) {
                const GenInstance *instance = &g_instances[k];
                Buf return_type;
                char *symbol;
                size_t p;
                if (instance->template_index != i ||
                    !instance_matches_parameters(&infos[i], instance,
                        function->type_parameters, function->type_parameter_count) ||
                    !str_eq(instance_requirement(instance), requirement)) continue;
                buf_init(&return_type);
                if (function->return_application.constructor != NULL) {
                    rewrite_c_type(function->return_type, &infos[i], instance,
                                   &function->return_application, &return_type);
                } else {
                    rewrite(function->return_type, &infos[i], instance,
                            &return_type);
                }
                symbol = join_suffix(function->name, instance->name);
                buf_puts(out, "extern ");
                buf_puts(out, return_type.p != NULL ? return_type.p : "void");
                buf_putc(out, ' ');
                buf_puts(out, symbol);
                buf_putc(out, '(');
                {
                    size_t emitted_parameters = 0U;
                for (p = 0U; p < function->parameter_count; ++p) {
                    Buf argument_type;
                    if (function->parameters[p].explicit_type) continue;
                    if (emitted_parameters++ != 0U) buf_puts(out, ", ");
                    buf_init(&argument_type);
                    if (function->parameters[p].application.constructor != NULL) {
                        rewrite_c_type(function->parameters[p].type, &infos[i], instance,
                                       &function->parameters[p].application,
                                       &argument_type);
                    } else {
                        rewrite(function->parameters[p].type, &infos[i], instance,
                                &argument_type);
                    }
                    buf_puts(out, argument_type.p != NULL ? argument_type.p : "int");
                    buf_free(&argument_type);
                }
                    if (emitted_parameters == 0U) buf_puts(out, "void");
                }
                buf_puts(out, ");\n");
                free(symbol);
                buf_free(&return_type);
            }
        }
    }
    buf_putc(out, '\n');
}

static void emit_bootstrap_function_macro(Buf *out, const char *name,
                                          const TemplateFunction *function,
                                          const Buf *expression)
{
    size_t i;
    buf_puts(out, "#undef ");
    buf_puts(out, name);
    buf_puts(out, "\n#define ");
    buf_puts(out, name);
    buf_puts(out, "(...) (");
    for (i = 0U; i < function->parameter_count; ++i) {
        if (i != 0U) buf_puts(out, ", ");
        buf_puts(out, "(void)GEN_ARG");
        buf_put_uint(out, (unsigned long)i);
        buf_puts(out, "(__VA_ARGS__)");
    }
    buf_puts(out, ", \\\n");
    buf_puts(out, expression->p != NULL ? expression->p : "0");
    buf_puts(out, "(__VA_ARGS__))\n");
}

/* Bootstrap dispatch mirrors the generated one: associations that name
 * project types are added by hooks defined in requirement sections, so a
 * translation unit that lacks those types still parses cleanly.
 * With `requirement` == NULL this emits the core macros, otherwise only the
 * hooks for that requirement. */
static void emit_bootstrap_function_macros(Buf *out, const TemplateInfo *infos,
                                           size_t template_count,
                                           const char *requirement)
{
    size_t i;
    for (i = 0U; i < template_count; ++i) {
        size_t j;
        for (j = 0U; j < infos[i].function_count; ++j) {
            const TemplateFunction *function = &infos[i].functions[j];
            AssocList assocs;
            if (function->dispatch_count == 0U ||
                function->type_parameter_count == 0U) continue;
            memset(&assocs, 0, sizeof(assocs));
            build_bootstrap_associations(i, &infos[i], function, &assocs);
            if (assocs.count != 0U) {
                DispatchPlan plan;
                Buf generic_expression, gnu_expression;
                int saved_backend = g_dispatch_backend;
                size_t h;
                buf_init(&generic_expression);
                buf_init(&gnu_expression);
                g_dispatch_backend = GEN_DISPATCH_AUTO;
                build_dispatch_plan(&plan, &infos[i], function, &assocs,
                                    &generic_expression, &gnu_expression);
                g_dispatch_backend = saved_backend;
                if (requirement == NULL) {
                    emit_no_match_declaration(out, function->name);
                    for (h = 0U; h < plan.hook_count; ++h) {
                        emit_hook_default(out, plan.hooks[h]);
                    }
                    buf_puts(out, "#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L\n");
                    emit_bootstrap_function_macro(out, function->name, function,
                                                  &generic_expression);
                    buf_puts(out, "#elif defined(__GNUC__) || defined(__clang__)\n");
                    emit_bootstrap_function_macro(out, function->name, function,
                                                  &gnu_expression);
                    buf_puts(out, "#else\n#error \"tmplgen bootstrap dispatch requires C11 or GNU C\"\n#endif\n\n");
                } else {
                    for (h = 0U; h < plan.hook_count; ++h) {
                        if (str_eq(plan.hooks[h]->requirement, requirement)) {
                            emit_hook_definition(out, plan.hooks[h]);
                        }
                    }
                }
                buf_free(&generic_expression);
                buf_free(&gnu_expression);
                dispatch_plan_free(&plan);
            } else if (requirement == NULL &&
                       function_has_constructor_parameters(&infos[i], function)) {
                /* Constructor parameters can be ambiguous in the first pass,
                 * where every F(T) collapses to T. Always provide a second
                 * pass macro so the exact specialization name can be matched. */
                Buf no_match;
                buf_init(&no_match);
                emit_no_match_declaration(out, function->name);
                buf_puts(&no_match, function->name);
                buf_puts(&no_match, "__no_match_for_argument_type");
                emit_bootstrap_function_macro(out, function->name, function,
                                              &no_match);
                buf_free(&no_match);
            } else if (requirement == NULL &&
                       function_has_type_variable_mismatch(function->name)) {
                emit_mismatch_only_macro(out, function);
            }
            assoc_list_free(&assocs);
        }
    }
}

void emit_bootstrap_header(Buf *out, const TemplateInfo *infos,
                                  size_t template_count, int second_pass)
{
    StringSet includes;
    StringSet type_stubs;
    size_t i;
    memset(&includes, 0, sizeof(includes));
    memset(&type_stubs, 0, sizeof(type_stubs));
    emit_preamble(out, GEN_GUARD, infos, template_count, NULL, &includes);
    for (i = 0U; i < template_count; ++i) {
        GenInstance empty_instance;
        size_t j;
        memset(&empty_instance, 0, sizeof(empty_instance));
        empty_instance.name = "";
        empty_instance.template_index = i;
        for (j = 0U; j < infos[i].constructor_count; ++j) {
            const TemplateType *constructor = &infos[i].constructors[j];
            if (constructor->parameter_count == 0U) {
                emit_instance(out, &infos[i], &empty_instance, g_output_path,
                              g_templates[i].stem, constructor->name,
                              constructor->start_offset, constructor->end_offset,
                              constructor->start_line, 0);
            }
        }
        for (j = 0U; j < infos[i].function_count; ++j) {
            const TemplateFunction *function = &infos[i].functions[j];
            if (function->type_parameter_count == 0U) {
                emit_instance(out, &infos[i], &empty_instance, g_output_path,
                              g_templates[i].stem, function->name,
                              function->start_offset, function->end_offset,
                              function->start_line, 0);
            }
        }
    }
    emit_arg_helpers(out, highest_function_argument(infos, template_count));
    if (second_pass) {
        for (i = 0U; i < g_type_use_count; ++i) {
            char *generated_name = join_suffix(g_type_uses[i].functor,
                                               g_type_uses[i].type_name);
            if (!string_set_has(&type_stubs, generated_name)) {
                size_t type_template_index = 0U;
                const TemplateType *constructor = NULL;
                string_set_add(&type_stubs, generated_name);
                (void)find_type_constructor(infos, template_count,
                    g_type_uses[i].functor, &type_template_index, &constructor);
                buf_puts(out, "typedef ");
                if (constructor != NULL && constructor->is_union_tag) {
                    buf_puts(out, "union ");
                } else {
                    buf_puts(out, "struct ");
                }
                buf_puts(out, generated_name);
                buf_puts(out, " { unsigned char _tmplgen_placeholder; } ");
                buf_puts(out, generated_name);
                buf_puts(out, ";\n");
            }
            free(generated_name);
        }
        if (type_stubs.count != 0U) buf_putc(out, '\n');
    }
    for (i = 0U; i < template_count; ++i) {
        size_t j;
        for (j = 0U; j < infos[i].constructor_count; ++j) {
            const TemplateType *constructor = &infos[i].constructors[j];
            size_t k;
            if (constructor->parameter_count == 0U) continue;
            buf_puts(out, "#undef ");
            buf_puts(out, constructor->name);
            buf_puts(out, "\n#define ");
            buf_puts(out, constructor->name);
            buf_putc(out, '(');
            for (k = 0U; k < constructor->parameter_count; ++k) {
                if (k != 0U) buf_puts(out, ", ");
                buf_puts(out, "tmplgen_arg");
                buf_put_uint(out, (unsigned long)k);
            }
            buf_puts(out, ") ");
            if (second_pass) {
                if (constructor->is_struct_tag) buf_puts(out, "struct ");
                else if (constructor->is_union_tag) buf_puts(out, "union ");
                buf_puts(out, constructor->name);
                for (k = 0U; k < constructor->parameter_count; ++k) {
                    if (k == 0U) buf_puts(out, "_##tmplgen_arg0");
                    else {
                        buf_puts(out, "##_##tmplgen_arg");
                        buf_put_uint(out, (unsigned long)k);
                    }
                }
                buf_putc(out, '\n');
            } else {
                buf_puts(out, "tmplgen_arg0\n");
            }
        }
    }
    if (second_pass) {
        for (i = 0U; i < template_count; ++i) {
            emit_explicit_function_macros(out, &infos[i]);
        }
        emit_bootstrap_function_prototypes(out, infos, template_count, "");
        emit_bootstrap_function_macros(out, infos, template_count, NULL);
    } else {
        for (i = 0U; i < template_count; ++i) {
            size_t j;
            for (j = 0U; j < infos[i].function_count; ++j) {
                const TemplateFunction *function = &infos[i].functions[j];
                size_t k;
                if (function_has_explicit_parameters(function)) {
                    buf_puts(out, "#undef ");
                    buf_puts(out, function->name);
                    buf_puts(out, "\n#define ");
                    buf_puts(out, function->name);
                    if (str_eq(function->return_type, "void")) {
                        buf_puts(out, "(...) ((void)0)\n");
                    } else {
                        buf_puts(out, "(type0, ...) ((type0){0})\n");
                    }
                    continue;
                }
                if (function->dispatch_count == 0U ||
                    function->type_parameter_count == 0U) continue;
                buf_puts(out, "#undef ");
                buf_puts(out, function->name);
                buf_puts(out, "\n#define ");
                buf_puts(out, function->name);
                buf_puts(out, "(...) (");
                for (k = 0U; k < function->parameter_count; ++k) {
                    if (k != 0U) buf_puts(out, ", ");
                    buf_puts(out, "(void)GEN_ARG");
                    buf_put_uint(out, (unsigned long)k);
                    buf_puts(out, "(__VA_ARGS__)");
                }
                buf_puts(out, ", GEN_ARG");
                buf_put_uint(out, (unsigned long)function->dispatch_indices[0]);
                buf_puts(out, "(__VA_ARGS__))\n");
            }
        }
    }
    if (second_pass) buf_puts(out, "#define TMPLGEN_BOOTSTRAP_READY\n");
    buf_puts(out, "\n#endif /* " GEN_GUARD " */\n");
    if (second_pass) {
        StringSet requirements;
        memset(&requirements, 0, sizeof(requirements));
        for (i = 0U; i < template_count; ++i) {
            collect_module_requirements(i, &requirements);
        }
        sort_requirements(&requirements);
        /* Every generated path maps to this same text, so nested includes
         * from the core (template includes of other modules) reach here
         * before the core's stubs exist; wait until it is complete. */
        if (requirements.count != 0U) {
            buf_puts(out, "#ifdef TMPLGEN_BOOTSTRAP_READY\n");
        }
        for (i = 0U; i < requirements.count; ++i) {
            buf_put_section_open(out, "TMPLGEN_HAVE_BOOTSTRAP_",
                                 requirements.items[i]);
            emit_bootstrap_function_prototypes(out, infos, template_count,
                                               requirements.items[i]);
            emit_bootstrap_function_macros(out, infos, template_count,
                                           requirements.items[i]);
            buf_puts(out, "#endif\n");
        }
        if (requirements.count != 0U) buf_puts(out, "#endif\n");
        string_set_free(&requirements);
    }
    string_set_free(&includes);
    string_set_free(&type_stubs);
}

int extract_template_argument(const char *pattern, const char *actual,
                                     const char *parameter, char **value_out)
{
    char *normalized_pattern = normalize_spaces(pattern);
    char *normalized_actual = normalize_spaces(actual);
    size_t parameter_length = strlen(parameter);
    size_t i = 0U;
    size_t start = (size_t)-1, end = 0U;
    size_t prefix_length, suffix_length, actual_length;
    int found = 0;
    while (normalized_pattern[i] != '\0') {
        if (is_ident_start(normalized_pattern[i])) {
            size_t word_start = i++;
            while (is_ident_char(normalized_pattern[i])) ++i;
            if (i - word_start == parameter_length &&
                memcmp(normalized_pattern + word_start, parameter, parameter_length) == 0) {
                if (found) {
                    free(normalized_pattern);
                    free(normalized_actual);
                    return 0;
                }
                start = word_start;
                end = i;
                found = 1;
            }
        } else {
            ++i;
        }
    }
    if (!found) {
        free(normalized_pattern);
        free(normalized_actual);
        return 0;
    }
    prefix_length = start;
    suffix_length = strlen(normalized_pattern + end);
    actual_length = strlen(normalized_actual);
    if (actual_length <= prefix_length + suffix_length ||
        strncmp(normalized_actual, normalized_pattern, prefix_length) != 0 ||
        strcmp(normalized_actual + actual_length - suffix_length,
               normalized_pattern + end) != 0) {
        free(normalized_pattern);
        free(normalized_actual);
        return 0;
    }
    *value_out = copy_range(normalized_actual + prefix_length,
                            actual_length - prefix_length - suffix_length);
    free(normalized_pattern);
    free(normalized_actual);
    return (*value_out)[0] != '\0';
}
