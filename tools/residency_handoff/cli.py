"""Command-line boundary for implementation-handoff validation."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .contract import HandoffError
from .validation import validate_manifest, validate_source_revision

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
    parser.add_argument("--phase", type=int)
    parser.add_argument("--source-revision", type=Path)
    parser.add_argument("--fork-binding", type=Path)
    parser.add_argument("--require-source-ready", action="store_true")
    return parser


def _render_error(error: BaseException) -> str:
    message = f"portable residency implementation handoff: invalid: {error}"
    if len(message) > MAX_DIAGNOSTIC - 1:
        message = message[: MAX_DIAGNOSTIC - 18] + "... [truncated]"
    return message + "\n"


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        if args.source_revision is not None:
            if args.phase is not None:
                raise ValueError("--phase cannot be combined with --source-revision")
            status = validate_source_revision(args.source_revision, args.fork_binding)
            if args.require_source_ready and status != "source_ready":
                if status == "awaiting_maintained_fork_binding":
                    raise ValueError(
                        "a maintained-fork binding is required for source readiness"
                    )
                raise ValueError(
                    "source revision is not ready; revalidation is required"
                )
        else:
            if args.fork_binding is not None:
                raise ValueError("--fork-binding requires --source-revision")
            if args.require_source_ready:
                raise ValueError("--require-source-ready requires --source-revision")
            if args.phase is None:
                raise ValueError(
                    "--phase is required for implementation handoff validation"
                )
            validate_manifest(args.manifest, args.phase)
    except (HandoffError, OSError, RuntimeError, ValueError) as error:
        sys.stderr.write(_render_error(error))
        return 2
    if args.source_revision is not None:
        rendered_status = {
            "awaiting_maintained_fork_binding": "awaiting maintained-fork binding",
            "awaiting_revalidation": "awaiting revalidation",
            "source_ready": "source ready",
        }[status]
        sys.stdout.write(f"portable residency source revision: {rendered_status}\n")
        return 0
    sys.stdout.write(
        f"portable residency implementation handoff: phase {args.phase} valid\n"
    )
    return 0


def entrypoint() -> None:
    raise SystemExit(main())
