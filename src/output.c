/* Generated paths, output registry, and argument-type requirements. */
#include "internal.h"

/* Module-private forward declarations. */
static int compute_instance_requirement(size_t instance_index, unsigned depth);


static const char *skip_c_trivia_text(const char *p)
{
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' ||
               *p == '\f' || *p == '\v') ++p;
        if (p[0] == '/' && p[1] == '/') {
            while (*p != '\0' && *p != '\n') ++p;
        } else if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p != '\0' && !(p[0] == '*' && p[1] == '/')) ++p;
            if (*p != '\0') p += 2;
        } else {
            return p;
        }
    }
}

static const char *skip_horizontal_space(const char *p)
{
    while (*p == ' ' || *p == '\t') ++p;
    return p;
}

static char *read_directive_identifier(const char **p)
{
    const char *start;
    *p = skip_horizontal_space(*p);
    if (!is_ident_start(**p)) return NULL;
    start = *p;
    while (is_ident_char(**p)) ++*p;
    return copy_range(start, (size_t)(*p - start));
}

static int directive_keyword(const char **p, const char *keyword)
{
    size_t length = strlen(keyword);
    if (strncmp(*p, keyword, length) != 0 || is_ident_char((*p)[length])) return 0;
    *p += length;
    return 1;
}

/* Returns the include-guard macro of a header written as
 *     #ifndef X / #if !defined(X)    followed by    #define X
 * before any other code, or NULL. */
static char *parse_include_guard(const char *text)
{
    const char *p = skip_c_trivia_text(text);
    char *guard = NULL;
    char *defined = NULL;
    if (*p != '#') return NULL;
    p = skip_horizontal_space(p + 1);
    if (directive_keyword(&p, "ifndef")) {
        guard = read_directive_identifier(&p);
    } else if (directive_keyword(&p, "if")) {
        int parenthesized;
        p = skip_horizontal_space(p);
        if (*p != '!') return NULL;
        p = skip_horizontal_space(p + 1);
        if (!directive_keyword(&p, "defined")) return NULL;
        p = skip_horizontal_space(p);
        parenthesized = *p == '(';
        if (parenthesized) ++p;
        guard = read_directive_identifier(&p);
        if (guard == NULL) return NULL;
        p = skip_horizontal_space(p);
        if (parenthesized) {
            if (*p != ')') {
                free(guard);
                return NULL;
            }
            ++p;
        }
    }
    if (guard == NULL) return NULL;
    p = skip_c_trivia_text(p);
    if (*p != '#') {
        free(guard);
        return NULL;
    }
    p = skip_horizontal_space(p + 1);
    if (directive_keyword(&p, "define")) defined = read_directive_identifier(&p);
    if (defined == NULL || !str_eq(defined, guard)) {
        free(defined);
        free(guard);
        return NULL;
    }
    free(defined);
    return guard;
}

const HeaderGuard *header_guard_for(const char *path)
{
    size_t i;
    Buf text;
    HeaderGuard *entry;
    for (i = 0U; i < g_header_guard_count; ++i) {
        if (str_eq(g_header_guards[i].path, path)) return &g_header_guards[i];
    }
    if (g_header_guard_count == g_header_guard_cap) {
        size_t new_cap = g_header_guard_cap == 0U ? 8U : g_header_guard_cap * 2U;
        HeaderGuard *next;
        if (new_cap < g_header_guard_cap ||
            new_cap > (size_t)-1 / sizeof(*g_header_guards)) die("too many headers");
        next = (HeaderGuard *)realloc(g_header_guards,
                                      new_cap * sizeof(*g_header_guards));
        if (next == NULL) die("out of memory");
        g_header_guards = next;
        g_header_guard_cap = new_cap;
    }
    entry = &g_header_guards[g_header_guard_count++];
    entry->path = copy_string(path);
    entry->guard = NULL;
    buf_init(&text);
    if (read_file(path, &text)) {
        entry->guard = parse_include_guard(text.p != NULL ? text.p : "");
    }
    buf_free(&text);
    return entry;
}

const char *header_path_for_guard(const char *guard)
{
    size_t i;
    for (i = 0U; i < g_header_guard_count; ++i) {
        if (g_header_guards[i].guard != NULL &&
            str_eq(g_header_guards[i].guard, guard)) return g_header_guards[i].path;
    }
    return NULL;
}

