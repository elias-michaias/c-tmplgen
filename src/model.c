/* Template model, specialization registries, and shared queries. */
#include "internal.h"

/* Module-private forward declarations. */
static long long integer_constant_unary(IntegerConstant *state);
static long long integer_constant_expression(IntegerConstant *state);


void type_origin_list_add(TypeOrigin **items, size_t *count, size_t *cap,
                                 const char *name, const char *path,
                                 unsigned offset)
{
    size_t i;
    TypeOrigin *next;
    for (i = 0U; i < *count; ++i) {
        if (str_eq((*items)[i].name, name)) return;
    }
    if (*count == *cap) {
        size_t new_cap = *cap == 0U ? 16U : *cap * 2U;
        if (new_cap < *cap || new_cap > (size_t)-1 / sizeof(**items)) {
            die("too many type declarations");
        }
        next = (TypeOrigin *)realloc(*items, new_cap * sizeof(**items));
        if (next == NULL) die("out of memory");
        *items = next;
        *cap = new_cap;
    }
    (*items)[*count].name = copy_string(name);
    (*items)[*count].path = copy_string(path);
    (*items)[*count].offset = offset;
    ++*count;
}

void type_origin_list_free(TypeOrigin *items, size_t count)
{
    size_t i;
    for (i = 0U; i < count; ++i) {
        free(items[i].name);
        free(items[i].path);
    }
    free(items);
}

const TypeOrigin *find_type_origin(const char *name)
{
    size_t i;
    for (i = 0U; i < g_type_origin_count; ++i) {
        if (str_eq(g_type_origins[i].name, name)) return &g_type_origins[i];
    }
    return NULL;
}

const char *find_alias_canonical(const char *name)
{
    size_t i;
    for (i = 0U; i < g_alias_count; ++i) {
        if (str_eq(g_aliases[i].name, name)) return g_aliases[i].canonical;
    }
    return NULL;
}

void add_alias(const char *name, const char *canonical)
{
    size_t i;
    TypeAlias *next;
    size_t new_cap;
    for (i = 0U; i < g_alias_count; ++i) {
        if (str_eq(g_aliases[i].name, name)) {
            if (!str_eq(g_aliases[i].canonical, canonical)) {
                free(g_aliases[i].canonical);
                g_aliases[i].canonical = copy_string(canonical);
            }
            return;
        }
    }
    if (g_alias_count == g_alias_cap) {
        new_cap = g_alias_cap == 0U ? 16U : g_alias_cap * 2U;
        if (new_cap < g_alias_cap || new_cap > (size_t)-1 / sizeof(*g_aliases)) {
            die("too many typedef names");
        }
        next = (TypeAlias *)realloc(g_aliases, new_cap * sizeof(*g_aliases));
        if (next == NULL) die("out of memory");
        g_aliases = next;
        g_alias_cap = new_cap;
    }
    g_aliases[g_alias_count].name = copy_string(name);
    g_aliases[g_alias_count].canonical = copy_string(canonical != NULL ? canonical : name);
    ++g_alias_count;
}

int type_arguments_compatible(const char *left_spelling,
                                     const char *left_canonical,
                                     const char *right_spelling,
                                     const char *right_canonical)
{
    const char *left_alias = valid_identifier(left_spelling) ?
        find_alias_canonical(left_spelling) : NULL;
    const char *right_alias = valid_identifier(right_spelling) ?
        find_alias_canonical(right_spelling) : NULL;
    const char *left = left_alias != NULL ? left_alias :
                       (left_canonical != NULL ? left_canonical : left_spelling);
    const char *right = right_alias != NULL ? right_alias :
                        (right_canonical != NULL ? right_canonical : right_spelling);
    return left != NULL && right != NULL && str_eq(left, right);
}

static char *type_suffix(const char *c_type)
{
    Buf name;
    size_t i;
    int underscore = 0;
    buf_init(&name);
    for (i = 0U; c_type[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)c_type[i];
        if (is_ident_char((char)c)) {
            buf_putc(&name, (char)c);
            underscore = 0;
        } else if (c == '*') {
            if (name.len != 0U && !underscore) buf_putc(&name, '_');
            buf_puts(&name, "ptr");
            underscore = 0;
        } else if (name.len != 0U && !underscore) {
            buf_putc(&name, '_');
            underscore = 1;
        }
    }
    while (name.len != 0U && name.p[name.len - 1U] == '_') {
        --name.len;
        name.p[name.len] = '\0';
    }
    if (name.len == 0U || (name.p[0] >= '0' && name.p[0] <= '9')) {
        Buf prefixed;
        buf_init(&prefixed);
        buf_puts(&prefixed, "type_");
        buf_puts(&prefixed, name.p != NULL ? name.p : "unknown");
        buf_free(&name);
        return prefixed.p;
    }
    return name.p;
}

static const IntegerOperator g_integer_operators[] = {
    { "||", 0 }, { "&&", 1 }, { "|", 2 }, { "^", 3 }, { "&", 4 },
    { "==", 5 }, { "!=", 5 }, { "<=", 6 }, { ">=", 6 },
    { "<", 6 }, { ">", 6 }, { "<<", 7 }, { ">>", 7 },
    { "+", 8 }, { "-", 8 }, { "*", 9 }, { "/", 9 }, { "%", 9 }
};

static void integer_constant_skip(IntegerConstant *state)
{
    while (isspace((unsigned char)*state->p)) ++state->p;
}

static int integer_constant_starts_unary(const char *p)
{
    return is_ident_start(*p) || (*p >= '0' && *p <= '9') ||
           *p == '(' || *p == '+' || *p == '-' || *p == '~' || *p == '!';
}

static void integer_constant_skip_balanced(IntegerConstant *state)
{
    size_t depth = 0U;
    if (*state->p != '(') return;
    do {
        if (*state->p == '(') ++depth;
        else if (*state->p == ')') {
            if (depth != 0U) --depth;
        } else if (*state->p == '\0') {
            state->failed = 1;
            return;
        }
        ++state->p;
    } while (depth != 0U);
}

