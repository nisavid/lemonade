"""Render source-owned residency JSON Schemas and examples."""

from __future__ import annotations

import copy
from collections.abc import Iterable, Mapping
from typing import Any
from urllib.parse import urldefrag, urljoin

UINT64_MAX = 18_446_744_073_709_551_615
SCHEMA_KEYS = (
    "artifact_quarantine_record",
    "artifact_writer_job_revision",
    "artifact_writer_request_result",
    "authority_transaction_result",
    "coordinator_step_result",
    "operation_revision",
    "reason",
    "request_error",
    "residency_profiles",
    "resource_diagnostic",
    "response_diagnostic",
    "staged_import_session_record",
)
REQUIRED_SCHEMA_KEYWORDS = (
    "x-max-utf8-bytes",
    "x-nfc",
    "x-residency-assert",
)


class SchemaRenderError(ValueError):
    pass


def _mapping(value: Any, label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise SchemaRenderError(f"{label} must be an object")
    return value


def _list(value: Any, label: str) -> list[Any]:
    if not isinstance(value, list):
        raise SchemaRenderError(f"{label} must be an array")
    return value


def _strings(value: Any, label: str) -> list[str]:
    members = _list(value, label)
    if not all(isinstance(member, str) and member for member in members):
        raise SchemaRenderError(f"{label} must contain nonempty strings")
    return list(members)


def _string_schema(
    *, values: Iterable[str] | None = None, max_length: int = 128
) -> dict[str, Any]:
    schema: dict[str, Any] = {
        "type": "string",
        "minLength": 1,
        "maxLength": max_length,
    }
    if values is not None:
        schema["enum"] = list(dict.fromkeys(values))
    return schema


def _opaque(max_bytes: int = 128) -> dict[str, Any]:
    return {
        **_string_schema(max_length=max_bytes),
        "pattern": r"^[\u0021-\u007e]+$",
        "x-max-utf8-bytes": max_bytes,
    }


def _fixed_lower_hex(length: int) -> dict[str, Any]:
    return {
        "type": "string",
        "minLength": length,
        "maxLength": length,
        "pattern": f"^[0-9a-f]{{{length}}}$",
        "x-max-utf8-bytes": length,
    }


def _object(properties: Mapping[str, Any], required: Iterable[str]) -> dict[str, Any]:
    return {
        "type": "object",
        "additionalProperties": False,
        "properties": dict(properties),
        "required": list(required),
    }


def _resolve(registry: Mapping[str, Any], reference: str) -> Any:
    value: Any = registry
    for part in reference.split("."):
        value = _mapping(value, f"registry reference {reference}")
        if part not in value:
            raise SchemaRenderError(f"registry reference {reference} is unavailable")
        value = value[part]
    return value


def _registry_values(registry: Mapping[str, Any], reference: str) -> list[str]:
    value = _resolve(registry, reference)
    if isinstance(value, dict):
        return list(value)
    return _strings(value, f"registry reference {reference}")


def _enum_ref(registry: Mapping[str, Any], reference: str) -> dict[str, Any]:
    values = _registry_values(registry, reference)
    return _string_schema(values=values, max_length=max(map(len, values)))


def _identity_scope(registry: Mapping[str, Any]) -> dict[str, Any]:
    baseline = _baseline(registry, artifact=False)
    return _object(
        {
            "scope_key": _opaque(),
            "target": _enum_ref(
                registry, "http_auth_registry.resource_vocabularies.identity_targets"
            ),
            "baseline": baseline,
            "disposition": _enum_ref(
                registry,
                "http_auth_registry.resource_vocabularies.identity_artifact_dispositions",
            ),
            "target_generation": _opaque(),
        },
        ("scope_key", "target", "baseline", "disposition"),
    )


def _baseline(registry: Mapping[str, Any], *, artifact: bool) -> dict[str, Any]:
    states = _registry_values(
        registry, "http_auth_registry.resource_vocabularies.baseline_states"
    )
    branches: list[dict[str, Any]] = []
    for state in states:
        if state == "present":
            generation = "artifact_generation" if artifact else "identity_generation"
            branches.append(
                _object(
                    {
                        "state": {"const": state},
                        generation: _opaque(),
                        "digest" if artifact else "etag": _opaque(),
                    },
                    ("state", generation, "digest" if artifact else "etag"),
                )
            )
        elif state == "absent":
            properties = (
                {"state": {"const": state}, "artifact_store_generation": _opaque()}
                if artifact
                else {
                    "state": {"const": state},
                    "identity_root_generation": _opaque(),
                    "absence_validator": _opaque(),
                }
            )
            branches.append(_object(properties, tuple(properties)))
        else:
            raise SchemaRenderError(f"unsupported baseline state {state}")
    return {"oneOf": branches}


def _resolution_evidence(registry: Mapping[str, Any]) -> dict[str, Any]:
    return _object(
        {
            "marker_kind": _enum_ref(
                registry,
                "http_auth_registry.resource_vocabularies.resolution_marker_kinds",
            ),
            "marker_generation": _opaque(),
            "manifest_generation": _opaque(),
        },
        ("marker_kind", "marker_generation"),
    )


def _promotion_unit(catalog: Mapping[str, Any]) -> dict[str, Any]:
    units = [
        _mapping(unit, "catalog promotion unit")
        for unit in _list(catalog.get("promotion_units"), "catalog promotion_units")
    ]
    return _object(
        {
            "id": _string_schema(
                values=[
                    _mapping(unit, "catalog promotion unit")["id"] for unit in units
                ],
                max_length=max(len(unit["id"]) for unit in units),
            ),
            "unit_kind": _string_schema(
                values=sorted({unit["unit_kind"] for unit in units}),
                max_length=max(len(unit["unit_kind"]) for unit in units),
            ),
            "capability_level": _string_schema(
                values=sorted({unit["capability_level"] for unit in units}),
                max_length=max(len(unit["capability_level"]) for unit in units),
            ),
            "delivery_state": _string_schema(
                values=sorted({unit["delivery_state"] for unit in units}),
                max_length=max(len(unit["delivery_state"]) for unit in units),
            ),
            "contract": {"type": "object"},
        },
        ("id", "unit_kind", "capability_level", "delivery_state", "contract"),
    )


def _fallback(catalog: Mapping[str, Any]) -> dict[str, Any]:
    fallbacks = [
        _mapping(fallback, "catalog fallback")
        for fallback in _list(catalog.get("fallbacks"), "catalog fallbacks")
    ]
    operation_values = sorted(
        {
            operation
            for fallback in fallbacks
            for operation in _strings(
                fallback.get("operations"), "catalog fallback operations"
            )
        }
    )
    return _object(
        {
            "id": _string_schema(
                values=[fallback["id"] for fallback in fallbacks],
                max_length=max(len(fallback["id"]) for fallback in fallbacks),
            ),
            "operations": {
                "type": "array",
                "minItems": 1,
                "maxItems": max(len(fallback["operations"]) for fallback in fallbacks),
                "uniqueItems": True,
                "items": _string_schema(
                    values=operation_values,
                    max_length=max(map(len, operation_values)),
                ),
            },
            "guard": _string_schema(max_length=512),
            "effect": _string_schema(max_length=512),
        },
        ("id", "operations", "guard", "effect"),
    )


def _text_field_schema(
    name: str,
    spec: Mapping[str, Any],
    registry: Mapping[str, Any],
    catalog: Mapping[str, Any] | None,
) -> dict[str, Any] | None:
    del catalog
    field_type = spec["type"]
    max_bytes = spec.get("max_bytes", 128)
    if field_type in {"schema_literal", "literal"}:
        return {"const": spec.get("value")}
    if field_type == "opaque":
        return _opaque(max_bytes)
    if field_type == "git_commit_sha1":
        return _fixed_lower_hex(40)
    if field_type == "sha256":
        return _fixed_lower_hex(64)
    if field_type == "nullable_opaque":
        return {"oneOf": [{"type": "null"}, _opaque(max_bytes)]}
    if field_type == "utf8":
        return {
            **_string_schema(max_length=max_bytes),
            "x-max-utf8-bytes": max_bytes,
        }
    if field_type == "strong_etag":
        return {
            **_string_schema(max_length=128),
            "pattern": '^"[^"\\u0000-\\u001f]{1,124}"$',
            "x-strong-etag": True,
        }
    if field_type == "enum":
        values = _strings(spec.get("values"), f"schema field {name}.values")
        return _string_schema(values=values, max_length=max(map(len, values)))
    if field_type == "enum_ref":
        return _enum_ref(registry, spec["ref"])
    if field_type == "nullable_enum_ref":
        return {"oneOf": [{"type": "null"}, _enum_ref(registry, spec["ref"])]}
    return None


def _scalar_field_schema(
    name: str,
    spec: Mapping[str, Any],
    registry: Mapping[str, Any],
    catalog: Mapping[str, Any] | None,
) -> dict[str, Any] | None:
    del catalog
    field_type = spec["type"]
    if field_type == "boolean":
        return {"type": "boolean"}
    if field_type in {"uint64", "http_status"}:
        return {
            "type": "integer",
            "minimum": spec.get("minimum", 0),
            "maximum": spec.get("maximum", UINT64_MAX),
        }
    if field_type == "rfc3339":
        return {"type": "string", "format": "date-time"}
    if field_type == "nullable_rfc3339":
        return {"oneOf": [{"type": "null"}, {"type": "string", "format": "date-time"}]}
    if field_type == "object":
        return (
            _resolution_evidence(registry)
            if name == "resolution_evidence"
            else {"type": "object"}
        )
    return None


def _reason_field_schema(
    name: str,
    spec: Mapping[str, Any],
    registry: Mapping[str, Any],
    catalog: Mapping[str, Any] | None,
) -> dict[str, Any] | None:
    del name, catalog
    field_type = spec["type"]
    max_bytes = spec.get("max_bytes", 128)
    if field_type == "reason":
        return {"$ref": "#/$defs/reason"}
    if field_type == "reason_array":
        return {
            "type": "array",
            "maxItems": 16,
            "items": {"$ref": "#/$defs/reason"},
            "x-canonical-order": bool(spec.get("ordered", False)),
        }
    if field_type == "reason_code":
        return _string_schema(max_length=max_bytes)
    if field_type == "nullable_reason_code":
        return {"oneOf": [{"type": "null"}, _string_schema(max_length=max_bytes)]}
    if field_type == "closed_action_kind":
        return _enum_ref(registry, "operation_registry.aliases.all_operations")
    if field_type == "closed_endpoint_boundary":
        return {"const": registry["http_auth_registry"]["compatibility_epoch"]}
    if field_type == "reason_presentation":
        return {"$ref": "#/$defs/reason_presentation"}
    if field_type == "detail_schema_join":
        return {"type": "object"}
    return None


def _collection_field_schema(
    name: str,
    spec: Mapping[str, Any],
    registry: Mapping[str, Any],
    catalog: Mapping[str, Any] | None,
) -> dict[str, Any] | None:
    del catalog
    field_type = spec["type"]
    if field_type == "enum_array":
        values = _strings(spec.get("values"), f"schema field {name}.values")
        return {
            "type": "array",
            "minItems": spec.get("min_items", 0),
            "maxItems": spec.get("max_items", len(values)),
            "uniqueItems": True,
            "items": _string_schema(values=values, max_length=max(map(len, values))),
        }
    if field_type == "identity_scope_array":
        return {
            "type": "array",
            "minItems": spec.get("min_items", 1),
            "maxItems": spec.get("max_items", 2),
            "items": _identity_scope(registry),
            "x-canonical-order": bool(spec.get("canonical_sorted", False)),
        }
    if field_type == "tagged_baseline":
        return _baseline(registry, artifact=True)
    if field_type == "opaque_array":
        return {"type": "array", "maxItems": 32, "items": _opaque()}
    if field_type == "uint64_array":
        return {
            "type": "array",
            "maxItems": 4096,
            "uniqueItems": True,
            "items": {"type": "integer", "minimum": 0, "maximum": UINT64_MAX},
        }
    return None


def _reference_field_schema(
    name: str,
    spec: Mapping[str, Any],
    registry: Mapping[str, Any],
    catalog: Mapping[str, Any] | None,
) -> dict[str, Any] | None:
    del name
    field_type = spec["type"]
    if field_type == "schema_ref":
        referenced = _mapping(_resolve(registry, spec["ref"]), spec["ref"])
        return {"$ref": f"../{referenced['schema_type']}"}
    if field_type == "promotion_unit_array":
        if catalog is None:
            raise SchemaRenderError("promotion_unit_array requires the catalog")
        return {
            "type": "array",
            "minItems": spec["min_items"],
            "maxItems": spec["max_items"],
            "items": _promotion_unit(catalog),
        }
    if field_type == "fallback_array":
        if catalog is None:
            raise SchemaRenderError("fallback_array requires the catalog")
        return {
            "type": "array",
            "minItems": spec["min_items"],
            "maxItems": spec["max_items"],
            "items": _fallback(catalog),
        }
    return None


def _field_schema(
    name: str,
    raw_spec: Any,
    registry: Mapping[str, Any],
    catalog: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    spec = _mapping(raw_spec, f"schema field {name}")
    field_type = spec.get("type")
    if not isinstance(field_type, str):
        raise SchemaRenderError(f"schema field {name}.type is invalid")
    for renderer in (
        _text_field_schema,
        _scalar_field_schema,
        _reason_field_schema,
        _collection_field_schema,
        _reference_field_schema,
    ):
        rendered = renderer(name, spec, registry, catalog)
        if rendered is not None:
            return rendered
    raise SchemaRenderError(f"schema field {name} has unsupported type {field_type}")


def _detail_definitions(registry: Mapping[str, Any]) -> dict[str, Any]:
    details = _mapping(registry.get("detail_schema_registry"), "detail_schema_registry")
    result: dict[str, Any] = {}
    for detail_id, raw_detail in details.items():
        detail = _mapping(raw_detail, f"detail_schema_registry.{detail_id}")
        fields = _mapping(
            detail.get("fields"), f"detail_schema_registry.{detail_id}.fields"
        )
        result[detail_id] = _object(
            {
                name: {
                    **_field_schema(name, spec, registry),
                    "x-audience": _mapping(
                        spec, f"detail_schema_registry.{detail_id}.fields.{name}"
                    ).get("audience"),
                }
                for name, spec in fields.items()
            },
            _strings(
                detail.get("required_fields"),
                f"detail_schema_registry.{detail_id}.required_fields",
            ),
        )
    return result


def _presentation_text(max_bytes: int) -> dict[str, Any]:
    return {
        **_string_schema(max_length=max_bytes),
        "pattern": r"^[^\u0000-\u001f\u007f-\u009f\u202a-\u202e\u2066-\u2069<>*_`\[\]{}]*$",
        "x-nfc": True,
        "x-max-utf8-bytes": max_bytes,
    }


def _presentation_values(registry: Mapping[str, Any], field: str) -> list[str]:
    presentations = _mapping(
        registry.get("presentation_registry"), "presentation_registry"
    )
    return sorted(
        {
            _mapping(row, f"presentation_registry.{presentation_id}")[field]
            for presentation_id, row in presentations.items()
        }
    )


def _presentation(
    registry: Mapping[str, Any], exact: dict[str, Any] | None = None
) -> dict[str, Any]:
    required = (
        "presentation_id",
        "category_id",
        "severity",
        "title",
        "default_message",
    )
    if exact is not None:
        source = exact["presentation"]
        return _object(
            {
                "presentation_id": {"const": exact["presentation_id"]},
                "category_id": {"const": exact["category_id"]},
                "severity": {"const": source["severity"]},
                "title": {
                    "const": source["title"],
                    **_presentation_text(80),
                },
                "default_message": {
                    "const": source["default_message"],
                    **_presentation_text(256),
                },
            },
            required,
        )
    return _object(
        {
            "presentation_id": _opaque(64),
            "category_id": _string_schema(
                values=_presentation_values(registry, "category_id"),
                max_length=64,
            ),
            "severity": _string_schema(
                values=_presentation_values(registry, "severity"),
                max_length=16,
            ),
            "title": _presentation_text(80),
            "default_message": _presentation_text(256),
        },
        required,
    )


def _reason_definitions(
    registry: Mapping[str, Any], reason_rows: list[dict[str, Any]]
) -> dict[str, Any]:
    definitions = _detail_definitions(registry)
    known_codes = [row["code"] for row in reason_rows]
    branches: list[dict[str, Any]] = []
    for row in reason_rows:
        properties: dict[str, Any] = {
            "code": {"const": row["code"]},
            "presentation": _presentation(registry, row),
        }
        required = ["code", "presentation"]
        detail_id = row["detail_schema_id"]
        if detail_id != "d_none":
            properties["details"] = {"$ref": f"#/$defs/{detail_id}"}
            required.append("details")
        branch = _object(properties, required)
        branch["x-envelope-legality"] = copy.deepcopy(row["contract"]["envelopes"])
        branches.append(branch)
    branches.append(
        _object(
            {
                "code": {
                    **_string_schema(max_length=128),
                    "not": {"enum": known_codes},
                },
                "presentation": _presentation(registry),
            },
            ("code", "presentation"),
        )
    )
    definitions["reason_presentation"] = _presentation(registry)
    definitions["reason"] = {"oneOf": branches}
    return definitions


def _reason_reference(registry: Mapping[str, Any], envelope: str) -> dict[str, Any]:
    reference = {"$ref": f"../{registry['schema_registry']['reason']['schema_type']}"}
    envelope_registry = _mapping(
        registry.get("reason_envelope_registry"), "reason_envelope_registry"
    )
    if envelope not in envelope_registry:
        return reference
    envelope_row = _mapping(
        envelope_registry[envelope], f"reason_envelope_registry.{envelope}"
    )
    allowed_codes = _strings(
        envelope_row.get("reason_codes"),
        f"reason_envelope_registry.{envelope}.reason_codes",
    )
    known_codes = list(_mapping(registry.get("reason_registry"), "reason_registry"))
    return {
        "allOf": [
            reference,
            {
                "type": "object",
                "properties": {
                    "code": {
                        "anyOf": [
                            {"enum": allowed_codes},
                            {"not": {"enum": known_codes}},
                        ]
                    }
                },
                "required": ["code"],
            },
        ],
        "x-reason-envelope": envelope,
    }


def _request_error_http_conditionals(
    registry: Mapping[str, Any], reason_rows: list[dict[str, Any]]
) -> list[dict[str, Any]]:
    allowed_codes = registry["reason_envelope_registry"]["request_error"][
        "reason_codes"
    ]
    rows_by_code = {row["code"]: row for row in reason_rows}
    result: list[dict[str, Any]] = []
    for code in allowed_codes:
        envelope = rows_by_code[code]["contract"]["envelopes"]["request_error"]
        statuses = {envelope["http_status"]}
        statuses.update(
            override["status"] for override in envelope.get("endpoint_overrides", [])
        )
        result.append(
            {
                "if": {
                    "properties": {
                        "reason": {
                            "type": "object",
                            "properties": {"code": {"const": code}},
                            "required": ["code"],
                        }
                    },
                    "required": ["reason"],
                },
                "then": {
                    "properties": {"http_status": {"enum": sorted(statuses)}},
                    "required": ["http_status"],
                },
            }
        )
    return result


def _predicate(raw: Mapping[str, Any], label: str) -> dict[str, Any]:
    parts: list[dict[str, Any]] = []
    field = raw.get("field")
    if isinstance(field, str):
        if "equals" in raw:
            parts.append(
                {"properties": {field: {"const": raw["equals"]}}, "required": [field]}
            )
        if "not_equals" in raw:
            parts.append(
                {
                    "properties": {field: {"not": {"const": raw["not_equals"]}}},
                    "required": [field],
                }
            )
        if "in" in raw:
            parts.append(
                {"properties": {field: {"enum": raw["in"]}}, "required": [field]}
            )
        if "not_in" in raw:
            parts.append(
                {
                    "properties": {field: {"not": {"enum": raw["not_in"]}}},
                    "required": [field],
                }
            )
    for key, value in raw.items():
        if isinstance(key, str) and key.endswith("_not_in"):
            other = key[: -len("_not_in")]
            parts.append(
                {
                    "properties": {other: {"not": {"enum": value}}},
                    "required": [other],
                }
            )
    if "reason_code" in raw:
        parts.append(
            {
                "properties": {
                    "reason": {
                        "type": "object",
                        "properties": {"code": {"const": raw["reason_code"]}},
                        "required": ["code"],
                    }
                },
                "required": ["reason"],
            }
        )
    if not parts:
        raise SchemaRenderError(f"{label} predicate is not translatable")
    return parts[0] if len(parts) == 1 else {"allOf": parts}


def _consequence(conditional: Mapping[str, Any], label: str) -> dict[str, Any] | None:
    parts: list[dict[str, Any]] = []
    if "require" in conditional:
        parts.append({"required": conditional["require"]})
    if "forbid" in conditional:
        parts.extend({"not": {"required": [field]}} for field in conditional["forbid"])
    for key, property_schema in (
        ("require_null", {"type": "null"}),
        ("require_nonnull", {"not": {"type": "null"}}),
        ("require_empty", {"maxItems": 0}),
        ("require_nonempty", {"minItems": 1}),
    ):
        if key not in conditional:
            continue
        fields = conditional[key]
        parts.append(
            {
                "properties": {field: property_schema for field in fields},
                "required": fields,
            }
        )
    if "require_values" in conditional:
        values = conditional["require_values"]
        if not values:
            raise SchemaRenderError(f"{label}.require_values is empty")
        parts.append(
            {
                "properties": {
                    field: {"const": value} for field, value in values.items()
                },
                "required": list(values),
            }
        )
    if not parts:
        if "allow_empty" in conditional:
            return None
        raise SchemaRenderError(f"{label} consequence is empty")
    return parts[0] if len(parts) == 1 else {"allOf": parts}


def _conditionals(raw_value: Any) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for index, raw_conditional in enumerate(_list(raw_value, "schema conditionals")):
        conditional = _mapping(raw_conditional, f"schema conditionals[{index}]")
        if "assert" in conditional:
            assertion = copy.deepcopy(
                _mapping(
                    conditional["assert"],
                    f"schema conditionals[{index}].assert",
                )
            )
            keyword = (
                "x-residency-derived-by"
                if "selected_by" in assertion
                else "x-residency-assert"
            )
            result.append({keyword: assertion})
            continue
        key = "if" if "if" in conditional else "unless"
        raw_predicate = _mapping(conditional.get(key), f"schema conditional {key}")
        label = f"schema conditionals[{index}]"
        predicate = _predicate(raw_predicate, label)
        if key == "unless":
            predicate = {"not": predicate}
        consequence = _consequence(conditional, label)
        if consequence is None:
            continue
        translated = {"if": predicate, "then": consequence}
        if "contexts" in raw_predicate:
            translated["x-residency-contexts"] = raw_predicate["contexts"]
        result.append(translated)
    return result


def _required_schema_keywords(value: Any) -> list[str]:
    required: set[str] = set()
    if isinstance(value, Mapping):
        required.update(
            keyword for keyword in REQUIRED_SCHEMA_KEYWORDS if keyword in value
        )
        for child in value.values():
            required.update(_required_schema_keywords(child))
    elif isinstance(value, list):
        for child in value:
            required.update(_required_schema_keywords(child))
    return sorted(required)


def _schema_references(document: Mapping[str, Any]) -> set[str]:
    base = document["$id"]
    references: set[str] = set()
    for _, mapping in _schema_mappings(document):
        reference = mapping.get("$ref")
        if isinstance(reference, str):
            resolved, _ = urldefrag(urljoin(base, reference))
            references.add(resolved)
    return references


def _declare_required_schema_keywords(
    schemas: Mapping[str, dict[str, Any]],
) -> None:
    by_type = {schema["$id"]: key for key, schema in schemas.items()}
    required = {
        key: set(_required_schema_keywords(schema)) for key, schema in schemas.items()
    }
    references = {
        key: {
            by_type[reference]
            for reference in _schema_references(schema)
            if reference in by_type
        }
        for key, schema in schemas.items()
    }
    changed = True
    while changed:
        changed = False
        for key, targets in references.items():
            transitive = set().union(*(required[target] for target in targets))
            expanded = required[key] | transitive
            if expanded != required[key]:
                required[key] = expanded
                changed = True
    for key, schema in schemas.items():
        if required[key]:
            schema["x-residency-required-keywords"] = sorted(required[key])


def _schema_mappings(value: Any) -> Iterable[tuple[str, Mapping[str, Any]]]:
    if isinstance(value, Mapping):
        yield "", value
        for child in value.values():
            yield from _schema_mappings(child)
    elif isinstance(value, list):
        for child in value:
            yield from _schema_mappings(child)


def _schema_document(
    key: str,
    row: Mapping[str, Any],
    registry: Mapping[str, Any],
    definitions: Mapping[str, Any],
    catalog: Mapping[str, Any],
    reason_rows: list[dict[str, Any]],
) -> dict[str, Any]:
    document_definitions: dict[str, Any]
    if key == "reason":
        root = copy.deepcopy(definitions["reason"])
        document_definitions = {
            name: copy.deepcopy(definition)
            for name, definition in definitions.items()
            if name != "reason"
        }
    else:
        fields = _mapping(row.get("fields"), f"schema_registry.{key}.fields")
        root = _object(
            {
                name: _field_schema(name, spec, registry, catalog)
                for name, spec in fields.items()
            },
            [
                name
                for name, spec in fields.items()
                if _mapping(spec, f"schema_registry.{key}.fields.{name}").get(
                    "required"
                )
                is True
            ],
        )
        translated = _conditionals(row.get("conditionals"))
        if translated:
            root["allOf"] = translated
        uses_reason = any(
            _mapping(spec, f"schema_registry.{key}.fields.{name}").get("type")
            in {"reason", "reason_array"}
            for name, spec in fields.items()
        )
        document_definitions = (
            {"reason": _reason_reference(registry, key)} if uses_reason else {}
        )
        if key == "request_error":
            root.setdefault("allOf", []).extend(
                _request_error_http_conditionals(registry, reason_rows)
            )
    document = {
        "$schema": "https://json-schema.org/draft/2020-12/schema",
        "$id": row["schema_type"],
        "title": " ".join(part.capitalize() for part in key.split("_")),
        **root,
        "$defs": document_definitions,
    }
    return document


def _reason_example(
    registry: Mapping[str, Any],
    rows: list[dict[str, Any]],
    code: str | None = None,
) -> dict[str, Any]:
    if code is None:
        operation_envelope = _mapping(
            _mapping(
                registry.get("reason_envelope_registry"),
                "reason_envelope_registry",
            ).get("operation_revision"),
            "reason_envelope_registry.operation_revision",
        )
        code = _strings(
            operation_envelope.get("reason_codes"),
            "reason_envelope_registry.operation_revision.reason_codes",
        )[0]
    row = next((candidate for candidate in rows if candidate["code"] == code), None)
    if row is None:
        raise SchemaRenderError(f"reason registry lacks {code}")
    result = {
        "code": code,
        "presentation": {
            "presentation_id": row["presentation_id"],
            "category_id": row["category_id"],
            "severity": row["presentation"]["severity"],
            "title": row["presentation"]["title"],
            "default_message": row["presentation"]["default_message"],
        },
    }
    detail_id = row["detail_schema_id"]
    if detail_id != "d_none":
        detail = _mapping(
            _mapping(
                registry.get("detail_schema_registry"), "detail_schema_registry"
            ).get(detail_id),
            f"detail_schema_registry.{detail_id}",
        )
        fields = _mapping(
            detail.get("fields"), f"detail_schema_registry.{detail_id}.fields"
        )
        result["details"] = {
            name: _field_example(name, fields[name], registry, rows)
            for name in _strings(
                detail.get("required_fields"),
                f"detail_schema_registry.{detail_id}.required_fields",
            )
        }
    return result


def _simple_field_example(
    name: str,
    spec: Mapping[str, Any],
    registry: Mapping[str, Any],
    reason_rows: list[dict[str, Any]],
) -> tuple[bool, Any]:
    del registry, reason_rows
    field_type = spec["type"]
    if field_type in {"schema_literal", "literal"}:
        return True, spec["value"]
    if field_type in {"opaque", "utf8"}:
        return True, f"{name}-1"
    if field_type in {"nullable_opaque", "nullable_enum_ref", "nullable_rfc3339"}:
        return True, None
    if field_type == "strong_etag":
        return True, '"etag-1"'
    if field_type == "enum":
        return True, spec["values"][0]
    if field_type == "boolean":
        return True, False
    if field_type in {"uint64", "http_status"}:
        return True, spec.get("minimum", 0)
    if field_type == "rfc3339":
        return True, "2026-01-01T00:00:00Z"
    return False, None


def _registry_field_example(
    name: str,
    spec: Mapping[str, Any],
    registry: Mapping[str, Any],
    reason_rows: list[dict[str, Any]],
) -> tuple[bool, Any]:
    del name
    field_type = spec["type"]
    if field_type == "enum_ref":
        return True, _registry_values(registry, spec["ref"])[0]
    if field_type == "reason":
        return True, _reason_example(registry, reason_rows)
    if field_type in {"reason_array", "opaque_array", "uint64_array", "enum_array"}:
        return True, []
    if field_type == "nullable_reason_code":
        return True, None
    if field_type == "reason_code":
        return True, reason_rows[0]["code"]
    if field_type == "closed_action_kind":
        return (
            True,
            _registry_values(registry, "operation_registry.aliases.all_operations")[0],
        )
    if field_type == "closed_endpoint_boundary":
        return True, registry["http_auth_registry"]["compatibility_epoch"]
    if field_type == "reason_presentation":
        return True, _reason_example(registry, reason_rows)["presentation"]
    if field_type in {"detail_schema_join", "promotion_unit_array", "fallback_array"}:
        return True, {} if field_type == "detail_schema_join" else []
    return False, None


def _resource_field_example(
    name: str,
    spec: Mapping[str, Any],
    registry: Mapping[str, Any],
    reason_rows: list[dict[str, Any]],
) -> tuple[bool, Any]:
    del reason_rows
    field_type = spec["type"]
    vocabularies = registry["http_auth_registry"]["resource_vocabularies"]
    if field_type == "object":
        if name != "resolution_evidence":
            return True, {}
        return True, {
            "marker_kind": vocabularies["resolution_marker_kinds"][0],
            "marker_generation": "generation-1",
        }
    if field_type == "identity_scope_array":
        return True, [
            {
                "scope_key": "scope-1",
                "target": vocabularies["identity_targets"][0],
                "baseline": {
                    "state": vocabularies["baseline_states"][0],
                    "identity_generation": "generation-1",
                    "etag": "etag-1",
                },
                "disposition": vocabularies["identity_artifact_dispositions"][0],
            }
        ]
    if field_type == "tagged_baseline":
        return True, {
            "state": vocabularies["baseline_states"][0],
            "artifact_generation": "generation-1",
            "digest": "digest-1",
        }
    return False, None


def _field_example(
    name: str,
    raw_spec: Any,
    registry: Mapping[str, Any],
    reason_rows: list[dict[str, Any]],
) -> Any:
    spec = _mapping(raw_spec, f"example field {name}")
    field_type = spec["type"]
    for renderer in (
        _simple_field_example,
        _registry_field_example,
        _resource_field_example,
    ):
        matched, example = renderer(name, spec, registry, reason_rows)
        if matched:
            return example
    raise SchemaRenderError(f"example field {name} has unsupported type {field_type}")


def _example(
    key: str,
    row: Mapping[str, Any],
    registry: Mapping[str, Any],
    reason_rows: list[dict[str, Any]],
    catalog: Mapping[str, Any],
) -> Any:
    if key == "reason":
        return _reason_example(registry, reason_rows)
    if key == "residency_profiles":
        return copy.deepcopy(catalog)
    fields = _mapping(row.get("fields"), f"schema_registry.{key}.fields")
    result = {
        name: _field_example(name, spec, registry, reason_rows)
        for name, spec in fields.items()
        if _mapping(spec, f"schema_registry.{key}.fields.{name}").get("required")
        is True
    }
    if key == "coordinator_step_result":
        result["candidate_id"] = "candidate-1"
        result["artifact_scope_key"] = "artifact-scope-1"
    if key == "request_error":
        reason_code = registry["reason_envelope_registry"]["request_error"][
            "reason_codes"
        ][0]
        result["reason"] = _reason_example(registry, reason_rows, reason_code)
        reason = next(
            candidate for candidate in reason_rows if candidate["code"] == reason_code
        )
        result["http_status"] = reason["contract"]["envelopes"]["request_error"][
            "http_status"
        ]
    if key == "resource_diagnostic":
        reason_code = registry["reason_envelope_registry"]["resource_diagnostic"][
            "reason_codes"
        ][0]
        result["reason"] = _reason_example(registry, reason_rows, reason_code)
        result["subject_generation"] = "generation-1"
    if key == "response_diagnostic":
        reason_code = registry["reason_envelope_registry"]["response_diagnostic"][
            "reason_codes"
        ][0]
        result["reason"] = _reason_example(registry, reason_rows, reason_code)
    return result


def build_schemas(
    registry: Mapping[str, Any],
    reason_rows: list[dict[str, Any]],
    catalog: Mapping[str, Any],
) -> tuple[dict[str, dict[str, Any]], dict[str, Any]]:
    schema_rows = _mapping(registry.get("schema_registry"), "schema_registry")
    if tuple(schema_rows) != SCHEMA_KEYS:
        raise SchemaRenderError("schema_registry order or membership drifted")
    definitions = _reason_definitions(registry, reason_rows)
    schemas = {
        key: _schema_document(
            key,
            schema_rows[key],
            registry,
            definitions,
            catalog,
            reason_rows,
        )
        for key in SCHEMA_KEYS
    }
    _declare_required_schema_keywords(schemas)
    examples = {
        key: _example(key, schema_rows[key], registry, reason_rows, catalog)
        for key in SCHEMA_KEYS
    }
    return schemas, examples
