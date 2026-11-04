/* Template argument rewriting and dependency expansion. */
#include "internal.h"


static char *instance_subset_suffix(const TemplateInfo *ti,
                                    const GenInstance *instance,
                                    char *const *parameter_names,
                                    size_t parameter_count)
{
    const char **spellings;
    size_t i;
    char *suffix;
    spellings = (const char **)malloc(parameter_count * sizeof(*spellings));
    if (spellings == NULL) die("out of memory");
    for (i = 0U; i < parameter_count; ++i) {
        int parameter_index = template_parameter_index(ti, parameter_names[i]);
        int argument_index;
        if (parameter_index < 0) {
            free(spellings);
            return NULL;
        }
        argument_index = instance_parameter_position(instance, (size_t)parameter_index);
        if (argument_index < 0) {
            free(spellings);
            return NULL;
        }
        spellings[i] = instance->arguments[argument_index].c_type;
    }
    suffix = instance_suffix(spellings, parameter_count);
    free(spellings);
    return suffix;
}

int parse_text_type_arguments(const char *source, size_t open,
                                     char ***arguments, size_t *argument_count,
                                     size_t *end_out)
{
    size_t start = open + 1U;
    size_t i = start;
    size_t depth = 1U;
    size_t brackets = 0U, braces = 0U;
    size_t count = 0U, cap = 0U;
    char **items = NULL;
    char quote = '\0';
    for (; source[i] != '\0'; ++i) {
        if (quote != '\0') {
            if (source[i] == '\\' && source[i + 1U] != '\0') ++i;
            else if (source[i] == quote) quote = '\0';
            continue;
        }
        if (source[i] == '"' || source[i] == '\'') quote = source[i];
        else if (source[i] == '/' && source[i + 1U] == '*') {
            i += 2U;
            while (source[i] != '\0' &&
                   !(source[i] == '*' && source[i + 1U] == '/')) ++i;
            if (source[i] != '\0') ++i;
        } else if (source[i] == '/' && source[i + 1U] == '/') {
            while (source[i] != '\0' && source[i] != '\n') ++i;
        } else if (source[i] == '(') ++depth;
        else if (source[i] == ')') {
            if (brackets != 0U || braces != 0U) continue;
            --depth;
            if (depth == 0U) {
                size_t left = start, right = i;
                while (left < right && isspace((unsigned char)source[left])) ++left;
                while (right > left && isspace((unsigned char)source[right - 1U])) --right;
                if (right > left) {
                    char *argument = copy_range(source + left, right - left);
                    list_add(&items, &count, &cap, argument);
                    free(argument);
                } else if (count != 0U) {
                    list_free(items, count);
                    return 0;
                }
                *arguments = items;
                *argument_count = count;
                *end_out = i + 1U;
                return count != 0U;
            }
        } else if (source[i] == '[') ++brackets;
        else if (source[i] == ']' && brackets != 0U) --brackets;
        else if (source[i] == '{') ++braces;
        else if (source[i] == '}' && braces != 0U) --braces;
        else if (source[i] == ',' && depth == 1U &&
                 brackets == 0U && braces == 0U) {
            size_t left = start, right = i;
            while (left < right && isspace((unsigned char)source[left])) ++left;
            while (right > left && isspace((unsigned char)source[right - 1U])) --right;
            if (right == left) {
                list_free(items, count);
                return 0;
            }
            {
                char *argument = copy_range(source + left, right - left);
                list_add(&items, &count, &cap, argument);
                free(argument);
            }
            start = i + 1U;
        }
    }
    list_free(items, count);
    return 0;
}

char *expand_short_constraint_call(const char *expression,
                                          const char *parameter,
                                          const char *concrete_type)
{
    size_t start = 0U, name_end, open, end = 0U, length;
    char **arguments = NULL;
    size_t argument_count = 0U;
    Buf expanded;
    while (isspace((unsigned char)expression[start])) ++start;
    if (!is_ident_start(expression[start])) return NULL;
    name_end = start + 1U;
    while (is_ident_char(expression[name_end])) ++name_end;
    open = name_end;
    {
        size_t paren = open;
        while (isspace((unsigned char)expression[paren])) ++paren;
        if (expression[paren] != '(') {
            char *name;
            size_t tail = paren;
            if (expression[tail] != '\0') return NULL;
            name = copy_range(expression + start, name_end - start);
            if (str_eq(name, parameter)) {
                free(name);
                return NULL;
            }
            buf_init(&expanded);
            buf_putn(&expanded, expression, start);
            buf_puts(&expanded, name);
            buf_putc(&expanded, '(');
            buf_puts(&expanded, concrete_type);
            buf_putc(&expanded, ')');
            free(name);
            return expanded.p != NULL ? expanded.p : copy_string("");
        }
        open = paren;
    }
    (void)parse_text_type_arguments(expression, open, &arguments,
                                    &argument_count, &end);
    length = strlen(expression);
    if (end == 0U) {
        list_free(arguments, argument_count);
        return NULL;
    }
    {
        size_t tail = end;
        while (tail < length && isspace((unsigned char)expression[tail])) ++tail;
        if (tail != length) {
            list_free(arguments, argument_count);
            return NULL;
        }
    }
    if (argument_count != 0U) {
        char *first_argument = normalize_spaces(arguments[0]);
        int already_full = valid_identifier(first_argument) &&
                           str_eq(first_argument, parameter);
        free(first_argument);
        if (already_full) {
            list_free(arguments, argument_count);
            return NULL;
        }
    }
    buf_init(&expanded);
    buf_putn(&expanded, expression, open + 1U);
    buf_puts(&expanded, concrete_type);
    {
        size_t argument_index;
        for (argument_index = 0U; argument_index < argument_count;
             ++argument_index) {
            buf_puts(&expanded, ", ");
            buf_puts(&expanded, arguments[argument_index]);
        }
    }
    buf_putc(&expanded, ')');
    list_free(arguments, argument_count);
    return expanded.p != NULL ? expanded.p : copy_string("");
}

const TemplateType *find_template_constructor_any(
    const TemplateInfo *infos, size_t count, const char *name,
    size_t *template_index)
{
    size_t i;
    for (i = 0U; i < count; ++i) {
        const TemplateType *found = find_template_type(&infos[i], name);
        if (found != NULL) {
            *template_index = i;
            return found;
        }
    }
    return NULL;
}

static int value_type_spelling_equal(const char *left, const char *right)
{
    char *normalized_left = normalize_spaces(left != NULL ? left : "");
    char *normalized_right = normalize_spaces(right != NULL ? right : "");
    int equal = str_eq(normalized_left, normalized_right);
    free(normalized_left);
    free(normalized_right);
    return equal;
}

int constructor_signature_matches(const TemplateParameterKind *expected,
                                         const TemplateInfo *candidate_ti,
                                         const TemplateType *candidate)
{
    size_t i;
    if (!expected->is_constructor ||
        expected->signature_count != candidate->parameter_count) return 0;
    for (i = 0U; i < candidate->parameter_count; ++i) {
        int parameter = template_parameter_index(candidate_ti,
            candidate->parameter_names[i]);
        const TemplateParameterKind *actual = parameter >= 0 &&
            candidate_ti->parameter_kinds != NULL ?
            &candidate_ti->parameter_kinds[parameter] : NULL;
        const TemplateParameterKind *want = &expected->signature[i];
        if (actual == NULL) return 0;
        if (want->is_constructor || actual->is_constructor) {
            if (!template_parameter_kind_equal(want, actual)) return 0;
        } else if (want->is_value != actual->is_value) {
            return 0;
        } else if (want->is_value &&
                   !value_type_spelling_equal(want->value_type, actual->value_type)) {
            return 0;
        }
    }
    return 1;
}

/* Resolve an application constructor. Usually it names a template type in the
 * same file; `F` may instead be a constructor parameter bound by `instance`
 * to a constructor declared in any template. */
static const TemplateType *resolve_application_constructor(
    const TemplateInfo *ti, const GenInstance *instance,
    const TemplateApplication *application, const TemplateInfo **constructor_ti)
{
    const TemplateType *constructor = find_template_type(ti, application->constructor);
    if (constructor != NULL) {
        *constructor_ti = ti;
        return constructor;
    }
    if (instance != NULL) {
        int parameter = template_parameter_index(ti, application->constructor);
        if (parameter >= 0 &&
            template_parameter_is_constructor(ti, (size_t)parameter)) {
            int argument = instance_parameter_position(instance, (size_t)parameter);
            if (argument >= 0) {
                size_t template_index = 0U;
                const TemplateType *bound = find_template_constructor_any(
                    g_all_template_infos, g_all_template_info_count,
                    instance->arguments[argument].c_type, &template_index);
                if (bound != NULL) {
                    *constructor_ti = &g_all_template_infos[template_index];
                    return bound;
                }
            }
        }
    }
    return NULL;
}