static const IntegerOperator *integer_constant_operator(const char *p)
{
    const IntegerOperator *best = NULL;
    size_t i;
    for (i = 0U; i < sizeof(g_integer_operators) / sizeof(g_integer_operators[0]);
         ++i) {
        size_t length = strlen(g_integer_operators[i].text);
        if (strncmp(p, g_integer_operators[i].text, length) != 0) continue;
        if (best == NULL || length > strlen(best->text)) {
            best = &g_integer_operators[i];
        }
    }
    return best;
}

static long long integer_constant_primary(IntegerConstant *state)
{
    long long value = 0;
    integer_constant_skip(state);
    if (*state->p == '(') {
        const char *q = state->p + 1;
        int saw_identifier = 0;
        int type_only = 1;
        for (;;) {
            while (isspace((unsigned char)*q)) ++q;
            if (is_ident_start(*q)) {
                while (is_ident_char(*q)) ++q;
                saw_identifier = 1;
            } else if (*q == '*') {
                ++q;
            } else {
                break;
            }
        }
        while (isspace((unsigned char)*q)) ++q;
        if (type_only && saw_identifier && *q == ')') {
            const char *after = q + 1;
            while (isspace((unsigned char)*after)) ++after;
            if (integer_constant_starts_unary(after)) {
                state->p = q + 1;
                state->unknown = 1;
                return integer_constant_unary(state);
            }
        }
        ++state->p;
        value = integer_constant_expression(state);
        integer_constant_skip(state);
        if (*state->p != ')') {
            state->failed = 1;
            return 0;
        }
        ++state->p;
        return value;
    }
    if (*state->p == '\'' || *state->p == '"') {
        char quote = *state->p++;
        while (*state->p != '\0' && *state->p != quote) {
            if (*state->p == '\\' && state->p[1] != '\0') ++state->p;
            ++state->p;
        }
        if (*state->p == quote) ++state->p;
        state->unknown = 1;
        return 0;
    }
    if (*state->p >= '0' && *state->p <= '9') {
        char *end = NULL;
        unsigned long long parsed = strtoull(state->p, &end, 0);
        if (end == state->p) {
            state->failed = 1;
            return 0;
        }
        state->p = end;
        while (*state->p == 'u' || *state->p == 'U' ||
               *state->p == 'l' || *state->p == 'L') ++state->p;
        return (long long)parsed;
    }
    if (is_ident_start(*state->p)) {
        const char *start = state->p;
        size_t length;
        while (is_ident_char(*state->p)) ++state->p;
        length = (size_t)(state->p - start);
        if ((length == 6U && strncmp(start, "sizeof", 6U) == 0) ||
            (length == 8U && strncmp(start, "_Alignof", 8U) == 0) ||
            (length == 8U && strncmp(start, "alignof", 8U) == 0)) {
            integer_constant_skip(state);
            if (*state->p == '(') integer_constant_skip_balanced(state);
            else (void)integer_constant_unary(state);
            state->unknown = 1;
            return 0;
        }
        state->unknown = 1;
        integer_constant_skip(state);
        if (*state->p == '(') integer_constant_skip_balanced(state);
        return 0;
    }
    state->failed = 1;
    return 0;
}

static long long integer_constant_unary(IntegerConstant *state)
{
    integer_constant_skip(state);
    if (*state->p == '+') {
        ++state->p;
        return integer_constant_unary(state);
    }
    if (*state->p == '-') {
        ++state->p;
        return -integer_constant_unary(state);
    }
    if (*state->p == '~') {
        ++state->p;
        return ~integer_constant_unary(state);
    }
    if (*state->p == '!') {
        ++state->p;
        return !integer_constant_unary(state);
    }
    return integer_constant_primary(state);
}

static long long integer_constant_apply(long long left, const char *op,
                                        long long right, IntegerConstant *state)
{
    if (str_eq(op, "||")) return (left || right) ? 1 : 0;
    if (str_eq(op, "&&")) return (left && right) ? 1 : 0;
    if (str_eq(op, "|")) return left | right;
    if (str_eq(op, "^")) return left ^ right;
    if (str_eq(op, "&")) return left & right;
    if (str_eq(op, "==")) return left == right;
    if (str_eq(op, "!=")) return left != right;
    if (str_eq(op, "<")) return left < right;
    if (str_eq(op, "<=")) return left <= right;
    if (str_eq(op, ">")) return left > right;
    if (str_eq(op, ">=")) return left >= right;
    if (str_eq(op, "<<")) return (right >= 0 && right < 64) ? left << right : 0;
    if (str_eq(op, ">>")) return (right >= 0 && right < 64) ? left >> right : 0;
    if (str_eq(op, "+")) return left + right;
    if (str_eq(op, "-")) return left - right;
    if (str_eq(op, "*")) return left * right;
    if (str_eq(op, "/")) {
        if (right == 0) {
            state->failed = 1;
            return 0;
        }
        return left / right;
    }
    if (str_eq(op, "%")) {
        if (right == 0) {
            state->failed = 1;
            return 0;
        }
        return left % right;
    }
    state->failed = 1;
    return 0;
}

static long long integer_constant_binary(IntegerConstant *state, int level)
{
    long long left;
    if (level > 9) return integer_constant_unary(state);
    left = integer_constant_binary(state, level + 1);
    for (;;) {
        const IntegerOperator *op;
        integer_constant_skip(state);
        op = integer_constant_operator(state->p);
        if (op == NULL || op->level != level) break;
        state->p += strlen(op->text);
        {
            long long right =
                integer_constant_binary(state, level + 1);
            if (!state->failed && !state->unknown) {
                left = integer_constant_apply(left, op->text, right, state);
            } else {
                left = 0;
            }
        }
    }
    return left;
}

static long long integer_constant_expression(IntegerConstant *state)
{
    long long condition = integer_constant_binary(state, 0);
    integer_constant_skip(state);
    if (*state->p == '?') {
        long long when_true;
        long long when_false;
        ++state->p;
        when_true = integer_constant_expression(state);
        integer_constant_skip(state);
        if (*state->p != ':') {
            state->failed = 1;
            return 0;
        }
        ++state->p;
        when_false = integer_constant_expression(state);
        if (state->unknown) return 0;
        return condition ? when_true : when_false;
    }
    return condition;
}

