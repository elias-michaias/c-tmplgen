#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CC=${CC:-cc}
BUILD=$(mktemp -d "$ROOT/test/.build.XXXXXX")
trap 'rm -rf "$BUILD"' EXIT HUP INT TERM

mkdir -p "$BUILD"
cd "$ROOT"

"$CC" -std=c99 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    tmplgen.c -lclang -o "$BUILD/tmplgen"
"$BUILD/tmplgen" --template-dir test/tmpl --output test/gen/all.gen.h \
    --clang-arg=-std=c23
"$BUILD/tmplgen" --template-dir test/tmpl --output test/gen/all.gen.h \
    --check --clang-arg=-std=c23
"$BUILD/tmplgen" --template-dir test/tmpl --output test/gen/all.gen.h \
    --check --no-cache --clang-arg=-std=c23
# An absolute template directory (as the clang/clangd plugins pass it) must
# produce the same root-relative #line paths when given --line-path-base.
"$BUILD/tmplgen" --template-dir "$ROOT/test/tmpl" --line-path-base "$ROOT" \
    --output test/gen/all.gen.h --check --clang-arg=-std=c23
if grep -R -q 'typedef int int' test/gen; then
    printf '%s\n' 'template LSP fallback leaked into generated output' >&2
    exit 1
fi
# Structural schemas are merged from every scanned source; each specialization
# must still assert each required field exactly once.
if ! awk '/^\/\* ---- / { split("", seen) } /^_Static_assert/ { if (seen[$0]++) exit 1 }' \
        test/gen/*.gen.h; then
    printf '%s\n' 'a specialization repeats the same constraint assertion' >&2
    exit 1
fi
if grep -Eq 'max_long' test/gen/math.gen.h || grep -q 'Vec_double' test/gen/vec.gen.h; then
    printf '%s\n' 'unrequested template specializations were generated' >&2
    exit 1
fi
for inferred_tuple in Result_InferredLong_InferredError \
                      Result_InferredDouble_ErrorCode \
                      Result_Tag_InferredError; do
    if ! grep -q "$inferred_tuple" test/gen/result.gen.h; then
        printf 'auto input inference did not generate %s\n' "$inferred_tuple" >&2
        exit 1
    fi
done
if ! grep -Fq '_Static_assert((INT_ALIGNMENT(PhysicsBody))' test/gen/result.gen.h ||
   ! grep -Fq '_Static_assert((NAMED_INT_ALIGNMENT(PhysicsBody, Physics))' \
        test/gen/result.gen.h; then
    printf '%s\n' 'constraint shorthand did not inject the type as the first macro argument' >&2
    exit 1
fi
if ! grep -q 'Triple_Tag_InferredError_InferredLong' test/gen/triple.gen.h; then
    printf '%s\n' 'auto input inference did not generate the inferred triple' >&2
    exit 1
fi
if ! grep -q '^/// static inline Result(T, E) result_make(ResultTag tag, T value, E error)$' test/gen/result.gen.h ||
   ! grep -q '^static inline Result_TestInt_ErrorCode result_make_TestInt_ErrorCode(ResultTag tag, TestInt value, ErrorCode error);$' \
         test/gen/result.gen.h ||
    ! grep -q '^/// static inline Result(T, E) result_make(ResultTag tag, T value, E error)$' test/gen/all.gen.h ||
    ! grep -q '^static inline Result_TestInt_ErrorCode result_make_TestInt_ErrorCode(ResultTag tag, TestInt value, ErrorCode error);$' \
         test/gen/all.gen.h; then
    printf '%s\n' 'dispatch docs or navigation prototype was not generated' >&2
    exit 1
fi
if ! grep -q '^/// static inline T option_value(Option(T) \* option)$' test/gen/result.gen.h ||
   ! grep -q '^/// static inline A triple_first(Triple(A, B, C) \* value)$' test/gen/triple.gen.h; then
    printf '%s\n' 'constructor applications were lost from generated signatures' >&2
    exit 1
fi

"$CC" -std=c99 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    test/cases/suffix_api.c -o "$BUILD/suffix-api"
"$BUILD/suffix-api"

"$CC" -std=c11 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    test/cases/generic_dispatch.c -o "$BUILD/generic-dispatch"
"$BUILD/generic-dispatch"
"$CC" -std=c11 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    test/cases/string_dispatch.c -o "$BUILD/string-dispatch"
"$BUILD/string-dispatch"

"$CC" -std=c11 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    test/cases/result_dispatch.c -o "$BUILD/result-dispatch"
"$BUILD/result-dispatch"

"$CC" -std=c11 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    test/cases/triple_dispatch.c -o "$BUILD/triple-dispatch"
"$BUILD/triple-dispatch"

"$CC" -std=c11 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    test/cases/builtin_types.c -o "$BUILD/builtin-types"
"$BUILD/builtin-types"

"$CC" -std=c11 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    test/cases/module_include.c -o "$BUILD/module-include"
"$BUILD/module-include"

"$CC" -std=c11 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    test/cases/tagged_struct.c -o "$BUILD/tagged-struct"
"$BUILD/tagged-struct"
"$CC" -std=gnu99 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    test/cases/tagged_struct.c -o "$BUILD/tagged-struct-gnu"
"$BUILD/tagged-struct-gnu"

"$CC" -std=c11 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    test/cases/function_recursion.c -o "$BUILD/function-recursion"
"$BUILD/function-recursion"
if ! grep -q 'chain_leaf_TestInt' test/gen/math.gen.h ||
   ! grep -q 'chain_middle_TestInt' test/gen/math.gen.h ||
   ! grep -q 'recursive_value_TestInt' test/gen/math.gen.h ||
   ! grep -q 'mutual_other_TestInt' test/gen/math.gen.h ||
   ! grep -q 'reorder_sink_double_TestInt' test/gen/math.gen.h ||
   ! grep -q 'cross_helper_TestInt' test/gen/result.gen.h; then
    printf '%s\n' 'nested or recursive generic function specialization was not emitted' >&2
    exit 1
fi
if ! grep -q '^#include "result.gen.h"$' test/gen/math.gen.h; then
    printf '%s\n' 'template include was not rewritten to the generated module' >&2
    exit 1
fi
if ! grep -q '^static inline TestInt cross_helper_TestInt(TestInt value);$' test/gen/math.gen.h; then
    printf '%s\n' 'cross-template helper prototype was not emitted before its caller' >&2
    exit 1
fi
if ! grep -q '^#line [0-9][0-9]* "test/tmpl/math.tmpl.h"$' test/gen/math.gen.h; then
    printf '%s\n' 'dispatch diagnostics are not mapped back to their template source' >&2
    exit 1
fi

"$CC" -std=c23 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    test/cases/auto_c23.c -o "$BUILD/auto-c23"
"$BUILD/auto-c23"
"$CC" -std=c11 -Wall -Wextra -Wconversion -Wshadow \
    test/cases/auto_type_c11.c -o "$BUILD/auto-type-c11"
"$BUILD/auto-type-c11"

"$CC" -std=gnu99 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    test/cases/result_dispatch.c -o "$BUILD/result-dispatch-gnu"
"$BUILD/result-dispatch-gnu"
"$CC" -std=gnu99 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    test/cases/triple_dispatch.c -o "$BUILD/triple-dispatch-gnu"
"$BUILD/triple-dispatch-gnu"

"$CC" -std=gnu99 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    test/cases/gnu_dispatch.c -o "$BUILD/gnu-dispatch"
"$BUILD/gnu-dispatch"

# A first run without any generated headers must produce the complete output
# (sources include headers that do not exist yet), and must not cache anything
# less: compare it with an uncached run once the headers exist. (The copy of
# the first output lives outside $FRESH, which is the scanned source root.)
FRESH="$BUILD/fresh"
mkdir -p "$FRESH/test"
cp -R test/tmpl test/cases test/lib "$FRESH/test/"
(
    cd "$FRESH"
    "$BUILD/tmplgen" --template-dir test/tmpl --output test/gen/all.gen.h \
        --clang-arg=-std=c23 >/dev/null
    cp -R test/gen "$BUILD/fresh-first-run"
    "$BUILD/tmplgen" --template-dir test/tmpl --output test/gen/all.gen.h \
        --clang-arg=-std=c23 --no-cache >/dev/null
)
for generated in "$FRESH"/test/gen/*.gen.h; do
    if ! cmp -s "$generated" "$BUILD/fresh-first-run/${generated##*/}"; then
        printf '%s\n' "first run without generated headers differs: ${generated##*/}" >&2
        exit 1
    fi
