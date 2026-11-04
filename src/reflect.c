/* Project reflection graph construction and emission. */
#include "internal.h"

/* Module-private forward declarations. */
static size_t reflect_node_for_type(ReflectGraph *graph, CXType type, unsigned depth);


typedef struct {
    enum CXTypeKind kind;
    const char *spelling;
    const char *name;
    int reflect_kind;
} ReflectBuiltin;

static const ReflectBuiltin g_reflect_builtins[] = {
    { CXType_Void, "void", "void", RK_VOID },
    { CXType_Bool, "_Bool", "_Bool", RK_BOOL },
    { CXType_Char_S, "char", "char", RK_INT },
    { CXType_SChar, "signed char", "signed_char", RK_INT },
    { CXType_UChar, "unsigned char", "unsigned_char", RK_INT },
    { CXType_Short, "short", "short", RK_INT },
    { CXType_UShort, "unsigned short", "unsigned_short", RK_INT },
    { CXType_Int, "int", "int", RK_INT },
    { CXType_UInt, "unsigned int", "unsigned_int", RK_INT },
    { CXType_Long, "long", "long", RK_INT },
    { CXType_ULong, "unsigned long", "unsigned_long", RK_INT },
    { CXType_LongLong, "long long", "long_long", RK_INT },
    { CXType_ULongLong, "unsigned long long", "unsigned_long_long", RK_INT },
    { CXType_Float, "float", "float", RK_FLOAT },
    { CXType_Double, "double", "double", RK_FLOAT },
    { CXType_LongDouble, "long double", "long_double", RK_FLOAT }
};
#define REFLECT_BUILTIN_COUNT (sizeof(g_reflect_builtins) / sizeof(g_reflect_builtins[0]))

static const ReflectGraph *g_reflect_sort_graph;

static size_t reflect_find_node(const ReflectGraph *graph, const char *key)
{
    size_t i;
    for (i = 0U; i < graph->node_count; ++i) {
        if (str_eq(graph->nodes[i].key, key)) return i;
    }
    return (size_t)-1;
}

static size_t reflect_add_node(ReflectGraph *graph, int kind, const char *key)
{
    ReflectNode *node;
    if (graph->node_count == graph->node_cap) {
        size_t new_cap = graph->node_cap == 0U ? 64U : graph->node_cap * 2U;
        ReflectNode *next;
        if (new_cap < graph->node_cap ||
            new_cap > (size_t)-1 / sizeof(*graph->nodes)) die("too many reflected types");
        next = (ReflectNode *)realloc(graph->nodes, new_cap * sizeof(*graph->nodes));
        if (next == NULL) die("out of memory");
        graph->nodes = next;
        graph->node_cap = new_cap;
    }
    node = &graph->nodes[graph->node_count];
    memset(node, 0, sizeof(*node));
    node->kind = kind;
    node->key = copy_string(key);
    node->requirement = copy_string("");
    node->inner = (size_t)-1;
    return graph->node_count++;
}

static void reflect_field_list_add(ReflectFieldList *list, const ReflectField *field)
{
    if (list->count == list->cap) {
        size_t new_cap = list->cap == 0U ? 8U : list->cap * 2U;
        ReflectField *next;
        if (new_cap < list->cap ||
            new_cap > (size_t)-1 / sizeof(*list->items)) die("too many reflected fields");
        next = (ReflectField *)realloc(list->items, new_cap * sizeof(*list->items));
        if (next == NULL) die("out of memory");
        list->items = next;
        list->cap = new_cap;
    }
    list->items[list->count++] = *field;
}

static void reflect_add_enumerator(ReflectGraph *graph, const char *name,
                                   long long value, int is_unsigned)
{
    if (graph->enumerator_count == graph->enumerator_cap) {
        size_t new_cap = graph->enumerator_cap == 0U ? 32U : graph->enumerator_cap * 2U;
        ReflectEnumerator *next;
        if (new_cap < graph->enumerator_cap ||
            new_cap > (size_t)-1 / sizeof(*graph->enumerators)) die("too many enumerators");
        next = (ReflectEnumerator *)realloc(graph->enumerators,
                                            new_cap * sizeof(*graph->enumerators));
        if (next == NULL) die("out of memory");
        graph->enumerators = next;
        graph->enumerator_cap = new_cap;
    }
    graph->enumerators[graph->enumerator_count].name = copy_string(name);
    graph->enumerators[graph->enumerator_count].value = value;
    graph->enumerators[graph->enumerator_count].is_unsigned = is_unsigned;
    ++graph->enumerator_count;
}

static void reflect_add_typedef(ReflectGraph *graph, const char *usr, const char *name)
{
    size_t i;
    for (i = 0U; i < graph->typedef_count; ++i) {
        if (str_eq(graph->typedef_usrs[i], usr)) {
            /* Prefer a public name (FILE) over an implementation one (__FILE). */
            if (graph->typedef_names[i][0] == '_' && name[0] != '_') {
                free(graph->typedef_names[i]);
                graph->typedef_names[i] = copy_string(name);
            }
            return;
        }
    }
    if (graph->typedef_count == graph->typedef_cap) {
        size_t new_cap = graph->typedef_cap == 0U ? 32U : graph->typedef_cap * 2U;
        char **usrs, **names;
        if (new_cap < graph->typedef_cap ||
            new_cap > (size_t)-1 / sizeof(char *)) die("too many typedefs");
        usrs = (char **)realloc(graph->typedef_usrs, new_cap * sizeof(char *));
        if (usrs == NULL) die("out of memory");
        graph->typedef_usrs = usrs;
        names = (char **)realloc(graph->typedef_names, new_cap * sizeof(char *));
        if (names == NULL) die("out of memory");
        graph->typedef_names = names;
        graph->typedef_cap = new_cap;
    }
    graph->typedef_usrs[graph->typedef_count] = copy_string(usr);
    graph->typedef_names[graph->typedef_count] = copy_string(name);
    ++graph->typedef_count;
}

static const char *reflect_typedef_name_for(const ReflectGraph *graph, const char *usr)
{
    size_t i;
    for (i = 0U; i < graph->typedef_count; ++i) {
        if (str_eq(graph->typedef_usrs[i], usr)) return graph->typedef_names[i];
    }
    return NULL;
}

static void reflect_graph_free(ReflectGraph *graph)
{
    size_t i;
    for (i = 0U; i < graph->node_count; ++i) {
        ReflectNode *node = &graph->nodes[i];
        free(node->key); free(node->name); free(node->display);
        free(node->spelling); free(node->header); free(node->requirement);
        free(node->generated);
    }
    free(graph->nodes);
    for (i = 0U; i < graph->field_count; ++i) {
        free(graph->fields[i].name);
        free(graph->fields[i].meta_type);
        free(graph->fields[i].meta_init);
    }
    free(graph->fields);
    for (i = 0U; i < graph->enumerator_count; ++i) free(graph->enumerators[i].name);
    free(graph->enumerators);
    string_set_free(&graph->project_headers);
    for (i = 0U; i < graph->typedef_count; ++i) {
        free(graph->typedef_usrs[i]);
        free(graph->typedef_names[i]);
    }
    free(graph->typedef_usrs);
    free(graph->typedef_names);
    for (i = 0U; i < graph->meta_type_count; ++i) free(graph->meta_types[i]);
    free(graph->meta_types);
    free(graph->meta_type_nodes);
    memset(graph, 0, sizeof(*graph));
}

/* "unsigned char *" -> "unsigned_char_ptr" */
static char *reflect_sanitize(const char *text)
{
    Buf out;
    int pending_underscore = 0;
    buf_init(&out);
    for (; *text != '\0'; ++text) {
        if (is_ident_char(*text)) {
            if (pending_underscore && out.len != 0U) buf_putc(&out, '_');
            pending_underscore = 0;
            buf_putc(&out, *text);
        } else if (*text == '*') {
            if (out.len != 0U) buf_putc(&out, '_');
            buf_puts(&out, "ptr");
            pending_underscore = 0;
        } else {
            pending_underscore = 1;
        }
    }
    return out.p != NULL ? out.p : copy_string("anonymous");
}

static unsigned long long reflect_hash(const char *text)
{
    unsigned long long hash = 14695981039346656037ULL;
    for (; *text != '\0'; ++text) {
        hash ^= (unsigned char)*text;
        hash *= 1099511628211ULL;
    }
    return hash;
}

static unsigned long long reflect_size(CXType type)
{
    long long size = clang_Type_getSizeOf(type);
    return size > 0 ? (unsigned long long)size : 0ULL;
}

static unsigned long long reflect_align(CXType type)
{
    long long align = clang_Type_getAlignOf(type);
    return align > 0 ? (unsigned long long)align : 0ULL;
}

static unsigned reflect_quals(CXType type)
{
    unsigned quals = 0U;
    if (clang_isConstQualifiedType(type)) quals |= RQUAL_CONST;
    if (clang_isVolatileQualifiedType(type)) quals |= RQUAL_VOLATILE;
    if (clang_isRestrictQualifiedType(type)) quals |= RQUAL_RESTRICT;
    return quals;
}

static char *reflect_cursor_file(CXCursor cursor, unsigned *offset)
{
    CXFile file = NULL;
    unsigned line = 0U, column = 0U;
    char *name;
    char *absolute;
    *offset = 0U;
    clang_getExpansionLocation(clang_getCursorLocation(cursor), &file, &line, &column,
                               offset);
    if (file == NULL) return NULL;
    name = copy_cx_string(clang_getFileName(file));
    if (name[0] == '\0') {
        free(name);
        return NULL;
    }
    absolute = absolute_path(name);
    free(name);
    return absolute;
}

/* A declaration belongs to the project when it is in a header that declares
 * project types (from the source scan), in the template directory, or in the
 * generated directory (specializations and template-provided types). */
static int reflect_project_path(const ReflectGraph *graph, const char *path)
{
    if (path == NULL) return 0;
    if (str_eq(path_leaf(path), "reflection.h") &&
        path_is_within_directory(path, g_generated_dir)) return 0;   /* stand-in */
    if (string_set_has(&graph->project_headers, path)) return 1;
    if (path_is_within_directory(path, g_absolute_template_dir) &&
        !path_has_suffix(path, GEN_TMPL_SUFFIX)) return 1;
    return path_is_within_directory(path, g_generated_dir);
}

/* ---- field metadata: // @meta Type{initializer} ------------------------- */

/* Fields that come from templates are the only ones allowed to carry @meta.
 * A generated header may be mapped back to its template by #line, so accept
 * both paths. */
static int reflect_meta_supported_path(const char *path)
{
    if (path == NULL) return 0;
    if (path_is_within_directory(path, g_generated_dir)) return 1;
    if (path_has_suffix(path, GEN_TMPL_SUFFIX)) return 1;
    return path_has_suffix(path, ".tmpl.c");
}

static const char *reflect_meta_marker(const char *text)
{
    const char *at = text;
    while ((at = strstr(at, "@meta")) != NULL) {
        if (isspace((unsigned char)at[5])) return at + 5;
        at += 5;
    }
    return NULL;
}

/* Parses the text after @meta: a compound literal `(Type){ initializer }`.
 * The annotation ends at the end of the comment line (or at the end of a
 * block comment); the type parentheses and the initializer braces must be
 * balanced, and nothing may follow the initializer. */
static int reflect_meta_parse(const char *at, char **type_out, char **init_out)
{
    const char *end = at;
    const char *close_paren = NULL;
    const char *type_start;
    const char *type_end;
    const char *brace;
    const char *init_end = NULL;
    const char *rest;
    const char *p;
    size_t depth;
    char quote;
    *type_out = NULL;
    *init_out = NULL;
    while (*end != '\0' && *end != '\n' && !(end[0] == '*' && end[1] == '/')) ++end;
    while (at < end && isspace((unsigned char)*at)) ++at;
    if (at == end || *at != '(') return 0;
    depth = 0U;
    quote = '\0';
    for (p = at; p < end; ++p) {
        if (quote != '\0') {
            if (*p == '\\' && p + 1 < end) ++p;
            else if (*p == quote) quote = '\0';
            continue;
        }
        if (*p == '"' || *p == '\'') {
            quote = *p;
        } else if (*p == '(') {
            ++depth;
        } else if (*p == ')') {
            --depth;
            if (depth == 0U) { close_paren = p; break; }
        }
    }
    if (close_paren == NULL) return 0;
    type_start = at + 1;
    while (type_start < close_paren &&
           isspace((unsigned char)*type_start)) ++type_start;
    type_end = close_paren;
    while (type_end > type_start && isspace((unsigned char)type_end[-1])) --type_end;
    if (type_end == type_start) return 0;
    brace = close_paren + 1;
    while (brace < end && isspace((unsigned char)*brace)) ++brace;
    if (brace == end || *brace != '{') return 0;
    depth = 0U;
    quote = '\0';
    for (p = brace; p < end; ++p) {
        if (quote != '\0') {
            if (*p == '\\' && p + 1 < end) ++p;
            else if (*p == quote) quote = '\0';
            continue;
        }
        if (*p == '"' || *p == '\'') {
            quote = *p;
        } else if (*p == '{') {
            ++depth;
        } else if (*p == '}') {
            --depth;
            if (depth == 0U) { init_end = p + 1; break; }
        }
    }
    if (init_end == NULL) return 0;
    rest = init_end;
    while (rest < end && isspace((unsigned char)*rest)) ++rest;
    if (rest != end) return 0;
    *type_out = copy_range(type_start, (size_t)(type_end - type_start));
    *init_out = copy_range(brace, (size_t)(init_end - brace));
    return 1;
}

