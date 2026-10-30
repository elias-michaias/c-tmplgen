#!/usr/bin/env python3
"""Generate synthetic projects and repeatedly measure tmplgen."""

from __future__ import annotations

import argparse
import csv
import os
import re
import shlex
import statistics
import subprocess
import tempfile
import time
from pathlib import Path
from typing import Iterable

from generate import generate_project


REPO_ROOT = Path(__file__).resolve().parents[2]
SPECIALIZATION_LINE = re.compile(
    r"^/\* ---- module_\d+: bench_m\d+_f\d+_[^ ]+ ---- \*/$",
    re.MULTILINE,
)


def parse_sizes(value: str) -> list[int]:
    try:
        sizes = [int(part.strip()) for part in value.split(",")]
    except ValueError as error:
        raise argparse.ArgumentTypeError("sizes must be comma-separated integers") from error
    if not sizes or any(size < 1 for size in sizes):
        raise argparse.ArgumentTypeError("each size must be at least 1")
    return sizes


def positive_argument(name: str):
    def parse(value: str) -> int:
        try:
            result = int(value)
        except ValueError as error:
            raise argparse.ArgumentTypeError("expected an integer") from error
        if result < 1:
            raise argparse.ArgumentTypeError(f"{name} must be at least 1")
        return result

    return parse


def compile_tmplgen(destination: Path) -> Path:
    compiler = shlex.split(os.environ.get("CC", "cc"))
    flags = shlex.split(os.environ.get("CFLAGS", "-O2 -std=c99 -Wall -Wextra"))
    command = compiler + flags + [
        str(REPO_ROOT / "tmplgen.c"),
        "-lclang",
        "-o",
        str(destination),
    ]
    result = subprocess.run(command, cwd=REPO_ROOT, text=True, capture_output=True)
    if result.returncode != 0:
        raise RuntimeError(
            "could not build tmplgen:\n"
            + " ".join(shlex.quote(part) for part in command)
            + "\n"
            + result.stderr
        )
    return destination


def tmplgen_command(
    binary: Path, project: Path, *, check: bool, dispatch: str
) -> list[str]:
    command = [
        str(binary),
        "--source-root",
        str(project),
        "--template-dir",
        str(project / "tmpl"),
        "--output",
        str(project / "gen" / "all.gen.h"),
        "--dispatch",
        dispatch,
        "--clang-arg=-std=c11",
    ]
    if check:
        command.append("--check")
    return command


def invoke(command: list[str], *, cwd: Path) -> float:
    start = time.perf_counter()
    result = subprocess.run(
        command,
        cwd=cwd,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )
    elapsed = time.perf_counter() - start
    if result.returncode != 0:
        raise RuntimeError(
            f"tmplgen failed with exit status {result.returncode}:\n"
            + " ".join(shlex.quote(part) for part in command)
            + "\n"
            + result.stderr[-8000:]
        )
    return elapsed


def tree_bytes(paths: Iterable[Path]) -> int:
    return sum(path.stat().st_size for path in paths if path.is_file())


def specialization_count(project: Path) -> int:
    count = 0
    for module in (project / "gen").glob("*.gen.h"):
        count += len(SPECIALIZATION_LINE.findall(module.read_text()))
    return count


