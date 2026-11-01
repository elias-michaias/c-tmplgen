#!/bin/sh
# Generated headers must only need the argument types a translation unit
# actually uses: specializations that name a project type are compiled in a
# section guarded by the include guard of the header declaring that type.
#
# usage: test/headers.sh TMPLGEN [CC]
set -eu

TMPLGEN=$1
CC=${2:-${CC:-cc}}
WORK=$(mktemp -d "${TMPDIR:-/tmp}/tmplgen-headers.XXXXXX")
[ -n "${KEEP:-}" ] || trap 'rm -rf "$WORK"' EXIT HUP INT TERM

fail() {
    printf 'headers: %s\n' "$*" >&2
    exit 1
}

# ---- fixture project ------------------------------------------------------
P="$WORK/project"
mkdir -p "$P/tmpl"
cat > "$P/tmpl/box.tmpl.h" <<'EOF'
#include <stddef.h>
#include <stdlib.h>

#ifndef TMPLGEN
typedef int T;
typedef int U;
#define Vec(type) Vec
#define Pair(type0, type1) Pair
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

// @template T, U
typedef struct {
    T first;
    U second;
} Pair;

// @template T, U
static inline Pair(T, U) pair_make(T first, U second)
{
    Pair(T, U) pair;
    pair.first = first;
    pair.second = second;
    return pair;
}
EOF
cat > "$P/foo.h" <<'EOF'
#ifndef FOO_H
#define FOO_H
typedef struct { int id; } Foo;          /* tagless: cannot be forward declared */
#endif
EOF
cat > "$P/item.h" <<'EOF'
#ifndef ITEM_H
#define ITEM_H
typedef struct { double weight; } Item;
#include "gen/all.gen.h"                  /* uses a specialization of its own type */
struct Inventory { Vec(Item) items; };
#endif
EOF
# a.c: its own types first, then the generated header.
cat > "$P/a.c" <<'EOF'
#include "foo.h"
#include "gen/all.gen.h"
int a_check(void)
{
    Vec(Foo) foos = {0};
    Foo foo = { 7 };
    Pair(int, Foo) tagged = pair_make(1, foo);
    push(&foos, foo);
    {
        int ok = foos.data[0].id == 7 && tagged.second.id == 7;
        free(foos.data);
        return ok;
    }
}
EOF
# b.c: never sees Foo or Item; must not need them (PROBLEM.md case 1).
cat > "$P/b.c" <<'EOF'
#include "gen/all.gen.h"
int a_check(void);
int c_check(void);
int d_check(void);
int main(void)
{
    Vec(int) numbers = {0};
    Pair(int, double) mixed = pair_make(1, 2.5);
    int ok;
    push(&numbers, 42);
    ok = numbers.data[0] == 42 && mixed.second == 2.5 &&
         a_check() && c_check() && d_check();
    free(numbers.data);
    return ok ? 0 : 1;
}
EOF
# c.c: first include of the generated header happens inside item.h, before
# foo.h (PROBLEM.md case 2); a two-header specialization; a nested one.
cat > "$P/c.c" <<'EOF'
#include "item.h"
#include "foo.h"
#include "gen/all.gen.h"
int c_check(void)
{
    struct Inventory inventory = {0};
    Item item = { 2.0 };
    Foo foo = { 3 };
    Pair(Foo, Item) both = pair_make(foo, item);
    Vec(Vec_Foo) nested = {0};
    Vec(Foo) inner = {0};
    push(&inventory.items, item);
    push(&inner, foo);
    push(&nested, inner);
    {
        int ok = inventory.items.data[0].weight == 2.0 && both.first.id == 3 &&
                 both.second.weight == 2.0 && nested.data[0].data[0].id == 3;
        free(inventory.items.data);
        free(inner.data);
        free(nested.data);
        return ok;
    }
}
EOF
# d.c: generated header first, types later, generated header again.
cat > "$P/d.c" <<'EOF'
#include "gen/all.gen.h"
#include "foo.h"
#include "gen/all.gen.h"
int d_check(void)
{
    Vec(Foo) foos = {0};
    Foo foo = { 9 };
    push(&foos, foo);
    {
        int ok = foos.data[0].id == 9;
        free(foos.data);
        return ok;
    }
}
EOF