static size_t reflect_meta_type_add(ReflectGraph *graph, const char *type)
{
    size_t i;
    for (i = 0U; i < graph->meta_type_count; ++i) {
        if (str_eq(graph->meta_types[i], type)) return i;
    }
    if (graph->meta_type_count == graph->meta_type_cap) {
        size_t new_cap = graph->meta_type_cap == 0U ? 8U : graph->meta_type_cap * 2U;
        char **types;
        size_t *nodes;
        if (new_cap < graph->meta_type_cap ||
            new_cap > (size_t)-1 / sizeof(*graph->meta_types)) die("too many @meta types");
        types = (char **)realloc(graph->meta_types, new_cap * sizeof(*types));
        if (types == NULL) die("out of memory");
        graph->meta_types = types;
        nodes = (size_t *)realloc(graph->meta_type_nodes, new_cap * sizeof(*nodes));
        if (nodes == NULL) die("out of memory");
        graph->meta_type_nodes = nodes;
        graph->meta_type_cap = new_cap;
    }
    graph->meta_types[graph->meta_type_count] = copy_string(type);
    graph->meta_type_nodes[graph->meta_type_count] = (size_t)-1;
    return graph->meta_type_count++;
}

static size_t reflect_meta_type_lookup(const ReflectGraph *graph, const char *type)
{
    size_t i;
    for (i = 0U; i < graph->meta_type_count; ++i) {
        if (str_eq(graph->meta_types[i], type)) return i;
    }
    return (size_t)-1;
}

/* Collects @meta types from generated header text. The same annotations are
 * parsed again, field by field, once libclang has the comments. */
static void reflect_meta_scan_text(ReflectGraph *graph, const char *text)
{
    size_t i = 0U;
    while (text[i] != '\0') {
        if (text[i] == '"' || text[i] == '\'') {
            char quote = text[i++];
            while (text[i] != '\0' && text[i] != quote) {
                if (text[i] == '\\' && text[i + 1U] != '\0') i += 2U;
                else ++i;
            }
            if (text[i] != '\0') ++i;
        } else if (text[i] == '/' && text[i + 1U] == '/') {
            size_t start = ++i;
            const char *at;
            while (text[i] != '\0' && text[i] != '\n') ++i;
            at = reflect_meta_marker(text + start);
            while (at != NULL && at < text + i) {
                char *type = NULL, *init = NULL;
                if (reflect_meta_parse(at, &type, &init)) reflect_meta_type_add(graph, type);
                free(type);
                free(init);
                at = reflect_meta_marker(at);
            }
        } else if (text[i] == '/' && text[i + 1U] == '*') {
            size_t start = i += 2U;
            const char *at;
            while (text[i] != '\0' && !(text[i] == '*' && text[i + 1U] == '/')) ++i;
            at = reflect_meta_marker(text + start);
            while (at != NULL && at < text + i) {
                char *type = NULL, *init = NULL;
                if (reflect_meta_parse(at, &type, &init)) reflect_meta_type_add(graph, type);
                free(type);
                free(init);
                at = reflect_meta_marker(at);
            }
            if (text[i] != '\0') i += 2U;
        } else {
            ++i;
        }
    }
}

static void reflect_field_meta(ReflectField *field, CXCursor cursor,
                               ReflectGraph *graph)
{
    unsigned offset = 0U;
    char *path = reflect_cursor_file(cursor, &offset);
    char *text;
    const char *at;
    if (!reflect_meta_supported_path(path)) {
        free(path);
        return;
    }
    text = copy_cx_string(clang_Cursor_getRawCommentText(cursor));
    at = reflect_meta_marker(text);
    if (at == NULL) {
        free(text);
        free(path);
        return;
    }
    if (reflect_meta_marker(at) != NULL) {
        report_error(path, offset, "@meta: only one annotation is allowed per field");
    } else if (!reflect_meta_parse(at, &field->meta_type, &field->meta_init)) {
        report_error(path, offset, "@meta: expected '@meta (Type){...}'");
    } else {
        size_t type = reflect_meta_type_lookup(graph, field->meta_type);
        if (type == (size_t)-1) {
            report_error(path, offset, "@meta: type '%s' was not found in the "
                         "generated headers", field->meta_type);
        } else {
            field->meta_node = graph->meta_type_nodes[type];
        }
    }
    free(text);
    free(path);
}

static enum CXVisitorResult reflect_visit_field(CXCursor cursor, CXClientData data)
{
    ReflectFieldVisit *visit = (ReflectFieldVisit *)data;
    char *name = copy_cx_string(clang_getCursorSpelling(cursor));
    CXType type = clang_getCursorType(cursor);
    CXType canonical = clang_getCanonicalType(type);
    long long offset = clang_Cursor_getOffsetOfField(cursor);
    unsigned long long offset_bits = visit->base_bits +
        (offset > 0 ? (unsigned long long)offset : 0ULL);
    if (!valid_identifier(name) && canonical.kind == CXType_Record) {
        /* C11 anonymous struct/union member: its fields are the owner's. */
        ReflectFieldVisit inner = *visit;
        inner.base_bits = offset_bits;
        (void)clang_Type_visitFields(canonical, reflect_visit_field, &inner);
    } else if (valid_identifier(name)) {
        ReflectField field;
        memset(&field, 0, sizeof(field));
        field.name = copy_string(name);
        field.type = reflect_node_for_type(visit->graph, type, visit->depth + 1U);
        field.offset_bits = offset_bits;
        field.quals = reflect_quals(type);
        field.meta_node = (size_t)-1;
        if (clang_Cursor_isBitField(cursor)) {
            int width = clang_getFieldDeclBitWidth(cursor);
            field.flags |= RFIELD_BITFIELD;
            field.bit_width = width > 0 ? (unsigned)width : 0U;
        }
        reflect_field_meta(&field, cursor, visit->graph);
        reflect_field_list_add(visit->fields, &field);
    }
    free(name);
    return CXVisit_Continue;
}

static enum CXChildVisitResult reflect_visit_enumerator(CXCursor cursor, CXCursor parent,
                                                        CXClientData data)
{
    ReflectEnumVisit *visit = (ReflectEnumVisit *)data;
    (void)parent;
    if (clang_getCursorKind(cursor) == CXCursor_EnumConstantDecl) {
        char *name = copy_cx_string(clang_getCursorSpelling(cursor));
        long long value = visit->is_unsigned ?
            (long long)clang_getEnumConstantDeclUnsignedValue(cursor) :
            clang_getEnumConstantDeclValue(cursor);
        reflect_add_enumerator(visit->graph, name, value, visit->is_unsigned);
        free(name);
    }
    return CXChildVisit_Continue;
}

/* Records and enums: identity by USR, named after a typedef when one exists. */
static size_t reflect_node_for_declaration(ReflectGraph *graph, CXType canonical,
                                           unsigned depth)
{
    CXCursor declaration = clang_getTypeDeclaration(canonical);
    char *usr = copy_cx_string(clang_getCursorUSR(declaration));
    char *tag = copy_cx_string(clang_getCursorSpelling(declaration));
    int is_enum = canonical.kind == CXType_Enum;
    int is_union = clang_getCursorKind(declaration) == CXCursor_UnionDecl;
    const char *keyword = is_enum ? "enum" : is_union ? "union" : "struct";
    unsigned offset = 0U;
    const char *typedef_name;
    char *path;
    size_t index;
    if (usr[0] == '\0') {
        free(usr);
        usr = copy_cx_string(clang_getTypeSpelling(canonical));
    }
    index = reflect_find_node(graph, usr);
    if (index != (size_t)-1) {
        free(usr);
        free(tag);
        return index;
    }
    path = reflect_cursor_file(declaration, &offset);
    index = reflect_add_node(graph, is_enum ? RK_ENUM : is_union ? RK_UNION : RK_STRUCT,
                             usr);
    typedef_name = reflect_typedef_name_for(graph, usr);
    if (typedef_name != NULL) {
        graph->nodes[index].name = copy_string(typedef_name);
        graph->nodes[index].spelling = copy_string(typedef_name);
        graph->nodes[index].display = copy_string(typedef_name);
    } else if (valid_identifier(tag)) {
        Buf spelled;
        buf_init(&spelled);
        buf_puts(&spelled, keyword);
        buf_putc(&spelled, ' ');
        buf_puts(&spelled, tag);
        graph->nodes[index].spelling = copy_string(spelled.p);
        graph->nodes[index].display = copy_string(spelled.p);
        graph->nodes[index].name = reflect_sanitize(spelled.p);
        buf_free(&spelled);
    } else {
        /* Unnamed: reachable through a field, describable, not nameable. */
        char name[48];
        (void)snprintf(name, sizeof(name), "%s_anon_%08llx", keyword,
                       reflect_hash(usr) & 0xffffffffULL);
        char *spelling = copy_cx_string(clang_getTypeSpelling(canonical));
        char *location = strstr(spelling, "(unnamed at ");
        if (location == NULL) location = strstr(spelling, "(anonymous at ");
        graph->nodes[index].name = copy_string(name);
        if (location != NULL) {
            /* The spelling names an absolute path; keep outputs portable. */
            Buf display;
            buf_init(&display);
            buf_putn(&display, spelling, (size_t)(location - spelling));
            buf_puts(&display, "(anonymous)");
            graph->nodes[index].display = display.p;
            free(spelling);
        } else {
            graph->nodes[index].display = spelling;
        }
        graph->nodes[index].flags |= RF_ANONYMOUS;
    }
    graph->nodes[index].size = reflect_size(canonical);
    graph->nodes[index].align = reflect_align(canonical);
    graph->nodes[index].decl_offset = offset;
    graph->nodes[index].project = reflect_project_path(graph, path);
    if (path != NULL && graph->nodes[index].project) {
        if (path_is_within_directory(path, g_generated_dir)) {
            graph->nodes[index].generated = copy_string(path);
        } else {
            graph->nodes[index].header = copy_string(path);
        }
    }
    if (is_enum) {
        CXType integer = clang_getEnumDeclIntegerType(declaration);
        size_t underlying = reflect_node_for_type(graph, integer, depth + 1U);
        ReflectEnumVisit visit;
        size_t first = graph->enumerator_count;
        visit.graph = graph;
        visit.is_unsigned = !(graph->nodes[underlying].flags & RF_SIGNED);
        (void)clang_visitChildren(declaration, reflect_visit_enumerator, &visit);
        graph->nodes[index].inner = underlying;
        graph->nodes[index].first_enumerator = first;
        graph->nodes[index].enumerator_count = graph->enumerator_count - first;
        graph->nodes[index].count = graph->nodes[index].enumerator_count;
        graph->nodes[index].flags |= graph->nodes[underlying].flags & RF_SIGNED;
    } else if (graph->nodes[index].size == 0U) {
        graph->nodes[index].kind = RK_OPAQUE;          /* incomplete */
    } else if (!graph->nodes[index].project) {
        graph->nodes[index].kind = RK_OPAQUE;          /* external: size only */
    } else {
        /* Collect into a local list (field types may create records whose
         * fields are appended first), then append contiguously. */
        ReflectFieldList fields;
        ReflectFieldVisit visit;
        size_t i;
        memset(&fields, 0, sizeof(fields));
        visit.graph = graph;
        visit.fields = &fields;
        visit.base_bits = 0ULL;
        visit.depth = depth;
        (void)clang_Type_visitFields(canonical, reflect_visit_field, &visit);
        graph->nodes[index].first_field = graph->field_count;
        graph->nodes[index].field_count = fields.count;
        for (i = 0U; i < fields.count; ++i) {
            if (graph->field_count == graph->field_cap) {
                size_t new_cap = graph->field_cap == 0U ? 64U : graph->field_cap * 2U;
                ReflectField *next;
                if (new_cap < graph->field_cap ||
                    new_cap > (size_t)-1 / sizeof(*graph->fields)) {
                    die("too many reflected fields");
                }
                next = (ReflectField *)realloc(graph->fields,
                                               new_cap * sizeof(*graph->fields));
                if (next == NULL) die("out of memory");
                graph->fields = next;
                graph->field_cap = new_cap;
            }
            graph->fields[graph->field_count++] = fields.items[i];
        }
        free(fields.items);
        graph->nodes[index].count = graph->nodes[index].field_count;
    }
    free(path);
    free(usr);
    free(tag);
    return index;
}

