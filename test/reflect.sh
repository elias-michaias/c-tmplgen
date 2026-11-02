#!/bin/sh
# --reflect=project: gen/reflection.h holds type ids and integer layout tables
# that are complete in every translation unit; sections guarded by each
# declaring header's include guard let the compiler verify the numbers.
#
# usage: test/reflect.sh TMPLGEN [CC]
set -eu

TMPLGEN=$1
CC=${2:-${CC:-cc}}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WORK=$(mktemp -d "${TMPDIR:-/tmp}/tmplgen-reflect.XXXXXX")
[ -n "${KEEP:-}" ] || trap 'rm -rf "$WORK"' EXIT HUP INT TERM

fail() {
    printf 'reflect: %s\n' "$*" >&2
    exit 1
}

P="$WORK/project"
mkdir -p "$P/tmpl"
cat > "$P/tmpl/vec.tmpl.h" <<'EOF'
#include <stddef.h>
#include <stdlib.h>

#ifndef TMPLGEN
typedef int T;
#define Vec(type) Vec
#endif

// @template T
typedef struct {
    T *data;
    size_t len, cap;
} Vec;

// @template T
static inline void push(Vec(T) *v, T x)
{
    if (v->len == v->cap) {
        v->cap = v->cap ? v->cap * 2 : 8;
        v->data = (T *)realloc(v->data, v->cap * sizeof(T));
    }
    v->data[v->len++] = x;
}
EOF
# count.tmpl.h: a template that uses reflection in its bodies.
cat > "$P/tmpl/count.tmpl.h" <<'EOF'
#include "../gen/reflection.h"
#ifndef TMPLGEN
typedef int T;
#endif
// @template T
static inline unsigned field_count(T *value)
{
    const TypeInfo *info = typeinfo(typetag_of(*value));
    return info->kind == TYPEKIND_STRUCT || info->kind == TYPEKIND_UNION ? info->count : 0u;
}
EOF
cat > "$P/point.h" <<'EOF'
#ifndef POINT_H
#define POINT_H
typedef struct {
#ifdef BIG_POINT            /* simulates a layout tmplgen did not see */
    double x, y;
#else
    float x, y;
#endif
} Point;
#endif
EOF
cat > "$P/player.h" <<'EOF'
#ifndef PLAYER_H
#define PLAYER_H
#include "point.h"
typedef enum { KIND_WARRIOR, KIND_MAGE } Kind;
typedef enum { LEVEL_LOW = -1, LEVEL_HIGH = 7 } Level;
struct Flags { int a; unsigned bits : 3; };
typedef struct {
    Point pos;
    int hp;
    Kind kind;
    char name[16];
    const char *title;
    union { int code; float ratio; };
} Player;
#include "gen/all.gen.h"
struct Party { Vec(Player) members; Level level; };
#endif
EOF
# printer.c: includes no project header, yet walks any value it is given.
cat > "$P/printer.c" <<'EOF'
#include <stdio.h>
#include <string.h>
#include "gen/reflection.h"
void print_value(FILE *out, Any v, int depth)
{
    const TypeInfo *ti = typeinfo(v.type);
    uint32_t i;
    switch (ti->kind) {
    case TYPEKIND_INT: {
        long long x = 0;
        if (ti->size == 1) x = *(const signed char *)v.data;
        else if (ti->size == 4) { int32_t y; memcpy(&y, v.data, 4); x = y; }
        else memcpy(&x, v.data, sizeof x);
        fprintf(out, "%lld", x);
        return;
    }
    case TYPEKIND_FLOAT: {
        float f;
        memcpy(&f, v.data, sizeof f);
        fprintf(out, "%g", (double)f);
        return;
    }
    case TYPEKIND_ENUM: {
        int32_t x;
        const TypeEnumerator *e = RuntimeTypeEnumTable + ti->first;
        memcpy(&x, v.data, sizeof x);
        for (i = 0; i < ti->count; i++) {
            if (e[i].value == x) { fputs(reflect_str(e[i].name), out); return; }
        }
        fprintf(out, "%d", (int)x);
        return;
    }
    case TYPEKIND_ARRAY:
        fprintf(out, "\"%.*s\"", (int)ti->count, (const char *)v.data);
        return;
    case TYPEKIND_POINTER: {
        const char *s;
        memcpy(&s, v.data, sizeof s);
        fprintf(out, "\"%s\"", s);
        return;
    }
    case TYPEKIND_STRUCT:
        fprintf(out, "%s {", typestr(v.type));
        for (i = 0; i < ti->count; i++) {
            const TypeField *f = &RuntimeTypeFieldTable[ti->first + i];
            fprintf(out, " .%s = ", reflect_str(f->name));
            print_value(out, any_field(v, f), depth + 1);
            fputc(';', out);
        }
        fputs(" }", out);
        return;
    default:
        fputs("?", out);
    }
}
EOF
cat > "$P/main.c" <<'EOF'
#include <stdio.h>
#include <string.h>
#include "player.h"
#include "gen/reflection.h"
void print_value(FILE *out, Any v, int depth);
int main(void)
{
    struct Party party = {0};
    Player player = { { 1.5f, 2.5f }, 10, KIND_MAGE, "bob", "Sir", { 3 } };
    Point *where = &player.pos;
    const char *label = "x";
    int ok = 1;
    push(&party.members, player);
    print_value(stdout, any_of(player), 0);
    putchar('\n');
    {
        /* typetag_of / any_of select by static type, -Wswitch friendly */
        Any items[4];
        items[0] = any_of(player);
        items[1] = any_of(party.members);
        items[2] = any_of(where);
        items[3] = any_of(label);
        ok &= items[0].type == typetag(Player) && items[1].type == typetag(Vec_Player) &&
              items[2].type == typetag(Point_ptr) && items[3].type == typetag(const_char_ptr);
        switch (typetag_of(player.pos)) {
        case typetag(Point): break;
        default: ok = 0; break;
        }
        ok &= typeinfo(typetag_of(player.hp)) == typeinfo(typetag(int));
        ok &= enumtag_of(player.kind) == typetag(Kind) && enumtag_of(party.level) == typetag(Level);
    }
    ok &= typeinfo(typetag(Player))->size == sizeof(Player);
    ok &= typeinfo(typetag(Vec_Player))->size == sizeof(Vec_Player);
    ok &= (typeinfo(typetag(Vec_Player))->flags & TYPEFLAG_SPECIALIZED) != 0;
    ok &= (typeinfo(typetag(Point))->flags & TYPEFLAG_POINTER_FREE) != 0;
    ok &= (typeinfo(typetag(Point))->flags & TYPEFLAG_NO_PADDING) != 0;
    ok &= (typeinfo(typetag(Player))->flags & TYPEFLAG_POINTER_FREE) == 0;
    ok &= (typeinfo(typetag(struct_Flags))->flags & TYPEFLAG_HAS_BITFIELDS) != 0;
    ok &= RuntimeTypeEnumTable[typeinfo(typetag(Level))->first].value == -1;
    ok &= typeinfo(typetag(Level))->count == 2;
    ok &= typeinfo(typetag(Player))->count == 7;      /* union members flattened */
    {
        int fields = 0;
        const TypeInfo *info = typeinfo(typetag(Player));
        uint32_t i;
        for (i = 0; i < info->count; i++) {
            const TypeField *f = &RuntimeTypeFieldTable[info->first + i];
            if (strcmp(reflect_str(f->name), "ratio") == 0 && f->offset == offsetof(Player, ratio)) ++fields;
            if (strcmp(reflect_str(f->name), "title") == 0 && f->type == typetag(const_char_ptr)) ++fields;
        }
        ok &= fields == 2;
    }
    ok &= strcmp(typestr(typetag(Vec_Player)), "Vec(Player)") == 0;
    /* count.tmpl.h: specializations compiled once reflection.h saw the types */
    ok &= field_count(&player) == 7 && field_count(&party.members) == 3 &&
          field_count(&player.hp) == 0;
    free(party.members.data);
    return ok ? 0 : 1;
}
EOF
# other.c: reflection.h before the type's header, then again after it.
cat > "$P/other.c" <<'EOF'
#include "gen/reflection.h"
#include "point.h"
#include "gen/reflection.h"
int other(void)
{
    Point p = { 0, 0 };
    return typetag_of(p) == typetag(Point) ? (int)typeinfo(typetag(Point))->size : -1;
}
EOF
# prefixed.c: TMPLGEN_SYMBOL_PREFIX renames every identifier; the macros are
# then only available under their TMPLGEN_ names. The prefix is project-wide,
# so this is its own project (count.tmpl.h is written for unprefixed names).
PX="$WORK/prefixed"
mkdir -p "$PX/tmpl"
cp "$P/tmpl/vec.tmpl.h" "$PX/tmpl/"
cp "$P/point.h" "$P/player.h" "$PX/"
cat > "$PX/prefixed.c" <<'EOF'
#define TMPLGEN_SYMBOL_PREFIX tmplgen_
#include "player.h"
#include "gen/reflection.h"
#if defined(typetag) || defined(typetag_of) || defined(enumtag_of) || \
    defined(any_of)