void clear_requirement_state(void)
{
    size_t i;
    for (i = 0U; i < g_header_guard_count; ++i) {
        free(g_header_guards[i].path);
        free(g_header_guards[i].guard);
    }
    free(g_header_guards);
    g_header_guards = NULL;
    g_header_guard_count = 0U;
    g_header_guard_cap = 0U;
    string_set_free(&g_reported_requirement_errors);
}

static size_t generated_include_before(const char *path, unsigned offset)
{
    return generated_include_before_target(path, offset, NULL);
}

/* As generated_include_before, but with `only_target` (absolute) only that
 * generated file counts. */
size_t generated_include_before_target(const char *path, unsigned offset,
                                             const char *only_target)
{
    Buf text;
    char *directory;
    const char *p;
    size_t line = 1U;
    size_t found = 0U;
    buf_init(&text);
    if (!read_file(path, &text) || text.p == NULL) {
        buf_free(&text);
        return 0U;
    }
    directory = path_parent(path);
    p = text.p;
    while (*p != '\0' && (size_t)(p - text.p) < (size_t)offset && found == 0U) {
        const char *q = skip_horizontal_space(p);
        if (*q == '#') {
            q = skip_horizontal_space(q + 1);
            if (directive_keyword(&q, "include")) {
                q = skip_horizontal_space(q);
                if (*q == '"') {
                    const char *end = strchr(q + 1, '"');
                    const char *newline = strchr(q + 1, '\n');
                    if (end != NULL && (newline == NULL || end < newline)) {
                        char *target = copy_range(q + 1, (size_t)(end - q - 1));
                        char *candidate = join_path(directory, target);
                        if (only_target != NULL) {
                            char *absolute = absolute_path(candidate);
                            if (str_eq(absolute, only_target)) found = line;
                            free(absolute);
                        } else if (is_generated_output_file(candidate)) {
                            found = line;
                        }
                        free(candidate);
                        free(target);
                    }
                }
            }
        }
        while (*p != '\0' && *p != '\n') ++p;
        if (*p == '\n') {
            ++p;
            ++line;
        }
    }
    free(directory);
    buf_free(&text);
    return found;
}

int path_has_suffix(const char *path, const char *suffix)
{
    size_t length = strlen(path);
    size_t suffix_length = strlen(suffix);
    return length >= suffix_length &&
           strcmp(path + length - suffix_length, suffix) == 0;
}

static void report_requirement_error(const GenInstance *instance,
                                     const char *type_name, const char *fmt,
                                     const char *a, const char *b)
{
    if (string_set_has(&g_reported_requirement_errors, type_name)) return;
    string_set_add(&g_reported_requirement_errors, type_name);
    report_error(instance->origin_path, instance->origin_line, fmt, a, b);
}

/* Adds the guard of the header that declares `origin` to `guards`. */
static int add_origin_requirement(const TypeOrigin *origin, const char *type_name,
                                  const GenInstance *instance, StringSet *guards)
{
    const HeaderGuard *header;
    size_t include_line;
    if (path_has_suffix(origin->path, ".c")) {
        report_requirement_error(instance, type_name,
            "template argument type '%s' is declared in %s, a .c file; "
            "declare it in a header with an include guard", type_name,
            origin->path);
        return 0;
    }
    header = header_guard_for(origin->path);
    if (header->guard == NULL) {
        report_requirement_error(instance, type_name,
            "template argument type '%s' is declared in %s, which has no "
            "#ifndef/#define include guard (generated code tests that macro; "
            "#pragma once alone is not enough)", type_name, origin->path);
        return 0;
    }
    include_line = generated_include_before(origin->path, origin->offset);
    if (include_line != 0U) {
        report_requirement_error(instance, type_name,
            "%s includes a generated header before declaring template "
            "argument type '%s'; declare the type before that #include",
            origin->path, type_name);
        return 0;
    }
    string_set_add(guards, header->guard);
    return 1;
}

static int is_builtin_type_word(const char *word)
{
    static const char *const words[] = {
        "void", "char", "short", "int", "long", "float", "double", "signed",
        "unsigned", "_Bool", "bool", "_Complex", "_Imaginary", "__int128",
        "const", "volatile", "restrict", "__restrict", "_Atomic", "static",
        "inline", "register", "extern"
    };
    size_t i;
    for (i = 0U; i < sizeof(words) / sizeof(words[0]); ++i) {
        if (str_eq(word, words[i])) return 1;
    }
    return 0;
}