static size_t reflect_derived_node(ReflectGraph *graph, int kind, const char *key,
                                   CXType type, size_t inner)
{
    size_t index = reflect_find_node(graph, key);
    if (index != (size_t)-1) return index;
    index = reflect_add_node(graph, kind, key);
    graph->nodes[index].inner = inner;
    graph->nodes[index].size = reflect_size(type);
    graph->nodes[index].align = reflect_align(type);
    return index;
}

static size_t reflect_node_for_type(ReflectGraph *graph, CXType type, unsigned depth)
{
    CXType canonical = clang_getCanonicalType(type);
    size_t i;
    char key[96];
    if (depth > 64U) canonical.kind = CXType_Unexposed;
    for (i = 0U; i < REFLECT_BUILTIN_COUNT; ++i) {
        if (g_reflect_builtins[i].kind == canonical.kind ||
            (canonical.kind == CXType_Char_U && g_reflect_builtins[i].kind == CXType_Char_S)) {
            return i;
        }
    }
    switch (canonical.kind) {
    case CXType_Record:
    case CXType_Enum:
        return reflect_node_for_declaration(graph, canonical, depth);
    case CXType_Pointer: {
        CXType pointee = clang_getPointeeType(canonical);
        unsigned quals = reflect_quals(pointee);
        size_t target = reflect_node_for_type(graph, pointee, depth + 1U);
        size_t index;
        (void)snprintf(key, sizeof(key), "ptr:%lu:%u", (unsigned long)target, quals);
        index = reflect_find_node(graph, key);
        if (index == (size_t)-1) {
            Buf name, display;
            index = reflect_derived_node(graph, RK_POINTER, key, canonical, target);
            buf_init(&name);
            buf_init(&display);
            if (quals & RQUAL_CONST) {
                buf_puts(&name, "const_");
                buf_puts(&display, "const ");
            }
            if (quals & RQUAL_VOLATILE) {
                buf_puts(&name, "volatile_");
                buf_puts(&display, "volatile ");
            }
            buf_puts(&name, graph->nodes[target].name);
            buf_puts(&name, "_ptr");
            buf_puts(&display, graph->nodes[target].display);
            buf_puts(&display, " *");
            graph->nodes[index].name = name.p;
            graph->nodes[index].display = display.p;
            if (graph->nodes[target].spelling != NULL) {
                graph->nodes[index].spelling = copy_string(display.p);
            }
        }
        return index;
    }
    case CXType_ConstantArray:
    case CXType_IncompleteArray: {
        CXType element = clang_getArrayElementType(canonical);
        size_t target = reflect_node_for_type(graph, element, depth + 1U);
        long long length = canonical.kind == CXType_ConstantArray ?
            clang_getArraySize(canonical) : -1;
        size_t index;
        (void)snprintf(key, sizeof(key), "arr:%lu:%lld", (unsigned long)target, length);
        index = reflect_find_node(graph, key);
        if (index == (size_t)-1) {
            Buf name, display;
            char suffix[40];
            index = reflect_derived_node(graph, RK_ARRAY, key, canonical, target);
            buf_init(&name);
            buf_init(&display);
            buf_puts(&name, graph->nodes[target].name);
            buf_puts(&display, graph->nodes[target].display);
            if (length >= 0) {
                (void)snprintf(suffix, sizeof(suffix), "_arr%lld", length);
                buf_puts(&name, suffix);
                (void)snprintf(suffix, sizeof(suffix), "[%lld]", length);
                buf_puts(&display, suffix);
                graph->nodes[index].count = (unsigned long long)length;
            } else {
                buf_puts(&name, "_arr");
                buf_puts(&display, "[]");
            }
            graph->nodes[index].name = name.p;
            graph->nodes[index].display = display.p;
        }
        return index;
    }
    case CXType_FunctionProto:
    case CXType_FunctionNoProto: {
        char *spelling = copy_cx_string(clang_getTypeSpelling(canonical));
        Buf function_key;
        size_t index;
        buf_init(&function_key);
        buf_puts(&function_key, "fn:");
        buf_puts(&function_key, spelling);
        index = reflect_find_node(graph, function_key.p);
        if (index == (size_t)-1) {
            char name[32];
            index = reflect_add_node(graph, RK_FUNCTION, function_key.p);
            (void)snprintf(name, sizeof(name), "fn_%08llx",
                           reflect_hash(spelling) & 0xffffffffULL);
            graph->nodes[index].name = copy_string(name);
            graph->nodes[index].display = copy_string(spelling);
            graph->nodes[index].inner = reflect_node_for_type(
                graph, clang_getResultType(canonical), depth + 1U);
        }
        buf_free(&function_key);
        free(spelling);
        return index;
    }
    default: {
        char *spelling = copy_cx_string(clang_getTypeSpelling(canonical));
        Buf opaque_key;
        size_t index;
        buf_init(&opaque_key);
        buf_puts(&opaque_key, "opaque:");
        buf_puts(&opaque_key, spelling);
        index = reflect_find_node(graph, opaque_key.p);
        if (index == (size_t)-1) {
            index = reflect_derived_node(graph, RK_OPAQUE, opaque_key.p, canonical,
                                         (size_t)-1);
            graph->nodes[index].name = reflect_sanitize(spelling);
            graph->nodes[index].display = copy_string(spelling);
        }
        buf_free(&opaque_key);
        free(spelling);
        return index;
    }
    }
}

/* ---- collection ----------------------------------------------------------- */

static int reflect_is_generator_helper(const char *name)
{
    return strncmp(name, "tmplgen_", 8U) == 0;
}

static enum CXChildVisitResult reflect_collect_cursor(CXCursor cursor, CXCursor parent,
                                                      CXClientData data)
{
    ReflectCollect *collect = (ReflectCollect *)data;
    ReflectGraph *graph = collect->graph;
    enum CXCursorKind kind = clang_getCursorKind(cursor);
    (void)parent;
    if (collect->pass == 1) {
        if (kind == CXCursor_TypedefDecl) {
            CXType underlying = clang_getCanonicalType(
                clang_getTypedefDeclUnderlyingType(cursor));
            if (underlying.kind == CXType_Record || underlying.kind == CXType_Enum) {
                char *name = copy_cx_string(clang_getCursorSpelling(cursor));
                char *usr = copy_cx_string(clang_getCursorUSR(
                    clang_getTypeDeclaration(underlying)));
                if (valid_identifier(name) && usr[0] != '\0') {
                    reflect_add_typedef(graph, usr, name);
                }
                free(name);
                free(usr);
            }
        } else if (kind == CXCursor_VarDecl) {
            char *name = copy_cx_string(clang_getCursorSpelling(cursor));
            if (strncmp(name, "tmplgen_reflect_builtin_", 24U) == 0) {
                unsigned long index = strtoul(name + 24, NULL, 10);
                CXType type = clang_getCanonicalType(clang_getCursorType(cursor));
                if (index < REFLECT_BUILTIN_COUNT) {
                    ReflectNode *node = &graph->nodes[index];
                    node->size = reflect_size(type);
                    node->align = reflect_align(type);
                    if (type.kind == CXType_SChar || type.kind == CXType_Char_S ||
                        type.kind == CXType_Short || type.kind == CXType_Int ||
                        type.kind == CXType_Long || type.kind == CXType_LongLong) {
                        node->flags |= RF_SIGNED;
                    }
                }
            } else if (strncmp(name, "tmplgen_reflect_meta_probe_", 27U) == 0) {
                unsigned long index = strtoul(name + 27, NULL, 10);
                if (index < graph->meta_type_count) {
                    graph->meta_type_nodes[index] =
                        reflect_node_for_type(graph, clang_getCursorType(cursor), 0U);
                }
            }
            free(name);
        }
        return CXChildVisit_Recurse;
    }
    if (kind == CXCursor_VarDecl) {
        char *name = copy_cx_string(clang_getCursorSpelling(cursor));
        if (strncmp(name, "tmplgen_reflect_pointer_", 24U) == 0) {
            (void)reflect_node_for_type(graph, clang_getCursorType(cursor), 0U);
        }
        free(name);
        return CXChildVisit_Continue;
    }
    if ((kind == CXCursor_StructDecl || kind == CXCursor_UnionDecl ||
         kind == CXCursor_EnumDecl) && clang_isCursorDefinition(cursor) != 0U) {
        unsigned offset = 0U;
        char *path = reflect_cursor_file(cursor, &offset);
        if (reflect_project_path(graph, path)) {
            CXType type = clang_getCanonicalType(clang_getCursorType(cursor));
            char *usr = copy_cx_string(clang_getCursorUSR(cursor));
            char *tag = copy_cx_string(clang_getCursorSpelling(cursor));
            /* Unnamed types are reached through the fields that use them. */
            if (valid_identifier(tag) || reflect_typedef_name_for(graph, usr) != NULL) {
                int generated = path_is_within_directory(path, g_generated_dir);
                int keep = 1;
                if (generated) {
                    /* Generated headers also contain the generator's own
                     * helper types; keep specializations and the
                     * parameterless types a template provides. */
                    const char *name = reflect_typedef_name_for(graph, usr);
                    keep = !reflect_is_generator_helper(name != NULL ? name : tag);
                }
                if (keep) (void)reflect_node_for_type(graph, type, 0U);
            }
            free(usr);
            free(tag);
        }
        free(path);
    }
    return CXChildVisit_Recurse;
}

/* ---- analysis --------------------------------------------------------------- */

/* "Vec(int)" for the specialization named `name`, or NULL. */
static char *reflect_specialization_display(const char *name, size_t *instance_out)
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
            size_t k;
            Buf display;
            if (constructor->parameter_count == 0U ||
                !instance_matches_parameters(ti, instance,
                    constructor->parameter_names, constructor->parameter_count) ||
                !type_use_matches(constructor->name, instance->name)) continue;
            generated = join_suffix(constructor->name, instance->name);
            match = str_eq(generated, name);
            free(generated);
            if (!match) continue;
            buf_init(&display);
            buf_puts(&display, constructor->name);
            buf_putc(&display, '(');
            for (k = 0U; k < constructor->parameter_count; ++k) {
                int parameter = template_parameter_index(ti,
                    constructor->parameter_names[k]);
                int position = parameter >= 0 ?
                    instance_parameter_position(instance, (size_t)parameter) : -1;
                if (k != 0U) buf_puts(&display, ", ");
                buf_puts(&display, position >= 0 ?
                         instance->arguments[position].c_type : "?");
            }
            buf_putc(&display, ')');
            *instance_out = i;
            return display.p;
        }
    }
    return NULL;
}

/* Include guard of the generated module at `path`, or NULL. */
static char *reflect_generated_module_guard(const char *path)
{
    char *absolute;
    size_t i;
    if (path == NULL) return NULL;
    absolute = absolute_path(path);
    for (i = 0U; i < gen_template_count(); ++i) {
        char *module = generated_module_path(g_templates[i].stem);
        char *module_absolute = absolute_path(module);
        int match = str_eq(module_absolute, absolute);
        free(module);
        free(module_absolute);
        if (match) {
            free(absolute);
            return generated_module_guard(g_templates[i].stem);
        }
    }
    free(absolute);
    return NULL;
}

static void reflect_set_requirement(ReflectNode *node, const char *requirement)
{
    free(node->requirement);
    node->requirement = copy_string(requirement);
}

/* Decides which nodes get a layout check and under which section; reports
 * declaring headers the sections cannot key on. */
