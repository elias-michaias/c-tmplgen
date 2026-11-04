/* Input signatures, inclusion tracking, and persistent caches. */
#include "internal.h"


void cache_collect_inclusion(CXFile included_file,
                                    CXSourceLocation *inclusion_stack,
                                    unsigned include_length,
                                    CXClientData client_data)
{
    CXString filename;
    const char *raw_path;
    (void)inclusion_stack;
    (void)include_length;
    if (included_file == NULL) return;
    filename = clang_getFileName(included_file);
    raw_path = clang_getCString(filename);
    if (raw_path != NULL && raw_path[0] != '\0') {
        cache_add_dependency_path((StringSet *)client_data, raw_path);
    }
    clang_disposeString(filename);
}

void cache_collect_source_inclusion(CXFile included_file,
                                           CXSourceLocation *inclusion_stack,
                                           unsigned include_length,
                                           CXClientData client_data)
{
    InclusionCapture *capture = (InclusionCapture *)client_data;
    CXString filename;
    const char *raw_path;
    char *absolute;
    (void)inclusion_stack;
    (void)include_length;
    if (included_file == NULL) return;
    filename = clang_getFileName(included_file);
    raw_path = clang_getCString(filename);
    if (raw_path == NULL || raw_path[0] == '\0') {
        clang_disposeString(filename);
        return;
    }
    absolute = absolute_path(raw_path);
    if (!path_is_within_directory(absolute, g_generated_dir)) {
        if (capture->local != NULL) string_set_add(capture->local, absolute);
        if (capture->all != NULL) {
            cache_add_dependency_path(capture->all, absolute);
        }
    }
    free(absolute);
    clang_disposeString(filename);
}

void clear_template_registry(void)
{
    size_t i;
    for (i = 0U; i < g_template_count; ++i) free(g_templates[i].stem);
    free(g_templates);
    g_templates = NULL;
    g_template_count = 0U;
    g_template_cap = 0U;
}

static void cache_record_list_free(CacheRecordList *records)
{
    size_t i;
    for (i = 0U; i < records->count; ++i) free(records->items[i].path);
    free(records->items);
    memset(records, 0, sizeof(*records));
}

static void cache_record_list_add(CacheRecordList *records, const char *path,
                                  unsigned long long hash1,
                                  unsigned long long hash2)
{
    CacheRecord *next;
    size_t new_cap;
    if (records->count == records->cap) {
        new_cap = records->cap == 0U ? 16U : records->cap * 2U;
        if (new_cap < records->cap ||
            new_cap > (size_t)-1 / sizeof(*records->items)) {
            die("too many cache records");
        }
        next = (CacheRecord *)realloc(records->items,
                                      new_cap * sizeof(*records->items));
        if (next == NULL) die("out of memory");
        records->items = next;
        records->cap = new_cap;
    }
    records->items[records->count].path = copy_string(path);
    records->items[records->count].hash1 = hash1;
    records->items[records->count].hash2 = hash2;
    ++records->count;
}

void signature_mix(unsigned long long *hash, const char *data, size_t length)
{
    size_t i;
    for (i = 0U; i < length; ++i) {
        *hash ^= (unsigned long long)(unsigned char)data[i];
        *hash *= 1099511628211ULL;
    }
    *hash ^= (unsigned long long)length;
    *hash *= 1099511628211ULL;
}

int source_file_path_compare(const void *left, const void *right)
{
    const SourceFile *a = (const SourceFile *)left;
    const SourceFile *b = (const SourceFile *)right;
    return strcmp(a->path, b->path);
}

