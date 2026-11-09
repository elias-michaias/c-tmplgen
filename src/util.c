/* Buffers, strings, paths, files, and other low-level utilities. */
#include "internal.h"


void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("tmplgen: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

void buf_init(Buf *b)
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

void buf_putn(Buf *b, const char *s, size_t n)
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

void buf_puts(Buf *b, const char *s)
{
    buf_putn(b, s, strlen(s));
}

void buf_putc(Buf *b, char c)
{
    buf_putn(b, &c, 1U);
}

void buf_put_uint(Buf *b, unsigned long value)
{
    char digits[sizeof(unsigned long) * CHAR_BIT + 1U];
    size_t used = 0U;
    do {
        digits[used++] = (char)('0' + (value % 10UL));
        value /= 10UL;
    } while (value != 0UL);
    while (used > 0U) buf_putc(b, digits[--used]);
}

void buf_free(Buf *b)
{
    free(b->p);
    buf_init(b);
}

static char *g_captured_error_message;
static char *g_captured_error_path;
static size_t g_captured_error_line;
static int g_capture_errors;

void tmplgen_plugin_reset_captured_diagnostic(void)
{
    free(g_captured_error_message);
    free(g_captured_error_path);
    g_captured_error_message = NULL;
    g_captured_error_path = NULL;
    g_captured_error_line = 0U;
    g_capture_errors = 1;
}

const char *tmplgen_plugin_captured_diagnostic_message(void)
{
    return g_captured_error_message != NULL ? g_captured_error_message : "";
}

const char *tmplgen_plugin_captured_diagnostic_path(void)
{
    return g_captured_error_path != NULL ? g_captured_error_path : "";
}

size_t tmplgen_plugin_captured_diagnostic_line(void)
{
    return g_captured_error_line;
}

static void capture_error(const char *path, size_t line, const char *fmt,
                          va_list ap)
{
    va_list copy;
    int length;
    char *message;
    if (!g_capture_errors || g_captured_error_message != NULL) return;
    va_copy(copy, ap);
    length = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (length < 0) return;
    message = (char *)malloc((size_t)length + 1U);
    if (message == NULL) return;
    va_copy(copy, ap);
    (void)vsnprintf(message, (size_t)length + 1U, fmt, copy);
    va_end(copy);
    g_captured_error_path = copy_string(path != NULL ? path : "");
    g_captured_error_line = line;
    g_captured_error_message = message;
}

void report_error(const char *path, size_t line, const char *fmt, ...)
{
    va_list ap;
    ++g_error_count;
    fprintf(stderr, "tmplgen: %s", path != NULL ? path : "");
    if (line != 0U) fprintf(stderr, ":%lu", (unsigned long)line);
    fputs(path != NULL ? ": error: " : "error: ", stderr);
    va_start(ap, fmt);
    capture_error(path, line, fmt, ap);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void report_warning(const char *path, size_t line, const char *fmt, ...)
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

int str_eq(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

int is_ident_start(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

int is_ident_char(char c)
{
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

int valid_identifier(const char *s)
{
    size_t i;
    if (s == NULL || !is_ident_start(s[0])) return 0;
    for (i = 1U; s[i] != '\0'; ++i) {
        if (!is_ident_char(s[i])) return 0;
    }
    return 1;
}

char *copy_range(const char *s, size_t n)
{
    char *p = (char *)malloc(n + 1U);
    if (p == NULL) die("out of memory");
    if (n != 0U) memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

char *copy_string(const char *s)
{
    return copy_range(s, strlen(s));
}

int read_file(const char *path, Buf *out)
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

char *join_path(const char *directory, const char *name)
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

char *absolute_path(const char *path)
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

#if defined(CLANGPLUGIN)
void tmplgen_plugin_clear_unsaved_source(void)
{
    free(g_unsaved_source_path);
    free(g_unsaved_source_contents);
    g_unsaved_source_path = NULL;
    g_unsaved_source_contents = NULL;
    g_unsaved_source_length = 0U;
}
#endif
#if defined(CLANGPLUGIN)

void tmplgen_plugin_set_unsaved_source(const char *path,
                                       const char *contents, size_t length)
{
    char *absolute;
    char *copy;
    if (path == NULL || path[0] == '\0' ||
        (contents == NULL && length != 0U)) {
        tmplgen_plugin_clear_unsaved_source();
        return;
    }
    absolute = absolute_path(path);
    copy = copy_range(contents != NULL ? contents : "", length);
    tmplgen_plugin_clear_unsaved_source();
    g_unsaved_source_path = absolute;
    g_unsaved_source_contents = copy;
    g_unsaved_source_length = length;
}
#endif
#if defined(CLANGPLUGIN) && !defined(_WIN32)
/* In plugin builds the generator lives in the plugin library; its file is the
 * identity the caches key on (without one, every cache is disabled and each
 * clangd rebuild re-analyzes the whole project). */
static const char g_plugin_identity_anchor = 0;

char *plugin_library_path(void)
{
    Dl_info info;
    if (dladdr(&g_plugin_identity_anchor, &info) == 0 || info.dli_fname == NULL ||
        info.dli_fname[0] == '\0') return NULL;
    return absolute_path(info.dli_fname);
}
#endif
#if defined(CLANGPLUGIN) && !defined(_WIN32)
#else
char *plugin_library_path(void)
{
    return NULL;
}
#endif

char *resolve_executable_path(const char *argument_zero)
{
    const char *path_environment;
    size_t offset = 0U;
    if (strchr(argument_zero, '/') != NULL || strchr(argument_zero, '\\') != NULL) {
        return absolute_path(argument_zero);
    }
    path_environment = getenv("PATH");
    if (path_environment == NULL) return NULL;
    while (1) {
        size_t end = offset;
        char *directory;
        char *candidate;
        while (path_environment[end] != '\0' &&
#if defined(_WIN32)
               path_environment[end] != ';'
#else
               path_environment[end] != ':'
#endif
        ) ++end;
        directory = end == offset ? copy_string(".") :
                    copy_range(path_environment + offset, end - offset);
        candidate = join_path(directory, argument_zero);
#if defined(_WIN32)
        {
            DWORD attributes = GetFileAttributesA(candidate);
            if (attributes != INVALID_FILE_ATTRIBUTES &&
                (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U) {
                char *absolute = absolute_path(candidate);
                free(candidate);
                free(directory);
                return absolute;
            }
        }
#else
        {
            struct stat info;
            if (stat(candidate, &info) == 0 && S_ISREG(info.st_mode) &&
                access(candidate, X_OK) == 0) {
                char *absolute = absolute_path(candidate);
                free(candidate);
                free(directory);
                return absolute;
            }
        }
#endif
        free(candidate);
#if defined(_WIN32)
        if (strchr(argument_zero, '.') == NULL) {
            Buf executable;
            buf_init(&executable);
            buf_puts(&executable, argument_zero);
            buf_puts(&executable, ".exe");
            candidate = join_path(directory, executable.p);
            buf_free(&executable);
            if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
                char *absolute = absolute_path(candidate);
                free(candidate);
                free(directory);
                return absolute;
            }
            free(candidate);
        }
#endif
        free(directory);
        if (path_environment[end] == '\0') break;
        offset = end + 1U;
    }
    return NULL;
}

static int source_file_extension(const char *name)
{
    size_t length = strlen(name);
    return (length >= 2U && strcmp(name + length - 2U, ".c") == 0) ||
           (length >= 2U && strcmp(name + length - 2U, ".h") == 0);
}

const char *path_leaf(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash)) slash = backslash;
    return slash != NULL ? slash + 1 : path;
}

int path_is_within_directory(const char *path, const char *directory)
{
    size_t length = strlen(directory);
    if (strncmp(path, directory, length) != 0) return 0;
    if (length != 0U && (directory[length - 1U] == '/' ||
                         directory[length - 1U] == '\\')) return 1;
    return path[length] == '/' || path[length] == '\\';
}

void cache_add_dependency_path(StringSet *dependencies, const char *path)
{
    char *absolute = absolute_path(path);
    int project_input = path_is_within_directory(absolute, g_source_root) ||
                        path_is_within_directory(absolute, g_absolute_template_dir) ||
                        path_is_within_directory(absolute, g_generated_dir);
    if (!project_input) string_set_add(dependencies, absolute);
    free(absolute);
}

static int template_stem_compare(const void *left, const void *right)
{
    const GenTemplate *a = (const GenTemplate *)left;
    const GenTemplate *b = (const GenTemplate *)right;
    return strcmp(a->stem, b->stem);
}

static int template_registry_add(const char *filename)
{
    size_t filename_length = strlen(filename);
    size_t suffix_length = strlen(GEN_TMPL_SUFFIX);
    size_t stem_length;
    size_t i;
    GenTemplate *next;
    size_t new_cap;
    if (filename_length < suffix_length ||
        strcmp(filename + filename_length - suffix_length, GEN_TMPL_SUFFIX) != 0) {
        return 1;
    }
    stem_length = filename_length - suffix_length;
    if (stem_length == 0U) {
        report_error(g_template_dir, 0U, "template filename has an empty stem: %s",
                     filename);
        return 0;
    }
    for (i = 0U; i < g_template_count; ++i) {
        if (strlen(g_templates[i].stem) == stem_length &&
            memcmp(g_templates[i].stem, filename, stem_length) == 0) {
            report_error(g_template_dir, 0U, "duplicate template stem '%s'",
                         g_templates[i].stem);
            return 0;
        }
    }
    if (g_template_count == g_template_cap) {
        new_cap = g_template_cap == 0U ? 8U : g_template_cap * 2U;
        if (new_cap < g_template_cap ||
            new_cap > (size_t)-1 / sizeof(*g_templates)) {
            die("too many template files");
        }
        next = (GenTemplate *)realloc(g_templates, new_cap * sizeof(*g_templates));
        if (next == NULL) die("out of memory");
        g_templates = next;
        g_template_cap = new_cap;
    }
    g_templates[g_template_count].stem = copy_range(filename, stem_length);
    ++g_template_count;
    return 1;
}

int discover_template_files(void)
{
    clear_template_registry();
#if defined(_WIN32)
    char *pattern = join_path(g_template_dir, "*");
    WIN32_FIND_DATAA entry;
    HANDLE handle = FindFirstFileA(pattern, &entry);
    free(pattern);
    if (handle == INVALID_HANDLE_VALUE) {
        report_error(g_template_dir, 0U, "cannot scan template directory");
        return 0;
    }
    do {
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0U &&
            !template_registry_add(entry.cFileName)) {
            (void)FindClose(handle);
            return 0;
        }
    } while (FindNextFileA(handle, &entry) != 0);
    (void)FindClose(handle);
#else
    DIR *directory = opendir(g_template_dir);
    struct dirent *entry;
    if (directory == NULL) {
        /* A reflection-only project needs no templates. */
        if (errno == ENOENT && g_reflect_mode != REFLECT_NONE) return 1;
        report_error(g_template_dir, 0U, "cannot scan template directory: %s",
                     strerror(errno));
        return 0;
    }
    while ((entry = readdir(directory)) != NULL) {
        size_t name_length = strlen(entry->d_name);
        size_t suffix_length = strlen(GEN_TMPL_SUFFIX);
        char *path;
        struct stat info;
        if (name_length < suffix_length ||
            strcmp(entry->d_name + name_length - suffix_length,
                   GEN_TMPL_SUFFIX) != 0) continue;
        path = join_path(g_template_dir, entry->d_name);
        if (stat(path, &info) != 0) {
            report_error(path, 0U, "cannot inspect template file: %s", strerror(errno));
            free(path);
            (void)closedir(directory);
            return 0;
        }
        free(path);
        if (S_ISREG(info.st_mode) && !template_registry_add(entry->d_name)) {
            (void)closedir(directory);
            return 0;
        }
    }
    if (closedir(directory) != 0) {
        report_error(g_template_dir, 0U, "cannot close template directory: %s",
                     strerror(errno));
        return 0;
    }
#endif
    if (g_template_count > 1U) {
        qsort(g_templates, g_template_count, sizeof(*g_templates),
              template_stem_compare);
    }
    return 1;
}

char *path_parent(const char *path)
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

int is_generated_output_file(const char *path)
{
    char *absolute = absolute_path(path);
    char *aggregate = absolute_path(g_output_path);
    size_t generated_dir_length = strlen(g_generated_dir);
    size_t absolute_length = strlen(absolute);
    size_t header_suffix_length = strlen(".gen.h");
    size_t source_suffix_length = strlen(".gen.c");
    int generated_module =
                      (absolute_length >= header_suffix_length &&
                       strcmp(absolute + absolute_length - header_suffix_length,
                              ".gen.h") == 0) ||
                      (absolute_length >= source_suffix_length &&
                       strcmp(absolute + absolute_length - source_suffix_length,
                              ".gen.c") == 0);
    int module_file = generated_module &&
                      strncmp(absolute, g_generated_dir, generated_dir_length) == 0 &&
                      ((generated_dir_length == 1U && g_generated_dir[0] == '/') ||
                       absolute[generated_dir_length] == '/' ||
                       absolute[generated_dir_length] == '\\');
    int generated = str_eq(absolute, aggregate);
    const char *leaf = path_leaf(absolute);
    int reflection_file = (str_eq(leaf, "reflection.h") ||
                           str_eq(leaf, "reflection.gen.c")) &&
        strncmp(absolute, g_generated_dir, generated_dir_length) == 0 &&
        (absolute[generated_dir_length] == '/' ||
         absolute[generated_dir_length] == '\\');
    free(aggregate);
    generated = generated || module_file || reflection_file;
    free(absolute);
    return generated;
}

static int is_generator_source_directory(const char *name, const char *path)
{
    char *header;
    FILE *file;
    char guard[64];
    int matches = 0;
    if (!str_eq(name, "src")) return 0;
    header = join_path(path, "internal.h");
    file = fopen(header, "rb");
    free(header);
    if (file == NULL) return 0;
    if (fgets(guard, (int)sizeof(guard), file) != NULL) {
        matches = strncmp(guard, "#ifndef TMPLGEN_INTERNAL_H", 26U) == 0;
    }
    (void)fclose(file);
    return matches;
}

static int skip_source_directory(const char *name, const char *path)
{
    char *absolute;
    int skip;
    if (name[0] == '.' || str_eq(name, path_leaf(g_template_dir)) ||
        str_eq(name, "build") || str_eq(name, "dist") ||
        is_generator_source_directory(name, path)) return 1;
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
#if defined(CLANGPLUGIN)
    if (g_unsaved_source_path != NULL &&
        str_eq(file->path, g_unsaved_source_path)) {
        if (g_unsaved_source_length != 0U &&
            memchr(g_unsaved_source_contents, '\0',
                   g_unsaved_source_length) != NULL) {
            report_error(file->path, 0U, "source contains a NUL byte");
            source_file_free(file);
            return 0;
        }
        buf_free(&file->text);
        buf_putn(&file->text, g_unsaved_source_contents,
                 g_unsaved_source_length);
    }
#endif
    ++files->count;
    return 1;
}

int scan_source_directory(SourceList *files, const char *directory)
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

void tokenize_source(SourceFile *file)
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

void source_file_free(SourceFile *file)
{
    size_t i;
    free(file->path);
    buf_free(&file->text);
    for (i = 0U; i < file->token_count; ++i) free(file->tokens[i].text);
    free(file->tokens);
    source_facts_free(&file->facts);
    memset(file, 0, sizeof(*file));
}

void source_list_free(SourceList *files)
{
    size_t i;
    for (i = 0U; i < files->count; ++i) source_file_free(&files->items[i]);
    free(files->items);
    memset(files, 0, sizeof(*files));
}

void generated_file_list_add(GeneratedFileList *files, const char *path,
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

void generated_file_list_free(GeneratedFileList *files)
{
    size_t i;
    for (i = 0U; i < files->count; ++i) {
        free(files->items[i].path);
        buf_free(&files->items[i].contents);
    }
    free(files->items);
    memset(files, 0, sizeof(*files));
}

int token_is(const SourceFile *file, size_t index, const char *text)
{
    return index < file->token_count && str_eq(file->tokens[index].text, text);
}

int cache_file_hash(const char *path, unsigned long long *hash1,
                           unsigned long long *hash2)
{
    FILE *file = fopen(path, "rb");
    unsigned char chunk[8192];
    size_t count;
    unsigned long long first = 14695981039346656037ULL;
    unsigned long long second = 7809847782465536322ULL;
    if (file == NULL) return 0;
    while ((count = fread(chunk, 1U, sizeof(chunk), file)) != 0U) {
        size_t i;
        for (i = 0U; i < count; ++i) {
            unsigned long long byte = (unsigned long long)chunk[i];
            first = (first ^ byte) * 1099511628211ULL;
            second ^= byte + 0x9e3779b97f4a7c15ULL + (second << 6U) +
                     (second >> 2U);
            second *= 0xbf58476d1ce4e5b9ULL;
        }
    }
    {
        int failed = ferror(file);
        if (fclose(file) != 0) failed = 1;
        if (failed) return 0;
    }
    *hash1 = first;
    *hash2 = second;
    return 1;
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

int ensure_parent_directories(const char *path)
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

char *normalize_spaces(const char *s)
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

const char *next_word(const char **p)
{
    const char *start;
    while (**p != '\0' && isspace((unsigned char)**p)) ++*p;
    if (**p == '\0') return NULL;
    start = *p;
    while (**p != '\0' && !isspace((unsigned char)**p)) ++*p;
    return start;
}

char *word_copy(const char *start, const char *end)
{
    if (start == NULL || end == NULL) return NULL;
    return copy_range(start, (size_t)(end - start));
}

int list_contains(char **items, size_t count, const char *s)
{
    size_t i;
    for (i = 0U; i < count; ++i) {
        if (str_eq(items[i], s)) return 1;
    }
    return 0;
}

void list_add(char ***items, size_t *count, size_t *cap, const char *s)
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

void list_free(char **items, size_t count)
{
    size_t i;
    for (i = 0U; i < count; ++i) free(items[i]);
    free(items);
}

char *copy_cx_string(CXString value)
{
    const char *text = clang_getCString(value);
    char *copy = copy_string(text != NULL ? text : "");
    clang_disposeString(value);
    return copy;
}
