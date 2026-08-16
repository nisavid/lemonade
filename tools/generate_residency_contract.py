#!/usr/bin/env python3
"""Generate committed portable-residency contract artifacts."""

from __future__ import annotations

import argparse
import os
import sys
import tempfile
from collections.abc import Sequence
from pathlib import Path
from typing import NoReturn

from residency_contract import OUTPUT_PATHS, generate_outputs

MAX_DIAGNOSTIC_LENGTH = 2_000
EXCLUSIVE_OUTPUT_DIRECTORIES = (
    "docs/api/schemas/residency",
    "test/residency/contract/generated",
)


class ArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> NoReturn:
        raise ValueError(message)


def parse_args(arguments: Sequence[str] | None = None) -> argparse.Namespace:
    parser = ArgumentParser()
    parser.add_argument(
        "--source",
        type=Path,
        default=Path("docs/research/portable-residency-capability-inventory.json"),
    )
    parser.add_argument("--output-root", type=Path, default=Path("."))
    parser.add_argument("--check", action="store_true")
    return parser.parse_args(arguments)


def _target(root: Path, relative: str) -> Path:
    relative_path = Path(relative)
    if relative_path.is_absolute() or ".." in relative_path.parts:
        raise ValueError(f"invalid generated path {relative}")
    return _resolve_contained(root, root / relative_path)


def _resolve_contained(root: Path, path: Path) -> Path:
    resolved = path.resolve(strict=False)
    try:
        resolved.relative_to(root)
    except ValueError as error:
        raise ValueError(
            f"generated path resolves outside output root: {path}"
        ) from error
    return resolved


def _validate_existing_output_tree(root: Path) -> None:
    for relative_directory in EXCLUSIVE_OUTPUT_DIRECTORIES:
        directory = _target(root, relative_directory)
        if not directory.is_dir():
            continue
        for path in directory.rglob("*"):
            _resolve_contained(root, path)


def _write_atomic(root: Path, path: Path, content: bytes) -> None:
    path = _resolve_contained(root, path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path = _resolve_contained(root, path)
    file_descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    temporary_path = Path(temporary_name)
    try:
        with os.fdopen(file_descriptor, "wb") as stream:
            temporary_path = _resolve_contained(root, temporary_path)
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary_path, 0o644)
        path = _resolve_contained(root, path)
        os.replace(temporary_path, path)
    except Exception:
        try:
            temporary_path.unlink()
        except OSError:
            pass
        raise


def _check(root: Path, outputs: dict[str, bytes]) -> None:
    missing: list[str] = []
    drifted: list[str] = []
    for relative in OUTPUT_PATHS:
        path = _target(root, relative)
        try:
            current = path.read_bytes()
        except FileNotFoundError:
            missing.append(relative)
            continue
        if current != outputs[relative]:
            drifted.append(relative)
    expected = set(OUTPUT_PATHS)
    unexpected: list[str] = []
    for relative_directory in EXCLUSIVE_OUTPUT_DIRECTORIES:
        directory = _target(root, relative_directory)
        if not directory.is_dir():
            continue
        unexpected.extend(
            path.relative_to(root).as_posix()
            for path in sorted(directory.rglob("*"))
            if path.is_file() and path.relative_to(root).as_posix() not in expected
        )
    if missing or drifted or unexpected:
        raise ValueError(
            "generated contract drift; "
            f"missing={missing!r}, drifted={drifted!r}, unexpected={unexpected!r}"
        )


def _one_line(error: Exception) -> str:
    message = " ".join(str(error).split()) or error.__class__.__name__
    if len(message) > MAX_DIAGNOSTIC_LENGTH:
        return f"{message[: MAX_DIAGNOSTIC_LENGTH - 15]}... [truncated]"
    return message


def run(arguments: Sequence[str] | None = None) -> int:
    args = parse_args(arguments)
    repo_root = Path(__file__).resolve().parents[1]
    source = args.source if args.source.is_absolute() else repo_root / args.source
    requested_output_root = (
        args.output_root
        if args.output_root.is_absolute()
        else repo_root / args.output_root
    )
    output_root = requested_output_root.resolve(strict=False)
    outputs = generate_outputs(source)
    _validate_existing_output_tree(output_root)
    targets = {relative: _target(output_root, relative) for relative in OUTPUT_PATHS}
    if args.check:
        _check(output_root, outputs)
        print("portable residency contract: valid")
        return 0
    for relative in OUTPUT_PATHS:
        _write_atomic(output_root, targets[relative], outputs[relative])
    print("portable residency contract: generated")
    return 0


def main() -> NoReturn:
    try:
        sys.exit(run())
    except Exception as error:  # noqa: BLE001
        print(
            f"portable residency contract: invalid: {_one_line(error)}", file=sys.stderr
        )
        sys.exit(1)


if __name__ == "__main__":
    main()