static int reflect_assign_requirements(ReflectGraph *graph)
{
    size_t i;
    int okay = 1;
    for (i = 0U; i < graph->node_count; ++i) {
        ReflectNode *node = &graph->nodes[i];
        if (i < REFLECT_BUILTIN_COUNT) {
            node->checked = node->kind != RK_VOID;
            continue;
        }
        if (node->spelling == NULL || (node->flags & RF_ANONYMOUS) ||
            !node->project || node->kind == RK_POINTER ||
            node->kind == RK_OPAQUE || node->size == 0U) continue;
        if (node->header != NULL) {
            const HeaderGuard *header = header_guard_for(node->header);
            if (header->guard == NULL) {
                report_error(node->header, 0U,
                    "reflected type '%s' is declared in a header without an "
                    "#ifndef/#define include guard; reflection sections test that "
                    "macro (#pragma once alone is not enough)", node->display);
                okay = 0;
                continue;
            }
            {
                /* Only reflection.h matters here: its section for this header
                 * names the type, so it must not run before the declaration. */
                char *reflection = reflection_header_path();
                char *absolute = absolute_path(reflection);
                size_t line = generated_include_before_target(node->header,
                                                              node->decl_offset,
                                                              absolute);
                free(reflection);
                free(absolute);
                if (line != 0U) {
                    report_error(node->header, line,
                        "includes gen/reflection.h before declaring reflected type "
                        "'%s'; declare the type before that #include", node->display);
                    okay = 0;
                    continue;
                }
            }
            reflect_set_requirement(node, header->guard);
            node->checked = 1;
        } else {
            /* Declared in a generated header. */
            size_t instance = (size_t)-1;
            char *display;
            const char *generated_name = node->spelling;
            if (reflect_is_generator_helper(node->name)) continue;
            /* Tagged constructors are spelled "struct TaggedBox_int". */
            if (strncmp(generated_name, "struct ", 7U) == 0) generated_name += 7;
            else if (strncmp(generated_name, "union ", 6U) == 0) generated_name += 6;
            else if (strncmp(generated_name, "enum ", 5U) == 0) generated_name += 5;
            display = reflect_specialization_display(generated_name, &instance);
            if (display != NULL) {
                /* Checked where its module's section has declared it. */
                char *marker = instance_section_marker(&g_instances[instance]);
                free(node->display);
                node->display = display;
                node->flags |= RF_SPECIALIZED;
                reflect_set_requirement(node, marker);
                free(marker);
            } else {
                /* Plain code of a template, such as an enum it declares:
                 * checked once its module is included. */
                char *guard = reflect_generated_module_guard(node->generated);
                if (guard == NULL) continue;
                reflect_set_requirement(node, guard);
                free(guard);
            }
            node->checked = 1;
        }
    }
    return okay;
}

/* `T *` for every nameable record, so typetag_of(&value) has an id. */
static void reflect_add_record_pointers(ReflectGraph *graph)
{
    size_t void_pointer = reflect_find_node(graph, "ptr:0:0");
    unsigned long long size, align;
    size_t i, count = graph->node_count;
    if (void_pointer == (size_t)-1) return;
    size = graph->nodes[void_pointer].size;
    align = graph->nodes[void_pointer].align;
    for (i = 0U; i < count; ++i) {
        char key[64];
        Buf name, display;
        size_t index;
        if (!(graph->nodes[i].kind == RK_STRUCT || graph->nodes[i].kind == RK_UNION) ||
            !graph->nodes[i].checked || graph->nodes[i].spelling == NULL) continue;
        (void)snprintf(key, sizeof(key), "ptr:%lu:0", (unsigned long)i);
        if (reflect_find_node(graph, key) != (size_t)-1) continue;
        index = reflect_add_node(graph, RK_POINTER, key);
        graph->nodes[index].inner = i;
        graph->nodes[index].size = size;
        graph->nodes[index].align = align;
        buf_init(&name);
        buf_init(&display);
        buf_puts(&name, graph->nodes[i].name);
        buf_puts(&name, "_ptr");
        buf_puts(&display, graph->nodes[i].display);
        buf_puts(&display, " *");
        graph->nodes[index].name = name.p;
        graph->nodes[index].display = copy_string(display.p);
        buf_free(&display);
        buf_init(&display);
        buf_puts(&display, graph->nodes[i].spelling);
        buf_puts(&display, " *");
        graph->nodes[index].spelling = display.p;
    }
}

static void reflect_compute_flags(ReflectGraph *graph)
{
    size_t i;
    int changed = 1;
    /* POINTER_FREE: fixpoint, optimistic for records (cycles only go through
     * pointers, which are never pointer-free). */
    for (i = 0U; i < graph->node_count; ++i) {
        ReflectNode *node = &graph->nodes[i];
        if (node->kind == RK_BOOL || node->kind == RK_INT || node->kind == RK_FLOAT ||
            node->kind == RK_ENUM || node->kind == RK_STRUCT || node->kind == RK_UNION ||
            node->kind == RK_ARRAY) {
            node->flags |= RF_POINTER_FREE;
        }
    }
    while (changed) {
        changed = 0;
        for (i = 0U; i < graph->node_count; ++i) {
            ReflectNode *node = &graph->nodes[i];
            int pointer_free = (node->flags & RF_POINTER_FREE) != 0U;
            size_t f;
            if (!pointer_free) continue;
            if (node->kind == RK_ARRAY && node->inner != (size_t)-1 &&
                !(graph->nodes[node->inner].flags & RF_POINTER_FREE)) pointer_free = 0;
            for (f = 0U; f < node->field_count && pointer_free; ++f) {
                const ReflectField *field = &graph->fields[node->first_field + f];
                if (!(graph->nodes[field->type].flags & RF_POINTER_FREE)) pointer_free = 0;
            }
            if (!pointer_free) {
                node->flags &= ~(unsigned)RF_POINTER_FREE;
                changed = 1;
            }
        }
    }
    /* NO_PADDING, HAS_BITFIELDS, FLEXIBLE, ENUM_DENSE */
    for (i = 0U; i < graph->node_count; ++i) {
        ReflectNode *node = &graph->nodes[i];
        if (node->kind == RK_BOOL || node->kind == RK_INT || node->kind == RK_POINTER ||
            node->kind == RK_ENUM ||
            (node->kind == RK_FLOAT && i != REFLECT_BUILTIN_COUNT - 1U)) {
            node->flags |= RF_NO_PADDING;
        }
    }
    changed = 1;
    while (changed) {
        changed = 0;
        for (i = 0U; i < graph->node_count; ++i) {
            ReflectNode *node = &graph->nodes[i];
            unsigned long long total = 0ULL;
            int padded = 0;
            size_t f;
            if ((node->flags & RF_NO_PADDING) || node->size == 0U) continue;
            if (node->kind == RK_ARRAY) {
                if (node->inner != (size_t)-1 &&
                    (graph->nodes[node->inner].flags & RF_NO_PADDING)) {
                    node->flags |= RF_NO_PADDING;
                    changed = 1;
                }
                continue;
            }
            if (node->kind != RK_STRUCT || node->field_count == 0U) continue;
            for (f = 0U; f < node->field_count; ++f) {
                const ReflectField *field = &graph->fields[node->first_field + f];
                const ReflectNode *type = &graph->nodes[field->type];
                if ((field->flags & RFIELD_BITFIELD) || !(type->flags & RF_NO_PADDING)) {
                    padded = 1;
                    break;
                }
                total += type->size;
            }
            if (!padded && total == node->size) {
                node->flags |= RF_NO_PADDING;
                changed = 1;
            }
        }
    }
    for (i = 0U; i < graph->node_count; ++i) {
        ReflectNode *node = &graph->nodes[i];
        size_t f;
        for (f = 0U; f < node->field_count; ++f) {
            const ReflectField *field = &graph->fields[node->first_field + f];
            if (field->flags & RFIELD_BITFIELD) node->flags |= RF_HAS_BITFIELDS;
            if (f + 1U == node->field_count &&
                graph->nodes[field->type].kind == RK_ARRAY &&
                graph->nodes[field->type].size == 0U) node->flags |= RF_FLEXIBLE;
        }
        if (node->kind == RK_ENUM && node->enumerator_count != 0U) {
            long long minimum = graph->enumerators[node->first_enumerator].value;
            int dense = 1;
            size_t e, k;
            for (e = 1U; e < node->enumerator_count; ++e) {
                long long value = graph->enumerators[node->first_enumerator + e].value;
                if (value < minimum) minimum = value;
            }
            for (k = 0U; k < node->enumerator_count && dense; ++k) {
                int found = 0;
                for (e = 0U; e < node->enumerator_count; ++e) {
                    if (graph->enumerators[node->first_enumerator + e].value ==
                        minimum + (long long)k) found = 1;
                }
                if (!found) dense = 0;
            }
            if (dense) node->flags |= RF_ENUM_DENSE;
        }
    }
}

static int reflect_order_class(const ReflectNode *node, size_t index)
{
    if (index < REFLECT_BUILTIN_COUNT) return 0;
    if ((node->kind == RK_STRUCT || node->kind == RK_UNION || node->kind == RK_ENUM ||
         node->kind == RK_OPAQUE) && node->project && node->spelling != NULL) return 1;
    return 2;
}

static int reflect_compare_nodes(const void *left, const void *right)
{
    size_t a = *(const size_t *)left;
    size_t b = *(const size_t *)right;
    const ReflectNode *na = &g_reflect_sort_graph->nodes[a];
    const ReflectNode *nb = &g_reflect_sort_graph->nodes[b];
    int ca = reflect_order_class(na, a);
    int cb = reflect_order_class(nb, b);
    int names;
    if (ca != cb) return ca < cb ? -1 : 1;
    if (ca == 0) return a < b ? -1 : (a > b ? 1 : 0);
    names = strcmp(na->name, nb->name);
    if (names != 0) return names;
    return strcmp(na->key, nb->key);
}

/* Deterministic ids: builtins, then nameable project types, then everything
 * reached from them; colliding names get a hash suffix. Returns the order. */
static size_t *reflect_assign_ids(ReflectGraph *graph)
{
    size_t *order = (size_t *)malloc((graph->node_count + 1U) * sizeof(*order));
    size_t i;
    if (order == NULL) die("out of memory");
    for (i = 0U; i < graph->node_count; ++i) order[i] = i;
    g_reflect_sort_graph = graph;
    qsort(order, graph->node_count, sizeof(*order), reflect_compare_nodes);
    for (i = 1U; i < graph->node_count; ++i) {
        ReflectNode *previous = &graph->nodes[order[i - 1U]];
        ReflectNode *node = &graph->nodes[order[i]];
        if (str_eq(previous->name, node->name)) {
            char suffix[32];
            Buf renamed;
            (void)snprintf(suffix, sizeof(suffix), "_h%08llx",
                           reflect_hash(node->key) & 0xffffffffULL);
            buf_init(&renamed);
            buf_puts(&renamed, node->name);
            buf_puts(&renamed, suffix);
            free(node->name);
            node->name = renamed.p;
            if (g_verbose) {
                fprintf(stderr, "tmplgen: reflected type name collision: %s\n",
                        node->name);
            }
        }
    }
    for (i = 0U; i < graph->node_count; ++i) {
        graph->nodes[order[i]].id = (unsigned)(i + 1U);    /* 0 is INVALID */
    }
    return order;
}

static unsigned reflect_string(ReflectStrings *strings, const char *text)
{
    size_t i;
    unsigned offset;
    for (i = 0U; i < strings->count; ++i) {
        if (str_eq(strings->strings[i], text)) return strings->offsets[i];
    }
    if (strings->count == strings->cap) {
        size_t new_cap = strings->cap == 0U ? 64U : strings->cap * 2U;
        char **next_strings;
        unsigned *next_offsets;
        if (new_cap < strings->cap || new_cap > (size_t)-1 / sizeof(char *)) {
            die("too many reflected strings");
        }
        next_strings = (char **)realloc(strings->strings, new_cap * sizeof(char *));
        if (next_strings == NULL) die("out of memory");
        strings->strings = next_strings;
        next_offsets = (unsigned *)realloc(strings->offsets, new_cap * sizeof(unsigned));
        if (next_offsets == NULL) die("out of memory");
        strings->offsets = next_offsets;
        strings->cap = new_cap;
    }
    offset = (unsigned)strings->pool.len;
    strings->strings[strings->count] = copy_string(text);
    strings->offsets[strings->count] = offset;
    ++strings->count;
    buf_putn(&strings->pool, text, strlen(text) + 1U);   /* with its NUL */
    return offset;
}

static void reflect_strings_free(ReflectStrings *strings)
{
    list_free(strings->strings, strings->count);
    free(strings->offsets);
    buf_free(&strings->pool);
    memset(strings, 0, sizeof(*strings));
}

static void buf_put_ull(Buf *out, unsigned long long value)
{
    char text[32];
    (void)snprintf(text, sizeof(text), "%llu", value);
    buf_puts(out, text);
}