done

NEWTYPE="$BUILD/newtype"
mkdir -p "$NEWTYPE/lib"
cp -R test/gen "$NEWTYPE/gen"
cp test/lib/physics.h "$NEWTYPE/lib/physics.h"
printf 'typedef double Fresh;\n' > "$NEWTYPE/fresh.h"
cat > "$NEWTYPE/main.c" <<'EOF'
#include "fresh.h"
#include "gen/vec.gen.h"
#include "gen/math.gen.h"

int main(void)
{
    Vec(Fresh) values = {0};
    Fresh a = 1.0, b = 2.0;
    push(&values, max(a, b));
    return values.data[0] != 2.0;
}
EOF
"$BUILD/tmplgen" --source-root "$NEWTYPE" --template-dir test/tmpl \
    --output "$NEWTYPE/gen/all.gen.h" \
    --clang-arg=-std=c11
if ! grep -q 'Vec_Fresh' "$NEWTYPE/gen/vec.gen.h"; then
    printf '%s\n' 'generator did not discover a new type from a stale header' >&2
    exit 1
fi
"$CC" -std=c11 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    "$NEWTYPE/main.c" -o "$NEWTYPE/main"
"$NEWTYPE/main"

if "$CC" -x c -I . -I test -std=c11 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    test/negative/unmatched_dispatch.c.in -o "$BUILD/unmatched" \
    >"$BUILD/unmatched.log" 2>&1; then
    printf '%s\n' 'unmatched dispatch unexpectedly compiled' >&2
    exit 1