char *canonical_value_argument(const char *text)
{
    IntegerConstant state;
    long long value;
    size_t i;
    for (i = 0U; text[i] != '\0'; ++i) {
        if (text[i] == '"' || text[i] == ';' || text[i] == '{' ||
            text[i] == '}') return NULL;
    }
    state.p = text;
    state.failed = 0;
    state.unknown = 0;
    value = integer_constant_expression(&state);
    integer_constant_skip(&state);
    if (!state.failed && *state.p == '\0' && !state.unknown) {
        Buf out;
        char digits[32];
        size_t count = 0U;
        unsigned long long magnitude;
        buf_init(&out);
        if (value < 0) {
            buf_putc(&out, '-');
            value = -value;
        }
        magnitude = (unsigned long long)value;
        do {
            digits[count++] = (char)('0' + (magnitude % 10ULL));
            magnitude /= 10ULL;
        } while (magnitude != 0ULL);
        while (count != 0U) buf_putc(&out, digits[--count]);
        return out.p != NULL ? out.p : copy_string("0");
    }
    return normalize_spaces(text);
}

static char *argument_suffix(const char *spelling)
{
    const char *digits = spelling;
    int negative = 0;
    size_t i;
    if (*digits == '-') {
        negative = 1;
        ++digits;
    } else if (*digits == '+') {
        ++digits;
    }
    for (i = 0U; digits[i] >= '0' && digits[i] <= '9'; ++i) {}
    if (i != 0U && digits[i] == '\0') {
        Buf suffix;
        buf_init(&suffix);
        if (negative) buf_putc(&suffix, 'n');
        buf_puts(&suffix, digits);
        return suffix.p != NULL ? suffix.p : copy_string("value");
    }
    return type_suffix(spelling);
}

static char *instance_key(const size_t *parameter_indices,
                          const char *const *spellings, size_t count)
{
    Buf key;
    size_t i;
    buf_init(&key);
    for (i = 0U; i < count; ++i) {
        buf_put_uint(&key, (unsigned long)parameter_indices[i]);
        buf_putc(&key, '=');
        buf_put_uint(&key, (unsigned long)strlen(spellings[i]));
        buf_putc(&key, ':');
        buf_puts(&key, spellings[i]);
        buf_putc(&key, ';');
    }
    return key.p;
}

char *instance_suffix(const char *const *spellings, size_t count)
{
    Buf suffix;
    size_t i;
    buf_init(&suffix);
    for (i = 0U; i < count; ++i) {
        char *part = argument_suffix(spellings[i]);
        if (i != 0U) buf_putc(&suffix, '_');
        buf_puts(&suffix, part);
        free(part);
    }
    return suffix.p;
}

GenInstance *find_instance(size_t template_index, const char *name,
                                  const size_t *parameter_indices, size_t arity)
{
    size_t i;
    for (i = 0U; i < g_instance_count; ++i) {
        size_t j;
        if (g_instances[i].template_index != template_index ||
            g_instances[i].argument_count != arity ||
            !str_eq(g_instances[i].name, name)) continue;
        for (j = 0U; j < arity; ++j) {
            if (g_instances[i].parameter_indices[j] != parameter_indices[j]) break;
        }
        if (j == arity) return &g_instances[i];
    }
    return NULL;
}

GenInstance *add_instance(size_t template_index,
                                 const size_t *parameter_indices,
                                 const char *const *spellings,
                                 const char *const *canonicals, size_t count)
{
    char *name = instance_suffix(spellings, count);
    char *key = instance_key(parameter_indices, spellings, count);
    GenInstance *found = find_instance(template_index, name, parameter_indices, count);
    GenInstance *next;
    size_t new_cap;
    size_t i;
    if (found != NULL) {
        if (!str_eq(found->key, key)) {
            report_error(NULL, 0U,
                         "type argument tuple maps to colliding specialization suffix '%s'",
                         name);
        }
        free(name);
        free(key);
        return found;
    }
    if (g_instance_count == g_instance_cap) {
        new_cap = g_instance_cap == 0U ? 8U : g_instance_cap * 2U;
        if (new_cap < g_instance_cap ||
            new_cap > (size_t)-1 / sizeof(*g_instances)) die("too many template instances");
        next = (GenInstance *)realloc(g_instances, new_cap * sizeof(*g_instances));
        if (next == NULL) die("out of memory");
        g_instances = next;
        g_instance_cap = new_cap;
    }
    g_instances[g_instance_count].name = name;
    g_instances[g_instance_count].key = key;
    g_instances[g_instance_count].argument_count = count;
    g_instances[g_instance_count].template_index = template_index;
    g_instances[g_instance_count].arguments = NULL;
    g_instances[g_instance_count].parameter_indices = NULL;
    g_instances[g_instance_count].origin_path = NULL;
    g_instances[g_instance_count].origin_line = 0U;
    if (count != 0U) {
        g_instances[g_instance_count].parameter_indices =
            (size_t *)malloc(count * sizeof(*g_instances[g_instance_count].parameter_indices));
        g_instances[g_instance_count].arguments =
            (TypeArgument *)calloc(count, sizeof(*g_instances[g_instance_count].arguments));
        if (g_instances[g_instance_count].arguments == NULL ||
            g_instances[g_instance_count].parameter_indices == NULL) die("out of memory");
        for (i = 0U; i < count; ++i) {
            g_instances[g_instance_count].parameter_indices[i] = parameter_indices[i];
            g_instances[g_instance_count].arguments[i].c_type = copy_string(spellings[i]);
            g_instances[g_instance_count].arguments[i].canonical =
                copy_string(canonicals != NULL && canonicals[i] != NULL ?
                            canonicals[i] : spellings[i]);
        }
    }
    return &g_instances[g_instance_count++];
}

void set_instance_origin(GenInstance *instance, const char *path,
                                size_t line)
{
    if (instance == NULL || instance->origin_path != NULL ||
        path == NULL || path[0] == '\0' || line == 0U) return;
    instance->origin_path = copy_string(path);
    instance->origin_line = line;
}