/* The parts of reflection.h that do not depend on the type graph. */
static void reflect_emit_schema(Buf *out)
{
    buf_puts(out,
        "#include <stddef.h>\n"
        "#include <stdint.h>\n\n"
        "/* Every identifier below is spelled TMPLGEN_SYM(name): define\n"
        "   TMPLGEN_SYMBOL_PREFIX (the same in every translation unit, e.g. with -D)\n"
        "   to prefix them all. The macros typetag, typetag_of, enumtag_of and any_of\n"
        "   are then only available as TMPLGEN_TYPETAG, TMPLGEN_TYPETAG_OF,\n"
        "   TMPLGEN_ENUMTAG_OF and TMPLGEN_ANY_OF. */\n"
        "#ifdef TMPLGEN_SYMBOL_PREFIX\n"
        "#define TMPLGEN_REFLECT_PREFIX_ TMPLGEN_SYMBOL_PREFIX\n"
        "#else\n"
        "#define TMPLGEN_REFLECT_PREFIX_\n"
        "#endif\n"
        "#define TMPLGEN_REFLECT_CAT_(a, b) a##b\n"
        "#define TMPLGEN_REFLECT_CAT(a, b) TMPLGEN_REFLECT_CAT_(a, b)\n"
        "#define TMPLGEN_SYM(name) TMPLGEN_REFLECT_CAT(TMPLGEN_REFLECT_PREFIX_, name)\n\n"
        "/* Layout checks: the tables hold libclang's numbers; the compiler confirms\n"
        "   them wherever the type is visible. */\n"
        "#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L\n"
        "#define TMPLGEN_REFLECT_CHECK(cond, msg) _Static_assert(cond, msg)\n"
        "#define TMPLGEN_REFLECT_ALIGN_IS(T, n) (_Alignof(T) == (n))\n"
        "#elif defined(__GNUC__) || defined(__clang__)\n"
        "#define TMPLGEN_REFLECT_CHECK(cond, msg) __extension__ _Static_assert(cond, msg)\n"
        "#define TMPLGEN_REFLECT_ALIGN_IS(T, n) 1\n"
        "#else\n"
        "#define TMPLGEN_REFLECT_CHECK(cond, msg) \\\n"
        "    typedef char TMPLGEN_REFLECT_CAT(tmplgen_reflect_check_, __LINE__)[(cond) ? 1 : -1]\n"
        "#define TMPLGEN_REFLECT_ALIGN_IS(T, n) 1\n"
        "#endif\n\n");
    buf_puts(out,
        "typedef enum TMPLGEN_SYM(TypeKind) {\n"
        "    TMPLGEN_SYM(TYPEKIND_INVALID), TMPLGEN_SYM(TYPEKIND_VOID),\n"
        "    TMPLGEN_SYM(TYPEKIND_BOOL), TMPLGEN_SYM(TYPEKIND_INT),\n"
        "    TMPLGEN_SYM(TYPEKIND_FLOAT), TMPLGEN_SYM(TYPEKIND_POINTER),\n"
        "    TMPLGEN_SYM(TYPEKIND_ARRAY), TMPLGEN_SYM(TYPEKIND_STRUCT),\n"
        "    TMPLGEN_SYM(TYPEKIND_UNION), TMPLGEN_SYM(TYPEKIND_ENUM),\n"
        "    TMPLGEN_SYM(TYPEKIND_FUNCTION), TMPLGEN_SYM(TYPEKIND_OPAQUE)\n"
        "} TMPLGEN_SYM(TypeKind);\n\n"
        "enum {\n"
        "    TMPLGEN_SYM(TYPEFLAG_SIGNED) = 1u << 0,\n"
        "    TMPLGEN_SYM(TYPEFLAG_POINTER_FREE) = 1u << 1, /* no pointers inside */\n"
        "    TMPLGEN_SYM(TYPEFLAG_NO_PADDING) = 1u << 2,\n"
        "    TMPLGEN_SYM(TYPEFLAG_HAS_BITFIELDS) = 1u << 3,\n"
        "    TMPLGEN_SYM(TYPEFLAG_FLEXIBLE) = 1u << 4,   /* flexible array member */\n"
        "    TMPLGEN_SYM(TYPEFLAG_ANONYMOUS) = 1u << 5,\n"
        "    TMPLGEN_SYM(TYPEFLAG_SPECIALIZED) = 1u << 6, /* template instance */\n"
        "    TMPLGEN_SYM(TYPEFLAG_ENUM_DENSE) = 1u << 7\n"
        "};\n"
        "enum { TMPLGEN_SYM(TYPEFIELD_BITFIELD) = 1u << 0 };\n"
        "enum {\n"
        "    TMPLGEN_SYM(TYPEQUAL_CONST) = 1u << 0,\n"
        "    TMPLGEN_SYM(TYPEQUAL_VOLATILE) = 1u << 1,\n"
        "    TMPLGEN_SYM(TYPEQUAL_RESTRICT) = 1u << 2\n"
        "};\n\n"
        "typedef struct TMPLGEN_SYM(TypeInfo) {\n"
        "    uint32_t size;\n"
        "    uint16_t align;\n"
        "    uint8_t kind;       /* TypeKind */\n"
        "    uint8_t reserved;\n"
        "    uint32_t flags;\n"
        "    uint32_t name;      /* offset into RuntimeTypeStrings */\n"
        "    uint32_t first;     /* first field or enumerator */\n"
        "    uint32_t count;     /* fields, enumerators, or array length */\n"
        "    uint32_t inner;     /* TypeTag: pointee, element, underlying, or result */\n"
        "    uint32_t display;   /* pretty name, e.g. \"Vec(int)\" */\n"
        "} TMPLGEN_SYM(TypeInfo);\n\n"
        "typedef struct TMPLGEN_SYM(TypeField) {\n"
        "    uint32_t name;\n"
        "    uint32_t type;      /* TypeTag */\n"
        "    uint32_t offset;    /* bytes; bits for TYPEFIELD_BITFIELD */\n"
        "    uint16_t bit_width;\n"
        "    uint8_t quals;      /* TYPEQUAL_* */\n"
        "    uint8_t flags;      /* TYPEFIELD_* */\n"
        "    uint32_t attr;      /* @meta index; 0 = none */\n"
        "    uint32_t meta_type; /* @meta TypeTag; TYPETAG_INVALID = none */\n"
        "} TMPLGEN_SYM(TypeField);\n\n"
        "typedef struct TMPLGEN_SYM(TypeEnumerator) {\n"
        "    int64_t value;\n"
        "    uint32_t name;\n"
        "    uint32_t pad;\n"
        "} TMPLGEN_SYM(TypeEnumerator);\n\n");
}

/* Declarations shared by the real header and the parsing stand-in, after the
 * TypeTag enum and table declarations. */
static void reflect_emit_api(Buf *out)
{
    buf_puts(out,
        "typedef struct TMPLGEN_SYM(Any) {\n"
        "    void *data;\n"
        "    TMPLGEN_SYM(TypeTag) type;\n"
        "} TMPLGEN_SYM(Any);\n\n"
        "/* typetag(T): the id of a type *name* (Hero, Role, struct_Packed,\n"
        "   const_char_ptr, Hero_ptr); needs no declaration of the type. */\n"
        "#define TMPLGEN_TYPETAG(T) TMPLGEN_SYM(TYPETAG_##T)\n\n"
        "/* A struct's or union's fields are RuntimeTypeFieldTable[first .. first+count),\n"
        "   an enum's enumerators RuntimeTypeEnumTable[first .. first+count). */\n"
        "static inline const TMPLGEN_SYM(TypeInfo) *\n"
        "TMPLGEN_SYM(typeinfo)(TMPLGEN_SYM(TypeTag) id)\n"
        "{\n"
        "    return &TMPLGEN_SYM(RuntimeTypeInfoTable)[id];\n"
        "}\n\n"
        "/* Display name of a type, e.g. \"Vec(Item)\". */\n"
        "static inline const char *TMPLGEN_SYM(typestr)(TMPLGEN_SYM(TypeTag) id)\n"
        "{\n"
        "    return TMPLGEN_SYM(RuntimeTypeStrings) +\n"
        "           TMPLGEN_SYM(RuntimeTypeInfoTable)[id].display;\n"
        "}\n\n"
        "/* The name stored at a string-table offset (TypeField.name,\n"
        "   TypeEnumerator.name, TypeInfo.name). */\n"
        "static inline const char *TMPLGEN_SYM(reflect_str)(uint32_t offset)\n"
        "{\n"
        "    return TMPLGEN_SYM(RuntimeTypeStrings) + offset;\n"
        "}\n\n"
        "/* The field `field` of the value in `value`. */\n"
        "static inline TMPLGEN_SYM(Any)\n"
        "TMPLGEN_SYM(any_field)(TMPLGEN_SYM(Any) value,\n"
        "                               const TMPLGEN_SYM(TypeField) *field)\n"
        "{\n"
        "    TMPLGEN_SYM(Any) result;\n"
        "    result.data = (char *)value.data + field->offset;\n"
        "    result.type = (TMPLGEN_SYM(TypeTag))field->type;\n"
        "    return result;\n"
        "}\n\n"
        "#ifndef TMPLGEN_SYMBOL_PREFIX\n"
        "#define typetag(T) TMPLGEN_TYPETAG(T)\n"
        "#endif\n\n");
}

static void reflect_emit_table_declarations(Buf *out, const char *storage,
                                            size_t type_count)
{
    buf_puts(out, storage);
    buf_puts(out, "const char TMPLGEN_SYM(RuntimeTypeStrings)[];\n");
    buf_puts(out, storage);
    buf_puts(out, "const TMPLGEN_SYM(TypeInfo) TMPLGEN_SYM(RuntimeTypeInfoTable)[");
    buf_put_ull(out, (unsigned long long)type_count);
    buf_puts(out, "];\n");
    buf_puts(out, storage);
    buf_puts(out, "const TMPLGEN_SYM(TypeField) TMPLGEN_SYM(RuntimeTypeFieldTable)[];\n");
    buf_puts(out, storage);
    buf_puts(out, "const TMPLGEN_SYM(TypeEnumerator) TMPLGEN_SYM(RuntimeTypeEnumTable)[];\n\n");
}

/* Operators built on TMPLGEN_TYPETAG_OF and TMPLGEN_ENUMTAG_OF; shared with
 * the parsing stand-in. */
static void reflect_emit_dispatch_api(Buf *out)
{
    buf_puts(out,
        "/* any_of(x): an Any pointing at the lvalue x. */\n"
        "#define TMPLGEN_ANY_OF(x) \\\n"
        "    ((TMPLGEN_SYM(Any)){ (void *)&(x), TMPLGEN_TYPETAG_OF(x) })\n"
        "#ifndef TMPLGEN_SYMBOL_PREFIX\n"
        "#define typetag_of(x) TMPLGEN_TYPETAG_OF(x)\n"
        "#define enumtag_of(x) TMPLGEN_ENUMTAG_OF(x)\n"
        "#define any_of(x) TMPLGEN_ANY_OF(x)\n"
        "#endif\n\n");
}

/* ---- dispatch: typetag_of(x), enumtag_of(x) --------------------------------
 * Two selections over the static type: typetag_of lists every nameable
 * non-enum type (builtins, project records, specializations, and pointers to
 * those), enumtag_of lists the enums. They are separate because an enum is
 * compatible with its integer type, so one selection cannot list both; two
 * enum types are never compatible with each other. Associations that name a
 * project type are added by hook macros redefined in that type's section, so
 * the macros only ever name types visible at the call. */

static const char *reflect_dispatch_requirement(const ReflectGraph *graph, size_t index,
                                                int enums)
{
    const ReflectNode *node = &graph->nodes[index];
    if (enums) {
        if (node->kind == RK_ENUM && node->checked && node->spelling != NULL) {
            return node->requirement;
        }
        return NULL;
    }
    if (index < REFLECT_BUILTIN_COUNT) return node->kind == RK_VOID ? NULL : "";
    if (node->kind == RK_POINTER) {
        const ReflectNode *target;
        if (node->spelling == NULL || node->inner == (size_t)-1) return NULL;
        target = &graph->nodes[node->inner];
        if (node->inner < REFLECT_BUILTIN_COUNT) return "";
        if ((target->kind == RK_STRUCT || target->kind == RK_UNION) && target->checked) {
            return target->requirement;
        }
        return NULL;
    }
    if ((node->kind == RK_STRUCT || node->kind == RK_UNION) && node->checked &&
        node->spelling != NULL) return node->requirement;
    return NULL;
}

static void buf_put_typetag(Buf *out, const ReflectNode *node)
{
    buf_puts(out, "TMPLGEN_SYM(TYPETAG_");
    buf_puts(out, node->name);
    buf_putc(out, ')');
}

static void buf_put_hook_name(Buf *out, const char *kind, const char *requirement)
{
    buf_puts(out, "TMPLGEN_REFLECT_");
    buf_puts(out, kind);
    buf_putc(out, '_');
    buf_put_requirement_id(out, requirement);
}

/* Hook names: the _Generic associations and the GNU chain of one list. */
static const char *reflect_generic_hook(int enums) { return enums ? "ENUMTAG" : "TYPETAG"; }
static const char *reflect_gnu_hook(int enums) { return enums ? "GNUENUM" : "GNU"; }

/* Generic-selection entries ("T: TYPETAG_T, ") and the GNU chain for the
 * types that need exactly `requirement`. */