/* Whether `header` (an #include operand as written, quotes included) of the
 * template `ti` names the generated reflection.h: this project's, or, since a
 * template directory can serve several projects, any "reflection.h" that does
 * not exist yet or was written by tmplgen. */
int template_include_is_reflection(const TemplateInfo *ti, const char *header)
{
    size_t length = strlen(header);
    char *directory, *target, *candidate, *absolute, *reflection, *reflection_absolute;
    int match;
    if (length < 2U || header[0] != '"' || header[length - 1U] != '"') return 0;
    target = copy_range(header + 1, length - 2U);
    if (!str_eq(path_leaf(target), "reflection.h")) {
        free(target);
        return 0;
    }
    directory = path_parent(ti->path);
    candidate = join_path(directory, target);
    absolute = absolute_path(candidate);
    reflection = reflection_header_path();
    reflection_absolute = absolute_path(reflection);
    match = str_eq(absolute, reflection_absolute);
    if (!match) {
        Buf text;
        buf_init(&text);
        match = !read_file(candidate, &text) ||
                (text.p != NULL &&
                 strstr(text.p, "AUTO-GENERATED by tmplgen (--reflect=project)") != NULL);
        buf_free(&text);
    }
    free(directory);
    free(target);
    free(candidate);
    free(absolute);
    free(reflection);
    free(reflection_absolute);
    return match;
}

/* A template that includes gen/reflection.h uses the reflection API in its
 * bodies: its specializations wait for reflection.h's section of their
 * argument types (see compute_instance_requirement). */
int template_uses_reflection(const TemplateInfo *ti)
{
    size_t i;
    for (i = 0U; i < ti->include_count; ++i) {
        if (template_include_is_reflection(ti, ti->includes[i])) return 1;
    }
    return 0;
}

int template_index_uses_reflection(size_t template_index)
{
    return g_all_template_infos != NULL && template_index < g_all_template_info_count &&
           template_uses_reflection(&g_all_template_infos[template_index]);
}

/* The macro that is defined once the specializations of `instance` have been
 * compiled: the module's include guard for those that need no project type,
 * otherwise the marker of the module's section for its requirement. */
char *instance_section_marker(const GenInstance *instance)
{
    char *guard = generated_module_guard(g_templates[instance->template_index].stem);
    const char *requirement = instance_requirement(instance);
    Buf marker;
    if (requirement[0] == '\0') return guard;
    buf_init(&marker);
    buf_puts(&marker, "TMPLGEN_HAVE_");
    buf_puts(&marker, guard);
    buf_putc(&marker, '_');
    buf_put_requirement_id(&marker, requirement);
    free(guard);
    return marker.p;
}

/* Index of the instance whose generated type is named `name` (for example
 * Vec_int, or the tag of a tagged constructor), or (size_t)-1. */
static size_t instance_for_generated_type(const char *name)
{
    size_t i;
    for (i = 0U; i < g_instance_count; ++i) {
        const GenInstance *instance = &g_instances[i];
        const TemplateInfo *ti;
        size_t j;
        if (instance->template_index >= g_all_template_info_count) continue;
        ti = &g_all_template_infos[instance->template_index];
        for (j = 0U; j < ti->constructor_count; ++j) {
            const TemplateType *constructor = &ti->constructors[j];
            char *generated;
            int match;
            if (constructor->parameter_count == 0U ||
                !instance_matches_parameters(ti, instance,
                    constructor->parameter_names, constructor->parameter_count) ||
                !type_use_matches(constructor->name, instance->name)) continue;
            generated = join_suffix(constructor->name, instance->name);
            match = str_eq(generated, name);
            free(generated);
            if (match) return i;
        }
    }
    return (size_t)-1;
}