int add_type_use(const char *functor, const char *type_name)
{
    size_t i;
    TypeUse *next;
    size_t new_cap;
    for (i = 0U; i < g_type_use_count; ++i) {
        if (str_eq(g_type_uses[i].functor, functor) &&
            str_eq(g_type_uses[i].type_name, type_name)) return 1;
    }
    if (g_type_use_count == g_type_use_cap) {
        new_cap = g_type_use_cap == 0U ? 8U : g_type_use_cap * 2U;
        if (new_cap < g_type_use_cap || new_cap > (size_t)-1 / sizeof(*g_type_uses)) {
            die("too many type-functor uses");
        }
        next = (TypeUse *)realloc(g_type_uses, new_cap * sizeof(*g_type_uses));
        if (next == NULL) die("out of memory");
        g_type_uses = next;
        g_type_use_cap = new_cap;
    }
    g_type_uses[g_type_use_count].functor = copy_string(functor);
    g_type_uses[g_type_use_count].type_name = copy_string(type_name);
    ++g_type_use_count;
    return 1;
}

static int generic_use_has_association(const GenericUse *use,
                                       const unsigned *indices,
                                       const char *const *canonicals,
                                       size_t dispatch_count)
{
    size_t i;
    if (!use->dispatch || use->dispatch_count != dispatch_count) return 0;
    for (i = 0U; i < dispatch_count; ++i) {
        if (use->dispatch_indices[i] != indices[i] ||
            !str_eq(use->dispatch_canonicals[i], canonicals[i])) return 0;
    }
    return 1;
}

static void generic_use_set_dispatch(GenericUse *use, const unsigned *indices,
                                     const char *const *types,
                                     const char *const *canonicals,
                                     size_t dispatch_count)
{
    size_t i;
    use->dispatch_indices = NULL;
    use->dispatch_types = NULL;
    use->dispatch_canonicals = NULL;
    use->dispatch_count = dispatch_count;
    if (dispatch_count == 0U) return;
    use->dispatch_indices = (unsigned *)malloc(dispatch_count * sizeof(*use->dispatch_indices));
    use->dispatch_types = (char **)calloc(dispatch_count, sizeof(*use->dispatch_types));
    use->dispatch_canonicals = (char **)calloc(dispatch_count,
                                               sizeof(*use->dispatch_canonicals));
    if (use->dispatch_indices == NULL || use->dispatch_types == NULL ||
        use->dispatch_canonicals == NULL) die("out of memory");
    for (i = 0U; i < dispatch_count; ++i) {
        use->dispatch_indices[i] = indices[i];
        use->dispatch_types[i] = copy_string(types[i]);
        use->dispatch_canonicals[i] = copy_string(canonicals[i]);
    }
}

int add_generic_use(const char *name, const char *type_name, int dispatch,
                           const unsigned *indices, const char *const *types,
                           const char *const *canonicals, size_t dispatch_count)
{
    size_t i;
    GenericUse *next;
    size_t new_cap;
    for (i = 0U; i < g_generic_use_count; ++i) {
        if (str_eq(g_generic_uses[i].name, name) &&
            str_eq(g_generic_uses[i].type_name, type_name)) {
            if (!dispatch) return 1;
            if (generic_use_has_association(&g_generic_uses[i], indices,
                                            canonicals, dispatch_count)) return 1;
            if (!g_generic_uses[i].dispatch) {
                g_generic_uses[i].dispatch = 1;
                generic_use_set_dispatch(&g_generic_uses[i], indices, types,
                                         canonicals, dispatch_count);
                return 1;
            }
        }
    }
    if (g_generic_use_count == g_generic_use_cap) {
        new_cap = g_generic_use_cap == 0U ? 8U : g_generic_use_cap * 2U;
        if (new_cap < g_generic_use_cap || new_cap > (size_t)-1 / sizeof(*g_generic_uses)) {
            die("too many generic function uses");
        }
        next = (GenericUse *)realloc(g_generic_uses, new_cap * sizeof(*g_generic_uses));
        if (next == NULL) die("out of memory");
        g_generic_uses = next;
        g_generic_use_cap = new_cap;
    }
    g_generic_uses[g_generic_use_count].name = copy_string(name);
    g_generic_uses[g_generic_use_count].type_name = copy_string(type_name);
    g_generic_uses[g_generic_use_count].dispatch = dispatch;
    g_generic_uses[g_generic_use_count].origin_path = NULL;
    g_generic_uses[g_generic_use_count].origin_line = 0U;
    g_generic_uses[g_generic_use_count].dispatch_indices = NULL;
    g_generic_uses[g_generic_use_count].dispatch_types = NULL;
    g_generic_uses[g_generic_use_count].dispatch_canonicals = NULL;
    g_generic_uses[g_generic_use_count].dispatch_count = 0U;
    if (dispatch) {
        generic_use_set_dispatch(&g_generic_uses[g_generic_use_count], indices,
                                 types, canonicals, dispatch_count);
    }
    ++g_generic_use_count;
    return 1;
}

void set_generic_use_origin(const char *name, const char *type_name,
                                   const char *path, size_t line)
{
    size_t i;
    if (path == NULL || path[0] == '\0' || line == 0U) return;
    for (i = 0U; i < g_generic_use_count; ++i) {
        GenericUse *use = &g_generic_uses[i];
        if (str_eq(use->name, name) && str_eq(use->type_name, type_name) &&
            use->origin_path == NULL) {
            use->origin_path = copy_string(path);
            use->origin_line = line;
            return;
        }
    }
}

void record_type_variable_mismatch(const char *path, size_t line,
                                         const char *function_name,
                                         const char *parameter_name,
                                         const char *first_type,
                                         const char *second_type)
{
    TypeVariableMismatch *next;
    size_t i;
    size_t new_cap;
    if (path == NULL || line == 0U || first_type == NULL ||
        second_type == NULL || str_eq(first_type, second_type)) return;
    for (i = 0U; i < g_type_variable_mismatch_count; ++i) {
        const TypeVariableMismatch *mismatch = &g_type_variable_mismatches[i];
        if (mismatch->line == line &&
            str_eq(mismatch->path, path) &&
            str_eq(mismatch->function_name, function_name) &&
            str_eq(mismatch->parameter_name, parameter_name) &&
            ((str_eq(mismatch->first_type, first_type) &&
              str_eq(mismatch->second_type, second_type)) ||
             (str_eq(mismatch->first_type, second_type) &&
              str_eq(mismatch->second_type, first_type)))) return;
    }
    if (g_type_variable_mismatch_count == g_type_variable_mismatch_cap) {
        new_cap = g_type_variable_mismatch_cap == 0U ? 4U :
                  g_type_variable_mismatch_cap * 2U;
        if (new_cap < g_type_variable_mismatch_cap ||
            new_cap > (size_t)-1 / sizeof(*g_type_variable_mismatches)) {
            die("too many type-variable mismatches");
        }
        next = (TypeVariableMismatch *)realloc(g_type_variable_mismatches,
            new_cap * sizeof(*g_type_variable_mismatches));
        if (next == NULL) die("out of memory");
        g_type_variable_mismatches = next;
        g_type_variable_mismatch_cap = new_cap;
    }
    g_type_variable_mismatches[g_type_variable_mismatch_count].path =
        copy_string(path);
    g_type_variable_mismatches[g_type_variable_mismatch_count].line = line;
    g_type_variable_mismatches[g_type_variable_mismatch_count].function_name =
        copy_string(function_name);
    g_type_variable_mismatches[g_type_variable_mismatch_count].parameter_name =
        copy_string(parameter_name);
    g_type_variable_mismatches[g_type_variable_mismatch_count].first_type =
        copy_string(first_type);
    g_type_variable_mismatches[g_type_variable_mismatch_count].second_type =
        copy_string(second_type);
    ++g_type_variable_mismatch_count;
}