fi
if ! grep -q 'struct tmplgen_no_association_for_push_argument' "$BUILD/unmatched.log"; then
    printf '%s\n' 'unmatched dispatch diagnostic was not clear' >&2
    exit 1
fi

if "$CC" -x c -I . -I test -std=c11 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    test/negative/unmatched_pair.c.in -o "$BUILD/unmatched-pair" \
    >"$BUILD/unmatched-pair.log" 2>&1; then
    printf '%s\n' 'unmatched pair dispatch unexpectedly compiled' >&2
    exit 1
fi
if ! grep -q 'struct tmplgen_no_association_for_result_make_argument' \
    "$BUILD/unmatched-pair.log"; then
    printf '%s\n' 'unmatched pair dispatch diagnostic was not clear' >&2
    exit 1
fi

if "$CC" -x c -I . -I test -std=c11 -Wall -Wextra -Werror=incompatible-pointer-types \
    test/negative/unmatched_option_pair.c.in -o "$BUILD/unmatched-option-pair" \
    >"$BUILD/unmatched-option-pair.log" 2>&1; then
    printf '%s\n' 'same-type constructor constraint unexpectedly accepted a mismatched pair' >&2
    exit 1
fi
if ! grep -q 'Option_TestInt' "$BUILD/unmatched-option-pair.log"; then
    printf '%s\n' 'same-type constructor mismatch did not show the specialized types' >&2
    exit 1
fi

SUBTYPE="$BUILD/subtyping"
mkdir -p "$SUBTYPE/gen" "$SUBTYPE/test/lib" "$SUBTYPE/lib"
cp test/lib/physics.h "$SUBTYPE/test/lib/physics.h"
cp test/lib/physics.h "$SUBTYPE/lib/physics.h"
cp test/negative/unmatched_structural_rows.c.in "$SUBTYPE/main.c"
"$BUILD/tmplgen" --source-root "$SUBTYPE" --template-dir test/tmpl \
    --output "$SUBTYPE/gen/all.gen.h" \
    --clang-arg=-std=c11
if "$CC" -std=c11 -Wall -Wextra -pedantic "$SUBTYPE/main.c" \
    -o "$SUBTYPE/main" >"$BUILD/subtyping.log" 2>&1; then
    printf '%s\n' 'incompatible structural field type unexpectedly compiled' >&2
    exit 1
fi
if ! grep -q 'tmplgen: TP is not a subtype of Physics' "$BUILD/subtyping.log"; then
    cat "$BUILD/subtyping.log" >&2
    printf '%s\n' 'structural constraint failure did not identify its row constraint' >&2
    exit 1