static char *application_suffix(const TemplateInfo *ti,
                                const TemplateInfo *constructor_ti,
                                const GenInstance *instance,
                                const TemplateType *constructor,
                                char **application_arguments,
                                size_t argument_count)
{
    const char **spellings;
    char **owned;
    char *suffix;
    size_t i;
    if (argument_count == 0U ||
        argument_count != constructor->parameter_count) return NULL;
    spellings = (const char **)calloc(argument_count, sizeof(*spellings));
    owned = (char **)calloc(argument_count, sizeof(*owned));
    if (spellings == NULL || owned == NULL) die("out of memory");
    for (i = 0U; i < argument_count; ++i) {
        char *argument = normalize_spaces(application_arguments[i]);
        int parameter = valid_identifier(argument) ?
                        template_parameter_index(ti, argument) : -1;
        int instance_argument = parameter >= 0 ?
            instance_parameter_position(instance, (size_t)parameter) : -1;
        spellings[i] = instance_argument >= 0 ?
            instance->arguments[instance_argument].c_type : argument;
        if (instance_argument >= 0) {
            free(argument);
        } else {
            int constructor_parameter = valid_identifier(
                constructor->parameter_names[i]) ?
                template_parameter_index(constructor_ti,
                                         constructor->parameter_names[i]) : -1;
            if (constructor_parameter >= 0 &&
                template_parameter_is_value(constructor_ti,
                                            (size_t)constructor_parameter)) {
                char *value = canonical_value_argument(argument);
                if (value != NULL) {
                    free(argument);
                    argument = value;
                    spellings[i] = argument;
                }
            }
            owned[i] = argument;
        }
    }
    suffix = instance_suffix(spellings, argument_count);
    for (i = 0U; i < argument_count; ++i) {
        free(owned[i]);
    }
    free(owned);
    free(spellings);
    return suffix;
}

void rewrite(const char *src, const TemplateInfo *ti,
                    const GenInstance *instance, Buf *out)
{
    size_t i = 0U;
    while (src[i] != '\0') {
        if (src[i] == '/' && src[i + 1U] == '/') {
            size_t start = i;
            while (src[i] != '\0' && src[i] != '\n') ++i;
            if (src[i] == '\n') ++i;
            buf_putn(out, src + start, i - start);
        } else if (src[i] == '/' && src[i + 1U] == '*') {
            size_t start = i;
            i += 2U;
            while (src[i] != '\0' && !(src[i] == '*' && src[i + 1U] == '/')) ++i;
            if (src[i] != '\0') i += 2U;
            buf_putn(out, src + start, i - start);
        } else if (src[i] == '"' || src[i] == '\'') {
            char quote = src[i];
            size_t start = i++;
            while (src[i] != '\0' && src[i] != quote) {
                if (src[i] == '\\' && src[i + 1U] != '\0') i += 2U;
                else ++i;
            }
            if (src[i] != '\0') ++i;
            buf_putn(out, src + start, i - start);
        } else if (is_ident_start(src[i])) {
            size_t start = i++;
            char *ident;
            while (is_ident_char(src[i])) ++i;
            ident = copy_range(src + start, i - start);
            if (str_eq(ident, "typename")) {
                /* Explicit type pseudo-arguments disappear from the emitted
                 * specialization, together with their separating comma. */
                size_t next = i;
                while (isspace((unsigned char)src[next])) ++next;
                if (is_ident_start(src[next])) {
                    size_t name_start = next;
                    char *parameter_name;
                    while (is_ident_char(src[next])) ++next;
                    parameter_name = copy_range(src + name_start, next - name_start);
                    if (template_parameter_index(ti, parameter_name) >= 0) {
                        size_t emitted = out->len;
                        while (isspace((unsigned char)src[next])) ++next;
                        if (src[next] == ',') {
                            ++next;
                            while (isspace((unsigned char)src[next])) ++next;
                        } else if (src[next] == ')') {
                            while (emitted > 0U &&
                                   isspace((unsigned char)out->p[emitted - 1U])) {
                                --emitted;
                            }
                            if (emitted > 0U && out->p[emitted - 1U] == '(') {
                                buf_puts(out, "void");
                            }
                        }
                        i = next;
                        free(parameter_name);
                        free(ident);
                        continue;
                    }
                    free(parameter_name);
                }
            }
            {
                int parameter = template_parameter_index(ti, ident);
                int argument = parameter >= 0 ?
                               instance_parameter_position(instance, (size_t)parameter) : -1;
                const TemplateType *constructor = parameter < 0 ?
                                                  find_template_type(ti, ident) : NULL;
                const TemplateFunction *function = parameter < 0 && constructor == NULL ?
                                                   find_template_function(ti, ident) : NULL;
                if (parameter >= 0 && argument >= 0 &&
                    template_parameter_is_constructor(ti, (size_t)parameter)) {
                    size_t open = i;
                    char **application_arguments = NULL;
                    size_t application_count = 0U, application_end = 0U;
                    int emitted = 0;
                    while (isspace((unsigned char)src[open])) ++open;
                    if (src[open] == '(' &&
                        parse_text_type_arguments(src, open,
                            &application_arguments, &application_count,
                            &application_end)) {
                        TemplateApplication application;
                        const TemplateInfo *bound_ti = ti;
                        const TemplateType *bound;
                        memset(&application, 0, sizeof(application));
                        application.constructor = ident;
                        bound = resolve_application_constructor(ti, instance,
                            &application, &bound_ti);
                        if (bound != NULL) {
                            char *suffix = application_suffix(ti, bound_ti, instance,
                                bound, application_arguments, application_count);
                            if (suffix != NULL) {
                                if (bound->is_struct_tag) buf_puts(out, "struct ");
                                else if (bound->is_union_tag) buf_puts(out, "union ");
                                buf_puts(out, instance->arguments[argument].c_type);
                                buf_putc(out, '_');
                                buf_puts(out, suffix);
                                free(suffix);
                                i = application_end;
                                emitted = 1;
                            }
                        }
                        list_free(application_arguments, application_count);
                    }
                    if (emitted) {
                        free(ident);
                        continue;
                    }
                    buf_puts(out, instance->arguments[argument].c_type);
                } else if (parameter >= 0 && argument >= 0) {
                    buf_puts(out, instance->arguments[argument].c_type);
                } else if (constructor != NULL) {
                    size_t open = i;
                    char **application_arguments = NULL;
                    size_t application_count = 0U, application_end = 0U;
                    while (isspace((unsigned char)src[open])) ++open;
                    if (constructor->parameter_count != 0U && src[open] == '(' &&
                        parse_text_type_arguments(src, open,
                            &application_arguments, &application_count,
                            &application_end)) {
                        char *suffix = application_suffix(ti, ti, instance, constructor,
                            application_arguments, application_count);
                        if (suffix != NULL) {
                            if (constructor->is_struct_tag) buf_puts(out, "struct ");
                            else if (constructor->is_union_tag) buf_puts(out, "union ");
                            buf_puts(out, ident);
                            buf_putc(out, '_');
                            buf_puts(out, suffix);
                            free(suffix);
                            i = application_end;
                        } else {
                            buf_puts(out, ident);
                        }
                        list_free(application_arguments, application_count);
                    } else if (constructor->parameter_count == 0U) {
                        buf_puts(out, ident);
                    } else {
                        char *suffix = instance_subset_suffix(ti, instance,
                            constructor->parameter_names,
                            constructor->parameter_count);
                        if (suffix != NULL) {
                            buf_puts(out, ident);
                            buf_putc(out, '_');
                            buf_puts(out, suffix);
                            free(suffix);
                        } else {
                            buf_puts(out, ident);
                        }
                    }
                } else if (function != NULL) {
                    char *suffix = function->type_parameter_count != 0U ?
                        instance_subset_suffix(ti, instance,
                            function->type_parameters,
                            function->type_parameter_count) : NULL;
                    if (function->type_parameter_count == 0U) {
                        buf_puts(out, ident);
                    } else if (suffix != NULL) {
                        buf_puts(out, ident);
                        buf_putc(out, '_');
                        buf_puts(out, suffix);
                        free(suffix);
                    } else {
                        buf_puts(out, ident);
                    }
                } else {
                    buf_puts(out, ident);
                }
            }
            free(ident);
        } else {
            buf_putc(out, src[i++]);
        }
    }
}