int has_type_variable_mismatch(const char *path, size_t line,
                                      const char *function_name)
{
    size_t i;
    for (i = 0U; i < g_type_variable_mismatch_count; ++i) {
        const TypeVariableMismatch *mismatch = &g_type_variable_mismatches[i];
        if (mismatch->line == line && str_eq(mismatch->path, path) &&
            str_eq(mismatch->function_name, function_name)) return 1;
    }
    return 0;
}


int generic_use_matches(const char *name, const char *type_name,
                               int dispatch_only)
{
    size_t i;
    for (i = 0U; i < g_generic_use_count; ++i) {
        if (str_eq(g_generic_uses[i].name, name) &&
            str_eq(g_generic_uses[i].type_name, type_name) &&
            (!dispatch_only || g_generic_uses[i].dispatch)) return 1;
    }
    return 0;
}

int type_use_matches(const char *functor, const char *type_name)
{
    size_t i;
    for (i = 0U; i < g_type_use_count; ++i) {
        if (str_eq(g_type_uses[i].functor, functor) &&
            str_eq(g_type_uses[i].type_name, type_name)) return 1;
    }
    return 0;
}

size_t gen_template_count(void)
{
    return g_template_count;
}

void add_function_call_dependency(size_t caller_template_index,
                                         const TemplateFunction *caller,
                                         const GenInstance *caller_instance,
                                         size_t callee_template_index,
                                         const TemplateFunction *callee,
                                         const GenInstance *callee_instance)
{
    size_t i;
    FunctionCallDependency *next;
    size_t new_cap;
    for (i = 0U; i < g_function_call_dependency_count; ++i) {
        const FunctionCallDependency *dependency =
            &g_function_call_dependencies[i];
        if (dependency->caller_template_index == caller_template_index &&
            dependency->callee_template_index == callee_template_index &&
            str_eq(dependency->caller_function, caller->name) &&
            str_eq(dependency->caller_instance, caller_instance->name) &&
            str_eq(dependency->callee_function, callee->name) &&
            str_eq(dependency->callee_instance, callee_instance->name)) return;
    }
    if (g_function_call_dependency_count == g_function_call_dependency_cap) {
        new_cap = g_function_call_dependency_cap == 0U ? 8U :
                  g_function_call_dependency_cap * 2U;
        if (new_cap < g_function_call_dependency_cap ||
            new_cap > (size_t)-1 / sizeof(*g_function_call_dependencies)) {
            die("too many specialized function dependencies");
        }
        next = (FunctionCallDependency *)realloc(g_function_call_dependencies,
            new_cap * sizeof(*g_function_call_dependencies));
        if (next == NULL) die("out of memory");
        g_function_call_dependencies = next;
        g_function_call_dependency_cap = new_cap;
    }
    g_function_call_dependencies[g_function_call_dependency_count].caller_template_index =
        caller_template_index;
    g_function_call_dependencies[g_function_call_dependency_count].caller_function =
        copy_string(caller->name);
    g_function_call_dependencies[g_function_call_dependency_count].caller_instance =
        copy_string(caller_instance->name);
    g_function_call_dependencies[g_function_call_dependency_count].callee_template_index =
        callee_template_index;
    g_function_call_dependencies[g_function_call_dependency_count].callee_function =
        copy_string(callee->name);
    g_function_call_dependencies[g_function_call_dependency_count].callee_instance =
        copy_string(callee_instance->name);
    ++g_function_call_dependency_count;
}

int template_requires_constraint(const TemplateInfo *ti, const char *name)
{
    size_t i, j;
    for (i = 0U; i < ti->annotation_count; ++i) {
        for (j = 0U; j < ti->annotations[i].constraint_count; ++j) {
            size_t k;
            const TemplateConstraint *constraint = &ti->annotations[i].constraints[j];
            for (k = 0U; k < constraint->required_type_count; ++k) {
                if (str_eq(constraint->required_types[k], name)) return 1;
            }
        }
    }
    return 0;
}

int any_template_requires_constraint(const char *name)
{
    size_t i;
    for (i = 0U; i < g_all_template_info_count; ++i) {
        if (template_requires_constraint(&g_all_template_infos[i], name)) return 1;
    }
    return 0;
}

void template_info_init(TemplateInfo *ti)
{
    memset(ti, 0, sizeof(*ti));
    buf_init(&ti->body);
}

void template_application_free(TemplateApplication *application)
{
    free(application->constructor);
    list_free(application->arguments, application->argument_count);
    memset(application, 0, sizeof(*application));
}

static void template_constraint_free(TemplateConstraint *constraint)
{
    free(constraint->parameter_name);
    list_free(constraint->required_types, constraint->required_type_count);
    list_free(constraint->expressions, constraint->expression_count);
    memset(constraint, 0, sizeof(*constraint));
}

void template_constraint_array_free(TemplateConstraint *constraints,
                                           size_t count)
{
    size_t i;
    for (i = 0U; i < count; ++i) template_constraint_free(&constraints[i]);
    free(constraints);
}

