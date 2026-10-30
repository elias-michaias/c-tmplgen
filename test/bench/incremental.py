#!/usr/bin/env python3
"""Measure one concrete-type/dispatch update after a generated project exists."""

from __future__ import annotations

import argparse
import csv
import os
import statistics
import tempfile
import time
from pathlib import Path

from generate import generate_project
from run import (
    REPO_ROOT,
    compile_tmplgen,
    invoke,
    positive_argument,
    specialization_count,
    tmplgen_command,
)


def add_type_and_retarget_calls(project: Path, old_name: str, new_name: str) -> tuple[int, int]:
    types_path = project / "include" / "types.h"
    types_contents = types_path.read_text()
    guard_end = types_contents.rfind("\n#endif")
    if guard_end < 0:
        raise RuntimeError(f"missing final include guard in {types_path}")
    declaration = (
        f"\nstruct {new_name} {{\n"
        "    unsigned long value;\n"
        "};\n"
    )
    types_path.write_text(types_contents[:guard_end] + declaration + types_contents[guard_end:])

    changed_files = 0
    changed_calls = 0
    for source in sorted((project / "src").glob("*.c")):
        original = source.read_text()
        updated = original.replace(f"struct {old_name}", f"struct {new_name}")
        if updated != original:
            changed_files += 1
            changed_calls += original.count(f"struct {old_name}")
            source.write_text(updated)
    if changed_calls == 0:
        raise RuntimeError(f"no call sites found for {old_name}")
    return changed_files, changed_calls


def restore_project(project: Path, types_header: str,
                    source_contents: dict[Path, str]) -> None:
    (project / "include" / "types.h").write_text(types_header)
    for path, contents in source_contents.items():
        path.write_text(contents)


