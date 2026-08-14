"""Command-line orchestration for residency inventory validation."""

from __future__ import annotations

import argparse
import subprocess
import sys
from collections.abc import Sequence
from pathlib import Path
from typing import Any, NoReturn

from .contract import ResidencyInventoryError, fail
from .documents import (
    BEGIN_MARKER,
    CAMPAIGN_BEGIN_MARKER,
    CAMPAIGN_END_MARKER,
    END_MARKER,
    render_campaign,
    render_inventory,
    validate_marked_block,
    validate_rendered_projections,
)
from .json_contract import parse_json_object
from .schema import validate_inventory
from .transaction import (
    inventory_lock,
    recover_pending_update,
    require_distinct_paths,
    require_no_pending_update,
    update_generated_documents,
)

MAX_DIAGNOSTIC_LENGTH = 2_000
TRUNCATION_MARKER = "... [truncated]"


def load_json(path: Path) -> dict[str, Any]:
    return parse_json_object(path.read_text(encoding="utf-8"), "inventory")


class DomainArgumentParser(argparse.ArgumentParser):
    """Route invalid command lines through the validator's domain boundary."""

    def error(self, message: str) -> NoReturn:
        fail(message)


def parse_args(arguments: Sequence[str] | None = None) -> argparse.Namespace:
    parser = DomainArgumentParser()
    parser.add_argument(
        "--inventory",
        type=Path,
        default=Path("docs/research/portable-residency-capability-inventory.json"),
    )
    parser.add_argument(
        "--matrix",
        type=Path,
        default=Path("docs/research/portable-residency-capability-matrix.md"),
    )
    parser.add_argument(
        "--campaign",
        type=Path,
        default=Path("docs/research/hatchery-campaign-parameters.md"),
    )
    output_mode = parser.add_mutually_exclusive_group()
    output_mode.add_argument("--render", action="store_true")
    output_mode.add_argument("--update", action="store_true")
    return parser.parse_args(arguments)


def _run(arguments: Sequence[str] | None = None) -> int:
    args = parse_args(arguments)
    repo = Path(__file__).resolve().parents[2]
    inventory_path = (repo / args.inventory).resolve()
    matrix_path = (repo / args.matrix).resolve()
    campaign_path = (repo / args.campaign).resolve()
    require_distinct_paths(inventory_path, matrix_path, campaign_path)
    with inventory_lock(inventory_path, exclusive=args.update):
        if args.update:
            recover_pending_update(repo, inventory_path, matrix_path, campaign_path)
        else:
            require_no_pending_update(inventory_path)
        inventory = load_json(inventory_path)
        projection = validate_inventory(repo, inventory)
        rendered_inventory = render_inventory(projection)
        rendered_campaign = render_campaign(projection)
        validate_rendered_projections(rendered_inventory, rendered_campaign)
        if args.render:
            print(rendered_inventory)
            print()
            print(rendered_campaign)
        else:
            if args.update:
                update_generated_documents(
                    repo,
                    inventory_path,
                    matrix_path,
                    campaign_path,
                    rendered_inventory,
                    rendered_campaign,
                )
            validate_marked_block(
                matrix_path,
                rendered_inventory,
                BEGIN_MARKER,
                END_MARKER,
                "support inventory",
            )
            validate_marked_block(
                campaign_path,
                rendered_campaign,
                CAMPAIGN_BEGIN_MARKER,
                CAMPAIGN_END_MARKER,
                "Hatchery exact cells",
            )
            status = "generated blocks updated" if args.update else "valid"
            print(f"portable residency capability inventory: {status}")
    return 0


def _truncate_diagnostic(message: str) -> str:
    if len(message) <= MAX_DIAGNOSTIC_LENGTH:
        return message
    prefix_length = MAX_DIAGNOSTIC_LENGTH - len(TRUNCATION_MARKER)
    return f"{message[:prefix_length]}{TRUNCATION_MARKER}"


def _bounded_error_text(error: Exception) -> str:
    message = str(error)
    if isinstance(error, subprocess.CalledProcessError):
        error_stderr = error.stderr
        if isinstance(error_stderr, bytes):
            error_stderr = error_stderr.decode(errors="replace")
        if isinstance(error_stderr, str) and error_stderr.strip():
            message = f"{message}; stderr: {error_stderr.strip()[:500]}"
    return _truncate_diagnostic(message)


def main(arguments: Sequence[str] | None = None) -> int:
    """Run the CLI contract while normalizing every failure to its domain error."""

    try:
        return _run(arguments)
    except ResidencyInventoryError:
        raise
    except Exception as error:
        raise ResidencyInventoryError(_bounded_error_text(error)) from error


def entrypoint() -> NoReturn:
    try:
        sys.exit(main())
    except ResidencyInventoryError as error:
        print(
            "portable residency capability inventory: invalid: "
            f"{_truncate_diagnostic(str(error))}",
            file=sys.stderr,
        )
        sys.exit(1)
