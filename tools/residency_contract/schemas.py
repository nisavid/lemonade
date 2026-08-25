"""Render source-owned residency JSON Schemas and examples."""

from __future__ import annotations

import copy
from collections.abc import Iterable, Mapping
from typing import Any
from urllib.parse import urldefrag, urljoin

from residency_inventory.contract import (
    EXPECTED_CONSTRAINT_KINDS,
    OPERATION_LEAVES_BY_TEMPLATE,
)

UINT64_MAX = 18_446_744_073_709_551_615
SCHEMA_KEYS = (
    "artifact_quarantine_record",
    "artifact_writer_job_revision",
    "artifact_writer_request_result",
    "authority_transaction_result",
    "coordinator_step_result",
    "deployment_local_overlay_object",
    "operation_revision",
    "overlay_activation_root",
    "profiling_input_envelope",
    "profiling_phase_attestation",
    "reason",
    "request_error",
    "residency_profiles",
    "resource_diagnostic",
    "response_diagnostic",
    "staged_import_session_record",
)
LOCAL_OVERLAY_SCHEMA_KEYS = frozenset(
    {
        "deployment_local_overlay_object",
        "overlay_activation_root",
        "profiling_input_envelope",
        "profiling_phase_attestation",
    }
)
LOCAL_OVERLAY_TEMPLATE_OPERATIONS = {
    template: tuple(sorted(operations))
    for template, operations in sorted(OPERATION_LEAVES_BY_TEMPLATE.items())
}
LOCAL_OVERLAY_OPERATION_TEMPLATES = tuple(LOCAL_OVERLAY_TEMPLATE_OPERATIONS)
LOCAL_OVERLAY_CONSTRAINT_KINDS = tuple(sorted(EXPECTED_CONSTRAINT_KINDS))
LOCAL_OVERLAY_UTC_SECOND_PATTERN = (
    r"^(?:(?:[0-9]{3}[1-9]|[0-9]{2}[1-9][0-9]|[0-9][1-9][0-9]{2}|"
    r"[1-9][0-9]{3})-(?:(?:01|03|05|07|08|10|12)-(?:0[1-9]|[12][0-9]|3[01])|"
    r"(?:04|06|09|11)-(?:0[1-9]|[12][0-9]|30)|02-(?:0[1-9]|1[0-9]|2[0-8]))|"
    r"(?:[0-9]{2}(?:0[48]|[2468][048]|[13579][26])|"
    r"(?:0[48]|[2468][048]|[13579][26])00)-02-29)"
    r"T(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]Z$"
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


def _local_overlay_claim_family(
    family: str, unit: str, *, compatibility: bool = False
) -> dict[str, Any]:
    amount = {"type": "integer", "minimum": 1, "maximum": UINT64_MAX}
    if compatibility:
        amount = {"const": 1, "type": "integer"}
    entry = _object(
        {
            "amount": amount,
            "constraint_id": _opaque(),
            "unit": {"const": unit},
        },
        ("amount", "constraint_id", "unit"),
    )
    bounded = _object(
        {
            "completeness": {"const": "bounded"},
            "entries": {
                "type": "array",
                "minItems": 1,
                "maxItems": 256,
                "items": entry,
                "x-canonical-order": True,
            },
            "family": {"const": family},
        },
        ("completeness", "entries", "family"),
    )
    empty = [
        _object(
            {
                "completeness": {"const": completeness},
                "entries": {"type": "array", "maxItems": 0},
                "family": {"const": family},
            },
            ("completeness", "entries", "family"),
        )
        for completeness in ("known_zero", "not_applicable")
    ]
    return {"oneOf": [bounded, *empty]}


def _local_overlay_claim_closure() -> dict[str, Any]:
    families = (
        _local_overlay_claim_family("consumable_capacity", "bytes"),
        _local_overlay_claim_family("safety_floor", "bytes"),
        _local_overlay_claim_family("cardinality_pool", "count"),
        _local_overlay_claim_family(
            "compatibility_exclusivity", "count", compatibility=True
        ),
    )
    return {
        "type": "array",
        "minItems": len(families),
        "maxItems": len(families),
        "prefixItems": list(families),
        "items": False,
        "x-canonical-order": True,
    }


def _local_overlay_safety_margin_claim_closure() -> dict[str, Any]:
    return {
        "allOf": [
            {"$ref": "#/$defs/claim_closure"},
            {
                "contains": {
                    "type": "object",
                    "properties": {"completeness": {"const": "bounded"}},
                    "required": ["completeness"],
                },
                "minContains": 1,
            },
        ]
    }


def _local_overlay_timestamp() -> dict[str, Any]:
    return {
        "type": "string",
        "format": "date-time",
        "minLength": 20,
        "maxLength": 20,
        "pattern": LOCAL_OVERLAY_UTC_SECOND_PATTERN,
        "x-max-utf8-bytes": 20,
    }


def _local_overlay_catalog_selector(registry: Mapping[str, Any]) -> dict[str, Any]:
    operation_kinds = _registry_values(
        registry, "operation_registry.aliases.all_operations"
    )
    result = _object(
        {
            "backend_channel": _opaque(),
            "base_variant": _opaque(),
            "constraints": {
                "type": "array",
                "minItems": 1,
                "maxItems": len(LOCAL_OVERLAY_CONSTRAINT_KINDS),
                "uniqueItems": True,
                "items": _string_schema(
                    values=LOCAL_OVERLAY_CONSTRAINT_KINDS,
                    max_length=max(map(len, LOCAL_OVERLAY_CONSTRAINT_KINDS)),
                ),
                "x-canonical-order": True,
            },
            "material_profiles": {
                "type": "object",
                "minProperties": 1,
                "maxProperties": 32,
                "propertyNames": _opaque(),
                "additionalProperties": _opaque(),
            },
            "model_type": _opaque(),
            "operation_kind": _string_schema(
                values=operation_kinds, max_length=max(map(len, operation_kinds))
            ),
            "operation_template": _string_schema(
                values=LOCAL_OVERLAY_OPERATION_TEMPLATES,
                max_length=max(map(len, LOCAL_OVERLAY_OPERATION_TEMPLATES)),
            ),
            "platform": _opaque(),
            "recovery": _opaque(),
            "source_support_baseline": _fixed_lower_hex(40),
        },
        (
            "backend_channel",
            "base_variant",
            "constraints",
            "material_profiles",
            "model_type",
            "operation_kind",
            "operation_template",
            "platform",
            "recovery",
            "source_support_baseline",
        ),
    )
    result["allOf"] = [
        {
            "if": {
                "properties": {"operation_template": {"const": template}},
                "required": ["operation_template"],
            },
            "then": {
                "properties": {
                    "operation_kind": _string_schema(
                        values=operations,
                        max_length=max(map(len, operations)),
                    )
                },
                "required": ["operation_kind"],
            },
        }
        for template, operations in LOCAL_OVERLAY_TEMPLATE_OPERATIONS.items()
    ]
    return result


def _local_overlay_selector_identity(registry: Mapping[str, Any]) -> dict[str, Any]:
    return _object(
        {
            "backend_build_sha256": _fixed_lower_hex(64),
            "canonical_model_id": _opaque(),
            "catalog": _local_overlay_catalog_selector(registry),
            "catalog_sha256": _fixed_lower_hex(64),
            "configuration_sha256": _fixed_lower_hex(64),
            "dependency_set_sha256": _fixed_lower_hex(64),
            "device_identity_sha256": _fixed_lower_hex(64),
            "driver_identity_sha256": _fixed_lower_hex(64),
            "model_artifact_sha256": _fixed_lower_hex(64),
            "operation_contract_sha256": _fixed_lower_hex(64),
            "topology_sha256": _fixed_lower_hex(64),
            "workload_sha256": _fixed_lower_hex(64),
        },
        (
            "backend_build_sha256",
            "canonical_model_id",
            "catalog",
            "catalog_sha256",
            "configuration_sha256",
            "dependency_set_sha256",
            "device_identity_sha256",
            "driver_identity_sha256",
            "model_artifact_sha256",
            "operation_contract_sha256",
            "topology_sha256",
            "workload_sha256",
        ),
    )


def _local_overlay_source_generations() -> dict[str, Any]:
    generation = {"type": "integer", "minimum": 1, "maximum": UINT64_MAX}
    fields = (
        "backend",
        "configuration",
        "device",
        "driver",
        "model",
        "topology",
        "workload",
    )
    return _object({field: generation for field in fields}, fields)


def _local_overlay_method_identity(registry: Mapping[str, Any]) -> dict[str, Any]:
    operation_kinds = _registry_values(
        registry, "operation_registry.aliases.all_operations"
    )
    result = _object(
        {
            "architecture_predicate_sha256": {
                "oneOf": [{"type": "null"}, _fixed_lower_hex(64)]
            },
            "calibration_revision_sha256": _fixed_lower_hex(64),
            "method_id": _opaque(),
            "method_revision_sha256": _fixed_lower_hex(64),
            "operation_kind": _string_schema(
                values=operation_kinds, max_length=max(map(len, operation_kinds))
            ),
            "scope": _string_schema(
                values=("architecture_predicate", "deployment_exact"),
                max_length=len("architecture_predicate"),
            ),
        },
        (
            "architecture_predicate_sha256",
            "calibration_revision_sha256",
            "method_id",
            "method_revision_sha256",
            "operation_kind",
            "scope",
        ),
    )
    result["allOf"] = [
        {
            "if": {
                "properties": {"scope": {"const": "architecture_predicate"}},
                "required": ["scope"],
            },
            "then": {
                "properties": {
                    "architecture_predicate_sha256": {"not": {"type": "null"}}
                },
                "required": ["architecture_predicate_sha256"],
            },
        },
        {
            "if": {
                "properties": {"scope": {"const": "deployment_exact"}},
                "required": ["scope"],
            },
            "then": {
                "properties": {"architecture_predicate_sha256": {"type": "null"}},
                "required": ["architecture_predicate_sha256"],
            },
        },
    ]
    return result


def _local_overlay_definitions(registry: Mapping[str, Any]) -> dict[str, Any]:
    return {
        "authority_status": {"const": "active"},
        "claim_closure": _local_overlay_claim_closure(),
        "decision_trace_reference": _fixed_lower_hex(64),
        "deployment_identity": _fixed_lower_hex(64),
        "expiry": {"$ref": "#/$defs/timestamp"},
        "method_identity": _local_overlay_method_identity(registry),
        "object_status": {"const": "qualified"},
        "previous_root_reference": {"oneOf": [{"type": "null"}, _fixed_lower_hex(64)]},
        "root_transition": _string_schema(
            values=("qualification", "rollback"), max_length=len("qualification")
        ),
        "safety_margin_claim_closure": _local_overlay_safety_margin_claim_closure(),
        "schema_version": _object(
            {"major": {"const": 1}, "minor": {"const": 0}},
            ("major", "minor"),
        ),
        "selector_identity": _local_overlay_selector_identity(registry),
        "source_generations": _local_overlay_source_generations(),
        "timestamp": _local_overlay_timestamp(),
    }


def _local_overlay_field_schema(
    name: str,
    spec: Mapping[str, Any],
    registry: Mapping[str, Any],
    catalog: Mapping[str, Any] | None,
) -> dict[str, Any] | None:
    del name, registry, catalog
    if spec["type"] == "local_overlay_positive_uint64":
        return {"type": "integer", "minimum": 1, "maximum": UINT64_MAX}
    if spec["type"] == "local_overlay_nonnegative_uint64":
        return {"type": "integer", "minimum": 0, "maximum": UINT64_MAX}
    if spec["type"] == "local_overlay_confidence_basis_points":
        return {"type": "integer", "minimum": 1, "maximum": 10000}
    if spec["type"] == "local_overlay_required_true":
        return {"const": True}
    if spec["type"] == "local_overlay_profiling_phase":
        values = ("baseline", "workload", "release")
        return _string_schema(values=values, max_length=max(map(len, values)))
    if spec["type"] == "local_overlay_profiling_health":
        return {"const": "valid"}
    if spec["type"] == "local_overlay_profiling_owner_coverage":
        return {"const": "complete"}
    if spec["type"] == "local_overlay_profiling_lifecycle_state":
        values = ("baseline_quiescent", "workload_complete", "release_verified")
        return _string_schema(values=values, max_length=max(map(len, values)))
    if spec["type"] == "local_overlay_zero_claim_closure":
        return {
            "allOf": [
                {"$ref": "#/$defs/claim_closure"},
                {
                    "not": {
                        "contains": {
                            "type": "object",
                            "properties": {"completeness": {"const": "bounded"}},
                            "required": ["completeness"],
                        }
                    }
                },
            ]
        }
    definitions = {
        "local_overlay_authority_status": "authority_status",
        "local_overlay_claim_closure": "claim_closure",
        "local_overlay_decision_trace_reference": "decision_trace_reference",
        "local_overlay_deployment_identity": "deployment_identity",
        "local_overlay_expiry": "expiry",
        "local_overlay_method_identity": "method_identity",
        "local_overlay_object_status": "object_status",
        "local_overlay_previous_root_reference": "previous_root_reference",
        "local_overlay_root_transition": "root_transition",
        "local_overlay_safety_margin_claim_closure": "safety_margin_claim_closure",
        "local_overlay_schema_version": "schema_version",
        "local_overlay_selector_identity": "selector_identity",
        "local_overlay_source_generations": "source_generations",
        "local_overlay_timestamp": "timestamp",
    }
    definition = definitions.get(spec["type"])
    return None if definition is None else {"$ref": f"#/$defs/{definition}"}


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
        _local_overlay_field_schema,
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
    if "assert" in conditional:
        assertion = copy.deepcopy(_mapping(conditional["assert"], f"{label}.assert"))
        keyword = (
            "x-residency-derived-by"
            if "selected_by" in assertion
            else "x-residency-assert"
        )
        parts.append({keyword: assertion})
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
        label = f"schema conditionals[{index}]"
        predicate_modes = set(conditional) & {"if", "unless"}
        if not predicate_modes:
            if set(conditional) != {"assert"}:
                if "assert" in conditional:
                    raise SchemaRenderError(
                        f"{label}.assert cannot include consequences"
                    )
                raise SchemaRenderError(
                    f"{label} must contain exactly one predicate mode"
                )
            assertion = copy.deepcopy(
                _mapping(
                    conditional["assert"],
                    f"{label}.assert",
                )
            )
            keyword = (
                "x-residency-derived-by"
                if "selected_by" in assertion
                else "x-residency-assert"
            )
            result.append({keyword: assertion})
            continue
        if len(predicate_modes) != 1:
            raise SchemaRenderError(f"{label} must contain exactly one predicate mode")
        key = "if" if "if" in conditional else "unless"
        raw_predicate = _mapping(conditional.get(key), f"schema conditional {key}")
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
        if key in LOCAL_OVERLAY_SCHEMA_KEYS:
            document_definitions.update(_local_overlay_definitions(registry))
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
    if field_type == "git_commit_sha1":
        return True, "0" * 40
    if field_type == "sha256":
        return True, "0" * 64
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


def _local_overlay_claim_example() -> list[dict[str, Any]]:
    return [
        {
            "completeness": "bounded",
            "entries": [{"amount": 4096, "constraint_id": "gpu/gtt", "unit": "bytes"}],
            "family": "consumable_capacity",
        },
        {"completeness": "known_zero", "entries": [], "family": "safety_floor"},
        {
            "completeness": "known_zero",
            "entries": [],
            "family": "cardinality_pool",
        },
        {
            "completeness": "not_applicable",
            "entries": [],
            "family": "compatibility_exclusivity",
        },
    ]


def _local_overlay_selector_example() -> dict[str, Any]:
    return {
        "backend_build_sha256": "2" * 64,
        "canonical_model_id": "model/alpha",
        "catalog": {
            "backend_channel": "stable",
            "base_variant": "llamacpp-rocm",
            "constraints": ["gpu_shared_residency"],
            "material_profiles": {
                "configuration_profile": "profile-free-residency-estimation-v1-text-only",
                "hardware_profile": "hatchery-gfx1151-shared-gtt-v1",
                "workload_profile": "hatchery-text-generation-campaign-v1",
            },
            "model_type": "llm",
            "operation_kind": "admission",
            "operation_template": "ADM",
            "platform": "linux-amd-rocm-llamacpp",
            "recovery": "native_subprocess_tree",
            "source_support_baseline": "a" * 40,
        },
        "catalog_sha256": "1" * 64,
        "configuration_sha256": "8" * 64,
        "dependency_set_sha256": "5" * 64,
        "device_identity_sha256": "3" * 64,
        "driver_identity_sha256": "6" * 64,
        "model_artifact_sha256": "9" * 64,
        "operation_contract_sha256": "0" * 64,
        "topology_sha256": "4" * 64,
        "workload_sha256": "7" * 64,
    }


def _local_overlay_field_example(
    name: str,
    spec: Mapping[str, Any],
    registry: Mapping[str, Any],
    reason_rows: list[dict[str, Any]],
) -> tuple[bool, Any]:
    del name, registry, reason_rows
    field_type = spec["type"]
    if field_type == "local_overlay_positive_uint64":
        return True, 1
    if field_type == "local_overlay_nonnegative_uint64":
        return True, 0
    if field_type == "local_overlay_confidence_basis_points":
        return True, 9900
    if field_type == "local_overlay_required_true":
        return True, True
    examples: dict[str, Any] = {
        "local_overlay_authority_status": "active",
        "local_overlay_claim_closure": _local_overlay_claim_example(),
        "local_overlay_decision_trace_reference": "d" * 64,
        "local_overlay_deployment_identity": "b" * 64,
        "local_overlay_expiry": "2026-09-23T10:01:00Z",
        "local_overlay_method_identity": {
            "architecture_predicate_sha256": None,
            "calibration_revision_sha256": "2" * 64,
            "method_id": "method/exact-profile",
            "method_revision_sha256": "1" * 64,
            "operation_kind": "admission",
            "scope": "deployment_exact",
        },
        "local_overlay_object_status": "qualified",
        "local_overlay_previous_root_reference": None,
        "local_overlay_root_transition": "qualification",
        "local_overlay_profiling_health": "valid",
        "local_overlay_profiling_lifecycle_state": "baseline_quiescent",
        "local_overlay_profiling_owner_coverage": "complete",
        "local_overlay_profiling_phase": "baseline",
        "local_overlay_safety_margin_claim_closure": _local_overlay_claim_example(),
        "local_overlay_schema_version": {"major": 1, "minor": 0},
        "local_overlay_selector_identity": _local_overlay_selector_example(),
        "local_overlay_source_generations": {
            "backend": 2,
            "configuration": 6,
            "device": 3,
            "driver": 5,
            "model": 1,
            "topology": 4,
            "workload": 7,
        },
        "local_overlay_timestamp": "2026-01-01T00:00:00Z",
        "local_overlay_zero_claim_closure": [
            {
                "completeness": "known_zero",
                "entries": [],
                "family": "consumable_capacity",
            },
            {
                "completeness": "known_zero",
                "entries": [],
                "family": "safety_floor",
            },
            {
                "completeness": "known_zero",
                "entries": [],
                "family": "cardinality_pool",
            },
            {
                "completeness": "not_applicable",
                "entries": [],
                "family": "compatibility_exclusivity",
            },
        ],
    }
    if field_type not in examples:
        return False, None
    return True, copy.deepcopy(examples[field_type])


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
        _local_overlay_field_example,
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