def summarize(samples: list[float]) -> str:
    milliseconds = [sample * 1000.0 for sample in samples]
    return (
        f"median={statistics.median(milliseconds):.2f} ms "
        f"min={min(milliseconds):.2f} ms max={max(milliseconds):.2f} ms"
    )


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    if not rows:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = list(rows[0])
    with path.open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def execute_benchmark(
    binary: Path,
    project: Path,
    *,
    types: int,
    modules: int,
    functions_per_module: int,
    calls_per_source: int,
    repeats: int,
    warmups: int,
    dispatch: str,
    mode: str,
) -> tuple[dict[str, object], list[dict[str, object]]]:
    metadata = generate_project(
        project,
        types=types,
        modules=modules,
        functions_per_module=functions_per_module,
        calls_per_source=calls_per_source,
    )
    generation_command = tmplgen_command(binary, project, check=False, dispatch=dispatch)
    check_command = tmplgen_command(binary, project, check=True, dispatch=dispatch)

    cold_seconds = invoke(generation_command, cwd=project)
    # --check is also a correctness gate: every emitted module and the
    # aggregate must be reproducible before the timed steady-state passes.
    invoke(check_command, cwd=project)

    observed_specializations = specialization_count(project)
    expected_specializations = int(metadata["expected_function_specializations"])
    if observed_specializations < expected_specializations:
        raise RuntimeError(
            f"expected at least {expected_specializations} generated function "
            f"specializations, found {observed_specializations} in {project}"
        )

    template_bytes = tree_bytes((project / "tmpl").glob("*.tmpl.h"))
    source_bytes = tree_bytes((project / "src").glob("*.c"))
    generated_bytes = tree_bytes((project / "gen").glob("*.gen.h"))
    metrics: dict[str, object] = {
        "types": types,
        "modules": modules,
        "functions_per_module": functions_per_module,
        "sources": metadata["source_files"],
        "calls": metadata["call_sites"],
        "expected_specializations": expected_specializations,
        "generated_specializations": observed_specializations,
        "template_bytes": template_bytes,
        "source_bytes": source_bytes,
        "generated_bytes": generated_bytes,
        "cold_ms": cold_seconds * 1000.0,
    }

    csv_rows: list[dict[str, object]] = []
    csv_rows.append(
        {**metrics, "phase": "cold", "iteration": 0, "seconds": cold_seconds}
    )
    samples_by_mode: dict[str, list[float]] = {}
    selected_modes = ["generate", "check"] if mode == "both" else [mode]
    commands = {"generate": generation_command, "check": check_command}
    for selected_mode in selected_modes:
        for _ in range(warmups):
            invoke(commands[selected_mode], cwd=project)
        samples: list[float] = []
        for iteration in range(1, repeats + 1):
            seconds = invoke(commands[selected_mode], cwd=project)
            samples.append(seconds)
            csv_rows.append(
                {**metrics, "phase": selected_mode, "iteration": iteration,
                 "seconds": seconds}
            )
        samples_by_mode[selected_mode] = samples
    metrics["samples"] = samples_by_mode
    return metrics, csv_rows


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sizes", type=parse_sizes, default=parse_sizes("8,32,128"))
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
    parser.add_argument("--repeats", type=positive_argument("repeats"), default=5)
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument(
        "--dispatch", choices=("auto", "generic", "gnu", "none"), default="auto"
    )
    parser.add_argument("--mode", choices=("generate", "check", "both"), default="both")
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--work-dir", type=Path)
    parser.add_argument("--keep-projects", action="store_true")
    parser.add_argument("--csv", type=Path, help="write individual timings and corpus metrics")
    arguments = parser.parse_args()
    if arguments.warmups < 0:
        parser.error("--warmups must be zero or greater")

    work_dir = arguments.work_dir.resolve() if arguments.work_dir else None
    if work_dir is not None:
        work_dir.mkdir(parents=True, exist_ok=True)

    rows: list[dict[str, object]] = []
    with tempfile.TemporaryDirectory(prefix="tmplgen-bench-build-") as build_temp:
        binary = arguments.binary.resolve() if arguments.binary else compile_tmplgen(
            Path(build_temp) / "tmplgen"
        )
        for type_count in arguments.sizes:
            if arguments.keep_projects:
                project = Path(
                    tempfile.mkdtemp(
                        prefix=f"tmplgen-bench-{type_count}-",
                        dir=work_dir,
                    )
                )
                metrics, size_rows = execute_benchmark(
                    binary,
                    project,
                    types=type_count,
                    modules=arguments.modules,
                    functions_per_module=arguments.functions_per_module,
                    calls_per_source=arguments.calls_per_source,
                    repeats=arguments.repeats,
                    warmups=arguments.warmups,
                    dispatch=arguments.dispatch,
                    mode=arguments.mode,
                )
                print(f"project kept at {project}")
            else:
                with tempfile.TemporaryDirectory(
                    prefix=f"tmplgen-bench-{type_count}-", dir=work_dir
                ) as project_temp:
                    metrics, size_rows = execute_benchmark(
                        binary,
                        Path(project_temp),
                        types=type_count,
                        modules=arguments.modules,
                        functions_per_module=arguments.functions_per_module,
                        calls_per_source=arguments.calls_per_source,
                        repeats=arguments.repeats,
                        warmups=arguments.warmups,
                        dispatch=arguments.dispatch,
                        mode=arguments.mode,
                    )
            rows.extend(size_rows)
            print(
                f"types={metrics['types']} modules={metrics['modules']} "
                f"functions/module={metrics['functions_per_module']} "
                f"sources={metrics['sources']} calls={metrics['calls']} "
                f"specializations={metrics['generated_specializations']} "
                f"source={int(metrics['source_bytes']) / 1024:.1f} KiB "
                f"generated={int(metrics['generated_bytes']) / 1024:.1f} KiB "
                f"cold={float(metrics['cold_ms']):.2f} ms"
            )
            for selected_mode, samples in metrics["samples"].items():
                print(f"  {selected_mode}: {summarize(samples)}")

    if arguments.csv:
        write_csv(arguments.csv.resolve(), rows)
        print(f"CSV written to {arguments.csv.resolve()}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError) as error:
        raise SystemExit(f"bench: {error}") from error