#error "short aliases must not exist with a prefix"
#endif
int main(void)
{
    Player player = { { 1.0f, 2.0f }, 10, KIND_MAGE, "ann", "Dame", { 1 } };
    tmplgen_Any value = TMPLGEN_ANY_OF(player);
    const tmplgen_TypeInfo *info = tmplgen_typeinfo(TMPLGEN_TYPETAG(Point));
    tmplgen_TypeTag id = TMPLGEN_TYPETAG(Point);
    return id == tmplgen_TYPETAG_Point && info->size == sizeof(Point) &&
           tmplgen_typestr(id)[0] == 'P' && value.type == tmplgen_TYPETAG_Player &&
           TMPLGEN_TYPETAG_OF(player.pos) == id &&
           TMPLGEN_ENUMTAG_OF(player.kind) == tmplgen_TYPETAG_Kind ? 0 : 1;
}
EOF

generate() {
    (cd "$P" && "$TMPLGEN" --clang-arg=-std=c11 --reflect=project "$@" >/dev/null)
}

compile_all() {
    label=$1
    shift
    for source in main printer other; do
        "$CC" "$@" -Wall -Wextra -Wconversion -Wshadow -Werror \
            -c "$P/$source.c" -o "$WORK/$source.o" ||
            fail "$label: $source.c did not compile"
    done
    extra=
    for generated in "$P"/gen/*.gen.c; do
        [ -f "$generated" ] || continue
        object="$WORK/$(basename "$generated" .c).o"
        "$CC" "$@" -Wall -Wextra -Werror -c "$generated" -o "$object" ||
            fail "$label: $(basename "$generated") did not compile"
        extra="$extra $object"
    done
    # shellcheck disable=SC2086
    "$CC" "$WORK/main.o" "$WORK/printer.o" "$WORK/other.o" $extra -o "$WORK/program" ||
        fail "$label: link failed"
    "$WORK/program" >"$WORK/output.txt" || fail "$label: program checks failed"
    grep -q 'Player { .pos = Point { .x = 1.5; .y = 2.5; }; .hp = 10; .kind = KIND_MAGE; .name = "bob"; .title = "Sir"; .code = 3; .ratio = ' \
        "$WORK/output.txt" || {
        cat "$WORK/output.txt" >&2
        fail "$label: printer output differs"
    }
}

rm -rf "$P/gen"
generate || fail "generation failed"
(cd "$P" && "$TMPLGEN" --clang-arg=-std=c11 --reflect=project --check >/dev/null) ||
    fail "--check right after generation reported differences"
cp "$P/gen/reflection.h" "$WORK/reflection.cached.h"
generate --no-cache || fail "uncached generation failed"
cmp -s "$P/gen/reflection.h" "$WORK/reflection.cached.h" ||
    fail "uncached generation differs from the cached one"

compile_all "c11" -std=c11 -pedantic
compile_all "gnu99" -std=gnu99
compile_all "c17" -std=c17 -pedantic
(cd "$PX" && "$TMPLGEN" --clang-arg=-std=c11 --reflect=project >/dev/null) ||
    fail "generation failed (prefixed)"
for std in c11 gnu99; do
    "$CC" -std=$std -Wall -Wextra -Werror -I"$PX" "$PX/prefixed.c" -o "$WORK/prefixed-bin" ||
        fail "prefixed.c did not compile ($std)"
    "$WORK/prefixed-bin" || fail "prefixed.c checks failed ($std)"
done

# A layout the generator did not see fails the check, naming the type.
if "$CC" -std=c11 -DBIG_POINT -c "$P/main.c" -o "$WORK/big.o" >"$WORK/big.log" 2>&1; then
    fail "a layout mismatch compiled"
fi
grep -q 'reflected layout of Point differs' "$WORK/big.log" ||
    fail "layout mismatch did not name the type"

# --compiled: tables in reflection.gen.c, which also runs every check.
rm -rf "$P/gen"
generate --compiled || fail "generation failed (--compiled)"
compile_all "compiled" -std=c11 -pedantic
if "$CC" -std=c11 -DBIG_POINT -c "$P/gen/reflection.gen.c" -o "$WORK/big.o" \
        >"$WORK/big.log" 2>&1; then
    fail "--compiled: reflection.gen.c accepted a layout mismatch"
fi

# examples/reflect_demo.c: print -> parse -> compare, for a pointer-free type.
cat > "$P/record.h" <<'EOF'
#ifndef RECORD_H
#define RECORD_H
#include "player.h"
typedef struct {
    Point pos;
    int hp;
    Kind kind;
    Level level;
    char name[16];
    unsigned short tags[3];
    double weight;
    _Bool alive;
} Record;
#endif
EOF
cat > "$P/roundtrip.c" <<'EOF'
#include <stdio.h>
#include <string.h>
#include "record.h"
#include "gen/reflection.h"
void reflect_print(FILE *out, Any value);
int reflect_parse(Any destination, const char *text);
int main(void)
{
    Record a, b;
    char text[2048];
    FILE *memory;
    long length;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    a.pos.x = 1.5f; a.pos.y = -2.25f; a.hp = 10; a.kind = KIND_MAGE;
    a.level = LEVEL_LOW; strcpy(a.name, "bob"); a.tags[0] = 7; a.tags[2] = 65535;
    a.weight = 0.1; a.alive = 1;
    memory = tmpfile();
    if (memory == NULL) return 2;
    reflect_print(memory, any_of(a));
    length = ftell(memory);
    rewind(memory);
    if (length <= 0 || (size_t)length >= sizeof text) return 3;
    text[fread(text, 1, (size_t)length, memory)] = '\0';
    fclose(memory);
    fputs(text, stdout);
    if (!reflect_parse(any_of(b), text)) { puts("parse error"); return 4; }
    return memcmp(&a, &b, sizeof a) != 0;
}
EOF
rm -rf "$P/gen"
generate || fail "generation failed (round trip)"
for std in c11 gnu99; do
    "$CC" -std=$std -Wall -Wextra -Werror -I"$P" "$P/roundtrip.c" \
        "$ROOT/examples/reflect_demo.c" -o "$WORK/roundtrip" ||
        fail "round trip did not compile ($std)"
    "$WORK/roundtrip" >"$WORK/roundtrip.txt" || {
        cat "$WORK/roundtrip.txt" >&2
        fail "print -> parse round trip changed the value ($std)"
    }
    grep -q '\.kind = KIND_MAGE' "$WORK/roundtrip.txt" ||
        fail "round trip output lacks enum names ($std)"
done
rm -f "$P/record.h" "$P/roundtrip.c"

# typetag_of on a type without an id, and enumtag_of on a non-enum, fail
# naming the problem.
rm -rf "$P/gen"
generate || fail "generation failed"
cat > "$P/unknown.c" <<'EOF'
#include "gen/reflection.h"
struct Local { int a; };
int unknown(void) { struct Local l = { 1 }; TypeTag id = typetag_of(l); return (int)id; }
EOF
cat > "$P/notenum.c" <<'EOF'
#include "point.h"
#include "gen/reflection.h"
int notenum(void) { Point p = { 0, 0 }; TypeTag id = enumtag_of(p); return (int)id; }
EOF
for case in unknown:typetag notenum:enumtag; do
    source=${case%%:*}
    macro=${case#*:}
    for std in c11 gnu99; do
        if "$CC" -std=$std -c "$P/$source.c" -o "$WORK/$source.o" >"$WORK/$source.log" 2>&1; then
            rm -f "$P/unknown.c" "$P/notenum.c"
            fail "$source.c compiled ($std)"
        fi
        grep -q "tmplgen_reflect_no_${macro}_for_this_argument_type\|tmplgen_reflect_unknown_type" \
            "$WORK/$source.log" || {
            cat "$WORK/$source.log" >&2
            rm -f "$P/unknown.c" "$P/notenum.c"
            fail "$source.c error did not name the problem ($std)"
        }
    done
done
rm -f "$P/unknown.c" "$P/notenum.c"

# ---- generation errors ------------------------------------------------------
expect_error() {
    label=$1
    message=$2
    if (cd "$P" && "$TMPLGEN" --clang-arg=-std=c11 --reflect=project \
            >"$WORK/error.log" 2>&1); then
        fail "$label: generation unexpectedly succeeded"
    fi
    grep -Fq "$message" "$WORK/error.log" || {
        cat "$WORK/error.log" >&2
        fail "$label: missing '$message'"
    }
}

rm -rf "$P/gen"
cat > "$P/bare.h" <<'EOF'
#pragma once
typedef struct { int b; } Bare;
EOF
expect_error "#pragma once header" "without an #ifndef/#define include guard"
rm -f "$P/bare.h"

cat > "$P/late.h" <<'EOF'
#ifndef LATE_H
#define LATE_H
#include "gen/reflection.h"
typedef struct { int l; } Late;
#endif
EOF
expect_error "late declaration" "includes gen/reflection.h before declaring reflected type 'Late'"
rm -f "$P/late.h"

# A template that uses reflection needs reflection.h to exist.
rm -rf "$P/gen"
if (cd "$P" && "$TMPLGEN" --clang-arg=-std=c11 >"$WORK/error.log" 2>&1); then
    fail "a template using reflection was specialized without --reflect=project"
fi
grep -Fq "includes gen/reflection.h; generate with --reflect=project" "$WORK/error.log" || {
    cat "$WORK/error.log" >&2
    fail "missing error for a template using reflection without --reflect=project"
}

printf '%s\n' 'reflect: all checks passed'
