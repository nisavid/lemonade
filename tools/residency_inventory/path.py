"""Shared schema-path grammar for inventory and generated-contract validation."""

from __future__ import annotations

import re

_PATH_SEGMENT = re.compile(
    r"(?P<field>[A-Za-z_][A-Za-z0-9_]*)(?P<indexes>(?:\[(?:0|[1-9][0-9]*)\])*)"
)
_PATH_INDEX = re.compile(r"\[(0|[1-9][0-9]*)\]")


def _path_tokens(path: str) -> tuple[str | int, ...] | None:
    tokens: list[str | int] = []
    for segment in path.split("."):
        match = _PATH_SEGMENT.fullmatch(segment)
        if match is None:
            return None
        tokens.append(match.group("field"))
        tokens.extend(
            int(index) for index in _PATH_INDEX.findall(match.group("indexes"))
        )
    return tuple(tokens)