static void reflect_emit_dispatch_entries(Buf *generic, Buf *gnu,
                                          const ReflectGraph *graph,
                                          const size_t *order, const char *requirement,
                                          int enums, const char *tail)
{
    size_t i, depth = 0U;
    for (i = 0U; i < graph->node_count; ++i) {
        const ReflectNode *node = &graph->nodes[order[i]];
        const char *needs = reflect_dispatch_requirement(graph, order[i], enums);
        if (needs == NULL || !str_eq(needs, requirement)) continue;
        buf_puts(generic, "    ");
        buf_puts(generic, node->spelling);
        buf_puts(generic, ": ");
        buf_put_typetag(generic, node);
        buf_puts(generic, ", \\\n");
        buf_puts(gnu, "    __builtin_choose_expr(__builtin_types_compatible_p(__typeof__(x), ");
        buf_puts(gnu, node->spelling);
        buf_puts(gnu, "), ");
        buf_put_typetag(gnu, node);
        buf_puts(gnu, ", \\\n");
        ++depth;
    }
    buf_puts(gnu, "    ");
    buf_puts(gnu, tail);
    for (i = 0U; i < depth; ++i) buf_putc(gnu, ')');
}

static void reflect_dispatch_requirements(const ReflectGraph *graph, int enums,
                                          StringSet *out)
{
    size_t i;
    for (i = 0U; i < graph->node_count; ++i) {
        const char *needs = reflect_dispatch_requirement(graph, i, enums);
        if (needs != NULL && needs[0] != '\0') string_set_add(out, needs);
    }
    sort_requirements(out);
}

/* The default hooks and the selection macro `name` for one list. */
static void reflect_emit_dispatch_macro(Buf *out, const ReflectGraph *graph,
                                        const size_t *order, int enums,
                                        const char *name, const char *unknown)
{
    StringSet requirements;
    Buf generic, gnu, tail;
    size_t i;
    memset(&requirements, 0, sizeof(requirements));
    reflect_dispatch_requirements(graph, enums, &requirements);
    buf_init(&generic);
    buf_init(&gnu);
    buf_init(&tail);
    for (i = 0U; i < requirements.count; ++i) {
        buf_puts(out, "#ifndef ");
        buf_put_hook_name(out, reflect_generic_hook(enums), requirements.items[i]);
        buf_puts(out, "\n#define ");
        buf_put_hook_name(out, reflect_generic_hook(enums), requirements.items[i]);
        buf_puts(out, "\n#endif\n#ifndef ");
        buf_put_hook_name(out, reflect_gnu_hook(enums), requirements.items[i]);
        buf_puts(out, "\n#define ");
        buf_put_hook_name(out, reflect_gnu_hook(enums), requirements.items[i]);
        buf_puts(out, "(x, next) next\n#endif\n");
        buf_put_hook_name(&tail, reflect_gnu_hook(enums), requirements.items[i]);
        buf_puts(&tail, "(x, ");
    }
    buf_puts(&tail, unknown);
    for (i = 0U; i < requirements.count; ++i) buf_putc(&tail, ')');
    reflect_emit_dispatch_entries(&generic, &gnu, graph, order, "", enums, tail.p);
    buf_puts(out, "#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L\n#define ");
    buf_puts(out, name);
    buf_puts(out, "(x) _Generic((x), \\\n");
    buf_puts(out, generic.p != NULL ? generic.p : "");
    for (i = 0U; i < requirements.count; ++i) {
        buf_puts(out, "    ");
        buf_put_hook_name(out, reflect_generic_hook(enums), requirements.items[i]);
        buf_puts(out, " \\\n");
    }
    buf_puts(out, "    default: ");
    buf_puts(out, unknown);
    buf_puts(out, ")\n#elif defined(__GNUC__) || defined(__clang__)\n#define ");
    buf_puts(out, name);
    buf_puts(out, "(x) \\\n");
    buf_puts(out, gnu.p != NULL ? gnu.p : "");
    buf_puts(out, "\n#endif\n\n");
    buf_free(&generic);
    buf_free(&gnu);
    buf_free(&tail);
    string_set_free(&requirements);
}

static void reflect_emit_dispatch_core(Buf *out, const ReflectGraph *graph,
                                       const size_t *order)
{
    buf_puts(out,
        "/* typetag_of(x): the id of the type of the expression x; enums are\n"
        "   compatible with their integer type and get its id. enumtag_of(x): the id\n"
        "   of the enum type of x. An argument whose type has no id selects one of\n"
        "   these objects, so the use fails to compile with its name in the message. */\n"
        "struct tmplgen_reflect_unknown_type { int unused; };\n"
        "extern const struct tmplgen_reflect_unknown_type\n"
        "    tmplgen_reflect_no_typetag_for_this_argument_type,\n"
        "    tmplgen_reflect_no_enumtag_for_this_argument_type;\n\n");
    reflect_emit_dispatch_macro(out, graph, order, 0, "TMPLGEN_TYPETAG_OF",
                                "tmplgen_reflect_no_typetag_for_this_argument_type");
    reflect_emit_dispatch_macro(out, graph, order, 1, "TMPLGEN_ENUMTAG_OF",
                                "tmplgen_reflect_no_enumtag_for_this_argument_type");
    reflect_emit_dispatch_api(out);
}

static void reflect_emit_dispatch_hooks(Buf *out, const ReflectGraph *graph,
                                        const size_t *order, const char *requirement,
                                        int enums)
{
    Buf generic, gnu;
    size_t i;
    int any = 0;
    for (i = 0U; i < graph->node_count; ++i) {
        const char *needs = reflect_dispatch_requirement(graph, i, enums);
        if (needs != NULL && str_eq(needs, requirement)) any = 1;
    }
    if (!any) return;
    buf_init(&generic);
    buf_init(&gnu);
    reflect_emit_dispatch_entries(&generic, &gnu, graph, order, requirement, enums, "next");
    buf_puts(out, "#undef ");
    buf_put_hook_name(out, reflect_generic_hook(enums), requirement);
    buf_puts(out, "\n#define ");
    buf_put_hook_name(out, reflect_generic_hook(enums), requirement);
    buf_puts(out, " \\\n");
    buf_puts(out, generic.p != NULL ? generic.p : "");
    buf_puts(out, "\n#undef ");
    buf_put_hook_name(out, reflect_gnu_hook(enums), requirement);
    buf_puts(out, "\n#define ");
    buf_put_hook_name(out, reflect_gnu_hook(enums), requirement);
    buf_puts(out, "(x, next) \\\n");
    buf_puts(out, gnu.p != NULL ? gnu.p : "");
    buf_puts(out, "\n");
    buf_free(&generic);
    buf_free(&gnu);
}

static void reflect_emit_dispatch_section(Buf *out, const ReflectGraph *graph,
                                          const size_t *order, const char *requirement)
{
    reflect_emit_dispatch_hooks(out, graph, order, requirement, 0);
    reflect_emit_dispatch_hooks(out, graph, order, requirement, 1);
}

/* gen/reflection.h stand-in while libclang parses project sources: same API,
 * ids only for builtins. */
void emit_reflection_bootstrap(Buf *out)
{
    size_t i;
    buf_puts(out, "#ifndef TMPLGEN_REFLECTION_H\n#define TMPLGEN_REFLECTION_H\n");
    reflect_emit_schema(out);
    buf_puts(out, "typedef enum TMPLGEN_SYM(TypeTag) {\n"
                  "    TMPLGEN_SYM(TYPETAG_INVALID)");
    for (i = 0U; i < REFLECT_BUILTIN_COUNT; ++i) {
        buf_puts(out, ",\n    TMPLGEN_SYM(TYPETAG_");
        buf_puts(out, g_reflect_builtins[i].name);
        buf_putc(out, ')');
    }
    buf_puts(out, "\n} TMPLGEN_SYM(TypeTag);\n");
    reflect_emit_table_declarations(out, "extern ", REFLECT_BUILTIN_COUNT + 1U);
    reflect_emit_api(out);
    /* Project type ids do not exist yet. A distinct constant per use keeps
     * expressions and case labels that name one (typetag(Kind)) valid, so
     * libclang still reports the types inside them (enumtag_of checks,
     * template calls). */
    buf_puts(out, "#undef TMPLGEN_TYPETAG\n"
                  "#define TMPLGEN_TYPETAG(T) \\\n"
                  "    ((TMPLGEN_SYM(TypeTag))(0x40000000 + __COUNTER__))\n");
    buf_puts(out, "#define TMPLGEN_TYPETAG_OF(x) \\\n"
                  "    ((void)(x), TMPLGEN_SYM(TYPETAG_INVALID))\n"
                  "#define TMPLGEN_ENUMTAG_OF(x) \\\n"
                  "    ((void)(x), TMPLGEN_SYM(TYPETAG_INVALID))\n"
                  "static inline const void *TMPLGEN_SYM(reflect_meta)(\n"
                  "    const TMPLGEN_SYM(TypeField) *field)\n"
                  "{ (void)field; return 0; }\n"
                  "static inline TMPLGEN_SYM(TypeTag) TMPLGEN_SYM(reflect_meta_type)(\n"
                  "    const TMPLGEN_SYM(TypeField) *field)\n"
                  "{ (void)field; return TMPLGEN_SYM(TYPETAG_INVALID); }\n"
                  "#ifndef TMPLGEN_SYMBOL_PREFIX\n"
                  "#define reflect_meta(field) TMPLGEN_SYM(reflect_meta)(field)\n"
                  "#define reflect_meta_type(field) TMPLGEN_SYM(reflect_meta_type)(field)\n"
                  "#endif\n");
    reflect_emit_dispatch_api(out);
    buf_puts(out, "#endif\n");
}

static void reflect_emit_check(Buf *out, const ReflectGraph *graph, const ReflectNode *node)
{
    size_t f;
    buf_puts(out, "TMPLGEN_REFLECT_CHECK(sizeof(");
    buf_puts(out, node->spelling);
    buf_puts(out, ") == ");
    buf_put_ull(out, node->size);
    if (node->align != 0U) {
        buf_puts(out, " && TMPLGEN_REFLECT_ALIGN_IS(");
        buf_puts(out, node->spelling);
        buf_puts(out, ", ");
        buf_put_ull(out, node->align);
        buf_putc(out, ')');
    }
    for (f = 0U; f < node->field_count; ++f) {
        const ReflectField *field = &graph->fields[node->first_field + f];
        if (field->flags & RFIELD_BITFIELD) continue;
        buf_puts(out, " &&\n    offsetof(");
        buf_puts(out, node->spelling);
        buf_puts(out, ", ");
        buf_puts(out, field->name);
        buf_puts(out, ") == ");
        buf_put_ull(out, field->offset_bits / 8U);
    }
    for (f = 0U; f < node->enumerator_count; ++f) {
        const ReflectEnumerator *enumerator =
            &graph->enumerators[node->first_enumerator + f];
        char value[48];
        if (enumerator->is_unsigned) {
            buf_puts(out, " &&\n    (unsigned long long)");
            buf_puts(out, enumerator->name);
            (void)snprintf(value, sizeof(value), " == %lluULL",
                           (unsigned long long)enumerator->value);
        } else {
            buf_puts(out, " &&\n    (long long)");
            buf_puts(out, enumerator->name);
            if (enumerator->value == LLONG_MIN) {
                (void)snprintf(value, sizeof(value), " == (-%lldLL - 1)", LLONG_MAX);
            } else {
                (void)snprintf(value, sizeof(value), " == %lldLL", enumerator->value);
            }
        }
        buf_puts(out, value);
    }
    buf_puts(out, ",\n    \"tmplgen: reflected layout of ");
    emit_escaped_c_string(out, node->display);
    buf_puts(out, " differs from this compiler or target; regenerate with matching "
                  "--clang-arg flags\");\n");
}

static void reflect_emit_meta_object(Buf *out, const ReflectField *field,
                                    size_t field_index)
{
    buf_puts(out, "static const void *const TMPLGEN_SYM(tmplgen_reflect_meta_");
    buf_put_ull(out, (unsigned long long)(field_index + 1U));
    buf_puts(out, ") = &(");
    buf_puts(out, field->meta_type);
    buf_putc(out, ')');
    buf_puts(out, field->meta_init);
    buf_puts(out, ";\n");
}

static void reflect_emit_meta_objects(Buf *out, const ReflectGraph *graph,
                                     const size_t *order, const char *requirement)
{
    size_t n, f;
    for (n = 0U; n < graph->node_count; ++n) {
        const ReflectNode *node = &graph->nodes[order[n]];
        if (!str_eq(node->requirement, requirement)) continue;
        for (f = 0U; f < node->field_count; ++f) {
            if (graph->fields[node->first_field + f].meta_init != NULL) {
                reflect_emit_meta_object(out, &graph->fields[node->first_field + f],
                                         node->first_field + f);
            }
        }
    }
}