void template_constraint_add(TemplateConstraint **constraints,
                                    size_t *constraint_count,
                                    size_t *constraint_cap,
                                    const char *parameter_name,
                                    const char *required_type,
                                    size_t line)
{
    size_t i;
    TemplateConstraint *constraint = NULL;
    for (i = 0U; i < *constraint_count; ++i) {
        if (str_eq((*constraints)[i].parameter_name, parameter_name)) {
            constraint = &(*constraints)[i];
            break;
        }
    }
    if (constraint == NULL) {
        if (*constraint_count == *constraint_cap) {
            size_t new_cap = *constraint_cap == 0U ? 2U : *constraint_cap * 2U;
            TemplateConstraint *next;
            if (new_cap < *constraint_cap ||
                new_cap > (size_t)-1 / sizeof(**constraints)) {
                die("too many template constraints");
            }
            next = (TemplateConstraint *)realloc(*constraints,
                                                  new_cap * sizeof(**constraints));
            if (next == NULL) die("out of memory");
            *constraints = next;
            *constraint_cap = new_cap;
        }
        constraint = &(*constraints)[(*constraint_count)++];
        memset(constraint, 0, sizeof(*constraint));
        constraint->parameter_name = copy_string(parameter_name);
        constraint->line = line;
    }
    if (!list_contains(constraint->required_types,
                       constraint->required_type_count, required_type)) {
        list_add(&constraint->required_types, &constraint->required_type_count,
                 &constraint->required_type_cap, required_type);
    }
}

void template_constraint_add_expression(TemplateConstraint **constraints,
                                              size_t *constraint_count,
                                              size_t *constraint_cap,
                                              const char *parameter_name,
                                              const char *expression,
                                              size_t line)
{
    size_t i;
    TemplateConstraint *constraint = NULL;
    for (i = 0U; i < *constraint_count; ++i) {
        if (str_eq((*constraints)[i].parameter_name, parameter_name)) {
            constraint = &(*constraints)[i];
            break;
        }
    }
    if (constraint == NULL) {
        if (*constraint_count == *constraint_cap) {
            size_t new_cap = *constraint_cap == 0U ? 2U : *constraint_cap * 2U;
            TemplateConstraint *next;
            if (new_cap < *constraint_cap ||
                new_cap > (size_t)-1 / sizeof(**constraints)) {
                die("too many template constraints");
            }
            next = (TemplateConstraint *)realloc(*constraints,
                                                  new_cap * sizeof(**constraints));
            if (next == NULL) die("out of memory");
            *constraints = next;
            *constraint_cap = new_cap;
        }
        constraint = &(*constraints)[(*constraint_count)++];
        memset(constraint, 0, sizeof(*constraint));
        constraint->parameter_name = copy_string(parameter_name);
        constraint->line = line;
    }
    if (!list_contains(constraint->expressions, constraint->expression_count,
                       expression)) {
        list_add(&constraint->expressions, &constraint->expression_count,
                 &constraint->expression_cap, expression);
    }
}

void template_parameter_kind_clear(TemplateParameterKind *kind)
{
    size_t i;
    free(kind->value_type);
    for (i = 0U; i < kind->signature_count; ++i) {
        template_parameter_kind_clear(&kind->signature[i]);
    }
    free(kind->signature);
    memset(kind, 0, sizeof(*kind));
}

void template_parameter_kind_copy(TemplateParameterKind *target,
                                         const TemplateParameterKind *source)
{
    size_t i;
    memset(target, 0, sizeof(*target));
    target->is_value = source->is_value;
    target->is_constructor = source->is_constructor;
    target->value_type = source->value_type != NULL ?
                         copy_string(source->value_type) : NULL;
    target->signature_count = source->signature_count;
    if (source->signature_count != 0U) {
        target->signature = (TemplateParameterKind *)calloc(
            source->signature_count, sizeof(*target->signature));
        if (target->signature == NULL) die("out of memory");
        for (i = 0U; i < source->signature_count; ++i) {
            template_parameter_kind_copy(&target->signature[i], &source->signature[i]);
        }
    }
}

int template_parameter_kind_equal(const TemplateParameterKind *left,
                                         const TemplateParameterKind *right)
{
    size_t i;
    if (left->is_value != right->is_value ||
        left->is_constructor != right->is_constructor ||
        !str_eq(left->value_type != NULL ? left->value_type : "",
                right->value_type != NULL ? right->value_type : "") ||
        left->signature_count != right->signature_count) return 0;
    for (i = 0U; i < left->signature_count; ++i) {
        if (!template_parameter_kind_equal(&left->signature[i],
                                           &right->signature[i])) return 0;
    }
    return 1;
}

void template_parameter_kinds_free(TemplateParameterKind *kinds,
                                          size_t count)
{
    size_t i;
    if (kinds == NULL) return;
    for (i = 0U; i < count; ++i) template_parameter_kind_clear(&kinds[i]);
    free(kinds);
}

size_t function_explicit_count(const TemplateFunction *function)
{
    size_t count = 0U;
    while (count < function->parameter_count &&
           function->parameters[count].explicit_type) ++count;
    return count;
}

int function_has_explicit_parameters(const TemplateFunction *function)
{
    size_t i;
    for (i = 0U; i < function->parameter_count; ++i) {
        if (function->parameters[i].explicit_type) return 1;
    }
    return 0;
}