def generated_snapshot(project: Path) -> dict[str, bytes]:
    return {
        path.name: path.read_bytes()
        for path in sorted((project / "gen").glob("*.gen.*"))
    }


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def measure(project: Path, binary: Path, *, types: int, modules: int,
            functions_per_module: int, calls_per_source: int,
            repeats: int, dispatch: str, compiled: bool) -> list[dict[str, object]]:
    metadata = generate_project(
        project,
        types=types,
        modules=modules,
        functions_per_module=functions_per_module,
        calls_per_source=calls_per_source,
    )
    generate = tmplgen_command(
        binary, project, check=False, dispatch=dispatch,
        use_cache=True, compiled_output=compiled,
    )
    check = tmplgen_command(
        binary, project, check=True, dispatch=dispatch,
        use_cache=True, compiled_output=compiled,
    )
    rows: list[dict[str, object]] = []

    start = time.perf_counter()
    invoke(generate, cwd=project)
    cold_seconds = time.perf_counter() - start
    invoke(check, cwd=project)
    expected = int(metadata["expected_function_specializations"])
    actual = specialization_count(project)
    if actual != expected:
        raise RuntimeError(f"expected {expected} specializations, found {actual}")

    base_metrics: dict[str, object] = {
        "types": types,
        "modules": modules,
        "functions_per_module": functions_per_module,
        "source_files": metadata["source_files"],
        "calls": metadata["call_sites"],
        "expected_specializations": expected,
        "generated_specializations": actual,
        "compiled_output": int(compiled),
    }
    rows.append({**base_metrics, "phase": "cold", "iteration": 0,
                 "changed_source_files": 0,
                 "changed_calls": 0,
                 "changed_outputs": len(generated_snapshot(project)),
                 "seconds": cold_seconds})

    source_paths = sorted((project / "src").glob("*.c"))
    original_sources = {path: path.read_text() for path in source_paths}
    original_types = (project / "include" / "types.h").read_text()
    old_name = "BenchType_000000"
    new_name = f"BenchType_{types:06d}"
    changed_source_files, changed_calls = add_type_and_retarget_calls(
        project, old_name, new_name
    )
    restore_project(project, original_types, original_sources)

    update_samples: list[float] = []
    for iteration in range(1, repeats + 1):
        before_snapshot = generated_snapshot(project)
        if iteration % 2 == 0:
            restore_project(project, original_types, original_sources)
        else:
            add_type_and_retarget_calls(project, old_name, new_name)
        start = time.perf_counter()
        invoke(generate, cwd=project)
        seconds = time.perf_counter() - start
        update_samples.append(seconds)
        current_snapshot = generated_snapshot(project)
        changed_outputs = sum(
            before_snapshot.get(name) != data
            for name, data in current_snapshot.items()
        )
        rows.append({
            **base_metrics,
            "phase": "type_update",
            "iteration": iteration,
            "changed_source_files": changed_source_files,
            "changed_calls": changed_calls,
            "changed_outputs": changed_outputs,
            "seconds": seconds,
        })

    # The last update leaves a complete generated tree; check it repeatedly.
    noop_samples: list[float] = []
    for iteration in range(1, repeats + 1):
        start = time.perf_counter()
        invoke(check, cwd=project)
        seconds = time.perf_counter() - start
        noop_samples.append(seconds)
        rows.append({
            **base_metrics,
            "phase": "unchanged_cache",
            "iteration": iteration,
            "changed_source_files": 0,
            "changed_calls": 0,
            "changed_outputs": 0,
            "seconds": seconds,
        })

    print(
        f"cold={cold_seconds * 1000.0:.2f} ms; "
        f"one-type update median={statistics.median(update_samples) * 1000.0:.2f} ms "
        f"({changed_source_files} source files, {changed_calls} call sites); "
        f"unchanged cache median={statistics.median(noop_samples) * 1000.0:.2f} ms"
    )
    return rows


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--types", type=positive_argument("types"), default=64)
    parser.add_argument("--modules", type=positive_argument("modules"), default=12)
    parser.add_argument(
        "--functions-per-module",
        type=positive_argument("functions-per-module"),
        default=8,
    )
    parser.add_argument(
        "--calls-per-source",
        type=positive_argument("calls-per-source"),
        default=128,
    )
    parser.add_argument("--repeats", type=positive_argument("repeats"), default=3)
    parser.add_argument("--dispatch", choices=("auto", "generic", "gnu", "none"),
                        default="auto")
    parser.add_argument("--compiled", action="store_true")
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--work-dir", type=Path)
    parser.add_argument("--keep-project", action="store_true")
    parser.add_argument("--csv", type=Path, default=Path("test/bench/incremental-results.csv"))
    arguments = parser.parse_args()

    work_dir = arguments.work_dir.resolve() if arguments.work_dir else None
    if work_dir is not None:
        work_dir.mkdir(parents=True, exist_ok=True)
    rows: list[dict[str, object]]
    with tempfile.TemporaryDirectory(prefix="tmplgen-incremental-build-") as build_temp:
        binary = arguments.binary.resolve() if arguments.binary else compile_tmplgen(
            Path(build_temp) / "tmplgen"
        )
        if arguments.keep_project:
            project = Path(tempfile.mkdtemp(prefix="tmplgen-incremental-", dir=work_dir))
            rows = measure(
                project, binary, types=arguments.types, modules=arguments.modules,
                functions_per_module=arguments.functions_per_module,
                calls_per_source=arguments.calls_per_source,
                repeats=arguments.repeats, dispatch=arguments.dispatch,
                compiled=arguments.compiled,
            )
            print(f"project kept at {project}")
        else:
            with tempfile.TemporaryDirectory(
                prefix="tmplgen-incremental-", dir=work_dir
            ) as project_dir:
                rows = measure(
                    Path(project_dir), binary, types=arguments.types,
                    modules=arguments.modules,
                    functions_per_module=arguments.functions_per_module,
                    calls_per_source=arguments.calls_per_source,
                    repeats=arguments.repeats, dispatch=arguments.dispatch,
                    compiled=arguments.compiled,
                )
    csv_path = arguments.csv.resolve()
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    write_csv(csv_path, rows)
    print(f"CSV written to {csv_path}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError) as error:
        raise SystemExit(f"incremental benchmark: {error}") from error