static void reflect_emit_meta_api(Buf *out, const ReflectGraph *graph,
                                  const size_t *order)
{
    size_t n, f;
    buf_puts(out,
        "\n/* Field metadata from template @meta annotations. */\n"
        "#ifndef TMPLGEN_REFLECT_META_API\n"
        "#define TMPLGEN_REFLECT_META_API\n"
        "static inline const void *TMPLGEN_SYM(reflect_meta)(\n"
        "    const TMPLGEN_SYM(TypeField) *field)\n"
        "{\n"
        "    switch (field->attr) {\n");
    for (n = 0U; n < graph->node_count; ++n) {
        const ReflectNode *node = &graph->nodes[order[n]];
        for (f = 0U; f < node->field_count; ++f) {
            size_t field_index = node->first_field + f;
            const ReflectField *field = &graph->fields[field_index];
            if (field->meta_init == NULL) continue;
            if (node->requirement[0] != '\0') {
                buf_puts(out, "#if ");
                buf_put_requirement_condition(out, node->requirement);
                buf_putc(out, '\n');
            }
            buf_puts(out, "    case ");
            buf_put_ull(out, (unsigned long long)(field_index + 1U));
            buf_puts(out, ": return TMPLGEN_SYM(tmplgen_reflect_meta_");
            buf_put_ull(out, (unsigned long long)(field_index + 1U));
            buf_puts(out, ");\n");
            if (node->requirement[0] != '\0') buf_puts(out, "#endif\n");
        }
    }
    buf_puts(out,
        "    default: return NULL;\n"
        "    }\n"
        "}\n\n"
        "static inline TMPLGEN_SYM(TypeTag)\n"
        "TMPLGEN_SYM(reflect_meta_type)(const TMPLGEN_SYM(TypeField) *field)\n"
        "{\n"
        "    return (TMPLGEN_SYM(TypeTag))field->meta_type;\n"
        "}\n\n"
        "#ifndef TMPLGEN_SYMBOL_PREFIX\n"
        "#define reflect_meta(field) TMPLGEN_SYM(reflect_meta)(field)\n"
        "#define reflect_meta_type(field) TMPLGEN_SYM(reflect_meta_type)(field)\n"
        "#endif\n"
        "#endif /* TMPLGEN_REFLECT_META_API */\n");
}

static void reflect_emit_tables(Buf *out, const ReflectGraph *graph, const size_t *order,
                                const char *storage)
{
    ReflectStrings strings;
    unsigned *name_offsets;
    unsigned *display_offsets;
    unsigned *field_name_offsets;
    unsigned *enumerator_name_offsets;
    size_t i;
    memset(&strings, 0, sizeof(strings));
    (void)reflect_string(&strings, "");
    name_offsets = (unsigned *)calloc(graph->node_count + 1U, sizeof(unsigned));
    display_offsets = (unsigned *)calloc(graph->node_count + 1U, sizeof(unsigned));
    field_name_offsets = (unsigned *)calloc(graph->field_count + 1U, sizeof(unsigned));
    enumerator_name_offsets = (unsigned *)calloc(graph->enumerator_count + 1U,
                                                 sizeof(unsigned));
    if (name_offsets == NULL || display_offsets == NULL || field_name_offsets == NULL ||
        enumerator_name_offsets == NULL) die("out of memory");
    for (i = 0U; i < graph->node_count; ++i) {
        const ReflectNode *node = &graph->nodes[order[i]];
        name_offsets[order[i]] = reflect_string(&strings, node->name);
        display_offsets[order[i]] = reflect_string(&strings, node->display);
    }
    for (i = 0U; i < graph->field_count; ++i) {
        field_name_offsets[i] = reflect_string(&strings, graph->fields[i].name);
    }
    for (i = 0U; i < graph->enumerator_count; ++i) {
        enumerator_name_offsets[i] = reflect_string(&strings, graph->enumerators[i].name);
    }

    buf_puts(out, storage);
    buf_puts(out, "const char TMPLGEN_SYM(RuntimeTypeStrings)[] =");
    if (strings.pool.len <= 4000U) {
        buf_putc(out, '\n');
        for (i = 0U; i < strings.count; ++i) {
            buf_puts(out, "    \"");
            emit_escaped_c_string(out, strings.strings[i]);
            buf_puts(out, "\\0\"");
            if (i + 1U == strings.count) buf_putc(out, ';');
            buf_puts(out, " /* ");
            buf_put_ull(out, strings.offsets[i]);
            buf_puts(out, " */\n");
        }
    } else {
        /* C99 only guarantees 4095-character string literals; a brace list
         * has no such limit. */
        buf_puts(out, " {\n");
        for (i = 0U; i < strings.count; ++i) {
            const char *p = strings.strings[i];
            buf_puts(out, "    ");
            for (; *p != '\0'; ++p) {
                if (isalnum((unsigned char)*p) || *p == '_' || *p == ' ' || *p == '*' ||
                    *p == '(' || *p == ')' || *p == '[' || *p == ']' || *p == ',' ||
                    *p == ':' || *p == '.') {
                    buf_putc(out, '\'');
                    buf_putc(out, *p);
                    buf_puts(out, "',");
                } else {
                    buf_put_ull(out, (unsigned long long)(unsigned char)*p);
                    buf_putc(out, ',');
                }
            }
            buf_puts(out, "0, /* ");
            buf_put_ull(out, strings.offsets[i]);
            buf_puts(out, ": ");
            buf_puts(out, strings.strings[i]);
            buf_puts(out, " */\n");
        }
        buf_puts(out, "};\n");
    }
    buf_puts(out, "\n");

    buf_puts(out, storage);
    buf_puts(out, "const TMPLGEN_SYM(TypeInfo) TMPLGEN_SYM(RuntimeTypeInfoTable)[");
    buf_put_ull(out, (unsigned long long)graph->node_count + 1ULL);
    buf_puts(out, "] = {\n    /* size, align, kind, -, flags, name, first, count, inner, display */\n"
                  "    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, /* 0: INVALID */\n");
    for (i = 0U; i < graph->node_count; ++i) {
        const ReflectNode *node = &graph->nodes[order[i]];
        unsigned long long first = node->kind == RK_ENUM ? node->first_enumerator :
                                   node->field_count != 0U ? node->first_field : 0U;
        unsigned long long inner = node->inner != (size_t)-1 ?
            graph->nodes[node->inner].id : 0U;
        char row[256];
        (void)snprintf(row, sizeof(row),
            "    { %llu, %llu, %d, 0, %u, %u, %llu, %llu, %llu, %u }, /* %u: ",
            node->size, node->align, node->kind, node->flags, name_offsets[order[i]],
            first, node->count, inner, display_offsets[order[i]], node->id);
        buf_puts(out, row);
        buf_puts(out, node->name);
        buf_puts(out, " */\n");
    }
    buf_puts(out, "};\n\n");

    buf_puts(out, storage);
    buf_puts(out, "const TMPLGEN_SYM(TypeField) TMPLGEN_SYM(RuntimeTypeFieldTable)[] = {\n"
                  "    /* name, type, offset, bit_width, quals, flags, attr, meta_type */\n");
    if (graph->field_count == 0U) buf_puts(out, "    { 0, 0, 0, 0, 0, 0, 0, 0 }\n");
    for (i = 0U; i < graph->field_count; ++i) {
        const ReflectField *field = &graph->fields[i];
        char row[192];
        unsigned long long offset = (field->flags & RFIELD_BITFIELD) ?
            field->offset_bits : field->offset_bits / 8U;
        unsigned attr = field->meta_init != NULL ? (unsigned)(i + 1U) : 0U;
        unsigned meta_type = field->meta_node != (size_t)-1 &&
            field->meta_node < graph->node_count ?
            graph->nodes[field->meta_node].id : 0U;
        (void)snprintf(row, sizeof(row),
                       "    { %u, %u, %llu, %u, %u, %u, %u, %u }, /* %s */\n",
                       field_name_offsets[i], graph->nodes[field->type].id, offset,
                       field->bit_width, field->quals, field->flags, attr, meta_type,
                       field->name);
        buf_puts(out, row);
    }
    buf_puts(out, "};\n\n");

    buf_puts(out, storage);
    buf_puts(out, "const TMPLGEN_SYM(TypeEnumerator) TMPLGEN_SYM(RuntimeTypeEnumTable)[] = {\n");
    if (graph->enumerator_count == 0U) buf_puts(out, "    { 0, 0, 0 }\n");
    for (i = 0U; i < graph->enumerator_count; ++i) {
        const ReflectEnumerator *enumerator = &graph->enumerators[i];
        char row[160];
        if (enumerator->is_unsigned) {
            (void)snprintf(row, sizeof(row), "    { (int64_t)%lluULL, %u, 0 }, /* %s */\n",
                           (unsigned long long)enumerator->value,
                           enumerator_name_offsets[i], enumerator->name);
        } else if (enumerator->value == LLONG_MIN) {
            (void)snprintf(row, sizeof(row), "    { -%lldLL - 1, %u, 0 }, /* %s */\n",
                           LLONG_MAX, enumerator_name_offsets[i], enumerator->name);
        } else {
            (void)snprintf(row, sizeof(row), "    { %lldLL, %u, 0 }, /* %s */\n",
                           enumerator->value, enumerator_name_offsets[i],
                           enumerator->name);
        }
        buf_puts(out, row);
    }
    buf_puts(out, "};\n\n");
    free(name_offsets);
    free(display_offsets);
    free(field_name_offsets);
    free(enumerator_name_offsets);
    reflect_strings_free(&strings);
}

static void reflect_emit_ids(Buf *out, const ReflectGraph *graph, const size_t *order)
{
    size_t i;
    buf_puts(out, "typedef enum TMPLGEN_SYM(TypeTag) {\n"
                  "    TMPLGEN_SYM(TYPETAG_INVALID) = 0");
    for (i = 0U; i < graph->node_count; ++i) {
        const ReflectNode *node = &graph->nodes[order[i]];
        buf_puts(out, ",\n");
        if (i == REFLECT_BUILTIN_COUNT) buf_puts(out, "    /* project types */\n");
        buf_puts(out, "    TMPLGEN_SYM(TYPETAG_");
        buf_puts(out, node->name);
        buf_putc(out, ')');
    }
    buf_puts(out, ",\n    TMPLGEN_SYM(TYPETAG_COUNT)\n} TMPLGEN_SYM(TypeTag);\n\n");
}