compile_project() {
    label=$1
    shift
    for source in a b c d; do
        if ! "$CC" "$@" -Wall -Wextra -Wconversion -Wshadow -Werror \
                -c "$P/$source.c" -o "$WORK/$source.o"; then
            fail "$label: $source.c did not compile"
        fi
    done
    extra=
    if [ -f "$P/gen/box.gen.c" ]; then
        "$CC" "$@" -Wall -Wextra -Werror -c "$P/gen/box.gen.c" \
            -o "$WORK/box.gen.o" || fail "$label: box.gen.c did not compile"
        extra="$WORK/box.gen.o"
    fi
    # shellcheck disable=SC2086
    "$CC" "$WORK/a.o" "$WORK/b.o" "$WORK/c.o" "$WORK/d.o" $extra \
        -o "$WORK/program" || fail "$label: link failed"
    "$WORK/program" || fail "$label: program returned failure"
}

generate() {
    (cd "$P" && "$TMPLGEN" --clang-arg=-std=c11 "$@" >/dev/null)
}

for dispatch in auto generic gnu; do
    rm -rf "$P/gen"
    generate --dispatch "$dispatch" || fail "generation failed ($dispatch)"
    case $dispatch in
        gnu) compile_project "dispatch=$dispatch" -std=gnu99 ;;
        generic) compile_project "dispatch=$dispatch" -std=c11 -pedantic ;;
        *)
            compile_project "dispatch=$dispatch c11" -std=c11 -pedantic
            compile_project "dispatch=$dispatch gnu99" -std=gnu99
            ;;
    esac
done

rm -rf "$P/gen"
generate --compiled || fail "generation failed (--compiled)"
compile_project "compiled" -std=c11 -pedantic

# A file that uses a type but never includes the generated header after it
# fails locally, naming the missing specialization.
rm -rf "$P/gen"
generate || fail "generation failed"
cat > "$WORK/wrong_order.c" <<'EOF'
#include "gen/all.gen.h"
#include "foo.h"
int wrong(void) { Vec(Foo) foos = {0}; return (int)foos.len; }
EOF
cp "$WORK/wrong_order.c" "$P/wrong_order.c"
if "$CC" -std=c11 -c "$P/wrong_order.c" -o "$WORK/wrong.o" \
        >"$WORK/wrong.log" 2>&1; then
    fail "wrong include order unexpectedly compiled"
fi
grep -q 'Vec_Foo' "$WORK/wrong.log" ||
    fail "wrong include order did not name the missing specialization"
rm -f "$P/wrong_order.c"

# ---- generation errors ------------------------------------------------------
expect_error() {
    label=$1
    message=$2
    if (cd "$P" && "$TMPLGEN" --clang-arg=-std=c11 >"$WORK/error.log" 2>&1); then
        fail "$label: generation unexpectedly succeeded"
    fi
    grep -Fq "$message" "$WORK/error.log" || {
        cat "$WORK/error.log" >&2
        fail "$label: missing '$message'"
    }
}

# A template argument type declared in a .c file (PROBLEM.md case 3).
cat > "$P/e.c" <<'EOF'
#include "gen/all.gen.h"
typedef struct Local { int x; } Local;
void e_use(void) { Vec(Local) v = {0}; Local l = { 1 }; push(&v, l); free(v.data); }
EOF
expect_error "type in .c file" "template argument type 'Local' is declared in"
rm -f "$P/e.c"

# A declaring header without an include-guard macro.
cat > "$P/bare.h" <<'EOF'
#pragma once
typedef struct { int b; } Bare;
EOF
cat > "$P/f.c" <<'EOF'
#include "bare.h"
#include "gen/all.gen.h"
void f_use(void) { Vec(Bare) v = {0}; Bare b = { 1 }; push(&v, b); free(v.data); }
EOF
expect_error "unguarded header" "which has no #ifndef/#define include guard"
rm -f "$P/bare.h" "$P/f.c"

# A header that includes the generated header before declaring its type.
cat > "$P/late.h" <<'EOF'
#ifndef LATE_H
#define LATE_H
#include "gen/all.gen.h"
typedef struct { int l; } Late;
#endif
EOF
cat > "$P/g.c" <<'EOF'
#include "late.h"
void g_use(void) { Vec(Late) v = {0}; Late l = { 1 }; push(&v, l); free(v.data); }
EOF
expect_error "late declaration" "includes a generated header before declaring template argument type 'Late'"
rm -f "$P/late.h" "$P/g.c"

printf '%s\n' 'headers: all checks passed'
