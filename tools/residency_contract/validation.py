"""Validate residency contracts with their declared extension vocabulary."""

from __future__ import annotations

import copy
import re
import unicodedata
from collections.abc import Iterator, Mapping
from typing import Any
from urllib.parse import urldefrag, urljoin

from jsonschema import Draft202012Validator, validators
from jsonschema.exceptions import SchemaError, ValidationError
from referencing import Registry
from referencing.jsonschema import DRAFT202012

REQUIRED_KEYWORDS_FIELD = "x-residency-required-keywords"
SUPPORTED_REQUIRED_KEYWORDS = frozenset(
    {"x-max-utf8-bytes", "x-nfc", "x-residency-assert"}
)
_PATH_SEGMENT = re.compile(
    r"(?P<field>[A-Za-z_][A-Za-z0-9_]*)(?P<indexes>(?:\[(?:0|[1-9][0-9]*)\])*)"
)
_PATH_INDEX = re.compile(r"\[(0|[1-9][0-9]*)\]")
_MISSING = object()


def _validation_error(message: str) -> Iterator[ValidationError]:
    yield ValidationError(message)


def _validate_nfc(
    validator: Any, expected: Any, instance: Any, schema: Any
) -> Iterator[ValidationError]:
    del validator, schema
    if expected is not True or not isinstance(instance, str):
        return
    try:
        instance.encode("utf-8")
    except UnicodeEncodeError:
        yield from _validation_error("string must contain valid Unicode scalar values")
        return
    if unicodedata.normalize("NFC", instance) != instance:
        yield from _validation_error("string must be NFC")


def _validate_utf8_bytes(
    validator: Any, maximum: Any, instance: Any, schema: Any
) -> Iterator[ValidationError]:
    del validator, schema
    if isinstance(maximum, bool) or not isinstance(maximum, int):
        return
    if not isinstance(instance, str):
        return
    try:
        encoded = instance.encode("utf-8")
    except UnicodeEncodeError:
        yield from _validation_error("string must contain valid Unicode scalar values")
        return
    if len(encoded) > maximum:
        yield from _validation_error(f"string exceeds {maximum} UTF-8 bytes")


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


def _resolve_path(instance: Any, path: str) -> Any:
    value = instance
    tokens = _path_tokens(path)
    if tokens is None:
        return _MISSING
    for token in tokens:
        if isinstance(token, str) and isinstance(value, Mapping):
            value = value.get(token, _MISSING)
        elif isinstance(token, int) and isinstance(value, list) and token < len(value):
            value = value[token]
        else:
            return _MISSING
        if value is _MISSING:
            return _MISSING
    return value


def _validate_residency_assert(
    validator: Any, assertion: Any, instance: Any, schema: Any
) -> Iterator[ValidationError]:
    del validator, schema
    if not isinstance(instance, Mapping) or not isinstance(assertion, Mapping):
        return
    field = assertion.get("field")
    comparisons = {
        key: assertion.get(key)
        for key in (
            "equals_path",
            "less_than_path",
            "less_than_or_equal_path",
        )
        if key in assertion
    }
    if (
        not isinstance(field, str)
        or len(comparisons) != 1
        or not all(isinstance(path, str) for path in comparisons.values())
    ):
        return
    comparison, path = next(iter(comparisons.items()))
    actual = _resolve_path(instance, field)
    expected = _resolve_path(instance, path)
    actual_is_null = actual is _MISSING or actual is None
    expected_is_null = expected is _MISSING or expected is None
    if comparison == "equals_path":
        if actual_is_null and expected_is_null:
            return
        if actual_is_null != expected_is_null or actual != expected:
            yield from _validation_error(f"{field} must equal the value at {path}")
        return
    if (
        actual_is_null
        or expected_is_null
        or isinstance(actual, bool)
        or isinstance(expected, bool)
        or not isinstance(actual, (int, float))
        or not isinstance(expected, (int, float))
    ):
        yield from _validation_error(
            f"{field} and {path} must be numbers for ordered comparison"
        )
        return
    valid = actual < expected if comparison == "less_than_path" else actual <= expected
    if not valid:
        relation = "less than" if comparison == "less_than_path" else "at most"
        yield from _validation_error(f"{field} must be {relation} the value at {path}")


ResidencyDraft202012Validator = validators.extend(
    Draft202012Validator,
    {
        "x-max-utf8-bytes": _validate_utf8_bytes,
        "x-nfc": _validate_nfc,
        "x-residency-assert": _validate_residency_assert,
    },
)


def _schema_mappings(
    value: Any, path: tuple[str, ...] = ()
) -> Iterator[tuple[tuple[str, ...], Mapping[str, Any]]]:
    if isinstance(value, Mapping):
        yield path, value
        for key, child in value.items():
            yield from _schema_mappings(child, (*path, str(key)))
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from _schema_mappings(child, (*path, str(index)))


def _schema_error(message: str) -> None:
    raise SchemaError(message)


