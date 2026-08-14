"""Strict JSON parsing shared by inventory and frozen source inputs."""

from __future__ import annotations

import json
from typing import Any

from .contract import fail, require_mapping


def _object_without_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            fail(f"duplicate object key {key!r}")
        result[key] = value
    return result


def parse_json_object(source: str, label: str) -> dict[str, Any]:
    """Parse a JSON object while rejecting duplicate keys at every depth."""

    try:
        value = json.loads(source, object_pairs_hook=_object_without_duplicate_keys)
    except json.JSONDecodeError as error:
        fail(f"{label} is invalid: {error}")
    return require_mapping(value, f"{label} root")