fi
if ! grep -q 'tmplgen: UE is not a subtype of Named' "$BUILD/subtyping.log"; then
    printf '%s\n' 'combined structural constraint failure did not identify the missing row' >&2
    exit 1
fi
if ! grep -q 'tmplgen: AI failed to satisfy NAMED_INT_ALIGNMENT(Physics)' \
    "$BUILD/subtyping.log"; then
    printf '%s\n' 'arbitrary constraint failure did not include its expression' >&2
    exit 1
fi
if ! grep -q 'tmplgen: @where failed to satisfy SAME_SIZE(T, U)' \
    "$BUILD/subtyping.log"; then
    printf '%s\n' '@where failure did not include its cross-variable expression' >&2
    exit 1
fi
if ! grep -q 'subtyping/main.c' "$BUILD/subtyping.log"; then
    printf '%s\n' 'structural constraint diagnostics were not mapped to a source callsite' >&2
    exit 1
fi
if ! grep -q 'subtyping/main.c:23:' "$BUILD/subtyping.log" ||
   ! grep -q 'subtyping/main.c:24:' "$BUILD/subtyping.log"; then
    printf '%s\n' 'structural constraint diagnostics did not point at the failing calls' >&2
    exit 1
fi
if ! grep -q 'subtyping/main.c:24:' "$BUILD/subtyping.log"; then
    printf '%s\n' 'expression constraint diagnostics did not point at the failing call' >&2
    exit 1
fi

COMPILED="$BUILD/compiled"
mkdir -p "$COMPILED/tmpl" "$COMPILED/test/lib" "$COMPILED/lib" "$COMPILED/gen"
cp -R test/tmpl/. "$COMPILED/tmpl/"
cp test/lib/physics.h "$COMPILED/test/lib/physics.h"
cp test/lib/physics.h "$COMPILED/lib/physics.h"
cat > "$COMPILED/main.c" <<'EOF'
#include "test/lib/physics.h"
#include "gen/all.gen.h"

int main(void)
{
    struct PhysicsBody body = { 1, 2, 0.0 };
    return physics_sum(&body) != 3 || max(2, 1) != 2;
}
EOF
"$BUILD/tmplgen" --source-root "$COMPILED" --template-dir "$COMPILED/tmpl" \
    --output "$COMPILED/gen/all.gen.h" --clang-arg=-std=c11 --compiled
if [ ! -f "$COMPILED/gen/math.gen.c" ] ||
   ! grep -Fq 'max_int' "$COMPILED/gen/math.gen.c"; then
    printf '%s\n' 'compiled mode did not emit specialization implementations' >&2
    exit 1
fi
"$BUILD/tmplgen" --source-root "$COMPILED" --template-dir "$COMPILED/tmpl" \
    --output "$COMPILED/gen/all.gen.h" --clang-arg=-std=c11 --compiled --check
printf '%s\n' 'double extra(void) { return max(1.0, 2.0); }' >> "$COMPILED/main.c"
"$BUILD/tmplgen" --source-root "$COMPILED" --template-dir "$COMPILED/tmpl" \
    --output "$COMPILED/gen/all.gen.h" --clang-arg=-std=c11 --compiled
if ! grep -Fq 'max_double' "$COMPILED/gen/math.gen.c"; then
    printf '%s\n' 'compiled mode cache did not notice changed source usages' >&2
    exit 1
fi
printf '%s\n' '/* tampered output */' >> "$COMPILED/gen/math.gen.h"
if "$BUILD/tmplgen" --source-root "$COMPILED" --template-dir "$COMPILED/tmpl" \
    --output "$COMPILED/gen/all.gen.h" --clang-arg=-std=c11 --compiled --check; then
    printf '%s\n' 'compiled-mode check accepted a modified generated header' >&2
    exit 1
fi
"$BUILD/tmplgen" --source-root "$COMPILED" --template-dir "$COMPILED/tmpl" \
    --output "$COMPILED/gen/all.gen.h" --clang-arg=-std=c11 --compiled
"$CC" -std=c11 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    "$COMPILED/main.c" "$COMPILED"/gen/*.gen.c -o "$COMPILED/main"
"$COMPILED/main"

printf '%s\n' 'all tmplgen tests passed'
