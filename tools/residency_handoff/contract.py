"""Strict manifest primitives for the implementation handoff."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any, NoReturn


class HandoffError(ValueError):
    """A fail-closed implementation-handoff validation error."""


def fail(message: str) -> NoReturn:
    raise HandoffError(message)


def _without_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            fail(f"duplicate object key {key!r}")
        result[key] = value
    return result


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        source = path.read_text(encoding="utf-8")
    except OSError as error:
        fail(f"cannot read manifest {path}: {error}")
    try:
        value = json.loads(source, object_pairs_hook=_without_duplicate_keys)
    except json.JSONDecodeError as error:
        fail(f"manifest is invalid JSON: {error}")
    if not isinstance(value, dict):
        fail("manifest root must be an object")
    return value


def canonical_digest(value: dict[str, Any], digest_field: str = "record_digest") -> str:
    payload = {key: item for key, item in value.items() if key != digest_field}
    encoded = json.dumps(
        payload,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()