static void reflect_emit(const ReflectGraph *graph, const size_t *order, Buf *header,
                         Buf *source)
{
    StringSet requirements;
    char *aggregate_leaf = copy_string(path_leaf(g_output_path));
    size_t i;
    int uses_reflection = 0;
    int has_meta = 0;
    memset(&requirements, 0, sizeof(requirements));
    for (i = 0U; i < graph->field_count; ++i) {
        if (graph->fields[i].meta_init != NULL) { has_meta = 1; break; }
    }
    for (i = 0U; i < graph->node_count; ++i) {
        const ReflectNode *node = &graph->nodes[i];
        if (node->checked && node->requirement[0] != '\0') {
            string_set_add(&requirements, node->requirement);
        }
    }
    reflect_dispatch_requirements(graph, 0, &requirements);
    reflect_dispatch_requirements(graph, 1, &requirements);
    /* Specializations of templates that use reflection wait for
     * TMPLGEN_REFLECT_HAVE_<guard>; every such guard gets a section, even an
     * empty one. */
    for (i = 0U; i < g_instance_count; ++i) {
        const char *requirement = instance_requirement(&g_instances[i]);
        if (!template_index_uses_reflection(g_instances[i].template_index)) continue;
        uses_reflection = 1;
        while (*requirement != '\0') {
            const char *comma = strchr(requirement, ',');
            size_t length = comma != NULL ? (size_t)(comma - requirement) :
                                            strlen(requirement);
            if (length > 21U && strncmp(requirement, "TMPLGEN_REFLECT_HAVE_", 21U) == 0) {
                char *guard = copy_range(requirement + 21U, length - 21U);
                string_set_add(&requirements, guard);
                free(guard);
            }
            requirement += length;
            if (*requirement == ',') ++requirement;
        }
    }
    for (i = 0U; i < g_all_template_info_count; ++i) {
        if (template_uses_reflection(&g_all_template_infos[i])) uses_reflection = 1;
    }
    sort_requirements(&requirements);

    buf_puts(header,
        "/* AUTO-GENERATED by tmplgen (--reflect=project). DO NOT EDIT.\n"
        "   Type ids and layout tables for every project type. The tables are\n"
        "   plain numbers, complete in every translation unit; the sections at the\n"
        "   end let the compiler verify them for the headers (and generated\n"
        "   headers) included before this one. */\n");
    buf_puts(header, "\n#ifndef TMPLGEN_REFLECTION_H\n#define TMPLGEN_REFLECTION_H\n");
    reflect_emit_schema(header);
    reflect_emit_ids(header, graph, order);
    if (source != NULL) {
        reflect_emit_table_declarations(header, "extern ", graph->node_count + 1U);
    } else {
        reflect_emit_tables(header, graph, order, "static ");
    }
    reflect_emit_api(header);
    if (has_meta) reflect_emit_meta_objects(header, graph, order, "");
    reflect_emit_dispatch_core(header, graph, order);
    if (source == NULL) {
        buf_puts(header, "\n/* Builtins and types every translation unit sees. */\n");
        for (i = 0U; i < graph->node_count; ++i) {
            const ReflectNode *node = &graph->nodes[order[i]];
            if (node->checked && node->requirement[0] == '\0') {
                reflect_emit_check(header, graph, node);
            }
        }
    }
    buf_puts(header, "\n#endif /* TMPLGEN_REFLECTION_H */\n");
    for (i = 0U; i < requirements.count; ++i) {
        size_t n;
        buf_putc(header, '\n');
        buf_put_section_open(header, "TMPLGEN_REFLECT_HAVE_", requirements.items[i]);
        if (source == NULL) {
            for (n = 0U; n < graph->node_count; ++n) {
                const ReflectNode *node = &graph->nodes[order[n]];
                if (node->checked && str_eq(node->requirement, requirements.items[i])) {
                    reflect_emit_check(header, graph, node);
                }
            }
        }
        reflect_emit_dispatch_section(header, graph, order, requirements.items[i]);
        if (has_meta) {
            reflect_emit_meta_objects(header, graph, order, requirements.items[i]);
        }
        buf_puts(header, "#endif\n");
    }
    if (has_meta) reflect_emit_meta_api(header, graph, order);
    if (uses_reflection) {
        /* Only the sections: the dispatch macros come from the generated
         * headers themselves, and a template including this header must not
         * see its own functions' dispatch macros. */
        buf_puts(header,
            "\n/* Templates that use reflection: their specializations are compiled\n"
            "   once the sections above have run. */\n"
            "#ifndef TMPLGEN_AGGREGATING\n#define TMPLGEN_AGGREGATING\n"
            "#define TMPLGEN_REFLECT_AGGREGATING_LOCAL\n#endif\n");
        for (i = 0U; i < g_all_template_info_count; ++i) {
            Buf module;
            if (!template_uses_reflection(&g_all_template_infos[i])) continue;
            buf_init(&module);
            buf_puts(&module, g_templates[i].stem);
            buf_puts(&module, ".gen.h");
            emit_include(header, module.p);
            buf_free(&module);
        }
        buf_puts(header,
            "#ifdef TMPLGEN_REFLECT_AGGREGATING_LOCAL\n"
            "#undef TMPLGEN_REFLECT_AGGREGATING_LOCAL\n#undef TMPLGEN_AGGREGATING\n#endif\n");
    }
    if (source != NULL) {
        /* --compiled: one translation unit defines the tables and, including
         * every declaring header, verifies every layout. */
        StringSet headers;
        memset(&headers, 0, sizeof(headers));
        buf_puts(source, "/* AUTO-GENERATED by tmplgen (--reflect=project --compiled). DO NOT EDIT. */\n");
        for (i = 0U; i < graph->node_count; ++i) {
            const ReflectNode *node = &graph->nodes[order[i]];
            if (node->checked && node->header != NULL &&
                !string_set_has(&headers, node->header)) {
                char *relative = relative_path_between(g_generated_dir, node->header);
                string_set_add(&headers, node->header);
                emit_include(source, relative);
                free(relative);
            }
        }
        emit_include(source, aggregate_leaf);
        emit_include(source, "reflection.h");
        buf_putc(source, '\n');
        reflect_emit_tables(source, graph, order, "");
        for (i = 0U; i < graph->node_count; ++i) {
            const ReflectNode *node = &graph->nodes[order[i]];
            if (node->checked) reflect_emit_check(source, graph, node);
        }
        string_set_free(&headers);
    }
    free(aggregate_leaf);
    string_set_free(&requirements);
}

/* ---- driver ------------------------------------------------------------------- */

static void reflect_report_diagnostics(CXTranslationUnit tu, int *failed)
{
    unsigned count = clang_getNumDiagnostics(tu);
    unsigned i;
    for (i = 0U; i < count; ++i) {
        CXDiagnostic diagnostic = clang_getDiagnostic(tu, i);
        if (clang_getDiagnosticSeverity(diagnostic) >= CXDiagnostic_Error) {
            char *text = copy_cx_string(clang_formatDiagnostic(
                diagnostic, clang_defaultDiagnosticDisplayOptions()));
            report_error(NULL, 0U, "reflection: project headers do not compile "
                         "together: %s", text);
            free(text);
            *failed = 1;
        }
        clang_disposeDiagnostic(diagnostic);
    }
}

/* Parses every header that declares a project type together with the new
 * generated headers, collects the type graph and layout, and adds
 * reflection.h (and reflection.gen.c) to `outputs`. */
int reflect_generate(GeneratedFileList *outputs)
{
    ReflectGraph graph;
    ReflectCollect collect;
    CXIndex index;
    CXTranslationUnit tu = NULL;
    Buf main_text, header, source;
    StringSet headers;
    struct CXUnsavedFile *unsaved;
    char **unsaved_paths;
    size_t unsaved_count = 0U;
    const char **arguments;
    int argument_count = (int)g_clang_arg_count + 3;
    char *main_path = join_path(g_generated_dir, ".tmplgen-reflect.c");
    char *aggregate = absolute_path(g_output_path);
    size_t *order;
    size_t i;
    int failed = 0;
    enum CXErrorCode result;

    memset(&graph, 0, sizeof(graph));
    memset(&headers, 0, sizeof(headers));
    for (i = 0U; i < g_type_origin_count; ++i) {
        const char *path = g_type_origins[i].path;
        if (!path_has_suffix(path, ".c")) string_set_add(&headers, path);
    }
    if (headers.count > 1U) {
        qsort(headers.items, headers.count, sizeof(*headers.items), compare_strings);
    }
    for (i = 0U; i < headers.count; ++i) {
        string_set_add(&graph.project_headers, headers.items[i]);
    }
    for (i = 0U; i < outputs->count; ++i) {
        const GeneratedFile *file = &outputs->items[i];
        if (path_has_suffix(file->path, ".h") && file->contents.p != NULL) {
            reflect_meta_scan_text(&graph, file->contents.p);
        }
    }

    buf_init(&main_text);
    for (i = 0U; i < REFLECT_BUILTIN_COUNT; ++i) {
        if (g_reflect_builtins[i].reflect_kind == RK_VOID) continue;
        buf_puts(&main_text, "extern ");
        buf_puts(&main_text, g_reflect_builtins[i].spelling);
        buf_puts(&main_text, " tmplgen_reflect_builtin_");
        buf_put_ull(&main_text, (unsigned long long)i);
        buf_puts(&main_text, ";\n");
    }
    buf_puts(&main_text, "extern void *tmplgen_reflect_pointer_0;\n"
                         "extern char *tmplgen_reflect_pointer_1;\n"
                         "extern const char *tmplgen_reflect_pointer_2;\n");
    for (i = 0U; i < headers.count; ++i) {
        buf_puts(&main_text, "#include \"");
        emit_escaped_c_string(&main_text, headers.items[i]);
        buf_puts(&main_text, "\"\n");
    }
    buf_puts(&main_text, "#include \"");
    emit_escaped_c_string(&main_text, aggregate);
    buf_puts(&main_text, "\"\n");
    for (i = 0U; i < graph.meta_type_count; ++i) {
        buf_puts(&main_text, "extern __typeof__((");
        buf_puts(&main_text, graph.meta_types[i]);
        buf_puts(&main_text, "){0}) tmplgen_reflect_meta_probe_");
        buf_put_ull(&main_text, (unsigned long long)i);
        buf_puts(&main_text, ";\n");
    }

    unsaved = (struct CXUnsavedFile *)calloc(outputs->count + 4U, sizeof(*unsaved));
    unsaved_paths = (char **)calloc(outputs->count + 4U, sizeof(*unsaved_paths));
    if (unsaved == NULL || unsaved_paths == NULL) die("out of memory");
    unsaved_paths[unsaved_count] = copy_string(main_path);
    unsaved[unsaved_count].Filename = unsaved_paths[unsaved_count];
    unsaved[unsaved_count].Contents = main_text.p != NULL ? main_text.p : "";
    unsaved[unsaved_count].Length = (unsigned long)main_text.len;
    ++unsaved_count;
    for (i = 0U; i < outputs->count; ++i) {
        const GeneratedFile *file = &outputs->items[i];
        if (!path_has_suffix(file->path, ".h")) continue;
        unsaved_paths[unsaved_count] = absolute_path(file->path);
        unsaved[unsaved_count].Filename = unsaved_paths[unsaved_count];
        unsaved[unsaved_count].Contents = file->contents.p != NULL ? file->contents.p : "";
        unsaved[unsaved_count].Length = (unsigned long)file->contents.len;
        ++unsaved_count;
    }
    unsaved_paths[unsaved_count] = reflection_header_path();
    unsaved[unsaved_count].Filename = unsaved_paths[unsaved_count];
    unsaved[unsaved_count].Contents = g_reflection_bootstrap.p != NULL ?
        g_reflection_bootstrap.p : "";
    unsaved[unsaved_count].Length = (unsigned long)g_reflection_bootstrap.len;
    ++unsaved_count;
#if defined(CLANGPLUGIN)
    if (g_unsaved_source_path != NULL) {
        unsaved_paths[unsaved_count] = copy_string(g_unsaved_source_path);
        unsaved[unsaved_count].Filename = unsaved_paths[unsaved_count];
        unsaved[unsaved_count].Contents = g_unsaved_source_contents != NULL ?
            g_unsaved_source_contents : "";
        unsaved[unsaved_count].Length = (unsigned long)g_unsaved_source_length;
        ++unsaved_count;
    }
#endif

    arguments = (const char **)malloc((size_t)argument_count * sizeof(*arguments));
    if (arguments == NULL) die("out of memory");
    arguments[0] = "-x";
    arguments[1] = "c";
    arguments[2] = "-fparse-all-comments";
    for (i = 0U; i < g_clang_arg_count; ++i) arguments[i + 3U] = g_clang_args[i];

    for (i = 0U; i < REFLECT_BUILTIN_COUNT; ++i) {
        size_t node = reflect_add_node(&graph, g_reflect_builtins[i].reflect_kind,
                                       g_reflect_builtins[i].spelling);
        graph.nodes[node].name = copy_string(g_reflect_builtins[i].name);
        graph.nodes[node].display = copy_string(g_reflect_builtins[i].spelling);
        graph.nodes[node].spelling = copy_string(g_reflect_builtins[i].spelling);
        graph.nodes[node].project = 1;
    }

    index = clang_createIndex(0, 0);
    if (index == NULL) die("libclang could not create an index");
    result = clang_parseTranslationUnit2(index, main_path, arguments, argument_count,
        unsaved, (unsigned)unsaved_count,
        CXTranslationUnit_SkipFunctionBodies | CXTranslationUnit_KeepGoing, &tu);
    if (result != CXError_Success || tu == NULL) {
        report_error(NULL, 0U, "reflection: libclang could not parse the project "
                     "headers (%d)", (int)result);
        failed = 1;
    } else {
        reflect_report_diagnostics(tu, &failed);
    }
    if (!failed) {
        collect.graph = &graph;
        collect.pass = 1;
        clang_visitChildren(clang_getTranslationUnitCursor(tu), reflect_collect_cursor,
                            &collect);
        collect.pass = 2;
        clang_visitChildren(clang_getTranslationUnitCursor(tu), reflect_collect_cursor,
                            &collect);
        if (g_error_count != 0U) failed = 1;
        if (!failed) {
            if (!reflect_assign_requirements(&graph)) failed = 1;
            else reflect_add_record_pointers(&graph);
        }
    }
    if (!failed) {
        reflect_compute_flags(&graph);
        order = reflect_assign_ids(&graph);
        buf_init(&header);
        buf_init(&source);
        reflect_emit(&graph, order, &header, g_compiled_output ? &source : NULL);
        {
            char *path = reflection_header_path();
            generated_file_list_add(outputs, path, &header);
            free(path);
        }
        if (g_compiled_output) {
            char *path = reflection_source_path();
            generated_file_list_add(outputs, path, &source);
            free(path);
        }
        buf_free(&header);
        buf_free(&source);
        free(order);
    }
    if (tu != NULL) clang_disposeTranslationUnit(tu);
    clang_disposeIndex(index);
    free(arguments);
    list_free(unsaved_paths, unsaved_count);
    free(unsaved);
    buf_free(&main_text);
    string_set_free(&headers);
    free(main_path);
    free(aggregate);
    reflect_graph_free(&graph);
    return !failed;
}
