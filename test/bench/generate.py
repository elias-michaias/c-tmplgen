#!/usr/bin/env python3
"""Generate a scalable synthetic tmplgen project for performance testing."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def require_positive(name: str, value: int) -> int:
    if value < 1:
        raise argparse.ArgumentTypeError(f"{name} must be at least 1")
    return value


def generate_project(
    root: Path,
    *,
    types: int,
    modules: int,
    functions_per_module: int,
    calls_per_source: int,
) -> dict[str, int]:
    """Create templates, concrete types, and source call sites under root."""
    for name, value in (
        ("types", types),
        ("modules", modules),
        ("functions_per_module", functions_per_module),
        ("calls_per_source", calls_per_source),
    ):
        if value < 1:
            raise ValueError(f"{name} must be at least 1")
    root = root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    if any(root.iterdir()):
        raise ValueError(f"benchmark project directory is not empty: {root}")

    include_dir = root / "include"
    template_dir = root / "tmpl"
    source_dir = root / "src"
    generated_dir = root / "gen"
    for directory in (include_dir, template_dir, source_dir, generated_dir):
        directory.mkdir()

    types_header = [
        "#ifndef TMPLGEN_BENCH_TYPES_H",
        "#define TMPLGEN_BENCH_TYPES_H",
        "",
        "#ifndef TMPLGEN",
        "typedef void *T;",
        "#endif",
        "",
    ]
    for type_index in range(types):
        type_name = f"BenchType_{type_index:06d}"
        types_header.extend(
            [
                f"struct {type_name} {{",
                "    unsigned long value;",
                "};",
                "",
            ]
        )
    types_header.append("#endif")
    (include_dir / "types.h").write_text("\n".join(types_header) + "\n")

    for module_index in range(modules):
        module_stem = f"module_{module_index:04d}"
        module_number = f"{module_index:04d}"
        guard = f"TMPLGEN_BENCH_{module_stem}_TMPL_H".upper()
        lines = [
            f"#ifndef {guard}",
            f"#define {guard}",
            "",
            '#include "../include/types.h"',
        ]
        if module_index > 0 and module_index < modules:
            previous = f"module_{module_index - 1:04d}.tmpl.h"
            lines.append(f'#include "{previous}"')
        lines.append("")

        # Reverse order makes each same-module call refer to an earlier
        # definition. The final function links each module to its predecessor.
        if module_index < modules:
            for function_index in reversed(range(functions_per_module)):
                function_name = f"bench_m{module_number}_f{function_index:04d}"
                lines.extend(["// @template T"])
                if function_index + 1 < functions_per_module:
                    next_name = f"bench_m{module_number}_f{function_index + 1:04d}"
                    body = f"return {next_name}(value);"
                elif module_index > 0:
                    previous_name = f"bench_m{module_index - 1:04d}_f0000"
                    body = f"return {previous_name}(value);"
                else:
                    body = "return value;"
                lines.extend(
                    [
                        f"static inline T {function_name}(T value)",
                        "{",
                        f"    {body}",
                        "}",
                        "",
                    ]
                )
        lines.extend(["#endif", ""])
        (template_dir / f"{module_stem}.tmpl.h").write_text("\n".join(lines))

    # Seed the aggregate with template declarations so the first source scan
    # can resolve calls before tmplgen has emitted the generated headers.
    last_template = f"module_{modules - 1:04d}.tmpl.h"
    (generated_dir / "all.gen.h").write_text(
        "#ifndef TMPLGEN_BENCH_BOOTSTRAP_H\n"
        "#define TMPLGEN_BENCH_BOOTSTRAP_H\n"
        f'#include "../tmpl/{last_template}"\n'
        "#endif\n"
    )

    call_sites = [
        (module_index, type_index)
        for module_index in range(modules)
        for type_index in range(types)
    ]
    for source_index, offset in enumerate(range(0, len(call_sites), calls_per_source)):
        group = call_sites[offset : offset + calls_per_source]
        lines = [
            '#include "../include/types.h"',
            '#include "../gen/all.gen.h"',
            "",
            f"void bench_source_{source_index:05d}(void)",
            "{",
        ]
        for module_index, type_index in group:
            function_name = f"bench_m{module_index:04d}_f0000"
            type_name = f"BenchType_{type_index:06d}"
            lines.append(f"    (void){function_name}((struct {type_name} *)0);")
        lines.extend(["}", ""])
        (source_dir / f"source_{source_index:05d}.c").write_text(
            "\n".join(lines)
        )

    source_count = len(list(source_dir.glob("*.c")))
    metadata = {
        "types": types,
        "modules": modules,
        "functions_per_module": functions_per_module,
        "calls_per_source": calls_per_source,
        "source_files": source_count,
        "template_files": modules,
        "call_sites": len(call_sites),
        "expected_function_specializations": (
            types * modules * functions_per_module
        ),
    }
    (root / "bench.json").write_text(json.dumps(metadata, indent=2) + "\n")
    return metadata


def positive_argument(name: str):
    def parse(value: str) -> int:
        try:
            result = int(value)
        except ValueError as error:
            raise argparse.ArgumentTypeError("expected an integer") from error
        return require_positive(name, result)

    return parse


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--types", type=positive_argument("types"), default=32)
    parser.add_argument("--modules", type=positive_argument("modules"), default=4)
    parser.add_argument(
        "--functions-per-module",
        type=positive_argument("functions-per-module"),
        default=6,
    )
    parser.add_argument(
        "--calls-per-source",
        type=positive_argument("calls-per-source"),
        default=256,
    )
    arguments = parser.parse_args()
    try:
        metadata = generate_project(
            arguments.root,
            types=arguments.types,
            modules=arguments.modules,
            functions_per_module=arguments.functions_per_module,
            calls_per_source=arguments.calls_per_source,
        )
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(json.dumps(metadata, indent=2))


if __name__ == "__main__":
    main()
