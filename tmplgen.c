/*
 * tmplgen.c - a small, single-file C99 generic-template generator.
 *
 * Build:   cc -std=c99 -Wall -Wextra -pedantic tmplgen.c -lclang -o tmplgen
 * Run:     ./tmplgen
 * Check:   ./tmplgen --check
 *
 * How it works:
 *   1. Read the explicitly configured template files.
 *   2. Read each file's // @template type-variable declaration.
 *   3. Analyze dependent typedefs and function signatures with libclang.
 *   4. Parse project sources to infer concrete type tuples from inputs.
 *   5. Rewrite and emit dependent declaration fragments.
 *   6. Append compiler-specific dispatch macros for observed input types.
 *   7. Strip template-only LSP fallback blocks while keeping their blank lines.
 *   8. Write per-template headers under gen/ plus an aggregate header, skipping unchanged files.
 *   9. Use --check in CI and --watch while editing templates.
 */

/* ===================== CONFIGURATION ===================== */

/* Dispatch backends for the generated macros (pick one for GEN_DISPATCH). */
#define GEN_DISPATCH_NONE    0  /* only emit suffixed functions: max_int, max_double */
#define GEN_DISPATCH_GENERIC 1  /* emit _Generic macros (C11 and newer)              */
#define GEN_DISPATCH_GNU     2  /* emit __builtin_choose_expr/__typeof__ chains      */
#define GEN_DISPATCH_AUTO    3  /* use _Generic in C11+, GNU builtins in GNU C       */

#define GEN_TMPL_DIR    "tmpl"       /* directory holding the templates               */
#define GEN_TMPL_SUFFIX ".tmpl.h"    /* appended to each template stem                */
#define GEN_SCAN_ROOT  "."           /* recursively scan this tree for C source usages */
#ifndef GEN_OUTPUT
#define GEN_OUTPUT      "gen/all.gen.h" /* aggregate header, relative to cwd           */
#endif
#define GEN_GUARD       "TMPL_H"     /* include guard for aggregate gen/all.gen.h       */
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

/* Template table. Each row enables one template file.
   stem : file is GEN_TMPL_DIR "/" stem GEN_TMPL_SUFFIX, e.g. tmpl/math.tmpl.h.
   Template parameters and dependent declarations are discovered from each file. */
typedef struct { const char *stem; } GenTemplate;
static const GenTemplate GEN_TEMPLATES[] = {
    { "math" },
    { "vec"  },
    { "result" },
    { "triple" },
    { 0 }
};

/* The final zero row is a sentinel, so an empty template table is valid C99. */
/* =================== END OF CONFIGURATION ================== */

#if GEN_ENABLE_WATCH && !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 199309L
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
#endif

typedef struct { char *p; size_t len, cap, newlines; } Buf;
typedef struct { char *path; Buf contents; } GeneratedFile;
typedef struct { GeneratedFile *items; size_t count, cap; } GeneratedFileList;
typedef struct { char *c_type; char *canonical; } TypeArgument;
typedef struct {
    char *name;
    char *key;
    TypeArgument *arguments;
    size_t *parameter_indices;
    size_t argument_count;
    size_t template_index;
} GenInstance;
typedef struct { char *functor; char *type_name; } TypeUse;
typedef struct {
    char *name;
    char *type_name;
    int dispatch;
    unsigned *dispatch_indices;
    char **dispatch_types;
    char **dispatch_canonicals;
    size_t dispatch_count;
} GenericUse;
typedef struct { char *name; char *canonical; } TypeAlias;
typedef struct {
    char *name;
    char *path;
    unsigned offset;
    size_t line;
    unsigned argument_index;
    char *argument_path;
    unsigned argument_start;
    unsigned argument_end;
    char *type_spelling;
    char *canonical_spelling;
} ClangUse;
typedef struct { ClangUse *items; size_t count, cap; } ClangUseList;
typedef struct { char **items; size_t count, cap; } StringSet;

typedef struct { char *text; size_t line; int kind; } CToken;
typedef struct { CToken *items; size_t count, cap; } TokenList;

static GenInstance *g_instances;
static size_t g_instance_count, g_instance_cap;
static TypeUse *g_type_uses;
static size_t g_type_use_count, g_type_use_cap;
static GenericUse *g_generic_uses;
static size_t g_generic_use_count, g_generic_use_cap;
static TypeAlias *g_aliases;
static size_t g_alias_count, g_alias_cap;
static char *g_source_root;
static char *g_template_dir;
static char *g_output_path;
static char *g_generated_dir;
static int g_dispatch_backend;
static int g_emit_line;
static char **g_clang_args;
static size_t g_clang_arg_count, g_clang_arg_cap;
static size_t g_error_count;

static void die(const char *fmt, ...);
static void report_error(const char *path, size_t line, const char *fmt, ...);
static int str_eq(const char *a, const char *b);
static int is_ident_char(char c);
static char *copy_string(const char *s);
static char *normalize_spaces(const char *s);
static size_t identifier_line_in_code(const char *src, const char *name);
static char *join_suffix(const char *base, const char *suffix);
static void buf_init(Buf *b);
static void buf_putc(Buf *b, char c);
static void buf_puts(Buf *b, const char *s);
static void buf_put_uint(Buf *b, unsigned long value);
static void buf_free(Buf *b);
static void emit_spaces(Buf *out, unsigned count);
static void emit_line_directive(Buf *out, unsigned long line, const char *path);
static char *copy_cx_string(CXString value);

static const char *find_alias_canonical(const char *name)
{
    size_t i;
    for (i = 0U; i < g_alias_count; ++i) {
        if (str_eq(g_aliases[i].name, name)) return g_aliases[i].canonical;
    }
    return NULL;
}

static void add_alias(const char *name, const char *canonical)
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

static char *instance_suffix(const char *const *spellings, size_t count)
{
    Buf suffix;
    size_t i;
    buf_init(&suffix);
    for (i = 0U; i < count; ++i) {
        char *part = type_suffix(spellings[i]);
        if (i != 0U) buf_putc(&suffix, '_');
        buf_puts(&suffix, part);
        free(part);
    }
    return suffix.p;
}