void rewrite_c_type(const char *type, const TemplateInfo *ti,
                           const GenInstance *instance,
                           const TemplateApplication *application, Buf *out)
{
    size_t i = 0U;
    const TemplateInfo *applied_ti = ti;
    const TemplateType *applied_constructor = application != NULL &&
        application->constructor != NULL ?
        resolve_application_constructor(ti, instance, application, &applied_ti) : NULL;
    char *applied_suffix = applied_constructor != NULL ?
        application_suffix(ti, applied_ti, instance, applied_constructor,
                           application->arguments, application->argument_count) : NULL;
    while (type[i] != '\0') {
        if (is_ident_start(type[i])) {
            size_t start = i++;
            char *ident;
            while (is_ident_char(type[i])) ++i;
            ident = copy_range(type + start, i - start);
            if (applied_suffix != NULL &&
                str_eq(ident, application->constructor)) {
                int parameter = template_parameter_index(ti, ident);
                int argument = parameter >= 0 ?
                    instance_parameter_position(instance, (size_t)parameter) : -1;
                if (parameter >= 0 && argument >= 0 &&
                    template_parameter_is_constructor(ti, (size_t)parameter)) {
                    buf_puts(out, instance->arguments[argument].c_type);
                } else {
                    buf_puts(out, ident);
                }
                buf_putc(out, '_');
                buf_puts(out, applied_suffix);
            } else {
                int parameter = template_parameter_index(ti, ident);
                int argument = parameter >= 0 ?
                    instance_parameter_position(instance, (size_t)parameter) : -1;
                const TemplateType *constructor = parameter < 0 ?
                    find_template_type(ti, ident) : NULL;
                if (argument >= 0) {
                    buf_puts(out, instance->arguments[argument].c_type);
                } else if (constructor != NULL &&
                           constructor->parameter_count != 0U) {
                    char *suffix = instance_subset_suffix(ti, instance,
                        constructor->parameter_names, constructor->parameter_count);
                    if (suffix != NULL) {
                        buf_puts(out, ident);
                        buf_putc(out, '_');
                        buf_puts(out, suffix);
                        free(suffix);
                    } else {
                        buf_puts(out, ident);
                    }
                } else {
                    buf_puts(out, ident);
                }
            }
            free(ident);
        } else {
            buf_putc(out, type[i++]);
        }
    }
    free(applied_suffix);
}

void emit_template_type_pattern(Buf *out, const char *type,
                                       const TemplateApplication *application)
{
    size_t i = 0U;
    if (application == NULL || application->constructor == NULL) {
        buf_puts(out, type);
        return;
    }
    while (type[i] != '\0') {
        if (is_ident_start(type[i])) {
            size_t start = i++;
            char *ident;
            while (is_ident_char(type[i])) ++i;
            ident = copy_range(type + start, i - start);
            if (str_eq(ident, "struct") || str_eq(ident, "union")) {
                size_t look = i;
                while (isspace((unsigned char)type[look])) ++look;
                if (strncmp(type + look, application->constructor,
                            strlen(application->constructor)) == 0 &&
                    !is_ident_char(type[look + strlen(application->constructor)])) {
                    free(ident);
                    continue;
                }
            }
            if (str_eq(ident, application->constructor)) {
                size_t j;
                buf_puts(out, ident);
                buf_putc(out, '(');
                for (j = 0U; j < application->argument_count; ++j) {
                    if (j != 0U) buf_puts(out, ", ");
                    buf_puts(out, application->arguments[j]);
                }
                buf_putc(out, ')');
            } else {
                buf_puts(out, ident);
            }
            free(ident);
        } else {
            buf_putc(out, type[i++]);
        }
    }
}


/* =========================== EMITTERS ====================== */

int string_set_has(const StringSet *set, const char *s)
{
    return list_contains(set->items, set->count, s);
}

static int assign_template_argument(const TemplateInfo *ti, size_t parameter_index,
                                    const char *spelling, const char *canonical,
                                    char **spellings, char **canonicals,
    unsigned char *assigned)
{
    if (assigned[parameter_index]) {
        return type_arguments_compatible(spellings[parameter_index],
                                         canonicals[parameter_index],
                                         spelling, canonical);
    }
    spellings[parameter_index] = copy_string(spelling);
    canonicals[parameter_index] = copy_string(canonical != NULL ? canonical : spelling);
    assigned[parameter_index] = 1U;
    (void)ti;
    return 1;
}

static void request_type_application(const TemplateInfo *ti,
                                     const GenInstance *instance,
                                     const char *name,
                                     char **arguments,
                                     size_t argument_count)
{
    const TemplateType *constructor = NULL;
    size_t template_index;
    size_t *indices;
    const char **spellings;
    const char **canonicals;
    unsigned char *owned;
    size_t i;
    if (argument_count == 0U ||
        !find_type_constructor(g_all_template_infos, g_all_template_info_count,
                               name, &template_index, &constructor) ||
        constructor == NULL ||
        constructor->parameter_count != argument_count) return;
    indices = (size_t *)malloc(argument_count * sizeof(*indices));
    spellings = (const char **)calloc(argument_count, sizeof(*spellings));
    canonicals = (const char **)calloc(argument_count, sizeof(*canonicals));
    owned = (unsigned char *)calloc(argument_count, sizeof(*owned));
    if (indices == NULL || spellings == NULL || canonicals == NULL || owned == NULL) {
        die("out of memory");
    }
    for (i = 0U; i < argument_count; ++i) {
        const TemplateInfo *constructor_ti =
            &g_all_template_infos[template_index];
        int parameter = template_parameter_index(
            constructor_ti, constructor->parameter_names[i]);
        char *argument = normalize_spaces(arguments[i]);
        int source_parameter = valid_identifier(argument) ?
            template_parameter_index(ti, argument) : -1;
        int source_argument = source_parameter >= 0 ?
            instance_parameter_position(instance, (size_t)source_parameter) : -1;
        if (parameter < 0) die("invalid type constructor parameter map");
        indices[i] = (size_t)parameter;
        if (source_parameter >= 0 && source_argument < 0) {
            size_t cleanup_index;
            free(argument);
            for (cleanup_index = 0U; cleanup_index < i; ++cleanup_index) {
                if (owned[cleanup_index]) {
                    free((char *)spellings[cleanup_index]);
                    if (canonicals[cleanup_index] != spellings[cleanup_index]) {
                        free((char *)canonicals[cleanup_index]);
                    }
                }
            }
            free(indices);
            free(spellings);
            free(canonicals);
            free(owned);
            return;
        }
        if (source_argument >= 0) {
            spellings[i] = instance->arguments[source_argument].c_type;
            canonicals[i] = instance->arguments[source_argument].canonical;
        } else {
            Buf rewritten;
            const char *alias;
            buf_init(&rewritten);
            rewrite(argument, ti, instance, &rewritten);
            spellings[i] = rewritten.p != NULL ? rewritten.p : copy_string(argument);
            owned[i] = 1U;
            alias = valid_identifier(spellings[i]) ?
                find_alias_canonical(spellings[i]) : NULL;
            canonicals[i] = copy_string(alias != NULL ? alias : spellings[i]);
            free(argument);
            argument = NULL;
        }
        if (template_parameter_is_constructor(constructor_ti, (size_t)parameter)) {
            size_t bound_template_index = 0U;
            const TemplateType *bound = find_template_constructor_any(
                g_all_template_infos, g_all_template_info_count,
                spellings[i], &bound_template_index);
            if (bound == NULL ||
                !constructor_signature_matches(&constructor_ti->parameter_kinds[parameter],
                    &g_all_template_infos[bound_template_index], bound)) {
                size_t cleanup_index;
                report_error(instance->origin_path, instance->origin_line,
                    "'%s' is not a template constructor matching parameter '%s'",
                    spellings[i], constructor->parameter_names[i]);
                for (cleanup_index = 0U; cleanup_index < argument_count;
                     ++cleanup_index) {
                    if (owned[cleanup_index]) {
                        free((char *)spellings[cleanup_index]);
                        if (canonicals[cleanup_index] != spellings[cleanup_index]) {
                            free((char *)canonicals[cleanup_index]);
                        }
                    }
                }
                free(indices);
                free(spellings);
                free(canonicals);
                free(owned);
                return;
            }
        }
        if (template_parameter_is_value(constructor_ti, (size_t)parameter)) {
            char *value = canonical_value_argument(spellings[i]);
            if (value == NULL) {
                size_t cleanup_index;
                report_error(instance->origin_path, instance->origin_line,
                    "value argument '%s' for '%s' must be an integer constant expression",
                    spellings[i], constructor->parameter_names[i]);
                free(argument);
                for (cleanup_index = 0U; cleanup_index < i; ++cleanup_index) {
                    if (owned[cleanup_index]) {
                        free((char *)spellings[cleanup_index]);
                        if (canonicals[cleanup_index] != spellings[cleanup_index]) {
                            free((char *)canonicals[cleanup_index]);
                        }
                    }
                }
                free(indices);
                free(spellings);
                free(canonicals);
                free(owned);
                return;
            }
            if (owned[i]) free((char *)spellings[i]);
            spellings[i] = value;
            canonicals[i] = value;
            owned[i] = 1U;
        }
        free(argument);
    }
    {
        GenInstance *type_instance = add_instance(template_index, indices,
            spellings, canonicals, argument_count);
        set_instance_origin(type_instance, instance->origin_path,
                            instance->origin_line);
        (void)add_type_use(name, type_instance->name);
    }
    for (i = 0U; i < argument_count; ++i) {
        if (owned[i]) {
            free((char *)spellings[i]);
            if (canonicals[i] != spellings[i]) {
                free((char *)canonicals[i]);
            }
        }
    }
    free(indices);
    free(spellings);
    free(canonicals);
    free(owned);
}

