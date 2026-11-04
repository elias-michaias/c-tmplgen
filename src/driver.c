/* Top-level generation, command line, watch mode, and entry points. */
#include "internal.h"


/* ========================== GENERATION ===================== */

static int config_validate(void)
{
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
    return 1;
}

void build_template_path(Buf *path, const char *stem)
{
    buf_puts(path, g_template_dir);
    buf_putc(path, '/');
    buf_puts(path, stem);
    buf_puts(path, GEN_TMPL_SUFFIX);
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
    clear_requirement_state();
    string_set_free(&g_clang_dependencies);
    g_error_count = 0U;
    buf_free(&g_reflection_bootstrap);
    if (g_reflect_mode != REFLECT_NONE) emit_reflection_bootstrap(&g_reflection_bootstrap);
    if (!discover_template_files()) return 0;
    if (!config_validate()) return 0;
    infos = (TemplateInfo *)calloc(gen_template_count() + 1U, sizeof(*infos));
    if (infos == NULL) die("out of memory");
    g_all_template_infos = infos;
    g_all_template_info_count = gen_template_count();
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
        build_template_path(&path, g_templates[i].stem);
        if (!parse_template(&infos[i], path.p)) {
            buf_free(&path);
            goto failed;
        }
        if (!analyze_template(&infos[i])) {
            buf_free(&path);
            goto failed;
        }

        buf_free(&path);
        module_path = generated_module_path(g_templates[i].stem);
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
        guard = generated_module_guard(g_templates[i].stem);
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
    if (!validate_structural_constraints(infos, gen_template_count())) goto failed;
    if (g_error_count != 0U) goto failed;
    if (!compute_instance_requirements() || g_error_count != 0U) goto failed;
    if (g_reflect_mode == REFLECT_NONE) {
        /* Specializations of a template that uses reflection wait for
         * reflection.h, which this mode does not write. */
        for (i = 0U; i < g_instance_count; ++i) {
            if (!template_uses_reflection(&infos[g_instances[i].template_index])) continue;
            report_error(g_instances[i].origin_path, g_instances[i].origin_line,
                         "%s needs a specialization of %s, which includes "
                         "gen/reflection.h; generate with --reflect=project",
                         g_instances[i].name, infos[g_instances[i].template_index].path);
            goto failed;
        }
    }
    for (i = 0U; i < gen_template_count(); ++i) {
        Buf module;
        Buf implementation;
        char *path = generated_module_path(g_templates[i].stem);
        char *guard = generated_module_guard(g_templates[i].stem);
        buf_init(&module);
        buf_init(&implementation);
        emit_template_module(&module, i, &infos[i], g_templates[i].stem,
                             path, guard,
                             g_compiled_output ? &implementation : NULL);
        generated_file_list_add(out, path, &module);
        buf_free(&module);
        if (g_compiled_output) {
            char *source_path = generated_module_source_path(g_templates[i].stem);
            generated_file_list_add(out, source_path, &implementation);
            free(source_path);
        }
        buf_free(&implementation);
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
    if (g_reflect_mode != REFLECT_NONE && !reflect_generate(out)) goto failed;
    string_set_free(&emitted_guards);
    source_list_free(&source_files);
    for (i = 0U; i < gen_template_count(); ++i) template_info_free(&infos[i]);
    free(infos);
    g_all_template_infos = NULL;
    g_all_template_info_count = 0U;
    clear_discovery_state();
    clear_requirement_state();
    return 1;

failed:
    string_set_free(&emitted_guards);
    source_list_free(&source_files);
    for (i = 0U; i < gen_template_count(); ++i) template_info_free(&infos[i]);
    free(infos);
    g_all_template_infos = NULL;
    g_all_template_info_count = 0U;
    clear_discovery_state();
    clear_requirement_state();
    return 0;
}

/* ============================= CLI ========================= */

static void usage(FILE *out)
{
    fprintf(out,
            "Usage: tmplgen [options] [--clang-arg ARG ...]\n"
            "       tmplgen --help\n\n"
            "Generate %s from templates and Clang type/macro-usage data.\n"
            "  --check    compare generated contents without writing\n"
            "  --watch    regenerate when sources or templates change (poll %u ms)\n"
            "  --no-cache disable the unchanged-input generation cache\n"
            "  --compiled emit function bodies in per-template .gen.c files\n"
            "  --verbose  report reads and writes to stderr\n"
            "  --source-root DIR   recursively scan this project tree\n"
            "  --template-dir DIR  discover *.tmpl.h templates in this directory\n"
            "  --output FILE       write the aggregate header here (modules beside it)\n"
            "  --dispatch MODE     auto, generic, gnu, or none\n"
            "  --clang-arg ARG     pass one argument to libclang (repeatable)\n"
            "  --line-path-base DIR  write template #line paths relative to DIR\n"
            "  --reflect=MODE      none (default) or project: also write gen/reflection.h\n"
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

static void reset_invocation_state(void)
{
    size_t i;
    free(g_source_root);
    free(g_template_dir);
    free(g_absolute_template_dir);
    free(g_output_path);
    free(g_generated_dir);
    free(g_line_path_base);
    free(g_executable_path);
    g_source_root = NULL;
    g_template_dir = NULL;
    g_absolute_template_dir = NULL;
    g_output_path = NULL;
    g_generated_dir = NULL;
    g_line_path_base = NULL;
    g_executable_path = NULL;
    for (i = 0U; i < g_clang_arg_count; ++i) free(g_clang_args[i]);
    free(g_clang_args);
    g_clang_args = NULL;
    g_clang_arg_count = 0U;
    g_clang_arg_cap = 0U;
    string_set_free(&g_clang_dependencies);
    clear_template_registry();
    g_error_count = 0U;
    g_verbose = 0;
    g_compiled_output = 0;
    g_reflect_mode = REFLECT_NONE;
    buf_free(&g_reflection_bootstrap);
}

/* Sources are parsed with the generated headers replaced by an in-memory
 * bootstrap, but libclang only applies such an override to a file that exists:
 * `#include "../gen/all.gen.h"` is resolved on disk first. Without the files,
 * a first run sees no generated API at all, emits incomplete output, and the
 * unchanged-input cache then keeps returning it. Create empty placeholders for
 * missing outputs before parsing; `created` records the ones made here. */
static int ensure_bootstrap_placeholder(const char *path, StringSet *created)
{
    FILE *file = fopen(path, "rb");
    if (file != NULL) {
        (void)fclose(file);
        return 1;
    }
    if (!ensure_parent_directories(path)) return 0;
    file = fopen(path, "ab");
    if (file == NULL) {
        report_error(path, 0U, "cannot create bootstrap placeholder: %s",
                     strerror(errno));
        return 0;
    }
    if (fclose(file) != 0) {
        report_error(path, 0U, "cannot close bootstrap placeholder: %s",
                     strerror(errno));
        return 0;
    }
    string_set_add(created, path);
    return 1;
}

static int ensure_bootstrap_placeholders(StringSet *created)
{
    OutputRegistry registry;
    size_t i;
    int okay = 1;
    if (!discover_template_files()) return 0;
    output_registry_build(&registry);
    for (i = 0U; i < registry.count && okay; ++i) {
        if (output_is_header(registry.items[i].kind)) {
            okay = ensure_bootstrap_placeholder(registry.items[i].path, created);
        }
    }
    output_registry_free(&registry);
    return okay;
}

/* After a failed run, drop placeholders that are still empty so a broken
 * generation does not leave misleading empty headers behind. The editor
 * plugins keep them: an empty header beats a missing one for a live parse. */
static void remove_unused_placeholders(const StringSet *created)
{
    size_t i;
    if (TMPLGEN_PLUGIN_BUILD) return;
    for (i = 0U; i < created->count; ++i) {
        FILE *file = fopen(created->items[i], "rb");
        int empty = 0;
        if (file != NULL) {
            empty = fgetc(file) == EOF;
            (void)fclose(file);
        }
        if (empty) (void)remove(created->items[i]);
    }
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

static int directory_has_file(const char *directory, const char *relative)
{
    char *path = join_path(directory, relative);
    FILE *file = fopen(path, "rb");
    free(path);
    if (file == NULL) return 0;
    (void)fclose(file);
    return 1;
}

/* Some libclang installs cannot locate their own builtin headers (stddef.h,
 * stdarg.h, ...). The generator used to parse such files with errors that
 * KeepGoing hid; the reflection parse must be complete. Unless the caller
 * passes -resource-dir, look for the matching install's headers. */
static void add_default_resource_dir(void)
{
    static const char *const prefixes[] = {
        "/usr/lib/clang/", "/usr/lib64/clang/", "/usr/local/lib/clang/",
        "/opt/homebrew/opt/llvm/lib/clang/", "/usr/local/opt/llvm/lib/clang/"
    };
    CXString version;
    const char *text;
    const char *digits;
    char major[16];
    size_t i, length = 0U;
    for (i = 0U; i < g_clang_arg_count; ++i) {
        if (strncmp(g_clang_args[i], "-resource-dir", 13U) == 0) return;
    }
    version = clang_getClangVersion();
    text = clang_getCString(version);
    digits = text != NULL ? strstr(text, "version ") : NULL;
    if (digits != NULL) {
        digits += 8;
        while (isdigit((unsigned char)digits[length]) && length + 1U < sizeof(major)) {
            major[length] = digits[length];
            ++length;
        }
    }
    major[length] = '\0';
    clang_disposeString(version);
    if (length == 0U) return;
    for (i = 0U; i < sizeof(prefixes) / sizeof(prefixes[0]) + 1U; ++i) {
        Buf candidate;
        buf_init(&candidate);
        if (i < sizeof(prefixes) / sizeof(prefixes[0])) {
            buf_puts(&candidate, prefixes[i]);
            buf_puts(&candidate, major);
        } else {
            buf_puts(&candidate, "/usr/lib/llvm-");
            buf_puts(&candidate, major);
            buf_puts(&candidate, "/lib/clang/");
            buf_puts(&candidate, major);
        }
        if (directory_has_file(candidate.p, "include/stddef.h")) {
            Buf argument;
            buf_init(&argument);
            buf_puts(&argument, "-resource-dir=");
            buf_puts(&argument, candidate.p);
            add_clang_argument(argument.p);
            buf_free(&argument);
            buf_free(&candidate);
            return;
        }
        buf_free(&candidate);
    }
}

static int parse_dispatch_backend(const char *name)
{
    if (str_eq(name, "none")) return GEN_DISPATCH_NONE;
    if (str_eq(name, "generic")) return GEN_DISPATCH_GENERIC;
    if (str_eq(name, "gnu")) return GEN_DISPATCH_GNU;
    if (str_eq(name, "auto")) return GEN_DISPATCH_AUTO;
    return -1;
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

static int run_once(int check_only)
{
    GeneratedFileList files;
    unsigned long long signature = 0ULL;
    unsigned long long signature_after = 0ULL;
    int result;
    int exists;
    int changed;
    int signature_ready = 0;
    size_t i;
    int status = 0;
    StringSet placeholders;
    memset(&files, 0, sizeof(files));
    memset(&placeholders, 0, sizeof(placeholders));
    if (!check_only && !ensure_bootstrap_placeholders(&placeholders)) {
        remove_unused_placeholders(&placeholders);
        string_set_free(&placeholders);
        return 1;
    }
    if (g_cache_enabled) {
        g_error_count = 0U;
        if (discover_template_files() && compute_input_signature(&signature) &&
            g_cache_enabled) {
            signature_ready = 1;
            if (cache_try_hit(signature, check_only)) {
                string_set_free(&placeholders);
                return 0;
            }
        }
    }
    if (!generate(&files)) {
        generated_file_list_free(&files);
        remove_unused_placeholders(&placeholders);
        string_set_free(&placeholders);
        return 1;
    }
    for (i = 0U; i < files.count; ++i) {
        GeneratedFile *file = &files.items[i];
        if (check_only) {
            result = file_matches(file->path, &file->contents, &exists);
            if (result < 0) {
                status = 1;
            } else if (result == 0) {
                if (!TMPLGEN_PLUGIN_BUILD) printf("%s: differs\n", file->path);
                status = 1;
            } else {
                if (!TMPLGEN_PLUGIN_BUILD) printf("%s: matches\n", file->path);
            }
        } else {
            if (!write_file_if_changed(file->path, &file->contents, &changed)) {
                status = 1;
            } else {
                if (!TMPLGEN_PLUGIN_BUILD) {
                    printf("%s: %s\n", file->path,
                           changed ? "written" : "unchanged");
                }
            }
        }
    }
    if (status == 0 && g_cache_enabled && signature_ready &&
        compute_input_signature(&signature_after) && signature_after == signature) {
        if (!cache_write_manifest(signature, &files) && g_verbose) {
            fputs("tmplgen: could not update unchanged-input cache\n", stderr);
        }
    }
    generated_file_list_free(&files);
    if (status != 0) remove_unused_placeholders(&placeholders);
    string_set_free(&placeholders);
    return status;
}

static int tmplgen_main(int argc, char **argv)
{
    int check_only = 0;
    int watch = 0;
    int i;
    reset_invocation_state();
    g_source_root = copy_string(GEN_SCAN_ROOT);
    g_template_dir = copy_string(GEN_TMPL_DIR);
    g_output_path = copy_string(GEN_OUTPUT);
    g_dispatch_backend = GEN_DISPATCH;
    g_emit_line = GEN_EMIT_LINE;
    g_cache_enabled = 1;
    g_tu_cache_enabled = 1;
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
        } else if (str_eq(argv[i], "--no-cache")) {
            g_cache_enabled = 0;
            g_tu_cache_enabled = 0;
        } else if (str_eq(argv[i], "--compiled")) {
            g_compiled_output = 1;
        } else if (str_eq(argv[i], "--source-root") ||
                   str_eq(argv[i], "--template-dir") ||
                   str_eq(argv[i], "--output") ||
                   str_eq(argv[i], "--dispatch") ||
                   str_eq(argv[i], "--line-path-base") ||
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
            else if (str_eq(option, "--line-path-base")) replace_option_string(&g_line_path_base, value);
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
        } else if (strncmp(argv[i], "--reflect=", 10U) == 0 ||
                   str_eq(argv[i], "--reflect")) {
            const char *mode = argv[i][9] == '=' ? argv[i] + 10U : NULL;
            if (mode == NULL) {
                if (i + 1 >= argc) {
                    fputs("tmplgen: --reflect requires a value\n", stderr);
                    return 2;
                }
                mode = argv[++i];
            }
            if (str_eq(mode, "none")) g_reflect_mode = REFLECT_NONE;
            else if (str_eq(mode, "project")) g_reflect_mode = REFLECT_PROJECT;
            else {
                fprintf(stderr, "tmplgen: unsupported reflection mode '%s'\n", mode);
                return 2;
            }
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
    if (!TMPLGEN_PLUGIN_BUILD) {
        g_executable_path = resolve_executable_path(argv[0]);
    } else {
        g_executable_path = plugin_library_path();
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
    g_absolute_template_dir = absolute_path(g_template_dir);
    add_default_resource_dir();
    if (g_line_path_base != NULL) {
        char *absolute_base = absolute_path(g_line_path_base);
        free(g_line_path_base);
        g_line_path_base = absolute_base;
    }
    if (watch) {
        g_cache_enabled = 0;
#if GEN_ENABLE_WATCH
        unsigned long long last_signature = 0ULL;
        int have_signature = 0;
        for (;;) {
            unsigned long long signature = 0ULL;
            if (!compute_input_signature(&signature)) {
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

#if defined(CLANGPLUGIN)
int tmplgen_plugin_main(int argc, char **argv)
{
    return tmplgen_main(argc, argv);
}
#endif
#if defined(CLANGPLUGIN)
#else
int main(int argc, char **argv)
{
    return tmplgen_main(argc, argv);
}
#endif