void template_info_free(TemplateInfo *ti)
{
    size_t i;
    list_free(ti->parameter_names, ti->parameter_count);
    template_parameter_kinds_free(ti->parameter_kinds, ti->parameter_count);
    for (i = 0U; i < ti->function_count; ++i) {
        size_t j;
        free(ti->functions[i].name);
        list_free(ti->functions[i].declared_parameters,
                  ti->functions[i].declared_parameter_count);
        for (j = 0U; j < ti->functions[i].constraint_count; ++j) {
            template_constraint_free(&ti->functions[i].constraints[j]);
        }
        free(ti->functions[i].constraints);
        for (j = 0U; j < ti->functions[i].parameter_count; ++j) {
            free(ti->functions[i].parameters[j].type);
            free(ti->functions[i].parameters[j].name);
            free(ti->functions[i].parameters[j].explicit_parameter);
            list_free(ti->functions[i].parameters[j].type_parameters,
                      ti->functions[i].parameters[j].type_parameter_count);
            template_application_free(&ti->functions[i].parameters[j].application);
        }
        free(ti->functions[i].parameters);
        free(ti->functions[i].return_type);
        list_free(ti->functions[i].return_parameters,
                  ti->functions[i].return_parameter_count);
        template_application_free(&ti->functions[i].return_application);
        free(ti->functions[i].dispatch_indices);
        list_free(ti->functions[i].type_parameters,
                  ti->functions[i].type_parameter_count);
    }
    free(ti->functions);
    for (i = 0U; i < ti->constructor_count; ++i) {
        size_t j;
        free(ti->constructors[i].name);
        list_free(ti->constructors[i].declared_parameters,
                  ti->constructors[i].declared_parameter_count);
        for (j = 0U; j < ti->constructors[i].constraint_count; ++j) {
            template_constraint_free(&ti->constructors[i].constraints[j]);
        }
        free(ti->constructors[i].constraints);
        list_free(ti->constructors[i].parameter_names,
                  ti->constructors[i].parameter_count);
    }
    free(ti->constructors);
    for (i = 0U; i < ti->annotation_count; ++i) {
        size_t j;
        list_free(ti->annotations[i].parameter_names,
                  ti->annotations[i].parameter_count);
        template_parameter_kinds_free(ti->annotations[i].parameter_kinds,
                                      ti->annotations[i].parameter_count);
        for (j = 0U; j < ti->annotations[i].constraint_count; ++j) {
            template_constraint_free(&ti->annotations[i].constraints[j]);
        }
        free(ti->annotations[i].constraints);
    }
    free(ti->annotations);
    list_free(ti->includes, ti->include_count);
    free(ti->path);
    buf_free(&ti->body);
    template_info_init(ti);
}

int template_parameter_index(const TemplateInfo *ti, const char *name)
{
    size_t i;
    for (i = 0U; i < ti->parameter_count; ++i) {
        if (str_eq(ti->parameter_names[i], name)) return (int)i;
    }
    return -1;
}

int template_parameter_is_value(const TemplateInfo *ti, size_t index)
{
    return ti != NULL && ti->parameter_kinds != NULL &&
           index < ti->parameter_count && ti->parameter_kinds[index].is_value;
}

int template_parameter_is_constructor(const TemplateInfo *ti, size_t index)
{
    return ti != NULL && ti->parameter_kinds != NULL &&
           index < ti->parameter_count && ti->parameter_kinds[index].is_constructor;
}

int function_has_constructor_parameters(const TemplateInfo *ti,
                                               const TemplateFunction *function)
{
    size_t i;
    for (i = 0U; i < function->type_parameter_count; ++i) {
        int parameter = template_parameter_index(ti, function->type_parameters[i]);
        if (parameter >= 0 &&
            template_parameter_is_constructor(ti, (size_t)parameter)) return 1;
    }
    return 0;
}

void copy_template_constraints(TemplateConstraint **target,
                                     size_t *target_count, size_t *target_cap,
                                     const TemplateConstraint *source,
                                     size_t source_count)
{
    size_t i, j;
    for (i = 0U; i < source_count; ++i) {
        TemplateConstraint *constraint;
        TemplateConstraint *next;
        if (*target_count == *target_cap) {
            size_t new_cap = *target_cap == 0U ? 2U : *target_cap * 2U;
            if (new_cap < *target_cap ||
                new_cap > (size_t)-1 / sizeof(**target)) {
                die("too many template constraints");
            }
            next = (TemplateConstraint *)realloc(*target,
                                                 new_cap * sizeof(**target));
            if (next == NULL) die("out of memory");
            *target = next;
            *target_cap = new_cap;
        }
        constraint = &(*target)[(*target_count)++];
        memset(constraint, 0, sizeof(*constraint));
        constraint->parameter_name = copy_string(source[i].parameter_name);
        constraint->line = source[i].line;
        for (j = 0U; j < source[i].required_type_count; ++j) {
            list_add(&constraint->required_types,
                     &constraint->required_type_count,
                     &constraint->required_type_cap,
                     source[i].required_types[j]);
        }
        for (j = 0U; j < source[i].expression_count; ++j) {
            list_add(&constraint->expressions, &constraint->expression_count,
                     &constraint->expression_cap, source[i].expressions[j]);
        }
    }
}

void add_template_type(TemplateInfo *ti, const char *name,
                              char **declared_parameters, size_t declared_count,
                              const TemplateConstraint *constraints,
                              size_t constraint_count,
                              char **parameters, size_t parameter_count,
                              unsigned start_offset, unsigned end_offset,
                              unsigned start_line, int is_struct_tag,
                              int is_union_tag)
{
    TemplateType *next;
    size_t new_cap;
    size_t i;
    for (i = 0U; i < ti->constructor_count; ++i) {
        if (str_eq(ti->constructors[i].name, name)) return;
    }
    if (ti->constructor_count == ti->constructor_cap) {
        new_cap = ti->constructor_cap == 0U ? 4U : ti->constructor_cap * 2U;
        if (new_cap < ti->constructor_cap ||
            new_cap > (size_t)-1 / sizeof(*ti->constructors)) {
            die("too many type constructors in template");
        }
        next = (TemplateType *)realloc(ti->constructors,
                                       new_cap * sizeof(*ti->constructors));
        if (next == NULL) die("out of memory");
        ti->constructors = next;
        ti->constructor_cap = new_cap;
    }
    if ((size_t)end_offset < ti->body.len && ti->body.p[end_offset] == ';') {
        ++end_offset;
    }
    ti->constructors[ti->constructor_count].name = copy_string(name);
    ti->constructors[ti->constructor_count].declared_parameters = NULL;
    ti->constructors[ti->constructor_count].declared_parameter_count = 0U;
    ti->constructors[ti->constructor_count].declared_parameter_cap = 0U;
    ti->constructors[ti->constructor_count].constraints = NULL;
    ti->constructors[ti->constructor_count].constraint_count = 0U;
    ti->constructors[ti->constructor_count].constraint_cap = 0U;
    ti->constructors[ti->constructor_count].parameter_names = NULL;
    ti->constructors[ti->constructor_count].parameter_count = 0U;
    ti->constructors[ti->constructor_count].parameter_cap = 0U;
    ti->constructors[ti->constructor_count].start_offset = start_offset;
    ti->constructors[ti->constructor_count].end_offset = end_offset;
    ti->constructors[ti->constructor_count].start_line = start_line;
    ti->constructors[ti->constructor_count].is_struct_tag = is_struct_tag;
    ti->constructors[ti->constructor_count].is_union_tag = is_union_tag;
    for (i = 0U; i < declared_count; ++i) {
        list_add(&ti->constructors[ti->constructor_count].declared_parameters,
                 &ti->constructors[ti->constructor_count].declared_parameter_count,
                 &ti->constructors[ti->constructor_count].declared_parameter_cap,
                 declared_parameters[i]);
    }
    copy_template_constraints(&ti->constructors[ti->constructor_count].constraints,
        &ti->constructors[ti->constructor_count].constraint_count,
        &ti->constructors[ti->constructor_count].constraint_cap,
        constraints, constraint_count);
    for (i = 0U; i < parameter_count; ++i) {
        list_add(&ti->constructors[ti->constructor_count].parameter_names,
                 &ti->constructors[ti->constructor_count].parameter_count,
                 &ti->constructors[ti->constructor_count].parameter_cap,
                 parameters[i]);
    }
    ++ti->constructor_count;
}