static int add_spelling_requirements(const char *spelling,
                                     const GenInstance *instance,
                                     StringSet *guards, unsigned depth)
{
    const char *p = spelling;
    const char *tag = NULL;
    int okay = 1;
    while (*p != '\0') {
        const char *start;
        char *word;
        if (!is_ident_start(*p)) {
            ++p;
            continue;
        }
        start = p;
        while (is_ident_char(*p)) ++p;
        word = copy_range(start, (size_t)(p - start));
        if (str_eq(word, "struct") || str_eq(word, "union") || str_eq(word, "enum")) {
            tag = str_eq(word, "struct") ? "struct " :
                  str_eq(word, "union") ? "union " : "enum ";
        } else if (!is_builtin_type_word(word)) {
            Buf name;
            const TypeOrigin *origin;
            buf_init(&name);
            if (tag != NULL) buf_puts(&name, tag);
            buf_puts(&name, word);
            origin = find_type_origin(name.p);
            if (origin != NULL) {
                if (!add_origin_requirement(origin, name.p, instance, guards)) okay = 0;
            } else {
                size_t generated = instance_for_generated_type(word);
                if (generated != (size_t)-1 &&
                    &g_instances[generated] != instance &&
                    compute_instance_requirement(generated, depth + 1U)) {
                    const char *requirement = g_instance_requirements[generated];
                    while (*requirement != '\0') {
                        const char *comma = strchr(requirement, ',');
                        size_t length = comma != NULL ? (size_t)(comma - requirement) :
                                                        strlen(requirement);
                        char *guard = copy_range(requirement, length);
                        string_set_add(guards, guard);
                        free(guard);
                        requirement += length;
                        if (*requirement == ',') ++requirement;
                    }
                    /* A type from another module also needs that module's
                     * section (or core) to have run, whatever the order the
                     * generated headers are included in. */
                    if (g_instances[generated].template_index != instance->template_index) {
                        char *marker = instance_section_marker(&g_instances[generated]);
                        string_set_add(guards, marker);
                        free(marker);
                    }
                }
            }
            buf_free(&name);
            tag = NULL;
        }
        free(word);
    }
    return okay;
}