static void request_type_applications(const TemplateInfo *ti,
                                     const GenInstance *instance,
                                     const char *fragment)
{
    size_t offset = 0U;
    while (fragment[offset] != '\0') {
        size_t start;
        size_t open;
        size_t argument_count = 0U;
        size_t end = 0U;
        char *name;
        char **arguments = NULL;
        size_t template_index;
        const TemplateType *constructor = NULL;
        offset = skip_c_trivia(fragment, offset);
        if (fragment[offset] == '\0') break;
        if (fragment[offset] == '"' || fragment[offset] == '\'') {
            offset = skip_c_literal(fragment, offset);
            continue;
        }
        if (!is_ident_start(fragment[offset])) {
            ++offset;
            continue;
        }
        start = offset++;
        while (is_ident_char(fragment[offset])) ++offset;
        name = copy_range(fragment + start, offset - start);
        open = skip_c_trivia(fragment, offset);
        {
            const char *constructor_name = name;
            int parameter = template_parameter_index(ti, name);
            if (parameter >= 0 &&
                template_parameter_is_constructor(ti, (size_t)parameter)) {
                int argument = instance_parameter_position(instance, (size_t)parameter);
                constructor_name = argument >= 0 ?
                    instance->arguments[argument].c_type : NULL;
            }
            if (constructor_name != NULL &&
                find_type_constructor(g_all_template_infos,
                                      g_all_template_info_count,
                                      constructor_name, &template_index, &constructor) &&
                constructor != NULL && fragment[open] == '(' &&
                parse_text_type_arguments(fragment, open, &arguments,
                                          &argument_count, &end)) {
                request_type_application(ti, instance, constructor_name, arguments,
                                         argument_count);
                list_free(arguments, argument_count);
            }
        }
        free(name);
    }
}

void request_instance_types(const TemplateInfo *ti,
                                   const TemplateFunction *requester,
                                   const GenInstance *instance)
{
    char *fragment;
    if (requester == NULL || requester->end_offset > ti->body.len ||
        requester->start_offset >= requester->end_offset) return;
    fragment = copy_range(ti->body.p + requester->start_offset,
                          (size_t)(requester->end_offset - requester->start_offset));
    request_type_applications(ti, instance, fragment);
    free(fragment);
}

static int application_candidate_matches(const TemplateInfo *ti,
                                        const TemplateArgument *parameter,
                                        const GenInstance *candidate,
                                        const char *actual_spelling,
                                        const char *actual_canonical)
{
    Buf expected;
    char *actual_normalized;
    char *expected_normalized;
    size_t pointer_count = 0U;
    size_t i;
    int matches = 0;
    buf_init(&expected);
    rewrite(parameter->type, ti, candidate, &expected);
    actual_normalized = normalize_spaces(actual_spelling);
    expected_normalized = normalize_spaces(expected.p != NULL ? expected.p : "");
    if (str_eq(actual_normalized, expected_normalized)) matches = 1;
    buf_free(&expected);
    free(actual_normalized);
    free(expected_normalized);
    if (matches) return 1;
    if (parameter->application.argument_count != candidate->argument_count) return 0;
    for (i = 0U; parameter->type[i] != '\0'; ++i) {
        if (parameter->type[i] == '*') ++pointer_count;
    }
    for (i = 0U; i < parameter->application.argument_count; ++i) {
        Buf expected_spelling, expected_canonical;
        size_t pointer_index;
        buf_init(&expected_spelling);
        buf_init(&expected_canonical);
        buf_puts(&expected_spelling, candidate->arguments[i].c_type);
        buf_puts(&expected_canonical, candidate->arguments[i].canonical);
        for (pointer_index = 0U; pointer_index < pointer_count; ++pointer_index) {
            buf_puts(&expected_spelling, " *");
            buf_puts(&expected_canonical, " *");
        }
        actual_normalized = normalize_spaces(actual_spelling);
        expected_normalized = normalize_spaces(expected_spelling.p != NULL ?
                                                expected_spelling.p : "");
        if (str_eq(actual_normalized, expected_normalized)) matches = 1;
        free(actual_normalized);
        free(expected_normalized);
        actual_normalized = normalize_spaces(actual_canonical);
        expected_normalized = normalize_spaces(expected_canonical.p != NULL ?
                                                expected_canonical.p : "");
        if (str_eq(actual_normalized, expected_normalized)) matches = 1;
        free(actual_normalized);
        free(expected_normalized);
        buf_free(&expected_spelling);
        buf_free(&expected_canonical);
        if (matches) break;
    }
    return matches ? 2 : 0;
}

static void assign_application_parameters(const TemplateInfo *ti,
                                          const TemplateFunction *function,
                                          const TemplateApplication *application,
                                          const GenInstance *candidate,
                                          char **spellings, char **canonicals,
                                          unsigned char *assigned,
                                          int exact_match,
                                          const ClangUse *use, int *okay)
{
    size_t i;
    for (i = 0U; i < application->argument_count &&
         i < candidate->argument_count; ++i) {
        char *argument = normalize_spaces(application->arguments[i]);
        size_t function_parameter;
        if (valid_identifier(argument)) {
            for (function_parameter = 0U;
                 function_parameter < function->type_parameter_count;
                 ++function_parameter) {
                if (str_eq(function->type_parameters[function_parameter], argument) &&
                    !assign_template_argument(ti, function_parameter,
                        candidate->arguments[i].c_type,
                        candidate->arguments[i].canonical,
                        spellings, canonicals, assigned)) {
                    if (exact_match && assigned[function_parameter]) {
                        record_type_variable_mismatch(use->path, use->line,
                            function->name,
                            function->type_parameters[function_parameter],
                            spellings[function_parameter],
                            candidate->arguments[i].c_type);
                    }
                    *okay = 0;
                }
            }
        }
        free(argument);
    }
}

