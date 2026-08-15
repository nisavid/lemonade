"""Command-line boundary for implementation-handoff validation."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .contract import HandoffError
from .validation import validate_manifest

MAX_DIAGNOSTIC = 2_000


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Validate a portable residency implementation handoff."
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("plan/portable-residency-implementation-base.json"),
    )
    parser.add_argument("--phase", type=int, required=True)
    return parser


def _render_error(error: BaseException) -> str:
    message = f"portable residency implementation handoff: invalid: {error}"
    if len(message) > MAX_DIAGNOSTIC - 1:
        message = message[: MAX_DIAGNOSTIC - 18] + "... [truncated]"
    return message + "\n"


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        validate_manifest(args.manifest, args.phase)
    except (HandoffError, OSError, RuntimeError, ValueError) as error:
        sys.stderr.write(_render_error(error))
        return 2
    sys.stdout.write(
        f"portable residency implementation handoff: phase {args.phase} valid\n"
    )
    return 0


def entrypoint() -> None:
    raise SystemExit(main())