static int directory_exists(const char *path)
{
#if defined(_WIN32)
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0U;
#else
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

int compute_input_signature(unsigned long long *signature)
{
    SourceList files;
    char *source_root = absolute_path(g_source_root);
    char *template_root = copy_string(g_absolute_template_dir);
    char *working_directory = absolute_path(".");
    unsigned long long hash = 14695981039346656037ULL;
    char config[128];
    CXString clang_version;
    char *clang_version_text;
    unsigned long long executable_hash1, executable_hash2;
    size_t i;

    memset(&files, 0, sizeof(files));
    if (!scan_source_directory(&files, source_root)) {
        free(source_root);
        free(template_root);
        free(working_directory);
        source_list_free(&files);
        return 0;
    }
    if (!str_eq(source_root, template_root) &&
        !(g_reflect_mode != REFLECT_NONE && !directory_exists(template_root)) &&
        !scan_source_directory(&files, template_root)) {
        free(source_root);
        free(template_root);
        free(working_directory);
        source_list_free(&files);
        return 0;
    }
    if (files.count > 1U) {
        qsort(files.items, files.count, sizeof(*files.items), source_file_path_compare);
    }
    signature_mix(&hash, source_root, strlen(source_root));
    signature_mix(&hash, template_root, strlen(template_root));
    signature_mix(&hash, working_directory, strlen(working_directory));
    signature_mix(&hash, g_output_path, strlen(g_output_path));
    signature_mix(&hash, g_generated_dir, strlen(g_generated_dir));
    if (g_line_path_base != NULL) {
        signature_mix(&hash, g_line_path_base, strlen(g_line_path_base));
    }
    signature_mix(&hash, GEN_GUARD, strlen(GEN_GUARD));
    signature_mix(&hash, GEN_LSP_GUARD, strlen(GEN_LSP_GUARD));
    signature_mix(&hash, GEN_TMPL_SUFFIX, strlen(GEN_TMPL_SUFFIX));
    signature_mix(&hash, GEN_PREAMBLE, strlen(GEN_PREAMBLE));
    (void)snprintf(config, sizeof(config), "%d:%d:%d:%d:%d",
                   g_dispatch_backend, g_emit_line, g_compiled_output,
                   GEN_ENABLE_WATCH, g_reflect_mode);
    signature_mix(&hash, config, strlen(config));
    signature_mix(&hash, __DATE__, strlen(__DATE__));
    signature_mix(&hash, __TIME__, strlen(__TIME__));
    if (g_executable_path != NULL &&
        cache_file_hash(g_executable_path, &executable_hash1, &executable_hash2)) {
        signature_mix(&hash, g_executable_path, strlen(g_executable_path));
        (void)snprintf(config, sizeof(config), "%016llx:%016llx",
                       executable_hash1, executable_hash2);
        signature_mix(&hash, config, strlen(config));
    } else if (g_cache_enabled) {
        /* Without a stable binary identity, conservatively disable the cache. */
        g_cache_enabled = 0;
        g_tu_cache_enabled = 0;
    }
    for (i = 0U; i < g_clang_arg_count; ++i) {
        signature_mix(&hash, g_clang_args[i], strlen(g_clang_args[i]));
    }
    {
        static const char *const clang_environment[] = {
            "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH",
            "OBJC_INCLUDE_PATH", "SDKROOT", "INCLUDE"
        };
        size_t environment_index;
        for (environment_index = 0U;
             environment_index < sizeof(clang_environment) /
                                     sizeof(clang_environment[0]);
             ++environment_index) {
            const char *value = getenv(clang_environment[environment_index]);
            signature_mix(&hash, clang_environment[environment_index],
                          strlen(clang_environment[environment_index]));
            if (value != NULL) signature_mix(&hash, value, strlen(value));
        }
    }
    clang_version = clang_getClangVersion();
    clang_version_text = copy_cx_string(clang_version);
    signature_mix(&hash, clang_version_text, strlen(clang_version_text));
    free(clang_version_text);
    for (i = 0U; i < files.count; ++i) {
        signature_mix(&hash, files.items[i].path, strlen(files.items[i].path));
        signature_mix(&hash, files.items[i].text.p, files.items[i].text.len);
    }
    *signature = hash;
    free(source_root);
    free(template_root);
    free(working_directory);
    source_list_free(&files);
    return 1;
}

static int cache_string_compare(const void *left, const void *right)
{
    const char *const *a = (const char *const *)left;
    const char *const *b = (const char *const *)right;
    return strcmp(*a, *b);
}

static void cache_manifest_free(CacheManifest *manifest)
{
    cache_record_list_free(&manifest->dependencies);
    cache_record_list_free(&manifest->outputs);
    memset(manifest, 0, sizeof(*manifest));
}

static int cache_hex_value(int character)
{
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

static int cache_read_record(FILE *file, CacheRecordList *records)
{
    unsigned long path_length;
    unsigned long long hash1, hash2;
    char *path;
    unsigned long i;
    if (fscanf(file, "%16llx %16llx %lu\n", &hash1, &hash2, &path_length) != 3 ||
        path_length == 0UL || path_length > 1048576UL) return 0;
    path = (char *)malloc((size_t)path_length + 1U);
    if (path == NULL) die("out of memory");
    for (i = 0UL; i < path_length; ++i) {
        int high = cache_hex_value(fgetc(file));
        int low = cache_hex_value(fgetc(file));
        if (high < 0 || low < 0) {
            free(path);
            return 0;
        }
        path[i] = (char)((high << 4) | low);
    }
    if (fgetc(file) != '\n') {
        free(path);
        return 0;
    }
    path[path_length] = '\0';
    cache_record_list_add(records, path, hash1, hash2);
    free(path);
    return 1;
}

static int cache_read_manifest(const char *path, CacheManifest *manifest)
{
    FILE *file = fopen(path, "rb");
    char header[40];
    unsigned long dependency_count, output_count, i;
    int okay = 0;
    memset(manifest, 0, sizeof(*manifest));
    if (file == NULL) return 0;
    if (fgets(header, sizeof(header), file) == NULL ||
        !str_eq(header, "TMPLGEN-CACHE-1\n") ||
        fscanf(file, "%16llx\n", &manifest->signature) != 1 ||
        fscanf(file, "%lu %lu\n", &dependency_count, &output_count) != 2 ||
        dependency_count > 1000000UL || output_count > 1000000UL) goto done;
    for (i = 0UL; i < dependency_count; ++i) {
        if (!cache_read_record(file, &manifest->dependencies)) goto done;
    }
    for (i = 0UL; i < output_count; ++i) {
        if (!cache_read_record(file, &manifest->outputs)) goto done;
    }
    if (fgetc(file) == EOF && !ferror(file)) okay = 1;
done:
    (void)fclose(file);
    if (!okay) cache_manifest_free(manifest);
    return okay;
}

static int cache_write_hashed_record(FILE *file, const CacheRecord *record)
{
    size_t i;
    if (fprintf(file, "%016llx %016llx %lu\n", record->hash1,
                record->hash2, (unsigned long)strlen(record->path)) < 0) return 0;
    for (i = 0U; record->path[i] != '\0'; ++i) {
        if (fprintf(file, "%02x", (unsigned int)(unsigned char)record->path[i]) < 0) {
            return 0;
        }
    }
    return fputc('\n', file) != EOF;
}

static int cache_write_record(FILE *file, const char *path)
{
    unsigned long long hash1, hash2;
    CacheRecord record;
    if (path == NULL || !cache_file_hash(path, &hash1, &hash2)) return 0;
    record.path = (char *)path;
    record.hash1 = hash1;
    record.hash2 = hash2;
    return cache_write_hashed_record(file, &record);
}

static char *cache_manifest_path(void)
{
    return join_path(g_generated_dir, ".tmplgen.cache");
}

static void cache_expected_outputs(StringSet *outputs)
{
    OutputRegistry registry;
    size_t i;
    output_registry_build(&registry);
    for (i = 0U; i < registry.count; ++i) {
        string_set_add(outputs, registry.items[i].path);
    }
    output_registry_free(&registry);
}

int cache_try_hit(unsigned long long signature, int check_only)
{
    CacheManifest manifest;
    StringSet expected_outputs;
    char *path = cache_manifest_path();
    size_t i;
    int okay = 0;
    memset(&expected_outputs, 0, sizeof(expected_outputs));
    if (!cache_read_manifest(path, &manifest)) {
        free(path);
        return 0;
    }
    free(path);
    cache_expected_outputs(&expected_outputs);
    if (manifest.signature != signature ||
        manifest.outputs.count != expected_outputs.count) goto done;
    for (i = 0U; i < manifest.dependencies.count; ++i) {
        CacheRecord *record = &manifest.dependencies.items[i];
        unsigned long long hash1, hash2;
        if (!cache_file_hash(record->path, &hash1, &hash2) ||
            hash1 != record->hash1 || hash2 != record->hash2) goto done;
    }
    for (i = 0U; i < expected_outputs.count; ++i) {
        CacheRecord *record = &manifest.outputs.items[i];
        unsigned long long hash1, hash2;
        if (!str_eq(record->path, expected_outputs.items[i]) ||
            !cache_file_hash(record->path, &hash1, &hash2) ||
            hash1 != record->hash1 || hash2 != record->hash2) goto done;
    }
    for (i = 0U; i < expected_outputs.count; ++i) {
        if (!TMPLGEN_PLUGIN_BUILD) {
            printf("%s: %s\n", expected_outputs.items[i],
                   check_only ? "matches" : "unchanged");
        }
    }
    if (g_verbose) fprintf(stderr, "tmplgen: unchanged-input cache hit\n");
    okay = 1;
done:
    string_set_free(&expected_outputs);
    cache_manifest_free(&manifest);
    return okay;
}

int cache_write_manifest(unsigned long long signature,
                                const GeneratedFileList *outputs)
{
    CacheRecordList dependencies;
    char *path = cache_manifest_path();
    FILE *file;
    size_t i;
    int okay = 0;
    memset(&dependencies, 0, sizeof(dependencies));
    if (g_clang_dependencies.count > 1U) {
        qsort(g_clang_dependencies.items, g_clang_dependencies.count,
              sizeof(*g_clang_dependencies.items), cache_string_compare);
    }
    for (i = 0U; i < g_clang_dependencies.count; ++i) {
        unsigned long long hash1, hash2;
        if (!cache_file_hash(g_clang_dependencies.items[i], &hash1, &hash2)) {
            cache_record_list_free(&dependencies);
            free(path);
            return 0;
        }
        cache_record_list_add(&dependencies, g_clang_dependencies.items[i],
                              hash1, hash2);
    }
    if (!ensure_parent_directories(path)) {
        cache_record_list_free(&dependencies);
        free(path);
        return 0;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        cache_record_list_free(&dependencies);
        free(path);
        return 0;
    }
    if (fprintf(file, "TMPLGEN-CACHE-1\n%016llx\n%lu %lu\n", signature,
                (unsigned long)dependencies.count,
                (unsigned long)outputs->count) < 0) goto done;
    for (i = 0U; i < dependencies.count; ++i) {
        CacheRecord *record = &dependencies.items[i];
        size_t byte_index;
        if (fprintf(file, "%016llx %016llx %lu\n", record->hash1,
                    record->hash2, (unsigned long)strlen(record->path)) < 0) goto done;
        for (byte_index = 0U; record->path[byte_index] != '\0'; ++byte_index) {
            if (fprintf(file, "%02x",
                        (unsigned int)(unsigned char)record->path[byte_index]) < 0) {
                goto done;
            }
        }
        if (fputc('\n', file) == EOF) goto done;
    }
    for (i = 0U; i < outputs->count; ++i) {
        if (!cache_write_record(file, outputs->items[i].path)) goto done;
    }
    okay = 1;
done:
    if (fclose(file) != 0) okay = 0;
    if (!okay) (void)remove(path);
    else if (g_verbose) fprintf(stderr, "tmplgen: updated cache %s\n", path);
    cache_record_list_free(&dependencies);
    free(path);
    return okay;
}

unsigned long long source_facts_key(const SourceFile *source,
                                           const Buf *bootstrap, int pass)
{
    unsigned long long hash = 14695981039346656037ULL;
    unsigned long long binary_hash1, binary_hash2;
    CXString clang_version;
    char *clang_version_text;
    char *working_directory = absolute_path(".");
    char config[128];
    size_t i;
    signature_mix(&hash, source->path, strlen(source->path));
    signature_mix(&hash, source->text.p != NULL ? source->text.p : "",
                  source->text.len);
    signature_mix(&hash, bootstrap->p != NULL ? bootstrap->p : "",
                  bootstrap->len);
    signature_mix(&hash, g_source_root, strlen(g_source_root));
    signature_mix(&hash, g_absolute_template_dir,
                  strlen(g_absolute_template_dir));
    signature_mix(&hash, working_directory, strlen(working_directory));
    signature_mix(&hash, g_output_path, strlen(g_output_path));
    signature_mix(&hash, g_generated_dir, strlen(g_generated_dir));
    if (g_line_path_base != NULL) {
        signature_mix(&hash, g_line_path_base, strlen(g_line_path_base));
    }
    (void)snprintf(config, sizeof(config), "%d:%d:%d:%d:%d", pass,
                   g_dispatch_backend, g_emit_line, g_compiled_output,
                   g_reflect_mode);
    signature_mix(&hash, config, strlen(config));
    signature_mix(&hash, __DATE__, strlen(__DATE__));
    signature_mix(&hash, __TIME__, strlen(__TIME__));
    for (i = 0U; i < g_clang_arg_count; ++i) {
        signature_mix(&hash, g_clang_args[i], strlen(g_clang_args[i]));
    }
    {
        static const char *const clang_environment[] = {
            "CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH",
            "OBJC_INCLUDE_PATH", "SDKROOT", "INCLUDE"
        };
        size_t environment_index;
        for (environment_index = 0U;
             environment_index < sizeof(clang_environment) /
                                     sizeof(clang_environment[0]);
             ++environment_index) {
            const char *value = getenv(clang_environment[environment_index]);
            signature_mix(&hash, clang_environment[environment_index],
                          strlen(clang_environment[environment_index]));
            if (value != NULL) signature_mix(&hash, value, strlen(value));
        }
    }
    clang_version = clang_getClangVersion();
    clang_version_text = copy_cx_string(clang_version);
    signature_mix(&hash, clang_version_text, strlen(clang_version_text));
    free(clang_version_text);
    if (g_executable_path != NULL &&
        cache_file_hash(g_executable_path, &binary_hash1, &binary_hash2)) {
        signature_mix(&hash, g_executable_path, strlen(g_executable_path));
        (void)snprintf(config, sizeof(config), "%016llx:%016llx",
                       binary_hash1, binary_hash2);
        signature_mix(&hash, config, strlen(config));
    }
    free(working_directory);
    return hash;
}

static char *source_facts_cache_path(const char *source_path)
{
    unsigned long long hash1 = 14695981039346656037ULL;
    unsigned long long hash2 = 7809847782465536322ULL;
    size_t i;
    char filename[96];
    for (i = 0U; source_path[i] != '\0'; ++i) {
        unsigned long long byte = (unsigned long long)(unsigned char)source_path[i];
        hash1 = (hash1 ^ byte) * 1099511628211ULL;
        hash2 ^= byte + 0x9e3779b97f4a7c15ULL + (hash2 << 6U) + (hash2 >> 2U);
        hash2 *= 0xbf58476d1ce4e5b9ULL;
    }
    (void)snprintf(filename, sizeof(filename),
                   ".tmplgen-facts-%016llx%016llx.cache", hash1, hash2);
    return join_path(g_generated_dir, filename);
}

static int source_facts_write_string(FILE *file, const char *value)
{
    size_t i;
    size_t length = strlen(value != NULL ? value : "");
    if (fprintf(file, "%lu\n", (unsigned long)length) < 0) return 0;
    value = value != NULL ? value : "";
    for (i = 0U; i < length; ++i) {
        if (fprintf(file, "%02x", (unsigned int)(unsigned char)value[i]) < 0) {
            return 0;
        }
    }
    return fputc('\n', file) != EOF;
}

static int source_facts_read_string(FILE *file, char **value)
{
    unsigned long length;
    unsigned long i;
    char *decoded;
    if (fscanf(file, "%lu\n", &length) != 1 || length > 16777216UL) return 0;
    decoded = (char *)malloc((size_t)length + 1U);
    if (decoded == NULL) die("out of memory");
    for (i = 0UL; i < length; ++i) {
        int high = cache_hex_value(fgetc(file));
        int low = cache_hex_value(fgetc(file));
        if (high < 0 || low < 0 || (high == 0 && low == 0)) {
            free(decoded);
            return 0;
        }
        decoded[i] = (char)((high << 4) | low);
    }
    if (fgetc(file) != '\n') {
        free(decoded);
        return 0;
    }
    decoded[length] = '\0';
    *value = decoded;
    return 1;
}

static int source_facts_write_uses(FILE *file, const ClangUseList *uses,
                                   const SourceList *files)
{
    size_t i;
    if (fprintf(file, "%lu\n", (unsigned long)uses->count) < 0) return 0;
    for (i = 0U; i < uses->count; ++i) {
        const ClangUse *use = &uses->items[i];
        const char *argument_path = "";
        if (use->argument_file_index < files->count) {
            argument_path = files->items[use->argument_file_index].path;
        }
        if (fprintf(file, "%u %lu %u %u %u\n", use->offset,
                    (unsigned long)use->line, use->argument_index,
                    use->argument_start, use->argument_end) < 0 ||
            !source_facts_write_string(file, use->name) ||
            !source_facts_write_string(file, use->path) ||
            !source_facts_write_string(file, argument_path) ||
            !source_facts_write_string(file, use->type_spelling) ||
            !source_facts_write_string(file, use->canonical_spelling)) return 0;
    }
    return 1;
}

static int source_facts_read_uses(FILE *file, ClangUseList *uses,
                                  const SourceList *files)
{
    unsigned long count, i;
    if (fscanf(file, "%lu\n", &count) != 1 || count > 10000000UL) return 0;
    for (i = 0UL; i < count; ++i) {
        unsigned long offset, line, argument_index, argument_start, argument_end;
        char *name = NULL, *path = NULL, *argument_path = NULL;
        char *type = NULL, *canonical = NULL;
        ClangUse *next;
        size_t new_cap;
        size_t argument_file_index;
        int okay = fscanf(file, "%lu %lu %lu %lu %lu\n", &offset, &line,
                          &argument_index, &argument_start, &argument_end) == 5 &&
                   offset <= UINT_MAX && argument_index <= UINT_MAX &&
                   argument_start <= UINT_MAX && argument_end <= UINT_MAX &&
                   source_facts_read_string(file, &name) &&
                   source_facts_read_string(file, &path) &&
                   source_facts_read_string(file, &argument_path) &&
                   source_facts_read_string(file, &type) &&
                   source_facts_read_string(file, &canonical);
        if (!okay) {
            free(name); free(path); free(argument_path); free(type); free(canonical);
            return 0;
        }
        argument_file_index = argument_path[0] != '\0' ?
            source_list_index_for_path(files, argument_path) : (size_t)-1;
        if (uses->count == uses->cap) {
            new_cap = uses->cap == 0U ? 8U : uses->cap * 2U;
            if (new_cap < uses->cap ||
                new_cap > (size_t)-1 / sizeof(*uses->items)) {
                die("too many cached Clang uses");
            }
            next = (ClangUse *)realloc(uses->items,
                                      new_cap * sizeof(*uses->items));
            if (next == NULL) die("out of memory");
            uses->items = next;
            uses->cap = new_cap;
        }
        memset(&uses->items[uses->count], 0, sizeof(*uses->items));
        uses->items[uses->count].name = name;
        uses->items[uses->count].path = path;
        uses->items[uses->count].offset = (unsigned)offset;
        uses->items[uses->count].line = (size_t)line;
        uses->items[uses->count].argument_index = (unsigned)argument_index;
        uses->items[uses->count].argument_file_index = argument_file_index;
        uses->items[uses->count].argument_start = (unsigned)argument_start;
        uses->items[uses->count].argument_end = (unsigned)argument_end;
        uses->items[uses->count].type_spelling = type[0] != '\0' ? type : NULL;
        uses->items[uses->count].canonical_spelling =
            canonical[0] != '\0' ? canonical : NULL;
        if (type[0] == '\0') free(type);
        if (canonical[0] == '\0') free(canonical);
        free(argument_path);
        ++uses->count;
    }
    return 1;
}

int source_facts_load(SourceFile *source, const SourceList *files)
{
    char *path = source_facts_cache_path(source->path);
    FILE *file = fopen(path, "rb");
    char header[40];
    unsigned long count, i;
    int okay = 0;
    source_facts_reset(&source->facts);
    if (file == NULL) {
        free(path);
        return 0;
    }
    if (fgets(header, sizeof(header), file) == NULL ||
        !str_eq(header, "TMPLGEN-FACTS-2\n") ||
        fscanf(file, "%16llx %16llx\n", &source->facts.pass1_key,
               &source->facts.pass2_key) != 2 ||
        fscanf(file, "%lu\n", &count) != 1 || count > 1000000UL) goto done;
    for (i = 0UL; i < count; ++i) {
        CacheRecordList dependency;
        unsigned long long hash1, hash2;
        memset(&dependency, 0, sizeof(dependency));
        if (!cache_read_record(file, &dependency) || dependency.count != 1U) {
            cache_record_list_free(&dependency);
            goto done;
        }
        hash1 = dependency.items[0].hash1;
        hash2 = dependency.items[0].hash2;
        if (!cache_file_hash(dependency.items[0].path, &hash1, &hash2) ||
            hash1 != dependency.items[0].hash1 ||
            hash2 != dependency.items[0].hash2) {
            cache_record_list_free(&dependency);
            goto done;
        }
        string_set_add(&source->facts.dependencies, dependency.items[0].path);
        cache_add_dependency_path(&g_clang_dependencies, dependency.items[0].path);
        cache_record_list_free(&dependency);
    }
    if (fscanf(file, "%lu\n", &count) != 1 || count > 1000000UL) goto done;
    for (i = 0UL; i < count; ++i) {
        char *name = NULL, *canonical = NULL;
        if (!source_facts_read_string(file, &name) ||
            !source_facts_read_string(file, &canonical)) {
            free(name); free(canonical);
            goto done;
        }
        source_facts_add_alias(&source->facts, name, canonical);
        free(name); free(canonical);
    }
    if (fscanf(file, "%lu\n", &count) != 1 || count > 1000000UL) goto done;
    for (i = 0UL; i < count; ++i) {
        unsigned long template_index, argument_count, j;
        char *functor = NULL;
        size_t *indices = NULL;
        char **spellings = NULL, **canonicals = NULL;
        if (fscanf(file, "%lu %lu\n", &template_index, &argument_count) != 2 ||
            template_index >= gen_template_count() || argument_count > 1024UL ||
            !source_facts_read_string(file, &functor)) {
            free(functor);
            goto done;
        }
        if (argument_count != 0UL) {
            indices = (size_t *)malloc((size_t)argument_count * sizeof(*indices));
            spellings = (char **)calloc((size_t)argument_count, sizeof(*spellings));
            canonicals = (char **)calloc((size_t)argument_count, sizeof(*canonicals));
            if (indices == NULL || spellings == NULL || canonicals == NULL) {
                die("out of memory");
            }
            for (j = 0UL; j < argument_count; ++j) {
                unsigned long parameter_index;
                if (fscanf(file, "%lu\n", &parameter_index) != 1 ||
                    !source_facts_read_string(file, &spellings[j]) ||
                    !source_facts_read_string(file, &canonicals[j])) {
                    size_t free_index;
                    free(functor);
                    for (free_index = 0U; free_index < (size_t)argument_count;
                         ++free_index) {
                        free(spellings[free_index]); free(canonicals[free_index]);
                    }
                    free(indices); free(spellings); free(canonicals);
                    goto done;
                }
                indices[j] = (size_t)parameter_index;
            }
        }
        source_facts_add_constructor(&source->facts, (size_t)template_index,
            functor, indices, (const char *const *)spellings,
            (const char *const *)canonicals, (size_t)argument_count);
        free(functor);
        for (j = 0UL; j < argument_count; ++j) {
            free(spellings[j]); free(canonicals[j]);
        }
        free(indices); free(spellings); free(canonicals);
    }
    if (fscanf(file, "%lu\n", &count) != 1 || count > 1000000UL) goto done;
    for (i = 0UL; i < count; ++i) {
        StructuralSchema schema;
        unsigned long is_union, field_count, j;
        memset(&schema, 0, sizeof(schema));
        if (fscanf(file, "%lu %lu\n", &is_union, &field_count) != 2 ||
            is_union > 1UL || field_count > 1000000UL ||
            !source_facts_read_string(file, &schema.name)) {
            free(schema.name);
            goto done;
        }
        schema.is_union = (int)is_union;
        for (j = 0UL; j < field_count; ++j) {
            char *field = NULL;
            if (!source_facts_read_string(file, &field)) {
                free(field); free(schema.name);
                list_free(schema.fields, schema.field_count);
                goto done;
            }
            list_add(&schema.fields, &schema.field_count, &schema.field_cap, field);
            free(field);
        }
        source_facts_add_schema(&source->facts, &schema);
        free(schema.name);
        list_free(schema.fields, schema.field_count);
    }
    if (fscanf(file, "%lu\n", &count) != 1 || count > 1000000UL) goto done;
    for (i = 0UL; i < count; ++i) {
        unsigned long origin_offset;
        char *name = NULL, *origin_path = NULL;
        if (fscanf(file, "%lu\n", &origin_offset) != 1 || origin_offset > UINT_MAX ||
            !source_facts_read_string(file, &name) ||
            !source_facts_read_string(file, &origin_path)) {
            free(name); free(origin_path);
            goto done;
        }
        source_facts_add_origin(&source->facts, name, origin_path,
                                (unsigned)origin_offset);
        free(name); free(origin_path);
    }
    if (!source_facts_read_uses(file, &source->facts.pass1_uses, files) ||
        !source_facts_read_uses(file, &source->facts.pass2_uses, files)) goto done;
    if (fgetc(file) == EOF && !ferror(file)) okay = 1;
done:
    (void)fclose(file);
    free(path);
    if (!okay) {
        source_facts_reset(&source->facts);
        return 0;
    }
    source->facts.loaded = 1;
    source->facts.pass1_valid = 1;
    source->facts.pass2_valid = 1;
    return 1;
}

int source_facts_save(const SourceFile *source,
                             const SourceList *files)
{
    CacheRecordList dependencies;
    char *path;
    char *temporary;
    FILE *file;
    size_t i;
    int okay = 0;
    if (!g_tu_cache_enabled || !source->facts.pass1_valid ||
        !source->facts.pass2_valid) return 0;
    memset(&dependencies, 0, sizeof(dependencies));
    for (i = 0U; i < source->facts.dependencies.count; ++i) {
        unsigned long long hash1, hash2;
        if (!cache_file_hash(source->facts.dependencies.items[i], &hash1, &hash2)) {
            cache_record_list_free(&dependencies);
            return 0;
        }
        cache_record_list_add(&dependencies, source->facts.dependencies.items[i],
                              hash1, hash2);
    }
    path = source_facts_cache_path(source->path);
    {
        Buf temp;
        buf_init(&temp);
        buf_puts(&temp, path);
        buf_puts(&temp, ".tmp");
        temporary = temp.p;
    }
    if (!ensure_parent_directories(path)) goto done;
    file = fopen(temporary, "wb");
    if (file == NULL) goto done;
    if (fprintf(file, "TMPLGEN-FACTS-2\n%016llx %016llx\n%lu\n",
                source->facts.pass1_key, source->facts.pass2_key,
                (unsigned long)dependencies.count) < 0) goto close_file;
    for (i = 0U; i < dependencies.count; ++i) {
        if (!cache_write_hashed_record(file, &dependencies.items[i])) goto close_file;
    }
    if (fprintf(file, "%lu\n", (unsigned long)source->facts.alias_count) < 0) {
        goto close_file;
    }
    for (i = 0U; i < source->facts.alias_count; ++i) {
        if (!source_facts_write_string(file, source->facts.aliases[i].name) ||
            !source_facts_write_string(file, source->facts.aliases[i].canonical)) {
            goto close_file;
        }
    }
    if (fprintf(file, "%lu\n", (unsigned long)source->facts.constructor_count) < 0) {
        goto close_file;
    }
    for (i = 0U; i < source->facts.constructor_count; ++i) {
        const SourceConstructorFact *fact = &source->facts.constructors[i];
        size_t argument_index;
        if (fprintf(file, "%lu %lu\n", (unsigned long)fact->template_index,
                    (unsigned long)fact->argument_count) < 0 ||
            !source_facts_write_string(file, fact->functor)) goto close_file;
        for (argument_index = 0U; argument_index < fact->argument_count;
             ++argument_index) {
            if (fprintf(file, "%lu\n",
                        (unsigned long)fact->parameter_indices[argument_index]) < 0 ||
                !source_facts_write_string(file, fact->spellings[argument_index]) ||
                !source_facts_write_string(file, fact->canonicals[argument_index])) {
                goto close_file;
            }
        }
    }
    if (fprintf(file, "%lu\n", (unsigned long)source->facts.schema_count) < 0) {
        goto close_file;
    }
    for (i = 0U; i < source->facts.schema_count; ++i) {
        const StructuralSchema *schema = &source->facts.schemas[i];
        size_t field_index;
        if (fprintf(file, "%u %lu\n", (unsigned)schema->is_union,
                    (unsigned long)schema->field_count) < 0 ||
            !source_facts_write_string(file, schema->name)) goto close_file;
        for (field_index = 0U; field_index < schema->field_count; ++field_index) {
            if (!source_facts_write_string(file, schema->fields[field_index])) {
                goto close_file;
            }
        }
    }
    if (fprintf(file, "%lu\n", (unsigned long)source->facts.origin_count) < 0) {
        goto close_file;
    }
    for (i = 0U; i < source->facts.origin_count; ++i) {
        const TypeOrigin *origin = &source->facts.origins[i];
        if (fprintf(file, "%u\n", origin->offset) < 0 ||
            !source_facts_write_string(file, origin->name) ||
            !source_facts_write_string(file, origin->path)) goto close_file;
    }
    if (!source_facts_write_uses(file, &source->facts.pass1_uses, files) ||
        !source_facts_write_uses(file, &source->facts.pass2_uses, files)) goto close_file;
    okay = 1;
close_file:
    if (fclose(file) != 0) okay = 0;
    if (okay) {
        (void)remove(path);
        if (rename(temporary, path) != 0) okay = 0;
    }
done:
    if (!okay) (void)remove(temporary);
    else if (g_verbose) fprintf(stderr, "tmplgen: saved source facts %s\n", path);
    free(temporary);
    free(path);
    cache_record_list_free(&dependencies);
    return okay;
}

#if GEN_ENABLE_WATCH
void sleep_ms(unsigned ms)
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