static GenInstance *find_instance(size_t template_index, const char *name,
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

static GenInstance *add_instance(size_t template_index,
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

static int add_type_use(const char *functor, const char *type_name)
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

static int add_generic_use(const char *name, const char *type_name, int dispatch,
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

static int generic_use_matches(const char *name, const char *type_name,
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

static int type_use_matches(const char *functor, const char *type_name)
{
    size_t i;
    for (i = 0U; i < g_type_use_count; ++i) {
        if (str_eq(g_type_uses[i].functor, functor) &&
            str_eq(g_type_uses[i].type_name, type_name)) return 1;
    }
    return 0;
}

static size_t gen_template_count(void)
{
    return sizeof(GEN_TEMPLATES) / sizeof(GEN_TEMPLATES[0]) - 1U;
}

/* ====================== DYNAMIC BUFFER ===================== */

enum { C_TOKEN_IDENT = 1, C_TOKEN_NUMBER, C_TOKEN_STRING, C_TOKEN_CHAR, C_TOKEN_PUNCT };

typedef struct {
    char *path;
    Buf text;
    CToken *tokens;
    size_t token_count, token_cap;
} SourceFile;

typedef struct {
    SourceFile *items;
    size_t count, cap;
} SourceList;

static void source_file_free(SourceFile *file);

static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("tmplgen: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

static void buf_init(Buf *b)
{
    b->p = NULL;
    b->len = 0U;
    b->cap = 0U;
    b->newlines = 0U;
}

static void buf_reserve(Buf *b, size_t extra)
{
    size_t need;
    size_t cap;
    char *next;

    if (extra > (size_t)-1 - b->len - 1U) die("buffer is too large");
    need = b->len + extra + 1U;
    if (need <= b->cap) return;
    cap = b->cap == 0U ? 256U : b->cap;
    while (cap < need) {
        if (cap > (size_t)-1 / 2U) {
            cap = need;
            break;
        }
        cap *= 2U;
    }
    next = (char *)realloc(b->p, cap);
    if (next == NULL) die("out of memory");
    b->p = next;
    b->cap = cap;
}

static void buf_putn(Buf *b, const char *s, size_t n)
{
    size_t i;
    buf_reserve(b, n);
    if (n != 0U) memcpy(b->p + b->len, s, n);
    for (i = 0U; i < n; ++i) {
        if (s[i] == '\n') ++b->newlines;
    }
    b->len += n;
    b->p[b->len] = '\0';
}

static void buf_puts(Buf *b, const char *s)
{
    buf_putn(b, s, strlen(s));
}

static void buf_putc(Buf *b, char c)
{
    buf_putn(b, &c, 1U);
}

static void buf_put_uint(Buf *b, unsigned long value)
{
    char digits[sizeof(unsigned long) * CHAR_BIT + 1U];
    size_t used = 0U;
    do {
        digits[used++] = (char)('0' + (value % 10UL));
        value /= 10UL;
    } while (value != 0UL);
    while (used > 0U) buf_putc(b, digits[--used]);
}

static void buf_free(Buf *b)
{
    free(b->p);
    buf_init(b);
}

/* =========================== HELPERS ======================= */

static int g_verbose;

static void report_error(const char *path, size_t line, const char *fmt, ...)
{
    va_list ap;
    ++g_error_count;
    fprintf(stderr, "tmplgen: %s", path != NULL ? path : "");
    if (line != 0U) fprintf(stderr, ":%lu", (unsigned long)line);
    fputs(path != NULL ? ": error: " : "error: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static void report_warning(const char *path, size_t line, const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "tmplgen: %s", path != NULL ? path : "");
    if (line != 0U) fprintf(stderr, ":%lu", (unsigned long)line);
    fputs(path != NULL ? ": warning: " : "warning: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static int str_eq(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

static int is_ident_start(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int is_ident_char(char c)
{
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static int valid_identifier(const char *s)
{
    size_t i;
    if (s == NULL || !is_ident_start(s[0])) return 0;
    for (i = 1U; s[i] != '\0'; ++i) {
        if (!is_ident_char(s[i])) return 0;
    }
    return 1;
}

static char *copy_range(const char *s, size_t n)
{
    char *p = (char *)malloc(n + 1U);
    if (p == NULL) die("out of memory");
    if (n != 0U) memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static char *copy_string(const char *s)
{
    return copy_range(s, strlen(s));
}

static int read_file(const char *path, Buf *out)
{
    FILE *f = fopen(path, "rb");
    char chunk[8192];
    size_t n;
    if (f == NULL) {
        report_error(path, 0U, "cannot open file: %s", strerror(errno));
        return 0;
    }
    while ((n = fread(chunk, 1U, sizeof(chunk), f)) != 0U) {
        buf_putn(out, chunk, n);
    }
    if (ferror(f)) {
        report_error(path, 0U, "cannot read file: %s", strerror(errno));
        (void)fclose(f);
        return 0;
    }
    if (fclose(f) != 0) {
        report_error(path, 0U, "cannot close file: %s", strerror(errno));
        return 0;
    }
    if (out->len != 0U && memchr(out->p, '\0', out->len) != NULL) {
        report_error(path, 0U, "template contains a NUL byte");
        return 0;
    }
    if (g_verbose) fprintf(stderr, "tmplgen: read %s\n", path);
    return 1;
}

static char *join_path(const char *directory, const char *name)
{
    Buf path;
    size_t length = strlen(directory);
    buf_init(&path);
    buf_puts(&path, directory);
    if (length != 0U && directory[length - 1U] != '/' && directory[length - 1U] != '\\') {
#if defined(_WIN32)
        buf_putc(&path, '\\');
#else
        buf_putc(&path, '/');
#endif
    }
    buf_puts(&path, name);
    return path.p;
}

static char *absolute_path(const char *path)
{
#if defined(_WIN32)
    DWORD needed = GetFullPathNameA(path, 0U, NULL, NULL);
    char *absolute;
    if (needed == 0U) die("cannot resolve path '%s'", path);
    absolute = (char *)malloc((size_t)needed + 1U);
    if (absolute == NULL) die("out of memory");
    if (GetFullPathNameA(path, needed + 1U, absolute, NULL) == 0U) {
        free(absolute);
        die("cannot resolve path '%s'", path);
    }
    return absolute;
#else
    char *cwd;
    size_t cap = 256U;
    Buf raw;
    char *normalized;
    size_t *component_starts;
    size_t raw_length;
    size_t output_length = 1U;
    size_t component_count = 0U;
    size_t offset = 0U;
    buf_init(&raw);
    for (;;) {
        cwd = (char *)malloc(cap);
        if (cwd == NULL) die("out of memory");
        if (getcwd(cwd, cap) != NULL) break;
        free(cwd);
        if (errno != ERANGE || cap > (size_t)-1 / 2U) {
            die("cannot get current directory: %s", strerror(errno));
        }
        cap *= 2U;
    }
    if (path[0] == '/') {
        buf_puts(&raw, path);
    } else {
        buf_puts(&raw, cwd);
        if (raw.len == 0U || raw.p[raw.len - 1U] != '/') buf_putc(&raw, '/');
        buf_puts(&raw, path);
    }
    free(cwd);
    raw_length = raw.len;
    if (raw_length > (size_t)-1 / sizeof(*component_starts) - 1U) {
        buf_free(&raw);
        die("path is too long");
    }
    normalized = (char *)malloc(raw_length + 2U);
    component_starts = (size_t *)malloc((raw_length + 1U) * sizeof(*component_starts));
    if (normalized == NULL || component_starts == NULL) die("out of memory");
    normalized[0] = '/';
    while (offset < raw_length) {
        size_t start;
        size_t length;
        while (offset < raw_length && raw.p[offset] == '/') ++offset;
        if (offset == raw_length) break;
        start = offset;
        while (offset < raw_length && raw.p[offset] != '/') ++offset;
        length = offset - start;
        if (length == 1U && raw.p[start] == '.') continue;
        if (length == 2U && raw.p[start] == '.' && raw.p[start + 1U] == '.') {
            if (component_count != 0U) {
                output_length = component_starts[--component_count];
            }
            continue;
        }
        component_starts[component_count++] = output_length;
        if (output_length > 1U) normalized[output_length++] = '/';
        memcpy(normalized + output_length, raw.p + start, length);
        output_length += length;
    }
    normalized[output_length] = '\0';
    free(component_starts);
    buf_free(&raw);
    return normalized;
#endif
}

static int source_file_extension(const char *name)
{
    size_t length = strlen(name);
    return (length >= 2U && strcmp(name + length - 2U, ".c") == 0) ||
           (length >= 2U && strcmp(name + length - 2U, ".h") == 0);
}

static const char *path_leaf(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
    return slash != NULL ? slash + 1 : path;
}

static char *path_parent(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    size_t length;
    if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
    if (slash == NULL) return copy_string(".");
    if (slash == path) return copy_range(path, 1U);
    length = (size_t)(slash - path);
    return copy_range(path, length);
}

static int is_generated_output_file(const char *path)
{
    char *absolute = absolute_path(path);
    char *aggregate = absolute_path(g_output_path);
    size_t generated_dir_length = strlen(g_generated_dir);
    size_t absolute_length = strlen(absolute);
    int module_file = absolute_length >= 7U &&
                      strcmp(absolute + absolute_length - 7U, ".gen.h") == 0 &&
                      strncmp(absolute, g_generated_dir, generated_dir_length) == 0 &&
                      ((generated_dir_length == 1U && g_generated_dir[0] == '/') ||
                       absolute[generated_dir_length] == '/' ||
                       absolute[generated_dir_length] == '\\');
    int generated = str_eq(absolute, aggregate);
    free(aggregate);
    generated = generated || module_file;
    free(absolute);
    return generated;
}

static int skip_source_directory(const char *name, const char *path)
{
    char *absolute;
    int skip;
    if (name[0] == '.' || str_eq(name, path_leaf(g_template_dir)) ||
        str_eq(name, "build") || str_eq(name, "dist")) return 1;
    if (g_generated_dir == NULL) return 0;
    absolute = absolute_path(path);
    skip = str_eq(absolute, g_generated_dir);
    free(absolute);
    return skip;
}

static int source_list_add_file(SourceList *files, const char *path)
{
    SourceFile *next;
    size_t new_cap;
    SourceFile *file;
    if (files->count == files->cap) {
        new_cap = files->cap == 0U ? 16U : files->cap * 2U;
        if (new_cap < files->cap || new_cap > (size_t)-1 / sizeof(*files->items)) {
            die("too many source files");
        }
        next = (SourceFile *)realloc(files->items, new_cap * sizeof(*files->items));
        if (next == NULL) die("out of memory");
        files->items = next;
        files->cap = new_cap;
    }
    file = &files->items[files->count];
    memset(file, 0, sizeof(*file));
    file->path = absolute_path(path);
    buf_init(&file->text);
    if (!read_file(file->path, &file->text)) {
        source_file_free(file);
        return 0;
    }
    ++files->count;
    return 1;
}

static int scan_source_directory(SourceList *files, const char *directory)
{
#if defined(_WIN32)
    char *pattern = join_path(directory, "*");
    WIN32_FIND_DATAA entry;
    HANDLE handle = FindFirstFileA(pattern, &entry);
    free(pattern);
    if (handle == INVALID_HANDLE_VALUE) {
        report_error(directory, 0U, "cannot scan directory");
        return 0;
    }
    do {
        char *child;
        if (str_eq(entry.cFileName, ".") || str_eq(entry.cFileName, "..")) continue;
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U) {
            child = join_path(directory, entry.cFileName);
            if (skip_source_directory(entry.cFileName, child) ||
                (entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
                free(child);
                continue;
            }
            if (!scan_source_directory(files, child)) {
                free(child);
                (void)FindClose(handle);
                return 0;
            }
            free(child);
        } else if (source_file_extension(entry.cFileName)) {
            child = join_path(directory, entry.cFileName);
            if (is_generated_output_file(child)) {
                free(child);
                continue;
            }
            if (!source_list_add_file(files, child)) {
                free(child);
                (void)FindClose(handle);
                return 0;
            }
            free(child);
        }
    } while (FindNextFileA(handle, &entry) != 0);
    (void)FindClose(handle);
    return 1;
#else
    DIR *dir = opendir(directory);
    struct dirent *entry;
    if (dir == NULL) {
        report_error(directory, 0U, "cannot scan directory: %s", strerror(errno));
        return 0;
    }
    while ((entry = readdir(dir)) != NULL) {
        char *child;
        struct stat info;
        if (str_eq(entry->d_name, ".") || str_eq(entry->d_name, "..")) continue;
        child = join_path(directory, entry->d_name);
        if (stat(child, &info) != 0) {
            report_error(child, 0U, "cannot inspect source path: %s", strerror(errno));
            free(child);
            (void)closedir(dir);
            return 0;
        }
        if (S_ISDIR(info.st_mode)) {
            if (!skip_source_directory(entry->d_name, child) &&
                !scan_source_directory(files, child)) {
                free(child);
                (void)closedir(dir);
                return 0;
            }
        } else if (S_ISREG(info.st_mode) && source_file_extension(entry->d_name)) {
            if (is_generated_output_file(child)) {
                free(child);
                continue;
            }
            if (!source_list_add_file(files, child)) {
                free(child);
                (void)closedir(dir);
                return 0;
            }
        }
        free(child);
    }
    if (closedir(dir) != 0) {
        report_error(directory, 0U, "cannot close directory: %s", strerror(errno));
        return 0;
    }
    return 1;
#endif
}

static void token_list_add(SourceFile *file, const char *start, size_t length,
                           size_t line, int kind)
{
    CToken *next;
    size_t new_cap;
    if (file->token_count == file->token_cap) {
        new_cap = file->token_cap == 0U ? 128U : file->token_cap * 2U;
        if (new_cap < file->token_cap || new_cap > (size_t)-1 / sizeof(*file->tokens)) {
            die("source contains too many tokens");
        }
        next = (CToken *)realloc(file->tokens, new_cap * sizeof(*file->tokens));
        if (next == NULL) die("out of memory");
        file->tokens = next;
        file->token_cap = new_cap;
    }
    file->tokens[file->token_count].text = copy_range(start, length);
    file->tokens[file->token_count].line = line;
    file->tokens[file->token_count].kind = kind;
    ++file->token_count;
}

static int is_number_char(char c)
{
    return is_ident_char(c) || c == '.';
}

static void tokenize_source(SourceFile *file)
{
    size_t i = 0U;
    size_t line = 1U;
    int line_prefix = 1;
    const char *src = file->text.p;
    while (src[i] != '\0') {
        if (isspace((unsigned char)src[i])) {
            if (src[i] == '\n') {
                ++line;
                line_prefix = 1;
            }
            ++i;
        } else if (src[i] == '/' && src[i + 1U] == '/') {
            while (src[i] != '\0' && src[i] != '\n') ++i;
        } else if (src[i] == '/' && src[i + 1U] == '*') {
            i += 2U;
            while (src[i] != '\0' && !(src[i] == '*' && src[i + 1U] == '/')) {
                if (src[i] == '\n') {
                    ++line;
                    line_prefix = 1;
                }
                ++i;
            }
            if (src[i] != '\0') i += 2U;
        } else if (line_prefix && src[i] == '#') {
            while (src[i] != '\0') {
                if (src[i] == '\n') {
                    size_t back = i;
                    while (back > 0U && (src[back - 1U] == ' ' || src[back - 1U] == '\t' ||
                                         src[back - 1U] == '\r')) --back;
                    ++line;
                    ++i;
                    if (back == 0U || src[back - 1U] != '\\') break;
                } else {
                    ++i;
                }
            }
            line_prefix = 1;
        } else if (is_ident_start(src[i])) {
            size_t start = i++;
            while (is_ident_char(src[i])) ++i;
            token_list_add(file, src + start, i - start, line, C_TOKEN_IDENT);
            line_prefix = 0;
        } else if ((src[i] >= '0' && src[i] <= '9') ||
                   (src[i] == '.' && src[i + 1U] >= '0' && src[i + 1U] <= '9')) {
            size_t start = i++;
            while (is_number_char(src[i])) ++i;
            token_list_add(file, src + start, i - start, line, C_TOKEN_NUMBER);
            line_prefix = 0;
        } else if (src[i] == '"' || src[i] == '\'') {
            char quote = src[i];
            int kind = quote == '"' ? C_TOKEN_STRING : C_TOKEN_CHAR;
            size_t start = i++;
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
            token_list_add(file, src + start, i - start, line, kind);
            line_prefix = 0;
        } else {
            size_t start = i++;
            token_list_add(file, src + start, 1U, line, C_TOKEN_PUNCT);
            line_prefix = 0;
        }
    }
}

static void source_file_free(SourceFile *file)
{
    size_t i;
    free(file->path);
    buf_free(&file->text);
    for (i = 0U; i < file->token_count; ++i) free(file->tokens[i].text);
    free(file->tokens);
    memset(file, 0, sizeof(*file));
}

static void source_list_free(SourceList *files)
{
    size_t i;
    for (i = 0U; i < files->count; ++i) source_file_free(&files->items[i]);
    free(files->items);
    memset(files, 0, sizeof(*files));
}

static void generated_file_list_add(GeneratedFileList *files, const char *path,
                                    Buf *contents)
{
    GeneratedFile *next;
    size_t new_cap;
    if (files->count == files->cap) {
        new_cap = files->cap == 0U ? 8U : files->cap * 2U;
        if (new_cap < files->cap ||
            new_cap > (size_t)-1 / sizeof(*files->items)) die("too many generated files");
        next = (GeneratedFile *)realloc(files->items, new_cap * sizeof(*files->items));
        if (next == NULL) die("out of memory");
        files->items = next;
        files->cap = new_cap;
    }
    files->items[files->count].path = copy_string(path);
    files->items[files->count].contents = *contents;
    ++files->count;
    buf_init(contents);
}

static void generated_file_list_free(GeneratedFileList *files)
{
    size_t i;
    for (i = 0U; i < files->count; ++i) {
        free(files->items[i].path);
        buf_free(&files->items[i].contents);
    }
    free(files->items);
    memset(files, 0, sizeof(*files));
}

static int token_is(const SourceFile *file, size_t index, const char *text)
{
    return index < file->token_count && str_eq(file->tokens[index].text, text);
}

static int file_matches(const char *path, const Buf *expected, int *exists)
{
    FILE *f = fopen(path, "rb");
    char chunk[8192];
    size_t n;
    size_t offset = 0U;
    int same = 1;

    *exists = 0;
    if (f == NULL) {
        if (errno == ENOENT) return 0;
        report_error(path, 0U, "cannot read output: %s", strerror(errno));
        return -1;
    }
    *exists = 1;
    while ((n = fread(chunk, 1U, sizeof(chunk), f)) != 0U) {
        if (offset > expected->len || n > expected->len - offset ||
            memcmp(chunk, expected->p + offset, n) != 0) same = 0;
        offset += n;
    }
    if (ferror(f)) {
        report_error(path, 0U, "cannot read output: %s", strerror(errno));
        (void)fclose(f);
        return -1;
    }
    if (fclose(f) != 0) {
        report_error(path, 0U, "cannot close output: %s", strerror(errno));
        return -1;
    }
    if (offset != expected->len) same = 0;
    return same;
}

static int create_directory_if_missing(const char *path)
{
#if defined(_WIN32)
    if (_mkdir(path) == 0 || errno == EEXIST) return 1;
#else
    if (mkdir(path, 0777) == 0 || errno == EEXIST) return 1;
#endif
    report_error(path, 0U, "cannot create output directory: %s", strerror(errno));
    return 0;
}

static int ensure_parent_directories(const char *path)
{
    char *parent = path_parent(path);
    char *prefix = copy_string(parent);
    size_t i;
    int okay = 1;
    free(parent);
    for (i = 0U; prefix[i] != '\0'; ++i) {
        if (prefix[i] == '/' || prefix[i] == '\\') {
            char saved = prefix[i];
            prefix[i] = '\0';
            if (prefix[0] != '\0' &&
                !(i == 2U && prefix[1] == ':') &&
                !create_directory_if_missing(prefix)) okay = 0;
            prefix[i] = saved;
        }
    }
    if (okay && prefix[0] != '\0' && !str_eq(prefix, ".") &&
        !create_directory_if_missing(prefix)) okay = 0;
    free(prefix);
    return okay;
}

static int write_file_if_changed(const char *path, const Buf *contents, int *changed)
{
    int exists;
    int same = file_matches(path, contents, &exists);
    FILE *f;
    if (same < 0) return 0;
    if (same != 0) {
        *changed = 0;
        return 1;
    }
    if (!ensure_parent_directories(path)) return 0;
    f = fopen(path, "wb");
    if (f == NULL) {
        report_error(path, 0U, "cannot write output: %s", strerror(errno));
        return 0;
    }
    if (contents->len != 0U &&
        fwrite(contents->p, 1U, contents->len, f) != contents->len) {
        report_error(path, 0U, "cannot write output: %s", strerror(errno));
        (void)fclose(f);
        return 0;
    }
    if (fclose(f) != 0) {
        report_error(path, 0U, "cannot close output: %s", strerror(errno));
        return 0;
    }
    *changed = 1;
    if (g_verbose) fprintf(stderr, "tmplgen: wrote %s\n", path);
    (void)exists;
    return 1;
}

static char *normalize_spaces(const char *s)
{
    Buf out;
    size_t i = 0U;
    int pending_space = 0;
    buf_init(&out);
    while (s[i] != '\0') {
        if (isspace((unsigned char)s[i])) {
            pending_space = out.len != 0U;
            ++i;
        } else {
            if (pending_space) buf_putc(&out, ' ');
            pending_space = 0;
            buf_putc(&out, s[i++]);
        }
    }
    if (out.p == NULL) return copy_string("");
    return out.p;
}

static const char *next_word(const char **p)
{
    const char *start;
    while (**p != '\0' && isspace((unsigned char)**p)) ++*p;
    if (**p == '\0') return NULL;
    start = *p;
    while (**p != '\0' && !isspace((unsigned char)**p)) ++*p;
    return start;
}

static char *word_copy(const char *start, const char *end)
{
    if (start == NULL || end == NULL) return NULL;
    return copy_range(start, (size_t)(end - start));
}

static int list_contains(char **items, size_t count, const char *s)
{
    size_t i;
    for (i = 0U; i < count; ++i) {
        if (str_eq(items[i], s)) return 1;
    }
    return 0;
}

static void list_add(char ***items, size_t *count, size_t *cap, const char *s)
{
    char **next;
    size_t new_cap;
    if (*count == *cap) {
        new_cap = *cap == 0U ? 4U : *cap * 2U;
        if (new_cap < *cap || new_cap > (size_t)-1 / sizeof(**items)) {
            die("too many items");
        }
        next = (char **)realloc(*items, new_cap * sizeof(**items));
        if (next == NULL) die("out of memory");
        *items = next;
        *cap = new_cap;
    }
    (*items)[*count] = copy_string(s);
    ++*count;
}

static void list_free(char **items, size_t count)
{
    size_t i;
    for (i = 0U; i < count; ++i) free(items[i]);
    free(items);
}

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
} TemplateArgument;

typedef struct {
    char **parameter_names;
    size_t parameter_count, parameter_cap;
    size_t line;
    unsigned offset;
    int used;
} TemplateAnnotation;

typedef struct {
    char *name;
    char **declared_parameters;
    size_t declared_parameter_count, declared_parameter_cap;
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

static const TemplateType *find_template_type(const TemplateInfo *ti,
                                              const char *name);
static const TemplateFunction *find_template_function(const TemplateInfo *ti,
                                                       const char *name);
static void emit_preamble(Buf *out, const char *guard,
                          const TemplateInfo *infos, size_t count,
                          StringSet *includes);
static void emit_type_functors(Buf *out, const TemplateInfo *ti);

static void template_info_init(TemplateInfo *ti)
{
    memset(ti, 0, sizeof(*ti));
    buf_init(&ti->body);
}

static void template_application_free(TemplateApplication *application)
{
    free(application->constructor);
    list_free(application->arguments, application->argument_count);
    memset(application, 0, sizeof(*application));
}

static void template_info_free(TemplateInfo *ti)
{
    size_t i;
    list_free(ti->parameter_names, ti->parameter_count);
    for (i = 0U; i < ti->function_count; ++i) {
        size_t j;
        free(ti->functions[i].name);
        list_free(ti->functions[i].declared_parameters,
                  ti->functions[i].declared_parameter_count);
        for (j = 0U; j < ti->functions[i].parameter_count; ++j) {
            free(ti->functions[i].parameters[j].type);
            free(ti->functions[i].parameters[j].name);
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
        free(ti->constructors[i].name);
        list_free(ti->constructors[i].declared_parameters,
                  ti->constructors[i].declared_parameter_count);
        list_free(ti->constructors[i].parameter_names,
                  ti->constructors[i].parameter_count);
    }
    free(ti->constructors);
    for (i = 0U; i < ti->annotation_count; ++i) {
        list_free(ti->annotations[i].parameter_names,
                  ti->annotations[i].parameter_count);
    }
    free(ti->annotations);
    list_free(ti->includes, ti->include_count);
    free(ti->path);
    buf_free(&ti->body);
    template_info_init(ti);
}

static int template_parameter_index(const TemplateInfo *ti, const char *name)
{
    size_t i;
    for (i = 0U; i < ti->parameter_count; ++i) {
        if (str_eq(ti->parameter_names[i], name)) return (int)i;
    }
    return -1;
}

static int template_name_in_path(const char *path, const char *expected)
{
    char *absolute = absolute_path(path);
    int matches = str_eq(absolute, expected);
    free(absolute);
    return matches;
}

static void add_template_type(TemplateInfo *ti, const char *name,
                              char **declared_parameters, size_t declared_count,
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
    for (i = 0U; i < parameter_count; ++i) {
        list_add(&ti->constructors[ti->constructor_count].parameter_names,
                 &ti->constructors[ti->constructor_count].parameter_count,
                 &ti->constructors[ti->constructor_count].parameter_cap,
                 parameters[i]);
    }
    ++ti->constructor_count;
}

static void add_template_function(TemplateInfo *ti, TemplateFunction *function)
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

static void collect_cursor_template_parameters(CXTranslationUnit tu, CXCursor cursor,
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

static void propagate_function_dependencies(TemplateInfo *ti)
{
    int changed = 1;
    while (changed) {
        size_t i;
        changed = 0;
        for (i = 0U; i < ti->function_count; ++i) {
            TemplateFunction *function = &ti->functions[i];
            char *body;
            size_t j;
            if (function->end_offset > ti->body.len ||
                function->start_offset >= function->end_offset) continue;
            body = copy_range(ti->body.p + function->start_offset,
                              (size_t)(function->end_offset - function->start_offset));
            for (j = 0U; j < ti->function_count; ++j) {
                size_t k;
                const TemplateFunction *callee = &ti->functions[j];
                if (i == j || identifier_line_in_code(body, callee->name) == 0U) continue;
                for (k = 0U; k < callee->type_parameter_count; ++k) {
                    const char *parameter = callee->type_parameters[k];
                    if (!list_contains(function->type_parameters,
                                       function->type_parameter_count, parameter)) {
                        list_add(&function->type_parameters,
                                 &function->type_parameter_count,
                                 &function->type_parameter_cap, parameter);
                        changed = 1;
                    }
                }
            }
            free(body);
        }
    }
    {
        size_t i;
        for (i = 0U; i < ti->function_count; ++i) {
            TemplateFunction *function = &ti->functions[i];
            char **ordered = NULL;
            size_t ordered_count = 0U, ordered_cap = 0U;
            size_t j;
            for (j = 0U; j < ti->parameter_count; ++j) {
                if (list_contains(function->type_parameters,
                                  function->type_parameter_count,
                                  ti->parameter_names[j])) {
                    list_add(&ordered, &ordered_count, &ordered_cap,
                             ti->parameter_names[j]);
                }
            }
            list_free(function->type_parameters, function->type_parameter_count);
            function->type_parameters = ordered;
            function->type_parameter_count = ordered_count;
            function->type_parameter_cap = ordered_cap;
        }
    }
}

typedef struct {
    TemplateInfo *info;
    CXTranslationUnit tu;
    const char *absolute_path;
    unsigned offset_bias;
    int failed;
} TemplateAnalysis;

static int parse_text_type_arguments(const char *source, size_t open,
                                     char ***arguments, size_t *argument_count,
                                     size_t *end_out);

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
            free(name);
            continue;
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
    clang_getExpansionLocation(location, &file, &line, &column, &offset);
    if (file == NULL) return CXChildVisit_Recurse;
    path = copy_cx_string(clang_getFileName(file));
    if (!template_name_in_path(path, analysis->absolute_path)) {
        free(path);
        return CXChildVisit_Recurse;
    }
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
        annotation = take_template_annotation(analysis, function.start_offset);
        function.has_template_annotation = annotation != NULL;
        if (annotation != NULL) {
            copy_declared_parameters(&function.declared_parameters,
                                     &function.declared_parameter_count,
                                     &function.declared_parameter_cap, annotation);
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

static int analyze_template(TemplateInfo *ti)
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
    struct CXUnsavedFile unsaved;
    size_t include_index;
    size_t parameter_index;
    buf_init(&contents);
    buf_puts(&contents, GEN_PREAMBLE);
    if (contents.len != 0U && contents.p[contents.len - 1U] != '\n') {
        buf_putc(&contents, '\n');
    }
    for (include_index = 0U; include_index < ti->include_count; ++include_index) {
        buf_puts(&contents, "#include ");
        buf_puts(&contents, ti->includes[include_index]);
        buf_putc(&contents, '\n');
    }
    for (parameter_index = 0U; parameter_index < ti->parameter_count; ++parameter_index) {
        buf_puts(&contents, "typedef int ");
        buf_puts(&contents, ti->parameter_names[parameter_index]);
        buf_puts(&contents, ";\n");
    }
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
    unsaved.Filename = path;
    unsaved.Contents = contents.p != NULL ? contents.p : "";
    unsaved.Length = (unsigned long)contents.len;
    result = clang_parseTranslationUnit2(index, path, arguments, argument_count,
                                         &unsaved, 1U,
                                         CXTranslationUnit_DetailedPreprocessingRecord |
                                         CXTranslationUnit_KeepGoing, &tu);
    free(arguments);
    if (result != CXError_Success || tu == NULL) {
        report_error(ti->path, 0U, "libclang could not parse template declarations (%d)",
                     (int)result);
        buf_free(&contents);
        free(path);
        clang_disposeIndex(index);
        return 0;
    }
    analysis.info = ti;
    analysis.tu = tu;
    analysis.absolute_path = path;
    analysis.failed = 0;
    clang_visitChildren(clang_getTranslationUnitCursor(tu),
                        collect_template_declarations, &analysis);
    propagate_function_dependencies(ti);
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
    free(path);
    return !analysis.failed;
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
    if (str_eq(command, "template")) {
        int expect_name = 1;
        char **names = NULL;
        size_t name_count = 0U, name_cap = 0U;
        while (*p != '\0') {
            const char *name_start;
            char *name;
            while (*p != '\0' && isspace((unsigned char)*p)) ++p;
            if (*p == '\0') break;
            if (*p == ',') {
                if (expect_name) {
                    report_error(ti->path, line_number, "expected a type parameter before ','");
                    list_free(names, name_count);
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
                free(command);
                return -1;
            }
            ++p;
            while (is_ident_char(*p)) ++p;
            name = copy_range(name_start, (size_t)(p - name_start));
            if (list_contains(names, name_count, name)) {
                report_error(ti->path, line_number, "duplicate @template parameter '%s'", name);
                free(name);
                list_free(names, name_count);
                free(command);
                return -1;
            }
            list_add(&names, &name_count, &name_cap, name);
            free(name);
            expect_name = 0;
            while (*p != '\0' && isspace((unsigned char)*p)) ++p;
            if (*p != '\0' && *p != ',') {
                report_error(ti->path, line_number, "expected ',' between @template parameters");
                list_free(names, name_count);
                free(command);
                return -1;
            }
        }
        if (expect_name && name_count != 0U) {
            report_error(ti->path, line_number, "expected a type parameter after ','");
            list_free(names, name_count);
            free(command);
            return -1;
        }
        if (ti->annotation_count == ti->annotation_cap) {
            size_t new_cap = ti->annotation_cap == 0U ? 8U : ti->annotation_cap * 2U;
            TemplateAnnotation *next;
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
        {
            size_t i;
            TemplateAnnotation *annotation = &ti->annotations[ti->annotation_count++];
            annotation->parameter_names = names;
            annotation->parameter_count = name_count;
            annotation->parameter_cap = name_cap;
            annotation->line = line_number;
            annotation->used = 0;
            for (i = 0U; i < name_count; ++i) {
                if (!list_contains(ti->parameter_names, ti->parameter_count,
                                   names[i])) {
                    list_add(&ti->parameter_names, &ti->parameter_count,
                             &ti->parameter_cap, names[i]);
                }
            }
        }
    } else {
        report_error(ti->path, line_number, "unknown directive '@%s'", command);
        free(command);
        return -1;
    }
    free(command);
    return 1;
}

static size_t identifier_line_in_code(const char *src, const char *name)
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

static int parse_template(TemplateInfo *ti, const char *path)
{
    Buf source;
    size_t offset = 0U;
    size_t line_number = 1U;
    int in_block_comment = 0;
    size_t lsp_fallback_depth = 0U;
    buf_init(&source);
    ti->path = copy_string(path);
    if (!read_file(path, &source)) {
        buf_free(&source);
        return 0;
    }
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
            if (is_preprocessor_open(line)) ++lsp_fallback_depth;
            else if (preprocessor_keyword(line, "endif") != NULL) --lsp_fallback_depth;
            buf_put_blanked_source(&ti->body, source.p, offset, next);
        } else if (!in_block_comment && is_lsp_fallback_start(line)) {
            lsp_fallback_depth = 1U;
            buf_put_blanked_source(&ti->body, source.p, offset, next);
        } else {
            size_t body_offset = ti->body.len;
            directive = in_block_comment ? 0 : parse_directive(ti, line, line_number);
            if (directive < 0) {
                free(line);
                buf_free(&source);
                return 0;
            }
            if (directive > 0) {
                ti->annotations[ti->annotation_count - 1U].offset =
                    (unsigned)body_offset;
            }
            if (directive == 0) {
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

static int instance_parameter_position(const GenInstance *instance,
                                      size_t parameter_index)
{
    size_t i;
    for (i = 0U; i < instance->argument_count; ++i) {
        if (instance->parameter_indices[i] == parameter_index) return (int)i;
    }
    return -1;
}

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

static int parse_text_type_arguments(const char *source, size_t open,
                                     char ***arguments, size_t *argument_count,
                                     size_t *end_out)
{
    size_t start = open + 1U;
    size_t i = start;
    size_t depth = 1U;
    size_t count = 0U, cap = 0U;
    char **items = NULL;
    for (; source[i] != '\0'; ++i) {
        if (source[i] == '(') ++depth;
        else if (source[i] == ')') {
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
        } else if (source[i] == ',' && depth == 1U) {
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

static char *application_suffix(const TemplateInfo *ti,
                                const GenInstance *instance,
                                const TemplateType *constructor,
                                char **application_arguments,
                                size_t argument_count)
{
    const char **spellings;
    char **owned;
    char *suffix;
    size_t i;
    if (argument_count != constructor->parameter_count) return NULL;
    spellings = (const char **)malloc(argument_count * sizeof(*spellings));
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
        if (instance_argument >= 0) free(argument);
        else owned[i] = argument;
    }
    suffix = instance_suffix(spellings, argument_count);
    for (i = 0U; i < argument_count; ++i) {
        free(owned[i]);
    }
    free(owned);
    free(spellings);
    return suffix;
}

static void rewrite(const char *src, const TemplateInfo *ti,
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
            {
                int parameter = template_parameter_index(ti, ident);
                int argument = parameter >= 0 ?
                               instance_parameter_position(instance, (size_t)parameter) : -1;
                const TemplateType *constructor = parameter < 0 ?
                                                  find_template_type(ti, ident) : NULL;
                const TemplateFunction *function = parameter < 0 && constructor == NULL ?
                                                   find_template_function(ti, ident) : NULL;
                if (parameter >= 0 && argument >= 0) {
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
                        char *suffix = application_suffix(ti, instance, constructor,
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

static void rewrite_c_type(const char *type, const TemplateInfo *ti,
                           const GenInstance *instance,
                           const TemplateApplication *application, Buf *out)
{
    size_t i = 0U;
    const TemplateType *applied_constructor = application != NULL &&
        application->constructor != NULL ?
        find_template_type(ti, application->constructor) : NULL;
    char *applied_suffix = applied_constructor != NULL ?
        application_suffix(ti, instance, applied_constructor,
                           application->arguments, application->argument_count) : NULL;
    while (type[i] != '\0') {
        if (is_ident_start(type[i])) {
            size_t start = i++;
            char *ident;
            while (is_ident_char(type[i])) ++i;
            ident = copy_range(type + start, i - start);
            if (applied_suffix != NULL &&
                str_eq(ident, application->constructor)) {
                buf_puts(out, ident);
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

static void emit_template_type_pattern(Buf *out, const char *type,
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

static int string_set_has(const StringSet *set, const char *s)
{
    return list_contains(set->items, set->count, s);
}

static void string_set_add(StringSet *set, const char *s)
{
    if (!string_set_has(set, s)) list_add(&set->items, &set->count, &set->cap, s);
}

static void string_set_free(StringSet *set)
{
    list_free(set->items, set->count);
    memset(set, 0, sizeof(*set));
}

static void emit_include(Buf *out, const char *header)
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

static char *include_key(const char *header)
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

static void collect_preamble_includes(StringSet *includes, const char *preamble)
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

static void emit_line_directive(Buf *out, unsigned long line, const char *path)
{
    size_t i;
    buf_puts(out, "#line ");
    buf_put_uint(out, line);
    buf_puts(out, " \"");
    for (i = 0U; path[i] != '\0'; ++i) {
        if (path[i] == '\\' || path[i] == '"') buf_putc(out, '\\');
        buf_putc(out, path[i]);
    }
    buf_puts(out, "\"\n");
}

static void emit_instance(Buf *out, const TemplateInfo *ti, const GenInstance *instance,
                          const char *output_path, const char *stem, const char *symbol,
                          unsigned start_offset, unsigned end_offset,
                          unsigned start_line)
{
    char *fragment;
    Buf rewritten;
    Buf path;
    if (end_offset > ti->body.len || start_offset >= end_offset) return;
    fragment = copy_range(ti->body.p + start_offset,
                          (size_t)(end_offset - start_offset));
    buf_init(&rewritten);
    buf_init(&path);
    rewrite(fragment, ti, instance, &rewritten);
    buf_puts(out, "\n/* ---- ");
    buf_puts(out, stem);
    buf_puts(out, ": ");
    buf_puts(out, symbol);
    if (instance->name != NULL && instance->name[0] != '\0') {
        buf_putc(out, '_');
        buf_puts(out, instance->name);
    }
    buf_puts(out, " ---- */\n");
    if (g_emit_line) {
        buf_puts(&path, g_template_dir);
        buf_putc(&path, '/');
        buf_puts(&path, stem);
        buf_puts(&path, GEN_TMPL_SUFFIX);
        emit_line_directive(out, (unsigned long)start_line, path.p);
    }
    buf_putn(out, rewritten.p, rewritten.len);
    if (out->len == 0U || out->p[out->len - 1U] != '\n') buf_putc(out, '\n');
    if (g_emit_line) {
        unsigned long resume_line = (unsigned long)out->newlines + 2UL;
        emit_line_directive(out, resume_line, output_path);
    }
    free(fragment);
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

static char *join_suffix(const char *base, const char *suffix)
{
    Buf b;
    buf_init(&b);
    buf_puts(&b, base);
    buf_putc(&b, '_');
    buf_puts(&b, suffix);
    return b.p;
}

static int assoc_add(AssocList *list, const unsigned *indices,
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
    buf_init(&return_type);
    if (instance != NULL) {
        rewrite_c_type(function->return_type, ti, instance,
                        &function->return_application, &return_type);
    } else {
        emit_template_type_pattern(&return_type, function->return_type,
                                   &function->return_application);
    }
    if (documentation) buf_puts(out, "/// ");
    if (function->is_static) buf_puts(out, "static ");
    if (function->is_inline) buf_puts(out, "inline ");
    buf_puts(out, return_type.p != NULL ? return_type.p : "void");
    buf_putc(out, ' ');
    buf_puts(out, name);
    buf_putc(out, '(');
    if (function->parameter_count == 0U) {
        buf_puts(out, "void");
    }
    for (i = 0U; i < function->parameter_count; ++i) {
        Buf parameter_type;
        if (i != 0U) buf_puts(out, ", ");
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
    buf_putc(out, ')');
    if (!documentation) buf_puts(out, ";\n");
    else buf_putc(out, '\n');
    buf_free(&return_type);
}

static int instance_matches_parameters(const TemplateInfo *ti,
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

static void emit_no_match_declaration(Buf *out, const char *name)
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

static void emit_generic_tree(Buf *out, const char *name,
                              const AssocList *assocs,
                              const size_t *candidates, size_t candidate_count,
                              size_t depth, unsigned indent,
                              const char *default_expression)
{
    const DispatchAssoc *first = &assocs->items[candidates[0]];
    size_t i;
    emit_spaces(out, indent);
    buf_puts(out, "_Generic((GEN_ARG");
    buf_put_uint(out, (unsigned long)first->indices[depth]);
    buf_puts(out, "(__VA_ARGS__)), \\\n");
    for (i = 0U; i < candidate_count; ++i) {
        size_t assoc_index = candidates[i];
        const DispatchAssoc *association = &assocs->items[assoc_index];
        size_t previous;
        int seen = 0;
        size_t subset_count = 0U;
        size_t *subset;
        size_t j;
        for (previous = 0U; previous < i; ++previous) {
            const DispatchAssoc *prior = &assocs->items[candidates[previous]];
            if (str_eq(prior->keys[depth], association->keys[depth])) {
                seen = 1;
                break;
            }
        }
        if (seen) continue;
        for (j = i; j < candidate_count; ++j) {
            const DispatchAssoc *other = &assocs->items[candidates[j]];
            if (str_eq(other->keys[depth], association->keys[depth])) ++subset_count;
        }
        subset = (size_t *)malloc(subset_count * sizeof(*subset));
        if (subset == NULL) die("out of memory");
        subset_count = 0U;
        for (j = i; j < candidate_count; ++j) {
            const DispatchAssoc *other = &assocs->items[candidates[j]];
            if (str_eq(other->keys[depth], association->keys[depth])) {
                subset[subset_count++] = candidates[j];
            }
        }
        emit_spaces(out, indent + 4U);
        buf_puts(out, association->types[depth]);
        buf_puts(out, ": ");
        if (depth + 1U == association->count) {
            buf_puts(out, association->function_name);
        } else {
            emit_generic_tree(out, name, assocs, subset, subset_count,
                              depth + 1U, indent + 4U, default_expression);
        }
        buf_puts(out, ", \\\n");
        free(subset);
    }
    emit_spaces(out, indent + 4U);
    buf_puts(out, "default: ");
    if (default_expression != NULL) {
        buf_puts(out, default_expression);
    } else {
        buf_puts(out, name);
        buf_puts(out, "__no_match_for_argument_type");
    }
    buf_putc(out, ')');
}

static void emit_generic_macro(Buf *out, const char *name,
                               const AssocList *assocs)
{
    size_t *candidates;
    size_t i;
    candidates = (size_t *)malloc(assocs->count * sizeof(*candidates));
    if (candidates == NULL) die("out of memory");
    for (i = 0U; i < assocs->count; ++i) candidates[i] = i;
    buf_puts(out, "#define ");
    buf_puts(out, name);
    buf_puts(out, "(...) ");
    emit_generic_tree(out, name, assocs, candidates, assocs->count, 0U, 0U, NULL);
    buf_puts(out, "(__VA_ARGS__)\n\n");
    free(candidates);
}

static void emit_spaces(Buf *out, unsigned count)
{
    unsigned i;
    for (i = 0U; i < count; ++i) buf_putc(out, ' ');
}

static void emit_gnu_expression(Buf *out, const AssocList *assocs,
                               const char *default_expression)
{
    size_t i;
    size_t depth;
    for (i = 0U; i < assocs->count; ++i) {
        emit_spaces(out, (unsigned)(4U + i * 4U));
        buf_puts(out, "__builtin_choose_expr( \\\n");
        emit_spaces(out, (unsigned)(8U + i * 4U));
        buf_putc(out, '(');
        for (depth = 0U; depth < assocs->items[i].count; ++depth) {
            if (depth != 0U) buf_puts(out, " && ");
            buf_puts(out, "__builtin_types_compatible_p(__typeof__(GEN_ARG");
            buf_put_uint(out, (unsigned long)assocs->items[i].indices[depth]);
            buf_puts(out, "(__VA_ARGS__)), ");
            buf_puts(out, assocs->items[i].types[depth]);
            buf_putc(out, ')');
        }
        buf_puts(out, "), \\\n");
        emit_spaces(out, (unsigned)(8U + i * 4U));
        buf_puts(out, assocs->items[i].function_name);
        buf_puts(out, ", \\\n");
    }
    emit_spaces(out, (unsigned)(4U + assocs->count * 4U));
    buf_puts(out, default_expression);
    for (i = 0U; i < assocs->count; ++i) buf_putc(out, ')');
}

static void emit_gnu_macro(Buf *out, const char *name, const AssocList *assocs)
{
    Buf fallback;
    buf_init(&fallback);
    buf_puts(&fallback, name);
    buf_puts(&fallback, "__no_match_for_argument_type");
    buf_puts(out, "#define ");
    buf_puts(out, name);
    buf_puts(out, "(...) \\\n");
    emit_gnu_expression(out, assocs, fallback.p != NULL ? fallback.p : "0");
    buf_puts(out, "(__VA_ARGS__)\n\n");
    buf_free(&fallback);
}

static void emit_dispatch_macro(Buf *out, const char *name,
                                const TemplateInfo *ti,
                                const TemplateFunction *function,
                                const AssocList *assocs)
{
    emit_no_match_declaration(out, name);
    buf_puts(out, "#undef ");
    buf_puts(out, name);
    buf_putc(out, '\n');
    if (g_dispatch_backend == GEN_DISPATCH_AUTO) {
        buf_puts(out, "#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L\n");
        emit_function_signature(out, ti, function, NULL, name, 1);
        emit_generic_macro(out, name, assocs);
        buf_puts(out, "#elif defined(__GNUC__) || defined(__clang__)\n");
        emit_function_signature(out, ti, function, NULL, name, 1);
        emit_gnu_macro(out, name, assocs);
        buf_puts(out, "#else\n#error \"GEN_DISPATCH_AUTO requires C11 or GNU-compatible C\"\n#endif\n\n");
    } else {
        if (g_dispatch_backend == GEN_DISPATCH_GENERIC) {
            emit_function_signature(out, ti, function, NULL, name, 1);
            emit_generic_macro(out, name, assocs);
        } else if (g_dispatch_backend == GEN_DISPATCH_GNU) {
            emit_function_signature(out, ti, function, NULL, name, 1);
            emit_gnu_macro(out, name, assocs);
        }
    }
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
            for (j = 0U; j < assocs.count; ++j) {
                GenInstance *instance = find_function_instance_for_symbol(
                    ti, &ti->functions[i], assocs.items[j].function_name);
                if (instance != NULL) {
                    emit_function_signature(out, ti, &ti->functions[i], instance,
                                            assocs.items[j].function_name, 1);
                    emit_function_signature(out, ti, &ti->functions[i], instance,
                                            assocs.items[j].function_name, 0);
                }
            }
            emit_dispatch_macro(out, ti->functions[i].name, ti,
                                &ti->functions[i], &assocs);
        }
        assoc_list_free(&assocs);
    }
}

static size_t template_dispatch_count(const TemplateInfo *ti)
{
    size_t i;
    size_t count = 0U;
    for (i = 0U; i < ti->function_count; ++i) {
        size_t j;
        for (j = 0U; j < g_generic_use_count; ++j) {
            if (str_eq(g_generic_uses[j].name, ti->functions[i].name) &&
                g_generic_uses[j].dispatch) {
                ++count;
                break;
            }
        }
    }
    return count;
}

static void emit_function_forward_declarations(Buf *out, size_t template_index,
                                              const TemplateInfo *ti)
{
    size_t i;
    for (i = 0U; i < ti->function_count; ++i) {
        const TemplateFunction *function = &ti->functions[i];
        if (function->type_parameter_count == 0U) {
            emit_function_signature(out, ti, function, NULL, function->name, 0);
        } else {
            size_t j;
            for (j = 0U; j < g_instance_count; ++j) {
                const GenInstance *instance = &g_instances[j];
                char *symbol;
                if (instance->template_index != template_index ||
                    !instance_matches_parameters(ti, instance,
                        function->type_parameters,
                        function->type_parameter_count) ||
                    !generic_use_matches(function->name, instance->name, 0)) continue;
                symbol = join_suffix(function->name, instance->name);
                emit_function_signature(out, ti, function, instance, symbol, 0);
                free(symbol);
            }
        }
    }
    buf_putc(out, '\n');
}

static void emit_template_module(Buf *out, size_t template_index,
                                 const TemplateInfo *ti, const char *stem,
                                 const char *output_path,
                                 const char *guard)
{
    StringSet includes;
    GenInstance empty_instance;
    size_t i;
    memset(&includes, 0, sizeof(includes));
    memset(&empty_instance, 0, sizeof(empty_instance));
    empty_instance.name = "";
    empty_instance.template_index = template_index;
    emit_preamble(out, guard, ti, 1U, &includes);
    for (i = 0U; i < ti->constructor_count; ++i) {
        const TemplateType *constructor = &ti->constructors[i];
        size_t j;
        if (constructor->parameter_count == 0U) {
            emit_instance(out, ti, &empty_instance, output_path, stem, constructor->name,
                          constructor->start_offset, constructor->end_offset,
                          constructor->start_line);
            continue;
        }
        for (j = 0U; j < g_instance_count; ++j) {
            GenInstance *instance = &g_instances[j];
            if (instance->template_index != template_index ||
                !instance_matches_parameters(ti, instance,
                    constructor->parameter_names, constructor->parameter_count) ||
                !type_use_matches(constructor->name, instance->name)) continue;
            emit_instance(out, ti, instance, output_path, stem, constructor->name,
                          constructor->start_offset, constructor->end_offset,
                          constructor->start_line);
        }
    }
    emit_function_forward_declarations(out, template_index, ti);
    for (i = 0U; i < ti->function_count; ++i) {
        const TemplateFunction *function = &ti->functions[i];
        size_t j;
        if (function->type_parameter_count == 0U) {
            emit_instance(out, ti, &empty_instance, output_path, stem, function->name,
                          function->start_offset, function->end_offset,
                          function->start_line);
            continue;
        }
        for (j = 0U; j < g_instance_count; ++j) {
            GenInstance *instance = &g_instances[j];
            if (instance->template_index != template_index ||
                !instance_matches_parameters(ti, instance,
                    function->type_parameters, function->type_parameter_count) ||
                !generic_use_matches(function->name, instance->name, 0)) continue;
            emit_instance(out, ti, instance, output_path, stem, function->name,
                          function->start_offset, function->end_offset,
                          function->start_line);
        }
    }
    buf_puts(out, "#ifndef TMPLGEN_AGGREGATING\n");
    emit_type_functors(out, ti);
    if (template_dispatch_count(ti) != 0U &&
        g_dispatch_backend != GEN_DISPATCH_NONE) {
        emit_arg_helpers(out, highest_function_argument(ti, 1U));
        emit_dispatch(out, ti);
    }
    buf_puts(out, "#endif /* TMPLGEN_AGGREGATING */\n");
    buf_puts(out, "#endif /* ");
    buf_puts(out, guard);
    buf_puts(out, " */\n");
    string_set_free(&includes);
}

static void emit_aggregate_header(Buf *out, const TemplateInfo *infos,
                                  size_t template_count)
{
    size_t i;
    size_t dispatch_count = 0U;
    buf_puts(out, "#ifndef " GEN_GUARD "\n#define " GEN_GUARD "\n");
    buf_puts(out, "/* AUTO-GENERATED aggregate. DO NOT EDIT. */\n");
    buf_puts(out, "#ifndef TMPLGEN_AGGREGATING\n#define TMPLGEN_AGGREGATING\n");
    buf_puts(out, "#define TMPLGEN_AGGREGATING_LOCAL\n#endif\n");
    for (i = 0U; i < template_count; ++i) {
        Buf relative;
        buf_init(&relative);
        buf_puts(&relative, GEN_TEMPLATES[i].stem);
        buf_puts(&relative, ".gen.h");
        emit_include(out, relative.p);
        buf_free(&relative);
    }
    buf_puts(out, "#ifdef TMPLGEN_AGGREGATING_LOCAL\n#undef TMPLGEN_AGGREGATING_LOCAL\n");
    buf_puts(out, "#undef TMPLGEN_AGGREGATING\n#endif\n\n");
    for (i = 0U; i < template_count; ++i) {
        emit_type_functors(out, &infos[i]);
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
    buf_puts(out, "\n#endif /* " GEN_GUARD " */\n");
}

/* ========================== GENERATION ===================== */

static int config_validate(void)
{
    size_t i;
    size_t j;
    if (!valid_identifier(GEN_GUARD)) {
        report_error(NULL, 0U, "GEN_GUARD is not a valid identifier");
        return 0;
    }
    if (g_emit_line != 0 && g_emit_line != 1) {
        report_error(NULL, 0U, "--emit-line must be 0 or 1");
        return 0;
    }
    if (GEN_ENABLE_WATCH != 0 && GEN_ENABLE_WATCH != 1) {
        report_error(NULL, 0U, "GEN_ENABLE_WATCH must be 0 or 1");
        return 0;
    }
    if (g_dispatch_backend != GEN_DISPATCH_NONE &&
        g_dispatch_backend != GEN_DISPATCH_GENERIC &&
        g_dispatch_backend != GEN_DISPATCH_GNU &&
        g_dispatch_backend != GEN_DISPATCH_AUTO) {
        report_error(NULL, 0U, "unsupported dispatch backend");
        return 0;
    }
    for (i = 0U; i < gen_template_count(); ++i) {
        if (GEN_TEMPLATES[i].stem == NULL || GEN_TEMPLATES[i].stem[0] == '\0') {
            report_error(NULL, 0U, "invalid row in GEN_TEMPLATES");
            return 0;
        }
        for (j = i + 1U; j < gen_template_count(); ++j) {
            if (GEN_TEMPLATES[j].stem != NULL &&
                str_eq(GEN_TEMPLATES[i].stem, GEN_TEMPLATES[j].stem)) {
                report_error(NULL, 0U, "duplicate template stem '%s'", GEN_TEMPLATES[i].stem);
                return 0;
            }
        }
    }
    return 1;
}

static int validate_template_info(const TemplateInfo *infos, size_t count)
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

static void build_template_path(Buf *path, const char *stem)
{
    buf_puts(path, g_template_dir);
    buf_putc(path, '/');
    buf_puts(path, stem);
    buf_puts(path, GEN_TMPL_SUFFIX);
}

static void emit_preamble(Buf *out, const char *guard,
                          const TemplateInfo *infos, size_t count,
                          StringSet *includes)
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
            char *key = include_key(infos[i].includes[j]);
            if (!string_set_has(includes, key)) {
                string_set_add(includes, key);
                emit_include(out, infos[i].includes[j]);
            }
            free(key);
        }
    }
    if (count != 0U) buf_putc(out, '\n');
}

static char *generated_module_path(const char *stem)
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

static char *generated_module_guard(const char *stem)
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

typedef struct {
    char *name;
    char *path;
    char *type_spelling;
    char *canonical_spelling;
    unsigned offset;
} SourceVariable;

typedef struct {
    SourceVariable *items;
    size_t count, cap;
} SourceVariableList;

typedef struct {
    CXTranslationUnit tu;
    const TemplateInfo *infos;
    size_t template_count;
    SourceList *files;
    ClangUseList *uses;
    SourceVariableList variables;
    int pass;
    int failed;
} ClangContext;

static char *copy_cx_string(CXString value)
{
    const char *text = clang_getCString(value);
    char *copy = copy_string(text != NULL ? text : "");
    clang_disposeString(value);
    return copy;
}

static void source_variable_add(SourceVariableList *variables, const char *name,
                                const char *path, const char *type_spelling,
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
    variables->items[variables->count].path = copy_string(path);
    variables->items[variables->count].type_spelling = copy_string(type_spelling);
    variables->items[variables->count].canonical_spelling =
        copy_string(canonical_spelling);
    variables->items[variables->count].offset = offset;
    ++variables->count;
}

static void source_variable_list_free(SourceVariableList *variables)
{
    size_t i;
    for (i = 0U; i < variables->count; ++i) {
        free(variables->items[i].name);
        free(variables->items[i].path);
        free(variables->items[i].type_spelling);
        free(variables->items[i].canonical_spelling);
    }
    free(variables->items);
    memset(variables, 0, sizeof(*variables));
}

static int path_is_scanned(const SourceList *files, const char *path)
{
    size_t i;
    for (i = 0U; i < files->count; ++i) {
        if (str_eq(files->items[i].path, path)) return 1;
    }
    return 0;
}

static const TemplateType *find_template_type(const TemplateInfo *ti,
                                              const char *name)
{
    size_t i;
    for (i = 0U; i < ti->constructor_count; ++i) {
        if (str_eq(ti->constructors[i].name, name)) return &ti->constructors[i];
    }
    return NULL;
}

static int find_type_constructor(const TemplateInfo *infos, size_t count,
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

static int builtin_type_argument(const char *name)
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

static int generic_function_template(const TemplateInfo *infos, size_t count,
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

static const TemplateFunction *find_template_function(const TemplateInfo *ti,
                                                       const char *name)
{
    size_t i;
    for (i = 0U; i < ti->function_count; ++i) {
        if (str_eq(ti->functions[i].name, name)) return &ti->functions[i];
    }
    return NULL;
}

static int macro_type_arguments(CXTranslationUnit tu, CXCursor cursor,
                                char ***arguments, size_t *argument_count)
{
    CXToken *tokens = NULL;
    unsigned count = 0U;
    unsigned i;
    unsigned open = 0U;
    unsigned start;
    size_t parens = 0U, brackets = 0U, braces = 0U;
    size_t capacity = 0U;
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
        if (start >= i || i - start != 1U) {
            okay = 0;
            break;
        }
        {
            char *argument = copy_cx_string(clang_getTokenSpelling(tu, tokens[start]));
            enum CXTokenKind kind = clang_getTokenKind(tokens[start]);
            if (kind != CXToken_Identifier &&
                !(kind == CXToken_Keyword && builtin_type_argument(argument))) {
                free(argument);
                okay = 0;
                break;
            }
            list_add(arguments, argument_count, &capacity, argument);
            free(argument);
        }
        if (closing) break;
        start = i + 1U;
    }
    if (*argument_count == 0U) okay = 0;
    clang_disposeTokens(tu, tokens, count);
    return okay;
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

static void clang_use_add(ClangUseList *list, const char *name, const char *path,
                          unsigned offset, size_t line, unsigned argument_index,
                          const char *argument_path,
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
    list->items[list->count].argument_path = copy_string(argument_path);
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
        free(uses->items[i].argument_path);
        free(uses->items[i].type_spelling);
        free(uses->items[i].canonical_spelling);
    }
    free(uses->items);
    memset(uses, 0, sizeof(*uses));
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
    char *path;
    (void)parent;
    clang_getExpansionLocation(location, &source_file, &line, &column, &offset);
    if (source_file == NULL) return CXChildVisit_Recurse;
    path = copy_cx_string(clang_getFileName(source_file));
    if (!path_is_scanned(context->files, path)) {
        free(path);
        return CXChildVisit_Recurse;
    }
    if (kind == CXCursor_VarDecl) {
        char *name = copy_cx_string(clang_getCursorSpelling(cursor));
        CXType declared_type = clang_getCursorType(cursor);
        CXType canonical_type = clang_getCanonicalType(declared_type);
        char *type_spelling = copy_cx_string(clang_getTypeSpelling(declared_type));
        char *canonical_spelling = copy_cx_string(
            clang_getTypeSpelling(canonical_type));
        if (valid_identifier(name) && type_spelling[0] != '\0') {
            source_variable_add(&context->variables, name, path, type_spelling,
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
        }
        free(name);
        free(canonical_name);
    } else if (kind == CXCursor_MacroExpansion) {
        char *name = copy_cx_string(clang_getCursorSpelling(cursor));
        size_t type_template_index = 0U;
        const TemplateType *constructor = NULL;
        if (context->pass == 1 &&
            find_type_constructor(context->infos, context->template_count, name,
                                  &type_template_index, &constructor)) {
            char **arguments = NULL;
            char **canonical_arguments = NULL;
            size_t argument_count = 0U;
            size_t j;
            if (!macro_type_arguments(context->tu, cursor, &arguments, &argument_count) ||
                argument_count != constructor->parameter_count) {
                report_error(path, line,
                             "%s() requires %lu single-token type arguments",
                             name, (unsigned long)constructor->parameter_count);
                context->failed = 1;
            } else {
                canonical_arguments = (char **)calloc(argument_count,
                                                      sizeof(*canonical_arguments));
                if (canonical_arguments == NULL) die("out of memory");
                for (j = 0U; j < argument_count; ++j) {
                    const char *canonical = find_alias_canonical(arguments[j]);
                    canonical_arguments[j] = copy_string(canonical != NULL ?
                                                         canonical : arguments[j]);
                    add_alias(arguments[j], canonical_arguments[j]);
                }
                {
                    size_t *parameter_indices =
                        (size_t *)malloc(argument_count * sizeof(*parameter_indices));
                    GenInstance *instance;
                    if (parameter_indices == NULL) die("out of memory");
                    for (j = 0U; j < argument_count; ++j) {
                        int parameter = template_parameter_index(
                            &context->infos[type_template_index],
                            constructor->parameter_names[j]);
                        if (parameter < 0) die("invalid template constructor parameter map");
                        parameter_indices[j] = (size_t)parameter;
                    }
                    instance = add_instance(
                        type_template_index, parameter_indices,
                        (const char *const *)arguments,
                        (const char *const *)canonical_arguments, argument_count);
                    (void)add_type_use(name, instance->name);
                    free(parameter_indices);
                }
                for (j = 0U; j < argument_count; ++j) free(canonical_arguments[j]);
                free(canonical_arguments);
            }
            list_free(arguments, argument_count);
        } else if (context->uses != NULL) {
            size_t template_index;
            if (generic_function_template(context->infos, context->template_count,
                                          name, &template_index)) {
                const TemplateFunction *function = find_template_function(
                    &context->infos[template_index], name);
                if (function != NULL) {
                    size_t k;
                    for (k = 0U; k < function->dispatch_count; ++k) {
                        unsigned argument = function->dispatch_indices[k];
                        char *argument_path = NULL;
                        unsigned argument_start = 0U;
                        unsigned argument_end = 0U;
                        if (macro_argument_source_range(context->tu, cursor, argument,
                                                        &argument_path, &argument_start,
                                                        &argument_end)) {
                            clang_use_add(context->uses, name, path, offset,
                                          (size_t)line, argument, argument_path,
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
    char *path;
    size_t i;
    (void)parent;
    if (!clang_isExpression(clang_getCursorKind(cursor))) return CXChildVisit_Recurse;
    location = clang_getCursorLocation(cursor);
    clang_getExpansionLocation(location, &source_file, &line, &column, &offset);
    if (source_file == NULL) return CXChildVisit_Recurse;
    path = copy_cx_string(clang_getFileName(source_file));
    for (i = 0U; i < context->uses->count; ++i) {
        ClangUse *use = &context->uses->items[i];
        CXSourceRange extent = clang_getCursorExtent(cursor);
        CXFile start_file = NULL;
        CXFile end_file = NULL;
        unsigned start = 0U, end = 0U, start_line = 0U, start_column = 0U;
        unsigned end_line = 0U, end_column = 0U;
        CXType expression_type;
        CXType canonical_type;
        if (use->type_spelling != NULL || use->argument_path == NULL ||
            !str_eq(use->argument_path, path)) continue;
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
        if (g_verbose) fprintf(stderr, "tmplgen: clang type %s:%u arg=%u..%u type=%s\n",
                               path, line, start, end, use->type_spelling);
    }
    free(path);
    (void)line;
    (void)column;
    return CXChildVisit_Recurse;
}

static const SourceFile *source_file_for_path(const SourceList *files,
                                              const char *path)
{
    size_t i;
    for (i = 0U; i < files->count; ++i) {
        if (str_eq(files->items[i].path, path)) return &files->items[i];
    }
    return NULL;
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
    if (use->argument_path == NULL ||
        use->argument_end <= use->argument_start) return 0;
    source = source_file_for_path(context->files, use->argument_path);
    if (source == NULL || (size_t)use->argument_end > source->text.len) return 0;
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
            !str_eq(candidate->path, use->argument_path) ||
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

static int parse_source_with_clang(CXIndex index, const SourceFile *source,
                                   const Buf *bootstrap,
                                   int pass, const TemplateInfo *infos,
                                   size_t template_count, SourceList *files,
                                   ClangUseList *uses)
{
    const char **arguments;
    int argument_count = (int)g_clang_arg_count + 2;
    int i;
    struct CXUnsavedFile *unsaved;
    char **unsaved_paths;
    size_t unsaved_count = gen_template_count() + 1U;
    size_t unsaved_index;
    CXTranslationUnit tu = NULL;
    enum CXErrorCode result;
    ClangContext context;
    arguments = (const char **)malloc((size_t)argument_count * sizeof(*arguments));
    if (arguments == NULL) die("out of memory");
    arguments[0] = "-x";
    arguments[1] = "c";
    for (i = 0; i < (int)g_clang_arg_count; ++i) arguments[i + 2] = g_clang_args[i];
    unsaved = (struct CXUnsavedFile *)calloc(unsaved_count, sizeof(*unsaved));
    unsaved_paths = (char **)calloc(unsaved_count, sizeof(*unsaved_paths));
    if (unsaved == NULL || unsaved_paths == NULL) die("out of memory");
    unsaved_paths[0] = absolute_path(g_output_path);
    for (unsaved_index = 1U; unsaved_index < unsaved_count; ++unsaved_index) {
        unsaved_paths[unsaved_index] = generated_module_path(
            GEN_TEMPLATES[unsaved_index - 1U].stem);
    }
    for (unsaved_index = 0U; unsaved_index < unsaved_count; ++unsaved_index) {
        unsaved[unsaved_index].Filename = unsaved_paths[unsaved_index];
        unsaved[unsaved_index].Contents = bootstrap->p != NULL ? bootstrap->p : "";
        unsaved[unsaved_index].Length = (unsigned long)bootstrap->len;
    }
    result = clang_parseTranslationUnit2(index, source->path, arguments, argument_count,
                                         unsaved, (unsigned)unsaved_count,
                                         CXTranslationUnit_DetailedPreprocessingRecord |
                                         CXTranslationUnit_KeepGoing, &tu);
    free(arguments);
    for (unsaved_index = 0U; unsaved_index < unsaved_count; ++unsaved_index) {
        free(unsaved_paths[unsaved_index]);
    }
    free(unsaved_paths);
    free(unsaved);
    if (result != CXError_Success || tu == NULL) {
        report_error(source->path, 0U, "libclang failed to parse this source file (%d)",
                     (int)result);
        return 0;
    }
    memset(&context.variables, 0, sizeof(context.variables));
    context.tu = tu;
    context.infos = infos;
    context.template_count = template_count;
    context.files = files;
    context.uses = uses;
    context.pass = pass;
    context.failed = 0;
    clang_visitChildren(clang_getTranslationUnitCursor(tu),
                        collect_clang_source_cursor, &context);
    if (uses != NULL) {
        size_t use_index;
        clang_visitChildren(clang_getTranslationUnitCursor(tu),
                            collect_clang_expression_type, &context);
        for (use_index = 0U; use_index < uses->count; ++use_index) {
            (void)infer_use_type_from_variable(&context, &uses->items[use_index]);
        }
    }
    source_variable_list_free(&context.variables);
    clang_disposeTranslationUnit(tu);
    return context.failed == 0;
}

static void build_bootstrap_associations(size_t template_index,
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

static void emit_bootstrap_function_prototypes(Buf *out,
                                               const TemplateInfo *infos,
                                               size_t template_count)
{
    size_t i;
    for (i = 0U; i < template_count; ++i) {
        size_t j;
        for (j = 0U; j < infos[i].function_count; ++j) {
            const TemplateFunction *function = &infos[i].functions[j];
            size_t k;
            if (function->dispatch_count == 0U ||
                function->type_parameter_count == 0U) continue;
            for (k = 0U; k < g_instance_count; ++k) {
                const GenInstance *instance = &g_instances[k];
                Buf return_type;
                char *symbol;
                size_t p;
                if (instance->template_index != i ||
                    !instance_matches_parameters(&infos[i], instance,
                        function->type_parameters, function->type_parameter_count)) continue;
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
                if (function->parameter_count == 0U) buf_puts(out, "void");
                for (p = 0U; p < function->parameter_count; ++p) {
                    Buf argument_type;
                    if (p != 0U) buf_puts(out, ", ");
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
                                          const AssocList *assocs,
                                          int gnu_backend)
{
    size_t i;
    size_t *candidates;
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
    buf_puts(out, ", \\\n    ");
    if (gnu_backend) {
        Buf fallback;
        buf_init(&fallback);
        buf_puts(&fallback, name);
        buf_puts(&fallback, "__no_match_for_argument_type");
        emit_gnu_expression(out, assocs, fallback.p != NULL ? fallback.p : "0");
        buf_free(&fallback);
    } else {
        candidates = (size_t *)malloc(assocs->count * sizeof(*candidates));
        if (candidates == NULL) die("out of memory");
        for (i = 0U; i < assocs->count; ++i) candidates[i] = i;
        emit_generic_tree(out, name, assocs, candidates, assocs->count, 0U, 0U, NULL);
        free(candidates);
    }
    buf_puts(out, "(__VA_ARGS__))\n");
}

static void emit_bootstrap_function_macros(Buf *out, const TemplateInfo *infos,
                                           size_t template_count)
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
                emit_no_match_declaration(out, function->name);
                buf_puts(out, "#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L\n");
                emit_bootstrap_function_macro(out, function->name, function,
                                              &assocs, 0);
                buf_puts(out, "#elif defined(__GNUC__) || defined(__clang__)\n");
                emit_bootstrap_function_macro(out, function->name, function,
                                              &assocs, 1);
                buf_puts(out, "#else\n#error \"tmplgen bootstrap dispatch requires C11 or GNU C\"\n#endif\n\n");
            }
            assoc_list_free(&assocs);
        }
    }
}

static void emit_bootstrap_header(Buf *out, const TemplateInfo *infos,
                                  size_t template_count, int second_pass)
{
    StringSet includes;
    StringSet type_stubs;
    size_t i;
    memset(&includes, 0, sizeof(includes));
    memset(&type_stubs, 0, sizeof(type_stubs));
    emit_preamble(out, GEN_GUARD, infos, template_count, &includes);
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
                              GEN_TEMPLATES[i].stem, constructor->name,
                              constructor->start_offset, constructor->end_offset,
                              constructor->start_line);
            }
        }
        for (j = 0U; j < infos[i].function_count; ++j) {
            const TemplateFunction *function = &infos[i].functions[j];
            if (function->type_parameter_count == 0U) {
                emit_instance(out, &infos[i], &empty_instance, g_output_path,
                              GEN_TEMPLATES[i].stem, function->name,
                              function->start_offset, function->end_offset,
                              function->start_line);
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
        emit_bootstrap_function_prototypes(out, infos, template_count);
        emit_bootstrap_function_macros(out, infos, template_count);
    } else {
        for (i = 0U; i < template_count; ++i) {
            size_t j;
            for (j = 0U; j < infos[i].function_count; ++j) {
                const TemplateFunction *function = &infos[i].functions[j];
                size_t k;
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
    buf_puts(out, "\n#endif /* " GEN_GUARD " */\n");
    string_set_free(&includes);
    string_set_free(&type_stubs);
}

static int extract_template_argument(const char *pattern, const char *actual,
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

static int assign_template_argument(const TemplateInfo *ti, size_t parameter_index,
                                    const char *spelling, const char *canonical,
                                    char **spellings, char **canonicals,
                                    unsigned char *assigned)
{
    if (assigned[parameter_index]) {
        return str_eq(canonicals[parameter_index], canonical);
    }
    spellings[parameter_index] = copy_string(spelling);
    canonicals[parameter_index] = copy_string(canonical != NULL ? canonical : spelling);
    assigned[parameter_index] = 1U;
    (void)ti;
    return 1;
}

static void request_instance_types(const TemplateInfo *ti,
                                   const TemplateFunction *requester,
                                   const GenInstance *instance)
{
    size_t i;
    char *fragment = NULL;
    if (requester != NULL && requester->end_offset <= ti->body.len &&
        requester->start_offset < requester->end_offset) {
        fragment = copy_range(ti->body.p + requester->start_offset,
                              (size_t)(requester->end_offset - requester->start_offset));
    }
    for (i = 0U; i < ti->constructor_count; ++i) {
        size_t j;
        size_t count = ti->constructors[i].parameter_count;
        size_t *indices;
        const char **spellings;
        const char **canonicals;
        int covered = 1;
        if (count == 0U) continue;
        if (fragment == NULL ||
            identifier_line_in_code(fragment, ti->constructors[i].name) == 0U) continue;
        for (j = 0U; j < count; ++j) {
            int parameter = template_parameter_index(ti,
                ti->constructors[i].parameter_names[j]);
            if (parameter < 0 ||
                instance_parameter_position(instance, (size_t)parameter) < 0) {
                covered = 0;
                break;
            }
        }
        if (!covered) continue;
        indices = (size_t *)malloc(count * sizeof(*indices));
        spellings = (const char **)malloc(count * sizeof(*spellings));
        canonicals = (const char **)malloc(count * sizeof(*canonicals));
        if (indices == NULL || spellings == NULL || canonicals == NULL) die("out of memory");
        for (j = 0U; j < count; ++j) {
            size_t parameter = (size_t)template_parameter_index(
                ti, ti->constructors[i].parameter_names[j]);
            size_t argument = (size_t)instance_parameter_position(instance, parameter);
            indices[j] = parameter;
            spellings[j] = instance->arguments[argument].c_type;
            canonicals[j] = instance->arguments[argument].canonical;
        }
        {
            GenInstance *type_instance = add_instance(instance->template_index,
                indices, spellings, canonicals, count);
            (void)add_type_use(ti->constructors[i].name, type_instance->name);
        }
        free(indices);
        free(spellings);
        free(canonicals);
    }
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
                                          unsigned char *assigned, int *okay)
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
                    *okay = 0;
                }
            }
        }
        free(argument);
    }
}

static int process_clang_use_group(ClangUseList *uses, size_t first,
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
            okay = 0;
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
                        if (!assign_template_argument(ti, k, spelling, canonical,
                                                      spellings, canonicals, assigned)) {
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
                        if (application_match == 0) continue;
                        assign_application_parameters(ti, function,
                            &parameter->application, candidate, spellings,
                            canonicals, assigned, &okay);
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
            }
        }
        free(dispatch_indices);
    } else if (report_incomplete) {
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

static GenInstance *project_instance(const TemplateInfo *ti,
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
    free(indices);
    free(spellings);
    free(canonicals);
    return result;
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
        for (j = 0U; j < ti->function_count; ++j) {
            const TemplateFunction *callee = &ti->functions[j];
            GenInstance *callee_instance;
            if (callee->type_parameter_count == 0U ||
                identifier_line_in_code(body, callee->name) == 0U) continue;
            callee_instance = project_instance(ti, &instance_copy, callee->type_parameters,
                                               callee->type_parameter_count);
            if (callee_instance == NULL) {
                if (report_unresolved) {
                    report_error(ti->path, function->start_line,
                                 "cannot monomorphize call to '%s' from specialization '%s': the caller does not bind every callee template parameter",
                                 callee->name, instance_copy.name);
                }
                continue;
            }
            if (!generic_use_matches(callee->name, callee_instance->name, 0)) {
                (void)add_generic_use(callee->name, callee_instance->name, 0,
                                      NULL, NULL, NULL, 0U);
            }
            request_instance_types(ti, callee, callee_instance);
        }
        free(body);
    }
}

static void expand_dependencies_to_fixpoint(const TemplateInfo *infos,
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


static void collect_suffix_api_uses(SourceFile *file, const TemplateInfo *infos,
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
                            }
                        }
                    }
                }
            }
        }
    }
}

static int discover_source_usages(const TemplateInfo *infos, size_t template_count,
                                 SourceList *files)
{
    CXIndex index;
    Buf bootstrap;
    size_t i;
    int success = 1;
    if (!scan_source_directory(files, g_source_root)) return 0;
    for (i = 0U; i < files->count; ++i) tokenize_source(&files->items[i]);
    index = clang_createIndex(0, 0);
    if (index == NULL) die("libclang could not create an index");
    buf_init(&bootstrap);
    emit_bootstrap_header(&bootstrap, infos, template_count, 0);
    for (i = 0U; i < files->count; ++i) {
        ClangUseList uses;
        memset(&uses, 0, sizeof(uses));
        if (!parse_source_with_clang(index, &files->items[i], &bootstrap,
                                     1, infos, template_count, files, &uses)) success = 0;
        (void)process_clang_uses(&uses, infos, template_count, 0);
        clang_use_list_free(&uses);
    }
    buf_free(&bootstrap);
    if (success) {
        for (i = 0U; i < files->count; ++i) {
            collect_suffix_api_uses(&files->items[i], infos, template_count);
        }
        expand_dependencies_to_fixpoint(infos, template_count, 0);
        emit_bootstrap_header(&bootstrap, infos, template_count, 1);
        for (i = 0U; i < files->count; ++i) {
            ClangUseList uses;
            memset(&uses, 0, sizeof(uses));
            if (!parse_source_with_clang(index, &files->items[i], &bootstrap,
                                          2, infos, template_count, files, &uses)) success = 0;
            if (!process_clang_uses(&uses, infos, template_count, 1)) {
                /* Inference warnings do not stop source discovery. */
            }
            clang_use_list_free(&uses);
        }
        expand_dependencies_to_fixpoint(infos, template_count, 1);
    }
    buf_free(&bootstrap);
    clang_disposeIndex(index);
    return success;
}

static void clear_discovery_state(void)
{
    size_t i;
    for (i = 0U; i < g_instance_count; ++i) {
        size_t j;
        free(g_instances[i].name);
        free(g_instances[i].key);
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
    for (i = 0U; i < g_alias_count; ++i) {
        free(g_aliases[i].name);
        free(g_aliases[i].canonical);
    }
    free(g_aliases);
    g_aliases = NULL;
    g_alias_count = 0U;
    g_alias_cap = 0U;
}

static int generate(GeneratedFileList *out)
{
    TemplateInfo *infos;
    StringSet emitted_guards;
    SourceList source_files;
    size_t i;
    memset(&source_files, 0, sizeof(source_files));
    memset(out, 0, sizeof(*out));
    clear_discovery_state();
    g_error_count = 0U;
    if (!config_validate()) return 0;
    infos = (TemplateInfo *)calloc(gen_template_count() + 1U, sizeof(*infos));
    if (infos == NULL) die("out of memory");
    for (i = 0U; i < gen_template_count(); ++i) {
        template_info_init(&infos[i]);
        infos[i].template_index = i;
    }
    memset(&emitted_guards, 0, sizeof(emitted_guards));
    for (i = 0U; i < gen_template_count(); ++i) {
        Buf path;
        char *module_path;
        char *absolute_module_path;
        char *absolute_aggregate_path;
        char *guard;
        buf_init(&path);
        build_template_path(&path, GEN_TEMPLATES[i].stem);
        if (!parse_template(&infos[i], path.p)) {
            buf_free(&path);
            goto failed;
        }
        if (!analyze_template(&infos[i])) {
            buf_free(&path);
            goto failed;
        }
        buf_free(&path);
        module_path = generated_module_path(GEN_TEMPLATES[i].stem);
        absolute_module_path = absolute_path(module_path);
        absolute_aggregate_path = absolute_path(g_output_path);
        if (str_eq(absolute_module_path, absolute_aggregate_path)) {
            report_error(module_path, 0U,
                         "generated module path collides with the aggregate header");
            free(module_path);
            free(absolute_module_path);
            free(absolute_aggregate_path);
            goto failed;
        }
        free(absolute_module_path);
        free(absolute_aggregate_path);
        free(module_path);
        guard = generated_module_guard(GEN_TEMPLATES[i].stem);
        if (string_set_has(&emitted_guards, guard)) {
            report_error(infos[i].path, 0U,
                         "template stem collides with another generated include guard");
            free(guard);
            goto failed;
        }
        string_set_add(&emitted_guards, guard);
        free(guard);
    }
    if (!validate_template_info(infos, gen_template_count())) goto failed;
    if (!discover_source_usages(infos, gen_template_count(), &source_files)) goto failed;
    if (g_error_count != 0U) goto failed;
    for (i = 0U; i < gen_template_count(); ++i) {
        Buf module;
        char *path = generated_module_path(GEN_TEMPLATES[i].stem);
        char *guard = generated_module_guard(GEN_TEMPLATES[i].stem);
        buf_init(&module);
        emit_template_module(&module, i, &infos[i], GEN_TEMPLATES[i].stem,
                             path, guard);
        generated_file_list_add(out, path, &module);
        buf_free(&module);
        free(path);
        free(guard);
    }
    {
        Buf aggregate;
        buf_init(&aggregate);
        emit_aggregate_header(&aggregate, infos, gen_template_count());
        generated_file_list_add(out, g_output_path, &aggregate);
        buf_free(&aggregate);
    }
    string_set_free(&emitted_guards);
    source_list_free(&source_files);
    for (i = 0U; i < gen_template_count(); ++i) template_info_free(&infos[i]);
    free(infos);
    clear_discovery_state();
    return 1;

failed:
    string_set_free(&emitted_guards);
    source_list_free(&source_files);
    for (i = 0U; i < gen_template_count(); ++i) template_info_free(&infos[i]);
    free(infos);
    clear_discovery_state();
    return 0;
}

/* ======================= OPTIONAL PLATFORM ================= */

#if GEN_ENABLE_WATCH
static void signature_mix(unsigned long long *hash, const char *data, size_t length)
{
    size_t i;
    for (i = 0U; i < length; ++i) {
        *hash ^= (unsigned long long)(unsigned char)data[i];
        *hash *= 1099511628211ULL;
    }
    *hash ^= (unsigned long long)length;
    *hash *= 1099511628211ULL;
}

static int source_file_path_compare(const void *left, const void *right)
{
    const SourceFile *a = (const SourceFile *)left;
    const SourceFile *b = (const SourceFile *)right;
    return strcmp(a->path, b->path);
}

static int compute_watch_signature(unsigned long long *signature)
{
    SourceList files;
    char *source_root = absolute_path(g_source_root);
    char *template_root = absolute_path(g_template_dir);
    unsigned long long hash = 14695981039346656037ULL;
    size_t i;

    memset(&files, 0, sizeof(files));
    if (!scan_source_directory(&files, source_root)) {
        free(source_root);
        free(template_root);
        source_list_free(&files);
        return 0;
    }
    if (!str_eq(source_root, template_root) &&
        !scan_source_directory(&files, template_root)) {
        free(source_root);
        free(template_root);
        source_list_free(&files);
        return 0;
    }
    if (files.count > 1U) {
        qsort(files.items, files.count, sizeof(*files.items), source_file_path_compare);
    }
    for (i = 0U; i < files.count; ++i) {
        signature_mix(&hash, files.items[i].path, strlen(files.items[i].path));
        signature_mix(&hash, files.items[i].text.p, files.items[i].text.len);
    }
    *signature = hash;
    free(source_root);
    free(template_root);
    source_list_free(&files);
    return 1;
}

static void sleep_ms(unsigned ms)
{
#if defined(_WIN32)
    Sleep((DWORD)ms);
#elif defined(_POSIX_C_SOURCE)
    struct timespec request;
    request.tv_sec = (time_t)(ms / 1000U);
    request.tv_nsec = (long)((ms % 1000U) * 1000000UL);
    while (nanosleep(&request, &request) != 0 && errno == EINTR) {
    }
#else
    clock_t start = clock();
    clock_t target = (clock_t)((double)ms * (double)CLOCKS_PER_SEC / 1000.0);
    /* This portable fallback burns CPU while waiting. */
    while (start != (clock_t)-1 && clock() - start < target) {
    }
#endif
}
#endif

/* ============================= CLI ========================= */

static void usage(FILE *out)
{
    fprintf(out,
            "Usage: tmplgen [options] [--clang-arg ARG ...]\n"
            "       tmplgen --help\n\n"
            "Generate %s from templates and Clang type/macro-usage data.\n"
            "  --check    compare generated contents without writing\n"
            "  --watch    regenerate when sources or templates change (poll %u ms)\n"
            "  --verbose  report reads and writes to stderr\n"
            "  --source-root DIR   recursively scan this project tree\n"
            "  --template-dir DIR  read templates from this directory\n"
            "  --output FILE       write the aggregate header here (modules beside it)\n"
            "  --dispatch MODE     auto, generic, gnu, or none\n"
            "  --clang-arg ARG     pass one argument to libclang (repeatable)\n"
            "  --emit-line         map generated definitions to template locations\n"
            "  --no-emit-line      keep generated locations in the output header\n",
            g_output_path != NULL ? g_output_path : GEN_OUTPUT,
            (unsigned)GEN_WATCH_INTERVAL_MS);
}

static void replace_option_string(char **target, const char *value)
{
    char *copy = copy_string(value);
    free(*target);
    *target = copy;
}

static void add_clang_argument(const char *argument)
{
    char **next;
    size_t new_cap;
    if (g_clang_arg_count == g_clang_arg_cap) {
        new_cap = g_clang_arg_cap == 0U ? 8U : g_clang_arg_cap * 2U;
        if (new_cap < g_clang_arg_cap ||
            new_cap > (size_t)-1 / sizeof(*g_clang_args)) die("too many Clang arguments");
        next = (char **)realloc(g_clang_args, new_cap * sizeof(*g_clang_args));
        if (next == NULL) die("out of memory");
        g_clang_args = next;
        g_clang_arg_cap = new_cap;
    }
    g_clang_args[g_clang_arg_count++] = copy_string(argument);
}

static int parse_dispatch_backend(const char *name)
{
    if (str_eq(name, "none")) return GEN_DISPATCH_NONE;
    if (str_eq(name, "generic")) return GEN_DISPATCH_GENERIC;
    if (str_eq(name, "gnu")) return GEN_DISPATCH_GNU;
    if (str_eq(name, "auto")) return GEN_DISPATCH_AUTO;
    return -1;
}

static int run_once(int check_only)
{
    GeneratedFileList files;
    int result;
    int exists;
    int changed;
    size_t i;
    int status = 0;
    memset(&files, 0, sizeof(files));
    if (!generate(&files)) {
        generated_file_list_free(&files);
        return 1;
    }
    for (i = 0U; i < files.count; ++i) {
        GeneratedFile *file = &files.items[i];
        if (check_only) {
            result = file_matches(file->path, &file->contents, &exists);
            if (result < 0) {
                status = 1;
            } else if (result == 0) {
                printf("%s: differs\n", file->path);
                status = 1;
            } else {
                printf("%s: matches\n", file->path);
            }
        } else {
            if (!write_file_if_changed(file->path, &file->contents, &changed)) {
                status = 1;
            } else {
                printf("%s: %s\n", file->path, changed ? "written" : "unchanged");
            }
        }
    }
    generated_file_list_free(&files);
    return status;
}

int main(int argc, char **argv)
{
    int check_only = 0;
    int watch = 0;
    int i;
    g_source_root = copy_string(GEN_SCAN_ROOT);
    g_template_dir = copy_string(GEN_TMPL_DIR);
    g_output_path = copy_string(GEN_OUTPUT);
    g_dispatch_backend = GEN_DISPATCH;
    g_emit_line = GEN_EMIT_LINE;
    for (i = 1; i < argc; ++i) {
        if (str_eq(argv[i], "--help")) {
            usage(stdout);
            return 0;
        } else if (str_eq(argv[i], "--verbose")) {
            g_verbose = 1;
        } else if (str_eq(argv[i], "--check")) {
            check_only = 1;
        } else if (str_eq(argv[i], "--watch")) {
            watch = 1;
        } else if (str_eq(argv[i], "--source-root") ||
                   str_eq(argv[i], "--template-dir") ||
                   str_eq(argv[i], "--output") ||
                   str_eq(argv[i], "--dispatch") ||
                   str_eq(argv[i], "--clang-arg")) {
            const char *option = argv[i];
            const char *value;
            if (i + 1 >= argc) {
                fprintf(stderr, "tmplgen: %s requires a value\n", option);
                return 2;
            }
            value = argv[++i];
            if (str_eq(option, "--source-root")) replace_option_string(&g_source_root, value);
            else if (str_eq(option, "--template-dir")) replace_option_string(&g_template_dir, value);
            else if (str_eq(option, "--output")) replace_option_string(&g_output_path, value);
            else if (str_eq(option, "--clang-arg")) add_clang_argument(value);
            else {
                int backend = parse_dispatch_backend(value);
                if (backend < 0) {
                    fprintf(stderr, "tmplgen: unsupported dispatch mode '%s'\n", value);
                    return 2;
                }
                g_dispatch_backend = backend;
            }
        } else if (strncmp(argv[i], "--clang-arg=", 12U) == 0) {
            add_clang_argument(argv[i] + 12U);
        } else if (str_eq(argv[i], "--emit-line")) {
            g_emit_line = 1;
        } else if (str_eq(argv[i], "--no-emit-line")) {
            g_emit_line = 0;
        } else {
            fprintf(stderr, "tmplgen: unknown option '%s'\n", argv[i]);
            usage(stderr);
            return 2;
        }
    }
    if (check_only && watch) {
        fputs("tmplgen: --check and --watch cannot be combined\n", stderr);
        usage(stderr);
        return 2;
    }
    {
        char *parent = path_parent(g_output_path);
        g_generated_dir = absolute_path(parent);
        free(parent);
    }
    {
        char *absolute_root = absolute_path(g_source_root);
        free(g_source_root);
        g_source_root = absolute_root;
    }
    if (watch) {
#if GEN_ENABLE_WATCH
        unsigned long long last_signature = 0ULL;
        int have_signature = 0;
        for (;;) {
            unsigned long long signature = 0ULL;
            if (!compute_watch_signature(&signature)) {
                (void)run_once(0);
            } else if (!have_signature || signature != last_signature) {
                last_signature = signature;
                have_signature = 1;
                (void)run_once(0);
            }
            sleep_ms((unsigned)GEN_WATCH_INTERVAL_MS);
        }
#else
        fputs("tmplgen: --watch support is disabled in this build\n", stderr);
        return 2;
#endif
    }
    return run_once(check_only);
}