int compare_strings(const void *left, const void *right)
{
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

static char *join_requirement(StringSet *guards)
{
    Buf key;
    size_t i;
    if (guards->count > 1U) {
        qsort(guards->items, guards->count, sizeof(*guards->items), compare_strings);
    }
    buf_init(&key);
    for (i = 0U; i < guards->count; ++i) {
        if (i != 0U) buf_putc(&key, ',');
        buf_puts(&key, guards->items[i]);
    }
    return key.p != NULL ? key.p : copy_string("");
}

static int compute_instance_requirement(size_t instance_index, unsigned depth)
{
    const GenInstance *instance = &g_instances[instance_index];
    StringSet guards;
    size_t i;
    int okay = 1;
    if (g_instance_requirements[instance_index] != NULL) return 1;
    if (depth > 64U) return 0;
    memset(&guards, 0, sizeof(guards));
    for (i = 0U; i < instance->argument_count; ++i) {
        if (!add_spelling_requirements(instance->arguments[i].c_type, instance,
                                       &guards, depth)) okay = 0;
    }
    if (template_index_uses_reflection(instance->template_index)) {
        /* The body names reflection.h's API and its typetag_of entries for
         * the argument types: wait for reflection.h's section of each guard
         * (reflection.h emits one for every such guard), or for reflection.h
         * itself when only builtins are involved. */
        size_t count = guards.count;
        if (count == 0U) string_set_add(&guards, "TMPLGEN_REFLECTION_H");
        for (i = 0U; i < count; ++i) {
            Buf have;
            buf_init(&have);
            buf_puts(&have, "TMPLGEN_REFLECT_HAVE_");
            buf_puts(&have, guards.items[i]);
            string_set_add(&guards, have.p);
            buf_free(&have);
        }
    }
    g_instance_requirements[instance_index] = join_requirement(&guards);
    string_set_free(&guards);
    return okay;
}

int compute_instance_requirements(void)
{
    size_t i;
    int okay = 1;
    list_free(g_instance_requirements, g_instance_requirement_count);
    g_instance_requirements = NULL;
    g_instance_requirement_count = g_instance_count;
    if (g_instance_count != 0U) {
        g_instance_requirements = (char **)calloc(g_instance_count,
                                                  sizeof(*g_instance_requirements));
        if (g_instance_requirements == NULL) die("out of memory");
    }
    for (i = 0U; i < g_instance_count; ++i) {
        if (!compute_instance_requirement(i, 0U)) okay = 0;
    }
    return okay;
}

const char *instance_requirement(const GenInstance *instance)
{
    size_t index;
    if (instance == NULL || g_instance_requirements == NULL ||
        instance < g_instances || instance >= g_instances + g_instance_count) {
        return "";
    }
    index = (size_t)(instance - g_instances);
    return g_instance_requirements[index] != NULL ?
        g_instance_requirements[index] : "";
}

/* "A,B" -> "A_AND_B", for building macro names. */
void buf_put_requirement_id(Buf *out, const char *requirement)
{
    for (; *requirement != '\0'; ++requirement) {
        if (*requirement == ',') buf_puts(out, "_AND_");
        else buf_putc(out, *requirement);
    }
}

/* "A,B" -> "defined(A) && defined(B)" */
void buf_put_requirement_condition(Buf *out, const char *requirement)
{
    int first = 1;
    while (*requirement != '\0') {
        const char *comma = strchr(requirement, ',');
        size_t length = comma != NULL ? (size_t)(comma - requirement) :
                                        strlen(requirement);
        if (!first) buf_puts(out, " && ");
        buf_puts(out, "defined(");
        buf_putn(out, requirement, length);
        buf_putc(out, ')');
        first = 0;
        requirement += length;
        if (*requirement == ',') ++requirement;
    }
}

static size_t requirement_guard_count(const char *requirement)
{
    size_t count = 0U;
    if (*requirement == '\0') return 0U;
    for (count = 1U; *requirement != '\0'; ++requirement) {
        if (*requirement == ',') ++count;
    }
    return count;
}

static int compare_requirements(const void *left, const void *right)
{
    const char *a = *(const char *const *)left;
    const char *b = *(const char *const *)right;
    size_t count_a = requirement_guard_count(a);
    size_t count_b = requirement_guard_count(b);
    if (count_a != count_b) return count_a < count_b ? -1 : 1;
    return strcmp(a, b);
}

/* Sorts requirement keys so that a key comes after all of its subsets. */
void sort_requirements(StringSet *requirements)
{
    if (requirements->count > 1U) {
        qsort(requirements->items, requirements->count,
              sizeof(*requirements->items), compare_requirements);
    }
}

/* Intersection of two requirement keys. */
char *requirement_intersection(const char *a, const char *b)
{
    StringSet guards;
    char *result;
    memset(&guards, 0, sizeof(guards));
    while (*a != '\0') {
        const char *comma = strchr(a, ',');
        size_t length = comma != NULL ? (size_t)(comma - a) : strlen(a);
        char *guard = copy_range(a, length);
        const char *p = b;
        while (*p != '\0') {
            const char *other_comma = strchr(p, ',');
            size_t other_length = other_comma != NULL ? (size_t)(other_comma - p) :
                                                        strlen(p);
            if (other_length == length && strncmp(p, guard, length) == 0) {
                string_set_add(&guards, guard);
                break;
            }
            p += other_length;
            if (*p == ',') ++p;
        }
        free(guard);
        a += length;
        if (*a == ',') ++a;
    }
    result = join_requirement(&guards);
    string_set_free(&guards);
    return result;
}

void emit_no_match_declaration(Buf *out, const char *name)
{
    buf_puts(out, "/* No dispatch association yet: either the argument type is wrong,\n"
                  "   or generated headers need regeneration after requesting this type. */\n");
    buf_puts(out, "struct tmplgen_no_association_for_");
    buf_puts(out, name);
    buf_puts(out, "_argument { int unused; };\n");
    buf_puts(out, "extern void ");
    buf_puts(out, name);
    buf_puts(out, "__no_match_for_argument_type(struct tmplgen_no_association_for_");
    buf_puts(out, name);
    buf_puts(out, "_argument, ...);\n");
}

void emit_spaces(Buf *out, unsigned count)
{
    unsigned i;
    for (i = 0U; i < count; ++i) buf_putc(out, ' ');
}

char *template_include_target(const char *header, const char *current_stem)
{
    const char *header_path = header;
    char *current_template;
    char *current_absolute;
    char *last_separator;
    char *directory;
    char *candidate_path;
    char *candidate_absolute;
    size_t i;
    char *result = NULL;
    size_t header_length = strlen(header);
    if (header_length >= 2U &&
        ((header[0] == '"' && header[header_length - 1U] == '"') ||
         (header[0] == '<' && header[header_length - 1U] == '>'))) {
        header_path = copy_range(header + 1U, header_length - 2U);
    } else {
        header_path = copy_string(header);
    }
    if (strlen(header_path) < strlen(GEN_TMPL_SUFFIX) ||
        !str_eq(header_path + strlen(header_path) - strlen(GEN_TMPL_SUFFIX),
                GEN_TMPL_SUFFIX)) {
        free((char *)header_path);
        return NULL;
    }
    {
        Buf current_path;
        buf_init(&current_path);
        build_template_path(&current_path, current_stem);
        current_template = current_path.p;
    }
    current_absolute = absolute_path(current_template);
    last_separator = strrchr(current_template, '/');
#if defined(_WIN32)
    {
        char *backslash = strrchr(current_template, '\\');
        if (backslash != NULL && (last_separator == NULL || backslash > last_separator)) {
            last_separator = backslash;
        }
    }
#endif
    if (last_separator == NULL) directory = copy_string(".");
    else directory = copy_range(current_template,
                                (size_t)(last_separator - current_template));
    candidate_path = join_path(directory, header_path);
    candidate_absolute = absolute_path(candidate_path);
    for (i = 0U; i < gen_template_count(); ++i) {
        Buf template_path;
        char *template_absolute;
        buf_init(&template_path);
        build_template_path(&template_path, g_templates[i].stem);
        template_absolute = absolute_path(template_path.p);
        if (str_eq(candidate_absolute, template_absolute)) {
            result = copy_string(g_templates[i].stem);
            free(template_absolute);
            buf_free(&template_path);
            break;
        }
        free(template_absolute);
        buf_free(&template_path);
    }
    free((char *)header_path);
    free(current_template);
    free(current_absolute);
    free(directory);
    free(candidate_path);
    free(candidate_absolute);
    return result;
}

char *relative_module_include(const char *current_stem,
                                    const char *target_stem)
{
    const char *current_slash = strrchr(current_stem, '/');
    const char *target_slash = strrchr(target_stem, '/');
    size_t current_dir_length = current_slash != NULL ?
        (size_t)(current_slash - current_stem) : 0U;
    size_t target_dir_length = target_slash != NULL ?
        (size_t)(target_slash - target_stem) : 0U;
    size_t common = 0U;
    size_t current_rest;
    size_t target_rest;
    Buf path;
    while (common < current_dir_length && common < target_dir_length &&
           current_stem[common] == target_stem[common]) ++common;
    if (common < current_dir_length && common < target_dir_length) {
        while (common != 0U && current_stem[common - 1U] != '/') --common;
    }
    current_rest = common;
    target_rest = common;
    while (current_rest < current_dir_length && current_stem[current_rest] == '/') {
        ++current_rest;
    }
    while (target_rest < target_dir_length && target_stem[target_rest] == '/') {
        ++target_rest;
    }
    buf_init(&path);
    while (current_rest < current_dir_length) {
        buf_puts(&path, "../");
        while (current_rest < current_dir_length && current_stem[current_rest] != '/') {
            ++current_rest;
        }
        while (current_rest < current_dir_length && current_stem[current_rest] == '/') {
            ++current_rest;
        }
    }
    if (target_rest < target_dir_length) {
        buf_putn(&path, target_stem + target_rest,
                 target_dir_length - target_rest);
        buf_putc(&path, '/');
    }
    buf_puts(&path, target_slash != NULL ? target_slash + 1 : target_stem);
    buf_puts(&path, ".gen.h");
    return path.p;
}

void emit_preamble(Buf *out, const char *guard,
                          const TemplateInfo *infos, size_t count,
                          const char *current_stem, StringSet *includes)
{
    size_t i;
    buf_puts(out, "#ifndef ");
    buf_puts(out, guard);
    buf_puts(out, "\n#define ");
    buf_puts(out, guard);
    buf_putc(out, '\n');
    buf_puts(out, "/* AUTO-GENERATED by tmplgen.c. DO NOT EDIT. */\n");
    buf_puts(out, GEN_PREAMBLE);
    if (out->len != 0U && out->p[out->len - 1U] != '\n') buf_putc(out, '\n');
    collect_preamble_includes(includes, GEN_PREAMBLE);
    for (i = 0U; i < count; ++i) {
        size_t j;
        for (j = 0U; j < infos[i].include_count; ++j) {
            const char *source_stem = current_stem != NULL ? current_stem :
                g_templates[infos[i].template_index].stem;
            char *target_stem;
            char *include_path;
            char *key;
            /* reflection.h includes the generated headers (at its end) to
             * compile the specializations that use it; it is not included
             * back. */
            if (template_include_is_reflection(&infos[i], infos[i].includes[j])) continue;
            target_stem = template_include_target(infos[i].includes[j], source_stem);
            include_path = target_stem != NULL ?
                relative_module_include(source_stem, target_stem) :
                copy_string(infos[i].includes[j]);
            key = include_key(include_path);
            if (!string_set_has(includes, key)) {
                string_set_add(includes, key);
                emit_include(out, include_path);
            }
            free(key);
            free(include_path);
            free(target_stem);
        }
    }
    if (count != 0U) buf_putc(out, '\n');
}

char *generated_module_source_path(const char *stem)
{
    Buf relative;
    char *path;
    buf_init(&relative);
    buf_puts(&relative, stem);
    buf_puts(&relative, ".gen.c");
    path = join_path(g_generated_dir, relative.p);
    buf_free(&relative);
    return path;
}

char *reflection_header_path(void)
{
    return join_path(g_generated_dir, "reflection.h");
}

char *reflection_source_path(void)
{
    return join_path(g_generated_dir, "reflection.gen.c");
}

static void output_registry_add(OutputRegistry *registry, char *path,
                                OutputKind kind, size_t template_index)
{
    if (registry->count == registry->cap) {
        size_t new_cap = registry->cap == 0U ? 8U : registry->cap * 2U;
        OutputEntry *next;
        if (new_cap < registry->cap ||
            new_cap > (size_t)-1 / sizeof(*registry->items)) die("too many outputs");
        next = (OutputEntry *)realloc(registry->items,
                                      new_cap * sizeof(*registry->items));
        if (next == NULL) die("out of memory");
        registry->items = next;
        registry->cap = new_cap;
    }
    registry->items[registry->count].path = path;
    registry->items[registry->count].kind = kind;
    registry->items[registry->count].template_index = template_index;
    ++registry->count;
}

/* Every output, in the order they are generated and recorded in the cache
 * manifest. Requires discovered templates. */
void output_registry_build(OutputRegistry *registry)
{
    size_t i;
    memset(registry, 0, sizeof(*registry));
    for (i = 0U; i < gen_template_count(); ++i) {
        output_registry_add(registry, generated_module_path(g_templates[i].stem),
                            OUTPUT_MODULE, i);
        if (g_compiled_output) {
            output_registry_add(registry,
                                generated_module_source_path(g_templates[i].stem),
                                OUTPUT_MODULE_SOURCE, i);
        }
    }
    output_registry_add(registry, copy_string(g_output_path), OUTPUT_AGGREGATE, 0U);
    if (g_reflect_mode != REFLECT_NONE) {
        output_registry_add(registry, reflection_header_path(), OUTPUT_REFLECTION, 0U);
        if (g_compiled_output) {
            output_registry_add(registry, reflection_source_path(),
                                OUTPUT_REFLECTION_SOURCE, 0U);
        }
    }
}

void output_registry_free(OutputRegistry *registry)
{
    size_t i;
    for (i = 0U; i < registry->count; ++i) free(registry->items[i].path);
    free(registry->items);
    memset(registry, 0, sizeof(*registry));
}

int output_is_header(OutputKind kind)
{
    return kind == OUTPUT_MODULE || kind == OUTPUT_AGGREGATE ||
           kind == OUTPUT_REFLECTION;
}

char *generated_module_guard(const char *stem)
{
    Buf guard;
    size_t i;
    buf_init(&guard);
    buf_puts(&guard, GEN_GUARD);
    buf_putc(&guard, '_');
    for (i = 0U; stem[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)stem[i];
        if (isalnum(c)) buf_putc(&guard, (char)toupper(c));
        else buf_putc(&guard, '_');
    }
    buf_puts(&guard, "_GEN_H");
    return guard.p;
}

unsigned template_type_name_line(const TemplateInfo *ti,
                                        const TemplateType *constructor)
{
    char *fragment;
    size_t relative_line;
    unsigned name_line;
    if (ti->body.p == NULL || constructor->end_offset <= constructor->start_offset) {
        return constructor->start_line;
    }
    fragment = copy_range(ti->body.p + constructor->start_offset,
                          (size_t)(constructor->end_offset - constructor->start_offset));
    relative_line = identifier_line_in_code(fragment, constructor->name);
    free(fragment);
    if (relative_line == 0U) return constructor->start_line;
    name_line = constructor->start_line + (unsigned)relative_line - 1U;
    return name_line;
}