int process_clang_use_group(ClangUseList *uses, size_t first,
                                   const TemplateInfo *infos, size_t template_count,
                                   unsigned char *processed,
                                   int report_incomplete)
{
    ClangUse *call = &uses->items[first];
    size_t template_index;
    const TemplateInfo *ti;
    const TemplateFunction *function;
    char **spellings;
    char **canonicals;
    unsigned char *assigned;
    const char **dispatch_types;
    const char **dispatch_canonicals;
    unsigned char *dispatch_pending;
    size_t *parameter_indices;
    size_t i, j;
    size_t binding_count;
    int okay = 1;
    if (!generic_function_template(infos, template_count, call->name, &template_index)) return 1;
    ti = &infos[template_index];
    function = find_template_function(ti, call->name);
    if (function == NULL) return 1;
    binding_count = function->type_parameter_count;
    spellings = (char **)calloc(binding_count, sizeof(*spellings));
    canonicals = (char **)calloc(binding_count, sizeof(*canonicals));
    assigned = (unsigned char *)calloc(binding_count, sizeof(*assigned));
    dispatch_types = (const char **)calloc(function->dispatch_count, sizeof(*dispatch_types));
    dispatch_canonicals = (const char **)calloc(function->dispatch_count,
                                                sizeof(*dispatch_canonicals));
    dispatch_pending = (unsigned char *)calloc(function->dispatch_count,
                                                sizeof(*dispatch_pending));
    parameter_indices = (size_t *)malloc(binding_count * sizeof(*parameter_indices));
    if (spellings == NULL || canonicals == NULL || assigned == NULL ||
        parameter_indices == NULL || dispatch_pending == NULL ||
        (function->dispatch_count != 0U &&
         (dispatch_types == NULL || dispatch_canonicals == NULL))) die("out of memory");
    for (i = 0U; i < binding_count; ++i) {
        int parameter = template_parameter_index(ti, function->type_parameters[i]);
        if (parameter < 0) die("invalid generic parameter map");
        parameter_indices[i] = (size_t)parameter;
    }
    for (i = first; i < uses->count; ++i) {
        ClangUse *use = &uses->items[i];
        if (!str_eq(use->name, call->name) || !str_eq(use->path, call->path) ||
            use->offset != call->offset) continue;
        processed[i] = 1U;
        if (use->type_spelling == NULL || use->type_spelling[0] == '\0' ||
            use->argument_index >= function->parameter_count) {
            int binding_required = use->argument_index >= function->parameter_count;
            if (!binding_required) {
                const TemplateArgument *parameter =
                    &function->parameters[use->argument_index];
                size_t parameter_index;
                size_t dispatch_index;
                for (parameter_index = 0U;
                     parameter_index < parameter->type_parameter_count;
                     ++parameter_index) {
                    int mapped = template_parameter_index(
                        ti, parameter->type_parameters[parameter_index]);
                    if (mapped < 0 || !assigned[(size_t)mapped]) {
                        binding_required = 1;
                        break;
                    }
                }
                for (dispatch_index = 0U;
                     dispatch_index < function->dispatch_count;
                     ++dispatch_index) {
                    if (function->dispatch_indices[dispatch_index] ==
                        use->argument_index) {
                        binding_required = 1;
                    }
                }
            }
            if (binding_required) okay = 0;
            continue;
        }
        if (g_verbose) {
            fprintf(stderr,
                    "tmplgen: infer %s arg%u formal='%s' actual='%s' canonical='%s' params=%lu\n",
                    use->name, use->argument_index,
                    function->parameters[use->argument_index].type,
                    use->type_spelling,
                    use->canonical_spelling != NULL ? use->canonical_spelling : "",
                    (unsigned long)binding_count);
        }
        for (j = 0U; j < function->dispatch_count; ++j) {
            if (function->dispatch_indices[j] == use->argument_index) {
                dispatch_types[j] = use->type_spelling;
                dispatch_canonicals[j] = use->canonical_spelling;
            }
        }
        {
            const TemplateArgument *parameter = &function->parameters[use->argument_index];
            size_t k;
            for (k = 0U; k < binding_count; ++k) {
                int parameter_kind = template_parameter_index(ti,
                    function->type_parameters[k]);
                if (parameter_kind >= 0 &&
                    template_parameter_is_constructor(ti, (size_t)parameter_kind)) {
                    /* Constructor parameters bind from the application, not
                     * from the stand-in spelling of the parameter type. */
                    continue;
                }
                if (identifier_line_in_code(parameter->type,
                                            function->type_parameters[k]) != 0U) {
                    char *spelling = NULL;
                    char *canonical = NULL;
                    if (extract_template_argument(parameter->type, use->type_spelling,
                                                  function->type_parameters[k], &spelling) &&
                        extract_template_argument(parameter->type,
                                                  use->canonical_spelling != NULL ?
                                                  use->canonical_spelling : use->type_spelling,
                                                  function->type_parameters[k], &canonical)) {
                        if (g_verbose && assigned[k]) {
                            fprintf(stderr,
                                "DEBUG assign %s %s: '%s' (%s) vs '%s' (%s)\n",
                                function->name, function->type_parameters[k],
                                spellings[k], canonicals[k], spelling, canonical);
                        }
                        if (!assign_template_argument(ti, k, spelling, canonical,
                                                      spellings, canonicals, assigned)) {
                            if (assigned[k]) {
                                record_type_variable_mismatch(use->path,
                                    use->line, function->name,
                                    function->type_parameters[k], spellings[k],
                                    spelling);
                            }
                            okay = 0;
                        }
                    }
                    free(spelling);
                    free(canonical);
                }
            }
            for (k = 0U; k < ti->constructor_count; ++k) {
                size_t use_index;
                const TemplateType *constructor = &ti->constructors[k];
                size_t *constructor_indices;
                int exact_application;
                if (identifier_line_in_code(parameter->type, constructor->name) == 0U) continue;
                constructor_indices = (size_t *)malloc(
                    constructor->parameter_count * sizeof(*constructor_indices));
                if (constructor_indices == NULL) die("out of memory");
                for (size_t p = 0U; p < constructor->parameter_count; ++p) {
                    int mapped = template_parameter_index(ti,
                        constructor->parameter_names[p]);
                    if (mapped < 0) die("invalid type constructor parameter map");
                    constructor_indices[p] = (size_t)mapped;
                }
                /* The loose element-type match (2) exists for the first pass,
                 * where Vec(T) is spelled T. Once some candidate matches the
                 * spelled type exactly, a loose match is a different
                 * instance (Vec(Vec_Foo) for an argument of type Vec_Foo *)
                 * and must not override it. */
                exact_application = 0;
                if (parameter->application.constructor != NULL &&
                    str_eq(parameter->application.constructor, constructor->name)) {
                    for (use_index = 0U; use_index < g_type_use_count; ++use_index) {
                        GenInstance *candidate;
                        if (!str_eq(g_type_uses[use_index].functor, constructor->name)) continue;
                        candidate = find_instance(template_index,
                            g_type_uses[use_index].type_name, constructor_indices,
                            constructor->parameter_count);
                        if (candidate != NULL &&
                            application_candidate_matches(ti, parameter, candidate,
                                use->type_spelling,
                                use->canonical_spelling != NULL ?
                                use->canonical_spelling : use->type_spelling) == 1) {
                            exact_application = 1;
                            break;
                        }
                    }
                }
                for (use_index = 0U; use_index < g_type_use_count; ++use_index) {
                    GenInstance *candidate;
                    Buf expected;
                    char *actual_normalized;
                    char *expected_normalized;
                    size_t parameter_index;
                    if (!str_eq(g_type_uses[use_index].functor, constructor->name)) continue;
                    candidate = find_instance(template_index,
                        g_type_uses[use_index].type_name, constructor_indices,
                        constructor->parameter_count);
                    if (candidate == NULL) continue;
                    if (parameter->application.constructor != NULL) {
                        int application_match;
                        if (!str_eq(parameter->application.constructor,
                                    constructor->name)) continue;
                        application_match = application_candidate_matches(
                                ti, parameter, candidate,
                                use->type_spelling,
                                use->canonical_spelling != NULL ?
                                use->canonical_spelling : use->type_spelling);
                        if (application_match == 0 ||
                            (application_match == 2 && exact_application)) continue;
                        assign_application_parameters(ti, function,
                            &parameter->application, candidate, spellings,
                            canonicals, assigned, application_match == 1,
                            use, &okay);
                        if (application_match == 2) {
                            size_t dispatch_index;
                            for (dispatch_index = 0U;
                                 dispatch_index < function->dispatch_count;
                                 ++dispatch_index) {
                                if (function->dispatch_indices[dispatch_index] ==
                                    use->argument_index) {
                                    dispatch_types[dispatch_index] = NULL;
                                    dispatch_canonicals[dispatch_index] = NULL;
                                    dispatch_pending[dispatch_index] = 1U;
                                }
                            }
                        }
                    } else {
                        buf_init(&expected);
                        rewrite(parameter->type, ti, candidate, &expected);
                        actual_normalized = normalize_spaces(use->type_spelling);
                        expected_normalized = normalize_spaces(
                            expected.p != NULL ? expected.p : "");
                        if (str_eq(actual_normalized, expected_normalized)) {
                            for (parameter_index = 0U; parameter_index < binding_count;
                                 ++parameter_index) {
                                int template_parameter = template_parameter_index(
                                    ti, function->type_parameters[parameter_index]);
                                int candidate_argument = instance_parameter_position(
                                    candidate, (size_t)template_parameter);
                                if (candidate_argument >= 0 &&
                                    !assign_template_argument(
                                        ti, parameter_index,
                                        candidate->arguments[candidate_argument].c_type,
                                        candidate->arguments[candidate_argument].canonical,
                                        spellings, canonicals, assigned)) okay = 0;
                            }
                        }
                        free(actual_normalized);
                        free(expected_normalized);
                        buf_free(&expected);
                    }
                }
                free(constructor_indices);
            }
            if (parameter->application.constructor != NULL) {
                int constructor_parameter = template_parameter_index(ti,
                    parameter->application.constructor);
                if (constructor_parameter >= 0 &&
                    template_parameter_is_constructor(ti, (size_t)constructor_parameter)) {
                    size_t use_index;
                    int exact_application = 0;
                    /* A constructor parameter decomposes the observed type:
                     * G(T, N) matched against Array_int_4 binds G = Array,
                     * T = int, N = 4. */
                    for (use_index = 0U; use_index < g_type_use_count; ++use_index) {
                        size_t candidate_template_index = 0U;
                        const TemplateType *candidate_constructor =
                            find_template_constructor_any(infos, template_count,
                                g_type_uses[use_index].functor,
                                &candidate_template_index);
                        GenInstance *candidate;
                        char *actual_normalized;
                        Buf expected;
                        char *expected_normalized;
                        size_t *candidate_indices;
                        size_t j2;
                        if (candidate_constructor == NULL) continue;
                        if (!constructor_signature_matches(
                                &ti->parameter_kinds[constructor_parameter],
                                &infos[candidate_template_index],
                                candidate_constructor)) continue;
                        candidate_indices = (size_t *)malloc(
                            candidate_constructor->parameter_count *
                            sizeof(*candidate_indices));
                        if (candidate_indices == NULL) die("out of memory");
                        for (j2 = 0U; j2 < candidate_constructor->parameter_count; ++j2) {
                            int mapped = template_parameter_index(
                                &infos[candidate_template_index],
                                candidate_constructor->parameter_names[j2]);
                            if (mapped < 0) die("invalid constructor parameter map");
                            candidate_indices[j2] = (size_t)mapped;
                        }
                        candidate = find_instance(candidate_template_index,
                            g_type_uses[use_index].type_name, candidate_indices,
                            candidate_constructor->parameter_count);
                        free(candidate_indices);
                        if (candidate == NULL) continue;
                        buf_init(&expected);
                        buf_puts(&expected, g_type_uses[use_index].functor);
                        buf_putc(&expected, '_');
                        buf_puts(&expected, candidate->name);
                        buf_puts(&expected, parameter->type +
                            strlen(parameter->application.constructor));
                        expected_normalized = normalize_spaces(
                            expected.p != NULL ? expected.p : "");
                        actual_normalized = normalize_spaces(use->type_spelling);
                        buf_free(&expected);
                        if (str_eq(actual_normalized, expected_normalized)) {
                            exact_application = 1;
                        }
                        free(actual_normalized);
                        free(expected_normalized);
                        if (exact_application) break;
                    }
                    for (use_index = 0U; use_index < g_type_use_count; ++use_index) {
                        const char *functor = g_type_uses[use_index].functor;
                        size_t candidate_template_index = 0U;
                        const TemplateType *candidate_constructor =
                            find_template_constructor_any(infos, template_count,
                                functor, &candidate_template_index);
                        const TemplateInfo *candidate_ti;
                        size_t *candidate_indices;
                        GenInstance *candidate;
                        size_t argument_index;
                        size_t binding_index;
                        int arguments_match = 1;
                        int application_match = 0;
                        if (candidate_constructor == NULL) continue;
                        candidate_ti = &infos[candidate_template_index];
                        if (!constructor_signature_matches(
                                &ti->parameter_kinds[constructor_parameter],
                                candidate_ti, candidate_constructor)) continue;
                        candidate_indices = (size_t *)malloc(
                            candidate_constructor->parameter_count *
                            sizeof(*candidate_indices));
                        if (candidate_indices == NULL) die("out of memory");
                        for (j = 0U; j < candidate_constructor->parameter_count; ++j) {
                            int mapped = template_parameter_index(candidate_ti,
                                candidate_constructor->parameter_names[j]);
                            if (mapped < 0) die("invalid constructor parameter map");
                            candidate_indices[j] = (size_t)mapped;
                        }
                        candidate = find_instance(candidate_template_index,
                            g_type_uses[use_index].type_name, candidate_indices,
                            candidate_constructor->parameter_count);
                        free(candidate_indices);
                        if (candidate == NULL) continue;
                        {
                            Buf expected;
                            char *actual_normalized = normalize_spaces(use->type_spelling);
                            char *expected_normalized;
                            buf_init(&expected);
                            buf_puts(&expected, functor);
                            buf_putc(&expected, '_');
                            buf_puts(&expected, candidate->name);
                            buf_puts(&expected, parameter->type +
                                strlen(parameter->application.constructor));
                            expected_normalized = normalize_spaces(
                                expected.p != NULL ? expected.p : "");
                            buf_free(&expected);
                            if (str_eq(actual_normalized, expected_normalized)) {
                                application_match = 1;
                            }
                            free(actual_normalized);
                            free(expected_normalized);
                        }
                        if (application_match == 0) {
                            application_match = application_candidate_matches(
                                ti, parameter, candidate, use->type_spelling,
                                use->canonical_spelling != NULL ?
                                use->canonical_spelling : use->type_spelling);
                        }
                        if (application_match == 0 ||
                            (application_match == 2 && exact_application)) continue;
                        for (argument_index = 0U;
                             argument_index < parameter->application.argument_count &&
                             argument_index < candidate->argument_count;
                             ++argument_index) {
                            char *argument = normalize_spaces(
                                parameter->application.arguments[argument_index]);
                            int is_variable = 0;
                            if (valid_identifier(argument)) {
                                for (binding_index = 0U; binding_index < binding_count;
                                     ++binding_index) {
                                    if (str_eq(function->type_parameters[binding_index],
                                               argument)) {
                                        is_variable = 1;
                                        break;
                                    }
                                }
                            }
                            if (!is_variable) {
                                char *value = canonical_value_argument(argument);
                                const char *expected_argument = value != NULL ?
                                    value : argument;
                                if (!str_eq(expected_argument,
                                        candidate->arguments[argument_index].c_type)) {
                                    arguments_match = 0;
                                }
                                free(value);
                            }
                            free(argument);
                            if (!arguments_match) break;
                        }
                        if (!arguments_match) continue;
                        for (binding_index = 0U; binding_index < binding_count;
                             ++binding_index) {
                            if (str_eq(function->type_parameters[binding_index],
                                       parameter->application.constructor)) {
                                if (!assign_template_argument(ti, binding_index,
                                        functor, functor, spellings, canonicals,
                                        assigned)) okay = 0;
                                break;
                            }
                        }
                        assign_application_parameters(ti, function,
                            &parameter->application, candidate, spellings,
                            canonicals, assigned, application_match == 1, use, &okay);
                        if (application_match == 2) {
                            size_t dispatch_index;
                            for (dispatch_index = 0U;
                                 dispatch_index < function->dispatch_count;
                                 ++dispatch_index) {
                                if (function->dispatch_indices[dispatch_index] ==
                                    use->argument_index) {
                                    dispatch_types[dispatch_index] = NULL;
                                    dispatch_canonicals[dispatch_index] = NULL;
                                    dispatch_pending[dispatch_index] = 1U;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    for (i = 0U; i < binding_count; ++i) {
        if (!assigned[i]) {
            if (g_verbose) fprintf(stderr, "tmplgen: unresolved %s parameter %s\n",
                                   call->name, function->type_parameters[i]);
            okay = 0;
        }
    }
    for (i = 0U; i < function->dispatch_count; ++i) {
        if ((dispatch_types[i] == NULL || dispatch_canonicals[i] == NULL) &&
            !(report_incomplete == 0 && dispatch_pending[i])) okay = 0;
    }
    if (okay) {
        GenInstance *instance = add_instance(template_index, parameter_indices,
                                             (const char *const *)spellings,
                                             (const char *const *)canonicals,
                                             binding_count);
        unsigned *dispatch_indices = NULL;
        set_instance_origin(instance, call->path, call->line);
        if (function->dispatch_count != 0U) {
            dispatch_indices = (unsigned *)malloc(function->dispatch_count *
                                                   sizeof(*dispatch_indices));
            if (dispatch_indices == NULL) die("out of memory");
            memcpy(dispatch_indices, function->dispatch_indices,
                   function->dispatch_count * sizeof(*dispatch_indices));
        }
        request_instance_types(ti, function, instance);
        {
            int complete_dispatch = 1;
            for (i = 0U; i < function->dispatch_count; ++i) {
                if (dispatch_types[i] == NULL || dispatch_canonicals[i] == NULL) {
                    complete_dispatch = 0;
                }
            }
            if (complete_dispatch) {
                (void)add_generic_use(call->name, instance->name, 1,
                                      dispatch_indices, dispatch_types,
                                      dispatch_canonicals,
                                      function->dispatch_count);
                set_generic_use_origin(call->name, instance->name,
                                       call->path, call->line);
            }
        }
        free(dispatch_indices);
    } else if (report_incomplete &&
               !has_type_variable_mismatch(call->path, call->line, call->name)) {
        report_warning(call->path, call->line,
                       "cannot infer all template parameters for %s() from its input types",
                       call->name);
    }
    for (i = 0U; i < binding_count; ++i) {
        free(spellings[i]);
        free(canonicals[i]);
    }
    free(spellings);
    free(canonicals);
    free(assigned);
    free(parameter_indices);
    free(dispatch_types);
    free(dispatch_canonicals);
    free(dispatch_pending);
    return okay;
}

static int find_function_parameter(const TemplateFunction *function,
                                   const char *name)
{
    size_t i;
    for (i = 0U; i < function->parameter_count; ++i) {
        if (str_eq(function->parameters[i].name, name)) return (int)i;
    }
    return -1;
}

static int call_argument_parameter(const TemplateFunction *function,
                                   const char *expression)
{
    const char *p = expression;
    const char *start;
    char *name;
    int parameter;
    while (isspace((unsigned char)*p)) ++p;
    while (*p == '&' || *p == '*') {
        ++p;
        while (isspace((unsigned char)*p)) ++p;
    }
    if (!is_ident_start(*p)) return -1;
    start = p++;
    while (is_ident_char(*p)) ++p;
    name = copy_range(start, (size_t)(p - start));
    while (isspace((unsigned char)*p)) ++p;
    if (*p != '\0') {
        free(name);
        return -1;
    }
    parameter = find_function_parameter(function, name);
    free(name);
    return parameter;
}

static int bind_callee_parameter(const TemplateInfo *caller_ti,
                                 const TemplateInfo *callee_ti,
                                 const TemplateFunction *callee,
                                 const GenInstance *caller_instance,
                                 const char *callee_name,
                                 const char *caller_name,
                                 char **spellings, char **canonicals,
                                 unsigned char *assigned)
{
    size_t binding_index;
    int callee_parameter = template_parameter_index(callee_ti, callee_name);
    int caller_parameter = template_parameter_index(caller_ti, caller_name);
    int caller_argument;
    if (callee_parameter < 0 || caller_parameter < 0) return 0;
    caller_argument = instance_parameter_position(caller_instance,
                                                   (size_t)caller_parameter);
    if (caller_argument < 0) return 0;
    for (binding_index = 0U; binding_index < callee->type_parameter_count;
         ++binding_index) {
        if (!str_eq(callee->type_parameters[binding_index], callee_name)) continue;
        return assign_template_argument(caller_ti, binding_index,
            caller_instance->arguments[caller_argument].c_type,
            caller_instance->arguments[caller_argument].canonical,
            spellings, canonicals, assigned);
    }
    return 0;
}

static GenInstance *resolve_function_call_instance(const TemplateInfo *caller_ti,
                                                   const TemplateFunction *caller,
                                                   const GenInstance *caller_instance,
                                                   const TemplateInfo *callee_ti,
                                                   size_t callee_template_index,
                                                   const TemplateFunction *callee,
                                                   char **call_arguments,
                                                   size_t call_argument_count)
{
    size_t binding_count = callee->type_parameter_count;
    size_t *indices;
    char **spellings;
    char **canonicals;
    unsigned char *assigned;
    size_t i;
    int okay = 1;
    GenInstance *result;
    if (binding_count == 0U) return add_instance(callee_template_index,
                                                  NULL, NULL, NULL, 0U);
    indices = (size_t *)malloc(binding_count * sizeof(*indices));
    spellings = (char **)calloc(binding_count, sizeof(*spellings));
    canonicals = (char **)calloc(binding_count, sizeof(*canonicals));
    assigned = (unsigned char *)calloc(binding_count, sizeof(*assigned));
    if (indices == NULL || spellings == NULL || canonicals == NULL || assigned == NULL) {
        die("out of memory");
    }
    for (i = 0U; i < call_argument_count && i < caller->parameter_count &&
         i < callee->parameter_count; ++i) {
        int caller_parameter = call_argument_parameter(caller, call_arguments[i]);
        const TemplateArgument *source_argument;
        const TemplateArgument *target_argument;
        size_t j;
        if (caller_parameter < 0) continue;
        source_argument = &caller->parameters[caller_parameter];
        target_argument = &callee->parameters[i];
        if (source_argument->application.constructor != NULL &&
            target_argument->application.constructor != NULL &&
            str_eq(source_argument->application.constructor,
                   target_argument->application.constructor) &&
            source_argument->application.argument_count ==
            target_argument->application.argument_count) {
            for (j = 0U; j < target_argument->application.argument_count; ++j) {
                char *source_name = normalize_spaces(
                    source_argument->application.arguments[j]);
                char *target_name = normalize_spaces(
                    target_argument->application.arguments[j]);
                if (valid_identifier(source_name) && valid_identifier(target_name) &&
                    !bind_callee_parameter(caller_ti, callee_ti, callee,
                        caller_instance,
                        target_name, source_name, spellings, canonicals, assigned)) {
                    okay = 0;
                }
                free(source_name);
                free(target_name);
            }
        } else if (source_argument->application.constructor == NULL &&
                   target_argument->application.constructor == NULL &&
                   source_argument->type_parameter_count ==
                   target_argument->type_parameter_count) {
            for (j = 0U; j < target_argument->type_parameter_count; ++j) {
                if (!bind_callee_parameter(caller_ti, callee_ti, callee,
                        caller_instance,
                        target_argument->type_parameters[j],
                        source_argument->type_parameters[j],
                        spellings, canonicals, assigned)) okay = 0;
            }
        }
    }
    for (i = 0U; i < binding_count; ++i) {
        int parameter;
        int caller_argument;
        if (assigned[i]) continue;
        parameter = template_parameter_index(caller_ti,
                                             callee->type_parameters[i]);
        caller_argument = parameter >= 0 ? instance_parameter_position(
            caller_instance, (size_t)parameter) : -1;
        if (caller_argument >= 0) {
            spellings[i] = copy_string(caller_instance->arguments[caller_argument].c_type);
            canonicals[i] = copy_string(caller_instance->arguments[caller_argument].canonical);
            assigned[i] = 1U;
        } else {
            okay = 0;
        }
    }
    if (!okay) {
        for (i = 0U; i < binding_count; ++i) {
            free(spellings[i]);
            free(canonicals[i]);
        }
        free(indices);
        free(spellings);
        free(canonicals);
        free(assigned);
        return NULL;
    }
    for (i = 0U; i < binding_count; ++i) {
        int parameter = template_parameter_index(callee_ti,
                                                 callee->type_parameters[i]);
        if (parameter < 0) die("invalid callee specialization parameter");
        indices[i] = (size_t)parameter;
    }
    result = add_instance(callee_template_index, indices,
        (const char *const *)spellings, (const char *const *)canonicals,
        binding_count);
    set_instance_origin(result, caller_instance->origin_path,
                        caller_instance->origin_line);
    for (i = 0U; i < binding_count; ++i) {
        free(spellings[i]);
        free(canonicals[i]);
    }
    free(indices);
    free(spellings);
    free(canonicals);
    free(assigned);
    return result;
}

char *specialize_function_calls(const char *fragment,
                                       const TemplateInfo *ti,
                                       const GenInstance *instance,
                                       const TemplateFunction *caller,
                                       int discover_dependencies,
                                       int report_unresolved)
{
    Buf output;
    size_t i = 0U;
    const char *body = strchr(fragment, '{');
    buf_init(&output);
    if (body == NULL) return copy_string(fragment);
    i = (size_t)(body - fragment) + 1U;
    buf_putn(&output, fragment, i);
    while (fragment[i] != '\0') {
        if (fragment[i] == '/' && fragment[i + 1U] == '/') {
            size_t start = i;
            while (fragment[i] != '\0' && fragment[i] != '\n') ++i;
            if (fragment[i] == '\n') ++i;
            buf_putn(&output, fragment + start, i - start);
        } else if (fragment[i] == '/' && fragment[i + 1U] == '*') {
            size_t start = i;
            i += 2U;
            while (fragment[i] != '\0' &&
                   !(fragment[i] == '*' && fragment[i + 1U] == '/')) ++i;
            if (fragment[i] != '\0') i += 2U;
            buf_putn(&output, fragment + start, i - start);
        } else if (fragment[i] == '"' || fragment[i] == '\'') {
            char quote = fragment[i];
            size_t start = i++;
            while (fragment[i] != '\0' && fragment[i] != quote) {
                if (fragment[i] == '\\' && fragment[i + 1U] != '\0') i += 2U;
                else ++i;
            }
            if (fragment[i] != '\0') ++i;
            buf_putn(&output, fragment + start, i - start);
        } else if (is_ident_start(fragment[i])) {
            size_t start = i++;
            size_t open;
            size_t argument_count = 0U, argument_end = 0U;
            char *name;
            char **arguments = NULL;
            const TemplateFunction *callee;
            const TemplateInfo *callee_ti = NULL;
            size_t callee_template_index = 0U;
            size_t template_index;
            int arguments_parsed = 0;
            GenInstance *callee_instance = NULL;
            char *specialized_name = NULL;
            while (is_ident_char(fragment[i])) ++i;
            name = copy_range(fragment + start, i - start);
            callee = NULL;
            for (template_index = 0U;
                 template_index < g_all_template_info_count;
                 ++template_index) {
                callee = find_template_function(&g_all_template_infos[template_index],
                                                name);
                if (callee != NULL) {
                    callee_ti = &g_all_template_infos[template_index];
                    callee_template_index = template_index;
                    break;
                }
            }
            open = i;
            while (isspace((unsigned char)fragment[open])) ++open;
            if (callee != NULL && callee->type_parameter_count != 0U &&
                fragment[open] == '(') {
                arguments_parsed = parse_text_type_arguments(fragment, open,
                    &arguments, &argument_count, &argument_end);
            }
            if (callee != NULL && callee->type_parameter_count != 0U &&
                fragment[open] == '(' && (arguments_parsed || argument_end > open)) {
                callee_instance = resolve_function_call_instance(ti, caller,
                    instance, callee_ti, callee_template_index, callee,
                    arguments, argument_count);
                if (callee_instance != NULL) {
                    specialized_name = join_suffix(callee->name,
                                                   callee_instance->name);
                    if (discover_dependencies) {
                        (void)add_generic_use(callee->name, callee_instance->name,
                                              0, NULL, NULL, NULL, 0U);
                        set_generic_use_origin(callee->name,
                            callee_instance->name, instance->origin_path,
                            instance->origin_line);
                        request_instance_types(callee_ti, callee, callee_instance);
                        add_function_call_dependency(ti->template_index, caller,
                            instance, callee_template_index, callee,
                            callee_instance);
                    }
                } else if (report_unresolved) {
                    report_error(ti->path, caller->start_line,
                                 "cannot monomorphize call to '%s' from specialization '%s': call arguments do not bind every callee template parameter",
                                 callee->name,
                                 instance->name != NULL ? instance->name : "");
                }
            }
            buf_puts(&output, specialized_name != NULL ? specialized_name : name);
            free(specialized_name);
            free(name);
            list_free(arguments, argument_count);
        } else {
            buf_putc(&output, fragment[i++]);
        }
    }
    if (output.p == NULL) return copy_string("");
    return output.p;
}

static void expand_type_dependencies(const TemplateInfo *infos,
                                    size_t template_count)
{
    size_t i = 0U;
    while (i < g_type_use_count) {
        char *functor = copy_string(g_type_uses[i].functor);
        char *instance_name = copy_string(g_type_uses[i].type_name);
        size_t template_index;
        const TemplateType *constructor = NULL;
        const TemplateInfo *ti;
        size_t *indices;
        size_t j;
        GenInstance *instance;
        GenInstance instance_copy;
        char *fragment;
        if (!find_type_constructor(infos, template_count, functor,
                                   &template_index, &constructor)) {
            free(functor);
            free(instance_name);
            ++i;
            continue;
        }
        ti = &infos[template_index];
        indices = (size_t *)malloc(constructor->parameter_count * sizeof(*indices));
        if (indices == NULL) die("out of memory");
        for (j = 0U; j < constructor->parameter_count; ++j) {
            int parameter = template_parameter_index(ti,
                constructor->parameter_names[j]);
            if (parameter < 0) die("invalid type constructor parameter map");
            indices[j] = (size_t)parameter;
        }
        instance = find_instance(template_index, instance_name, indices,
                                 constructor->parameter_count);
        free(indices);
        if (instance == NULL || constructor->start_offset >= constructor->end_offset ||
            constructor->end_offset > ti->body.len) {
            free(functor);
            free(instance_name);
            ++i;
            continue;
        }
        instance_copy = *instance;
        fragment = copy_range(ti->body.p + constructor->start_offset,
                              (size_t)(constructor->end_offset - constructor->start_offset));
        /* Applications in the body request their own specializations,
         * including applications of bound constructor parameters. */
        request_type_applications(ti, &instance_copy, fragment);
        for (j = 0U; j < ti->constructor_count; ++j) {
            const TemplateType *dependency = &ti->constructors[j];
            GenInstance *dependency_instance;
            if (str_eq(dependency->name, constructor->name) ||
                dependency->parameter_count == 0U ||
                identifier_line_in_code(fragment, dependency->name) == 0U) continue;
            dependency_instance = project_instance(ti, &instance_copy,
                dependency->parameter_names, dependency->parameter_count);
            if (dependency_instance != NULL) {
                (void)add_type_use(dependency->name, dependency_instance->name);
            }
        }
        free(fragment);
        free(functor);
        free(instance_name);
        ++i;
    }
}

static void expand_function_dependencies(const TemplateInfo *infos,
                                         size_t template_count,
                                         int report_unresolved)
{
    size_t i = 0U;
    while (i < g_generic_use_count) {
        GenericUse *use = &g_generic_uses[i++];
        size_t template_index;
        const TemplateInfo *ti;
        const TemplateFunction *function;
        size_t *indices;
        size_t j;
        GenInstance *instance;
        GenInstance instance_copy;
        char *body;
        if (!generic_function_template(infos, template_count, use->name,
                                       &template_index)) continue;
        ti = &infos[template_index];
        function = find_template_function(ti, use->name);
        if (function == NULL) continue;
        indices = (size_t *)malloc(function->type_parameter_count * sizeof(*indices));
        if (indices == NULL) die("out of memory");
        for (j = 0U; j < function->type_parameter_count; ++j) {
            int parameter = template_parameter_index(ti, function->type_parameters[j]);
            if (parameter < 0) die("invalid generic function parameter map");
            indices[j] = (size_t)parameter;
        }
        instance = find_instance(template_index, use->type_name, indices,
                                 function->type_parameter_count);
        free(indices);
        if (instance == NULL || function->end_offset > ti->body.len ||
            function->start_offset >= function->end_offset) continue;
        instance_copy = *instance;
        body = copy_range(ti->body.p + function->start_offset,
                          (size_t)(function->end_offset - function->start_offset));
        request_instance_types(ti, function, &instance_copy);
        {
            char *rewritten_calls = specialize_function_calls(body, ti,
                &instance_copy, function, 1, report_unresolved);
            free(rewritten_calls);
        }
        free(body);
    }
}

void expand_dependencies_to_fixpoint(const TemplateInfo *infos,
                                            size_t template_count,
                                            int report_unresolved)
{
    size_t old_instance_count;
    size_t old_type_use_count;
    size_t old_generic_use_count;
    do {
        old_instance_count = g_instance_count;
        old_type_use_count = g_type_use_count;
        old_generic_use_count = g_generic_use_count;
        expand_type_dependencies(infos, template_count);
        expand_function_dependencies(infos, template_count, report_unresolved);
        expand_type_dependencies(infos, template_count);
    } while (old_instance_count != g_instance_count ||
             old_type_use_count != g_type_use_count ||
             old_generic_use_count != g_generic_use_count);
}


void collect_suffix_api_uses(SourceFile *file, const TemplateInfo *infos,
                                    size_t template_count)
{
    size_t i;
    for (i = 0U; i + 1U < file->token_count; ++i) {
        size_t j;
        if (file->tokens[i].kind != C_TOKEN_IDENT || !token_is(file, i + 1U, "(")) continue;
        for (j = 0U; j < template_count; ++j) {
            size_t k;
            for (k = 0U; k < infos[j].function_count; ++k) {
                const char *base = infos[j].functions[k].name;
                size_t base_length = strlen(base);
                const char *token = file->tokens[i].text;
                if (strlen(token) > base_length && strncmp(token, base, base_length) == 0 &&
                    token[base_length] == '_') {
                    const char *type_name = token + base_length + 1U;
                    const TemplateFunction *function = &infos[j].functions[k];
                    if (function->type_parameter_count == 0U) continue;
                    if (function->type_parameter_count == 1U &&
                        valid_identifier(type_name)) {
                        const char *canonical = find_alias_canonical(type_name);
                        const char *spellings[1];
                        const char *canonicals[1];
                        size_t parameter_indices[1];
                        int parameter = template_parameter_index(
                            &infos[j], function->type_parameters[0]);
                        GenInstance *instance;
                        if (parameter < 0) continue;
                        add_alias(type_name, canonical != NULL ? canonical : type_name);
                        spellings[0] = type_name;
                        canonicals[0] = canonical != NULL ? canonical : type_name;
                        parameter_indices[0] = (size_t)parameter;
                        instance = add_instance(j, parameter_indices,
                                                spellings, canonicals, 1U);
                        (void)add_generic_use(base, instance->name, 0,
                                              NULL, NULL, NULL, 0U);
                        set_generic_use_origin(base, instance->name,
                            file->path, file->tokens[i].line);
                    } else if (function != NULL) {
                        size_t use_index;
                        for (use_index = 0U; use_index < g_instance_count; ++use_index) {
                            GenInstance *instance = &g_instances[use_index];
                            if (instance->template_index == j &&
                                instance_matches_parameters(&infos[j], instance,
                                    function->type_parameters,
                                    function->type_parameter_count) &&
                                str_eq(instance->name, type_name)) {
                                (void)add_generic_use(base, instance->name, 0,
                                                      NULL, NULL, NULL, 0U);
                                set_generic_use_origin(base, instance->name,
                                    file->path, file->tokens[i].line);
                            }
                        }
                    }
                }
            }
        }
    }
}