def _declared_keywords(schema: Mapping[str, Any]) -> set[str]:
    declaration = schema.get(REQUIRED_KEYWORDS_FIELD)
    if declaration is None:
        return set()
    if not isinstance(declaration, list) or not all(
        isinstance(keyword, str) and keyword for keyword in declaration
    ):
        _schema_error(f"{REQUIRED_KEYWORDS_FIELD} must be an array of keyword names")
    if declaration != sorted(set(declaration)):
        _schema_error(f"{REQUIRED_KEYWORDS_FIELD} must be sorted and unique")
    unknown = set(declaration) - SUPPORTED_REQUIRED_KEYWORDS
    if unknown:
        _schema_error(f"unsupported required residency keywords: {sorted(unknown)!r}")
    return set(declaration)


def _validate_assertion(value: Any) -> None:
    if not isinstance(value, Mapping):
        _schema_error("x-residency-assert must be an object")
    comparisons = set(value) & {
        "equals_path",
        "less_than_path",
        "less_than_or_equal_path",
    }
    if len(comparisons) != 1 or set(value) != {"field", *comparisons}:
        _schema_error(
            "x-residency-assert requires field and exactly one path comparison"
        )
    field = value["field"]
    comparison = next(iter(comparisons))
    path = value[comparison]
    if not isinstance(field, str) or _path_tokens(field) is None:
        _schema_error("x-residency-assert field path is invalid")
    if not isinstance(path, str) or _path_tokens(path) is None:
        _schema_error(f"x-residency-assert {comparison} is invalid")


def _used_keywords(schema: Mapping[str, Any]) -> set[str]:
    used: set[str] = set()
    for path, mapping in _schema_mappings(schema):
        if path and REQUIRED_KEYWORDS_FIELD in mapping:
            _schema_error(f"{REQUIRED_KEYWORDS_FIELD} is permitted only at the root")
        for keyword in SUPPORTED_REQUIRED_KEYWORDS:
            if keyword not in mapping:
                continue
            value = mapping[keyword]
            if keyword == "x-nfc" and value is not True:
                _schema_error("x-nfc must be true")
            if keyword == "x-max-utf8-bytes" and (
                isinstance(value, bool) or not isinstance(value, int) or value < 0
            ):
                _schema_error("x-max-utf8-bytes must be a nonnegative integer")
            if keyword == "x-residency-assert":
                _validate_assertion(value)
            used.add(keyword)
    return used


def _schema_references(schema: Mapping[str, Any], base_uri: str) -> set[str]:
    references: set[str] = set()
    base = schema.get("$id", base_uri)
    for _, mapping in _schema_mappings(schema):
        reference = mapping.get("$ref")
        if isinstance(reference, str):
            resolved, _ = urldefrag(urljoin(base, reference))
            references.add(resolved)
    return references


def _bundle_schemas(
    schema: Mapping[str, Any], registry: Any
) -> dict[str, Mapping[str, Any]]:
    bundled: dict[str, Mapping[str, Any]] = {}
    if registry is not None:
        for uri, resource in registry.items():
            if isinstance(resource.contents, Mapping):
                bundled[str(uri)] = resource.contents
    root_uri = schema.get("$id", "urn:residency-contract:root")
    bundled[str(root_uri)] = schema
    return bundled


def _validate_bundle_declarations(schema: Mapping[str, Any], registry: Any) -> None:
    bundled = _bundle_schemas(schema, registry)
    aliases = {candidate.get("$id", uri): uri for uri, candidate in bundled.items()}
    declared: dict[str, set[str]] = {}
    required: dict[str, set[str]] = {}
    references: dict[str, set[str]] = {}
    for uri, candidate in bundled.items():
        ResidencyDraft202012Validator.check_schema(candidate)
        declared[uri] = _declared_keywords(candidate)
        required[uri] = _used_keywords(candidate)
        references[uri] = {
            aliases[reference]
            for reference in _schema_references(candidate, uri)
            if reference in aliases
        }
    changed = True
    while changed:
        changed = False
        for uri, targets in references.items():
            transitive = set().union(*(required[target] for target in targets))
            expanded = required[uri] | transitive
            if expanded != required[uri]:
                required[uri] = expanded
                changed = True
    for uri in bundled:
        if declared[uri] == required[uri]:
            continue
        missing = sorted(required[uri] - declared[uri])
        unused = sorted(declared[uri] - required[uri])
        _schema_error(
            f"required residency keyword declaration differs for {uri}; "
            f"missing={missing!r}, unused={unused!r}"
        )


def _without_dialect(value: Any) -> Any:
    if isinstance(value, Mapping):
        return {
            key: _without_dialect(child)
            for key, child in value.items()
            if key != "$schema"
        }
    if isinstance(value, list):
        return [_without_dialect(child) for child in value]
    return copy.deepcopy(value)


def _validation_registry(schema: Mapping[str, Any], registry: Any) -> Registry[Any]:
    resources = []
    if registry is not None:
        resources.extend(
            (
                str(uri),
                DRAFT202012.create_resource(_without_dialect(resource.contents)),
            )
            for uri, resource in registry.items()
        )
    root_uri = schema.get("$id", "urn:residency-contract:root")
    resources.append(
        (str(root_uri), DRAFT202012.create_resource(_without_dialect(schema)))
    )
    return Registry().with_resources(resources)


def contract_validator(schema: Mapping[str, Any], *, registry: Any = None) -> Any:
    """Return a Draft 2020-12 validator with declared residency extensions."""

    _validate_bundle_declarations(schema, registry)
    return ResidencyDraft202012Validator(
        _without_dialect(schema), registry=_validation_registry(schema, registry)
    )


__all__ = ["contract_validator"]
