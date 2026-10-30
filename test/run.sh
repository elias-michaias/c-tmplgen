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
"$BUILD/tmplgen" --clang-arg=-std=c11
"$BUILD/tmplgen" --check --clang-arg=-std=c11
if grep -q 'typedef int int' tmpl.h; then
    printf '%s\n' 'template LSP fallback leaked into generated code' >&2
    exit 1
fi
if grep -Eq 'max_long|Vec_double' tmpl.h; then
    printf '%s\n' 'unrequested template specializations were generated' >&2
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

"$CC" -std=gnu99 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    test/cases/result_dispatch.c -o "$BUILD/result-dispatch-gnu"
"$BUILD/result-dispatch-gnu"
"$CC" -std=gnu99 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    test/cases/triple_dispatch.c -o "$BUILD/triple-dispatch-gnu"
"$BUILD/triple-dispatch-gnu"

"$CC" -std=gnu99 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    test/cases/gnu_dispatch.c -o "$BUILD/gnu-dispatch"
"$BUILD/gnu-dispatch"

NEWTYPE="$BUILD/newtype"
mkdir -p "$NEWTYPE"
cp tmpl.h "$NEWTYPE/tmpl.h"
printf 'typedef double Fresh;\n' > "$NEWTYPE/fresh.h"
cat > "$NEWTYPE/main.c" <<'EOF'
#include "fresh.h"
#include "tmpl.h"

int main(void)
{
    Vec(Fresh) values = {0};
    Fresh a = 1.0, b = 2.0;
    push(&values, max(a, b));
    return values.data[0] != 2.0;
}
EOF
"$BUILD/tmplgen" --source-root "$NEWTYPE" --output "$NEWTYPE/tmpl.h" \
    --clang-arg=-std=c11
if ! grep -q 'Vec_Fresh' "$NEWTYPE/tmpl.h"; then
    printf '%s\n' 'generator did not discover a new type from a stale header' >&2
    exit 1
fi
"$CC" -std=c11 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    "$NEWTYPE/main.c" -o "$NEWTYPE/main"
"$NEWTYPE/main"

if "$CC" -x c -std=c11 -Wall -Wextra -pedantic -Wconversion -Wshadow \
    test/negative/unmatched_dispatch.c.in -o "$BUILD/unmatched" \
    >"$BUILD/unmatched.log" 2>&1; then
    printf '%s\n' 'unmatched dispatch unexpectedly compiled' >&2
    exit 1
fi
if ! grep -q 'struct tmplgen_no_association_for_push_argument' "$BUILD/unmatched.log"; then
    printf '%s\n' 'unmatched dispatch diagnostic was not clear' >&2
    exit 1
fi

if "$CC" -x c -std=c11 -Wall -Wextra -pedantic -Wconversion -Wshadow \
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

printf '%s\n' 'all tmplgen tests passed'
