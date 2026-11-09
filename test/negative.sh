#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CC=${CC:-cc}
"$ROOT/test/run.sh"

BUILD=$(mktemp -d "$ROOT/test/.negative.XXXXXX")
trap 'rm -rf "$BUILD"' EXIT HUP INT TERM
"$CC" -std=c99 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    "$ROOT"/src/*.c -lclang -o "$BUILD/tmplgen"

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
cp "$ROOT/test/negative/structural_rows_types.h" "$SUBTYPE/structural_rows_types.h"
printf '\n===== unmatched_structural_rows.c.in =====\n'
if ! (CDPATH= cd -- "$SUBTYPE" && "$BUILD/tmplgen" \
    --source-root "$SUBTYPE" --template-dir "$SUBTYPE/tmpl" \
    --output "$SUBTYPE/gen/all.gen.h" \
    --clang-arg=-std=c11); then
    printf '%s\n' 'tmplgen failed before compiling unmatched_structural_rows.c.in' >&2
    failed=1
elif (CDPATH= cd -- "$SUBTYPE" && "$CC" -std=c11 -Wall -Wextra \
    -pedantic -Wconversion -Wshadow "$SUBTYPE/main.c" \
    -o "$BUILD/unmatched-structural" >"$BUILD/subtyping.log" 2>&1); then
    cat "$BUILD/subtyping.log" >&2
    printf '%s\n' 'ERROR: unmatched_structural_rows.c.in unexpectedly compiled' >&2
    failed=1
else
    cat "$BUILD/subtyping.log" >&2
    printf '%s\n' 'Expected compile failure for unmatched_structural_rows.c.in (diagnostics above).'
fi

TYPEVAR="$BUILD/type-variable"
mkdir -p "$TYPEVAR/tmpl" "$TYPEVAR/gen" "$TYPEVAR/test/cases" \
    "$TYPEVAR/test/lib" "$TYPEVAR/lib"
cp -R "$ROOT/test/tmpl/." "$TYPEVAR/tmpl/"
cp "$ROOT/test/cases/test_types.h" "$TYPEVAR/test/cases/test_types.h"
cp "$ROOT/test/lib/physics.h" "$TYPEVAR/test/lib/physics.h"
cp "$ROOT/test/lib/physics.h" "$TYPEVAR/lib/physics.h"
cp "$ROOT/test/negative/type_variable_mismatch.c.in" "$TYPEVAR/main.c"
printf '\n===== type_variable_mismatch.c.in =====\n'
if ! "$BUILD/tmplgen" --source-root "$TYPEVAR" \
    --template-dir "$TYPEVAR/tmpl" --output "$TYPEVAR/gen/all.gen.h" \
    --clang-arg=-std=c11; then
    printf '%s\n' 'tmplgen failed before compiling type_variable_mismatch.c.in' >&2
    failed=1
elif (CDPATH= cd -- "$TYPEVAR" && "$CC" -std=c11 -Wall -Wextra \
    -pedantic -Wconversion -Wshadow main.c -o "$BUILD/type-variable" \
    >"$BUILD/type-variable.log" 2>&1); then
    cat "$BUILD/type-variable.log" >&2
    printf '%s\n' 'ERROR: type_variable_mismatch.c.in unexpectedly compiled' >&2
    failed=1
else
    cat "$BUILD/type-variable.log" >&2
    if ! grep -Fq 'tmplgen: max type variable T must be consistent across arguments (got float vs char *)' \
        "$BUILD/type-variable.log"; then
        printf '%s\n' 'same-variable argument mismatch was not diagnosed clearly' >&2
        failed=1
    fi
    if ! grep -Fq 'tmplgen: option_choose type variable T must be consistent across arguments (got TestInt vs double)' \
        "$BUILD/type-variable.log"; then
        printf '%s\n' 'nested constructor mismatch was not diagnosed clearly' >&2
        failed=1
    fi
    if ! grep -Fq 'tmplgen: swap_values type variable T must be consistent across arguments (got double vs int)' \
        "$BUILD/type-variable.log"; then
        printf '%s\n' 'zero-association mismatch was not diagnosed clearly' >&2
        failed=1
    fi
    if ! grep -Fq 'main.c:12:' "$BUILD/type-variable.log" ||
       ! grep -Fq 'main.c:14:' "$BUILD/type-variable.log" ||
       ! grep -Fq 'main.c:15:' "$BUILD/type-variable.log"; then
        printf '%s\n' 'type-variable mismatch diagnostics were not mapped to their callsites' >&2
        failed=1
    fi
    if grep -Fq "call to undeclared function 'swap_values'" "$BUILD/type-variable.log"; then
        printf '%s\n' 'mismatch-only generic call fell through to an undeclared function' >&2
        failed=1
    fi
    if grep -Fq '__builtin_types_compatible_p' "$BUILD/type-variable.log"; then
        printf '%s\n' 'type-variable diagnostics exposed the generated compatibility expression' >&2
        failed=1
    fi
    printf '%s\n' 'Expected compile failure for type_variable_mismatch.c.in (diagnostics above).'
fi

printf '\n===== type_variable_mismatch.c.in (reflection) =====\n'
if (CDPATH= cd -- "$TYPEVAR" && "$BUILD/tmplgen" \
    --source-root "$TYPEVAR" --template-dir "$TYPEVAR/tmpl" \
    --output "$TYPEVAR/gen/all.gen.h" --reflect=project \
    --clang-arg=-std=c11 >"$BUILD/type-variable-reflect.log" 2>&1); then
    cat "$BUILD/type-variable-reflect.log" >&2
    printf '%s\n' 'ERROR: reflection accepted type_variable_mismatch.c.in' >&2
    failed=1
else
    cat "$BUILD/type-variable-reflect.log" >&2
    if ! grep -Fq 'main.c:12:' "$BUILD/type-variable-reflect.log" ||
       ! grep -Fq 'main.c:14:' "$BUILD/type-variable-reflect.log" ||
       ! grep -Fq 'main.c:15:' "$BUILD/type-variable-reflect.log"; then
        printf '%s\n' 'reflection type-variable diagnostics were not mapped to their callsites' >&2
        failed=1
    fi
    if grep -Fq 'all.gen.h:' "$BUILD/type-variable-reflect.log"; then
        printf '%s\n' 'reflection diagnostics exposed generated-header locations' >&2
        failed=1
    fi
    printf '%s\n' 'Expected reflection failure for type_variable_mismatch.c.in (diagnostics above).'
fi

VALUEARG="$BUILD/value-argument"
mkdir -p "$VALUEARG/tmpl" "$VALUEARG/gen" "$VALUEARG/test/cases"
cp -R "$ROOT/test/tmpl/." "$VALUEARG/tmpl/"
cp "$ROOT/test/cases/test_types.h" "$VALUEARG/test/cases/test_types.h"
cp "$ROOT/test/negative/value_argument.c.in" "$VALUEARG/main.c"
printf '\n===== value_argument.c.in =====\n'
if "$BUILD/tmplgen" --source-root "$VALUEARG" \
    --template-dir "$VALUEARG/tmpl" --output "$VALUEARG/gen/all.gen.h" \
    --clang-arg=-std=c11 >"$BUILD/value-argument.log" 2>&1; then
    cat "$BUILD/value-argument.log" >&2
    printf '%s\n' 'ERROR: value_argument.c.in unexpectedly generated' >&2
    failed=1
elif ! grep -Fq "Array() requires a single-token value argument for 'N'" \
        "$BUILD/value-argument.log"; then
    cat "$BUILD/value-argument.log" >&2
    printf '%s\n' 'non-constant value argument was not diagnosed clearly' >&2
    failed=1
else
    cat "$BUILD/value-argument.log" >&2
    printf '%s\n' 'Expected tmplgen failure for value_argument.c.in (diagnostics above).'
fi

MALFORMED="$BUILD/malformed-constructor"
mkdir -p "$MALFORMED/tmpl" "$MALFORMED/gen" "$MALFORMED/test/cases"
cp -R "$ROOT/test/tmpl/." "$MALFORMED/tmpl/"
cp "$ROOT/test/cases/test_types.h" "$MALFORMED/test/cases/test_types.h"
printf '#include "test/cases/test_types.h"\n#include "gen/all.gen.h"\n\nint main(void)\n{\n    Pair(int,) value = {0};\n    (void)value;\n    return 0;\n}\n' \
    > "$MALFORMED/main.c"
printf '\n===== malformed_constructor =====\n'
if "$BUILD/tmplgen" --source-root "$MALFORMED" \
    --template-dir "$MALFORMED/tmpl" --output "$MALFORMED/gen/all.gen.h" \
    --clang-arg=-std=c11 >"$BUILD/malformed-constructor.log" 2>&1; then
    cat "$BUILD/malformed-constructor.log" >&2
    printf '%s\n' 'ERROR: malformed constructor use unexpectedly generated' >&2
    failed=1
else
    cat "$BUILD/malformed-constructor.log" >&2
    if grep -Fq 'double free' "$BUILD/malformed-constructor.log" ||
       grep -Fq 'free():' "$BUILD/malformed-constructor.log"; then
        printf '%s\n' 'malformed constructor use crashed the generator' >&2
        failed=1
    elif ! grep -Fq 'Pair() requires 2 type or value arguments' \
        "$BUILD/malformed-constructor.log"; then
        printf '%s\n' 'malformed constructor use was not diagnosed clearly' >&2
        failed=1
    else
        printf '%s\n' 'Expected tmplgen failure for malformed_constructor (diagnostics above).'
    fi
fi

MIXED="$BUILD/explicit-mixed"
mkdir -p "$MIXED/tmpl" "$MIXED/gen"
cp -R "$ROOT/test/tmpl/." "$MIXED/tmpl/"
cp "$ROOT/test/negative/explicit_mixed.tmpl.h" "$MIXED/tmpl/"
printf 'int main(void) { return 0; }\n' > "$MIXED/main.c"
printf '\n===== explicit_mixed.tmpl.h =====\n'
if "$BUILD/tmplgen" --source-root "$MIXED" \
    --template-dir "$MIXED/tmpl" --output "$MIXED/gen/all.gen.h" \
    --clang-arg=-std=c11 >"$BUILD/explicit-mixed.log" 2>&1; then
    cat "$BUILD/explicit-mixed.log" >&2
    printf '%s\n' 'ERROR: explicit_mixed.tmpl.h unexpectedly generated' >&2
    failed=1
elif ! grep -Fq "mixes explicit type parameters with inferred parameter 'U'" \
        "$BUILD/explicit-mixed.log"; then
    cat "$BUILD/explicit-mixed.log" >&2
    printf '%s\n' 'mixed explicit and inferred parameters were not diagnosed clearly' >&2
    failed=1
else
    cat "$BUILD/explicit-mixed.log" >&2
    printf '%s\n' 'Expected tmplgen failure for explicit_mixed.tmpl.h (diagnostics above).'
fi

HKTMISMATCH="$BUILD/hkt-mismatch"
mkdir -p "$HKTMISMATCH/tmpl" "$HKTMISMATCH/gen" "$HKTMISMATCH/test/cases"
cp -R "$ROOT/test/tmpl/." "$HKTMISMATCH/tmpl/"
cp "$ROOT/test/cases/test_types.h" "$HKTMISMATCH/test/cases/test_types.h"
cp "$ROOT/test/negative/hkt_mismatch.c.in" "$HKTMISMATCH/main.c"
printf '\n===== hkt_mismatch.c.in =====\n'
if "$BUILD/tmplgen" --source-root "$HKTMISMATCH" \
    --template-dir "$HKTMISMATCH/tmpl" --output "$HKTMISMATCH/gen/all.gen.h" \
    --clang-arg=-std=c11 >"$BUILD/hkt-mismatch.log" 2>&1; then
    cat "$BUILD/hkt-mismatch.log" >&2
    printf '%s\n' 'ERROR: hkt_mismatch.c.in unexpectedly generated' >&2
    failed=1
elif ! grep -Fq "Boxed() argument 'Array' is not a template constructor matching 'F'" \
        "$BUILD/hkt-mismatch.log"; then
    cat "$BUILD/hkt-mismatch.log" >&2
    printf '%s\n' 'constructor signature mismatch was not diagnosed clearly' >&2
    failed=1
else
    cat "$BUILD/hkt-mismatch.log" >&2
    printf '%s\n' 'Expected tmplgen failure for hkt_mismatch.c.in (diagnostics above).'
fi

for variant in missing-bound missing-impl missing-impl-call unknown-bound \
               bad-signature unknown-impl ambiguous-impl unsatisfied-bound \
               expression-bound function-bound; do
    TRAITS="$BUILD/traits-$variant"
    mkdir -p "$TRAITS/tmpl" "$TRAITS/gen"
    cp "$ROOT/test/negative/traits/$variant/traits.tmpl.h.in" \
        "$TRAITS/tmpl/traits.tmpl.h"
    cp "$ROOT/test/negative/traits/$variant/main.c.in" "$TRAITS/main.c"
    printf '\n===== traits/%s.tmpl.h =====\n' "$variant"
    if "$BUILD/tmplgen" --source-root "$TRAITS" \
        --template-dir "$TRAITS/tmpl" --output "$TRAITS/gen/all.gen.h" \
        --clang-arg=-std=c11 >"$BUILD/traits-$variant.log" 2>&1; then
        cat "$BUILD/traits-$variant.log" >&2
        printf 'ERROR: traits/%s.tmpl.h unexpectedly generated\n' "$variant" >&2
        failed=1
        continue
    fi
    cat "$BUILD/traits-$variant.log" >&2
    while IFS= read -r expected; do
        if ! grep -Fq "$expected" "$BUILD/traits-$variant.log"; then
            printf 'missing diagnostic for traits/%s: %s\n' \
                "$variant" "$expected" >&2
            failed=1
        fi
    done < "$ROOT/test/negative/traits/$variant/expected"
    printf 'Expected tmplgen failure for traits/%s.tmpl.h (diagnostics above).\n' \
        "$variant"
done

exit "$failed"