void add_template_function(TemplateInfo *ti, TemplateFunction *function)
{
    TemplateFunction *next;
    size_t new_cap;
    if (ti->function_count == ti->function_cap) {
        new_cap = ti->function_cap == 0U ? 8U : ti->function_cap * 2U;
        if (new_cap < ti->function_cap ||
            new_cap > (size_t)-1 / sizeof(*ti->functions)) {
            die("too many generic functions in template");
        }
        next = (TemplateFunction *)realloc(ti->functions,
                                           new_cap * sizeof(*ti->functions));
        if (next == NULL) die("out of memory");
        ti->functions = next;
        ti->function_cap = new_cap;
    }
    ti->functions[ti->function_count++] = *function;
    memset(function, 0, sizeof(*function));
}

void collect_cursor_template_parameters(CXTranslationUnit tu, CXCursor cursor,
                                               const TemplateInfo *ti,
                                               char ***names, size_t *count,
                                               size_t *capacity)
{
    CXToken *tokens = NULL;
    unsigned token_count = 0U;
    unsigned i;
    clang_tokenize(tu, clang_getCursorExtent(cursor), &tokens, &token_count);
    for (i = 0U; i < token_count; ++i) {
        char *token;
        size_t constructor_index;
        if (clang_getTokenKind(tokens[i]) != CXToken_Identifier) continue;
        token = copy_cx_string(clang_getTokenSpelling(tu, tokens[i]));
        if (template_parameter_index(ti, token) >= 0 &&
            !list_contains(*names, *count, token)) {
            list_add(names, count, capacity, token);
        }
        {
            int applied = 0;
            if (i + 1U < token_count) {
                char *next_token = copy_cx_string(
                    clang_getTokenSpelling(tu, tokens[i + 1U]));
                applied = str_eq(next_token, "(");
                free(next_token);
            }
        for (constructor_index = 0U; constructor_index < ti->constructor_count;
             ++constructor_index) {
            const TemplateType *constructor = &ti->constructors[constructor_index];
            size_t parameter_index;
            if (!str_eq(token, constructor->name)) continue;
            if (applied) break;
            for (parameter_index = 0U;
                 parameter_index < constructor->parameter_count;
                 ++parameter_index) {
                const char *parameter = constructor->parameter_names[parameter_index];
                if (!list_contains(*names, *count, parameter)) {
                    list_add(names, count, capacity, parameter);
                }
            }
        }
        }
        free(token);
    }
    clang_disposeTokens(tu, tokens, token_count);
}

const StructuralSchema *find_structural_schema(const char *name,
                                                       int *ambiguous)
{
    const StructuralSchema *found = NULL;
    size_t i;
    *ambiguous = 0;
    for (i = 0U; i < g_structural_schema_count; ++i) {
        if (!str_eq(g_structural_schemas[i].name, name)) continue;
        if (found != NULL && found->is_union != g_structural_schemas[i].is_union) {
            *ambiguous = 1;
            return NULL;
        }
        found = &g_structural_schemas[i];
    }
    return found;
}

const TemplateType *find_template_type(const TemplateInfo *ti,
                                              const char *name)
{
    size_t i;
    for (i = 0U; i < ti->constructor_count; ++i) {
        if (str_eq(ti->constructors[i].name, name)) return &ti->constructors[i];
    }
    return NULL;
}

int find_type_constructor(const TemplateInfo *infos, size_t count,
                                 const char *name, size_t *template_index,
                                 const TemplateType **constructor)
{
    size_t i;
    for (i = 0U; i < count; ++i) {
        const TemplateType *found = find_template_type(&infos[i], name);
        if (found != NULL && found->parameter_count != 0U) {
            *template_index = i;
            *constructor = found;
            return 1;
        }
    }
    return 0;
}

int builtin_type_argument(const char *name)
{
    static const char *const names[] = {
        "void", "char", "short", "int", "long", "float", "double",
        "signed", "unsigned", "_Bool"
    };
    size_t i;
    for (i = 0U; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (str_eq(name, names[i])) return 1;
    }
    return 0;
}

int generic_function_template(const TemplateInfo *infos, size_t count,
                                     const char *name, size_t *template_index)
{
    size_t i;
    for (i = 0U; i < count; ++i) {
        const TemplateFunction *function = find_template_function(&infos[i], name);
        if (function != NULL && function->type_parameter_count != 0U &&
            function->dispatch_count != 0U) {
            *template_index = i;
            return 1;
        }
    }
    return 0;
}

const TemplateFunction *find_template_function(const TemplateInfo *ti,
                                                       const char *name)
{
    size_t i;
    for (i = 0U; i < ti->function_count; ++i) {
        if (str_eq(ti->functions[i].name, name)) return &ti->functions[i];
    }
    return NULL;
}

int function_explicit_index(const TemplateFunction *function,
                                   const char *parameter)
{
    size_t i;
    for (i = 0U; i < function->parameter_count; ++i) {
        if (function->parameters[i].explicit_type &&
            str_eq(function->parameters[i].explicit_parameter, parameter)) {
            return (int)i;
        }
    }
    return -1;
}
