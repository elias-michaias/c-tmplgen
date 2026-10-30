#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CC=${CC:-cc}
"$ROOT/test/run.sh"

BUILD=$(mktemp -d "$ROOT/test/.negative.XXXXXX")
trap 'rm -rf "$BUILD"' EXIT HUP INT TERM
"$CC" -std=c99 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    "$ROOT/tmplgen.c" -lclang -o "$BUILD/tmplgen"

failed=0
for fixture in "$ROOT"/test/negative/*.c.in; do
    filename=${fixture##*/}
    name=${filename%.c.in}
    if [ "$name" = unmatched_structural_rows ]; then
        continue
    fi
    printf '\n===== %s =====\n' "$filename"
    if "$CC" -x c -I "$ROOT" -I "$ROOT/test" -std=c11 -Wall -Wextra \
        -pedantic -Wconversion -Wshadow -Werror=incompatible-pointer-types \
        "$fixture" -o "$BUILD/$name"; then
        printf 'ERROR: %s unexpectedly compiled\n' "$filename" >&2
        failed=1
    else
        printf 'Expected compile failure for %s (diagnostics above).\n' "$filename"
    fi
done

SUBTYPE="$BUILD/subtyping"
mkdir -p "$SUBTYPE/tmpl" "$SUBTYPE/gen" "$SUBTYPE/test/lib" "$SUBTYPE/lib"
cp -R "$ROOT/test/tmpl/." "$SUBTYPE/tmpl/"
cp "$ROOT/test/lib/physics.h" "$SUBTYPE/test/lib/physics.h"
cp "$ROOT/test/lib/physics.h" "$SUBTYPE/lib/physics.h"
cp "$ROOT/test/negative/unmatched_structural_rows.c.in" "$SUBTYPE/main.c"
printf '\n===== unmatched_structural_rows.c.in =====\n'
if ! (CDPATH= cd -- "$SUBTYPE" && "$BUILD/tmplgen" \
    --source-root "$SUBTYPE" --template-dir "$SUBTYPE/tmpl" \
    --output "$SUBTYPE/gen/all.gen.h" \
    --clang-arg=-std=c11); then
    printf '%s\n' 'tmplgen failed before compiling unmatched_structural_rows.c.in' >&2
    failed=1
elif (CDPATH= cd -- "$SUBTYPE" && "$CC" -std=c11 -Wall -Wextra \
    -pedantic -Wconversion -Wshadow "$SUBTYPE/main.c" -o "$BUILD/unmatched-structural"); then
    printf '%s\n' 'ERROR: unmatched_structural_rows.c.in unexpectedly compiled' >&2
    failed=1
else
    printf '%s\n' 'Expected compile failure for unmatched_structural_rows.c.in (diagnostics above).'
fi

exit "$failed"
