"""Render the inactive portable-residency contract from the validated registry."""

from __future__ import annotations

import copy
import hashlib
import json
from collections.abc import Iterable, Mapping
from pathlib import Path
from typing import Any

from residency_inventory.schema import validate_inventory

from .schemas import SCHEMA_KEYS, build_schemas

OUTPUT_PATHS = (
    "docs/api/schemas/residency/artifact_quarantine_record.schema.json",
    "docs/api/schemas/residency/artifact_writer_job_revision.schema.json",
    "docs/api/schemas/residency/artifact_writer_request_result.schema.json",
    "docs/api/schemas/residency/authority_transaction_result.schema.json",
    "docs/api/schemas/residency/coordinator_step_result.schema.json",
    "docs/api/schemas/residency/operation_revision.schema.json",
    "docs/api/schemas/residency/reason.schema.json",
    "docs/api/schemas/residency/request_error.schema.json",
    "docs/api/schemas/residency/residency_profiles.schema.json",
    "docs/api/schemas/residency/resource_diagnostic.schema.json",
    "docs/api/schemas/residency/response_diagnostic.schema.json",
    "docs/api/schemas/residency/staged_import_session_record.schema.json",
    "src/cpp/include/lemon/residency/generated_contract.h",
    "src/cpp/resources/residency_profiles.json",
    "src/cpp/server/residency/generated_contract.cpp",
    "test/residency/contract/generated/catalog.json",
    "test/residency/contract/generated/http_auth.json",
    "test/residency/contract/generated/reasons.json",
    "test/residency/contract/generated/schema_examples.json",
)

CONTRACT_REGISTRY_KEYS = {
    "schema",
    "mode_registry",
    "operation_registry",
    "reason_envelope_registry",
    "request_context_registry",
    "request_stage_registry",
    "reason_registry",
    "presentation_registry",
    "detail_schema_registry",
    "retention_registry",
    "http_auth_registry",
    "schema_registry",
}


class ContractGenerationError(ValueError):
    pass


def _strict_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ContractGenerationError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def load_source(path: Path) -> dict[str, Any]:
    try:
        content = path.read_text(encoding="utf-8")
    except OSError as error:
        raise ContractGenerationError(f"cannot read source: {error}") from error
    try:
        source = json.loads(content, object_pairs_hook=_strict_object)
    except json.JSONDecodeError as error:
        raise ContractGenerationError(f"source JSON is invalid: {error}") from error
    if not isinstance(source, dict):
        raise ContractGenerationError("source root must be an object")
    return source


def _mapping(value: Any, label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ContractGenerationError(f"{label} must be an object")
    return value


def _list(value: Any, label: str) -> list[Any]:
    if not isinstance(value, list):
        raise ContractGenerationError(f"{label} must be an array")
    return value


def _string(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value:
        raise ContractGenerationError(f"{label} must be a nonempty string")
    return value


def _string_list(value: Any, label: str) -> list[str]:
    members = _list(value, label)
    result = [_string(member, f"{label} member") for member in members]
    if len(result) != len(set(result)):
        raise ContractGenerationError(f"{label} contains duplicate values")
    return result


def _require_exact_keys(
    value: Mapping[str, Any], expected: set[str], label: str
) -> None:
    actual = set(value)
    if actual != expected:
        missing = sorted(expected - actual)
        extra = sorted(actual - expected)
        raise ContractGenerationError(
            f"{label} keys differ; missing={missing!r}, extra={extra!r}"
        )


def _validated_projection(source: dict[str, Any]) -> dict[str, Any]:
    if source.get("schema_version") != 7:
        raise ContractGenerationError("source schema_version must be 7")
    repo_root = Path(__file__).resolve().parents[2]
    try:
        projection = validate_inventory(repo_root, source)
    except Exception as error:
        raise ContractGenerationError(str(error)) from error
    return copy.deepcopy(_mapping(projection, "validated inventory projection"))


def _validated_registry(projection: Mapping[str, Any]) -> dict[str, Any]:
    registry = _mapping(projection.get("contract_registry"), "contract_registry")
    registry = _mapping(registry, "validated contract_registry")
    _require_exact_keys(registry, CONTRACT_REGISTRY_KEYS, "validated contract_registry")
    if registry.get("schema") != "residency.explanation/1.0":
        raise ContractGenerationError(
            "contract_registry.schema must be residency.explanation/1.0"
        )
    return copy.deepcopy(registry)


def _fallbacks(source: dict[str, Any]) -> list[dict[str, Any]]:
    registry = _mapping(source.get("fallback_registry"), "fallback_registry")
    if len(registry) != 14:
        raise ContractGenerationError("fallback_registry must contain 14 entries")
    result: list[dict[str, Any]] = []
    for fallback_id in sorted(registry):
        row = copy.deepcopy(
            _mapping(registry[fallback_id], f"fallback_registry.{fallback_id}")
        )
        result.append({"id": _string(fallback_id, "fallback ID"), **row})
    return result


def _unit_fallback_ids(row: Mapping[str, Any], label: str) -> set[str]:
    fallback_map = _mapping(row.get("fallbacks"), f"{label}.fallbacks")
    return {
        _string(fallback_id, f"{label}.fallbacks value")
        for fallback_id in fallback_map.values()
    }


def _promotion_source_rows(
    source: Mapping[str, Any], source_key: str, expected_count: int
) -> list[Any]:
    rows = _list(source.get(source_key), source_key)
    if len(rows) != expected_count:
        raise ContractGenerationError(
            f"{source_key} must contain {expected_count} entries"
        )
    return rows


def _promotion_unit(
    raw_row: Any,
    label: str,
    *,
    id_key: str,
    unit_kind: str,
    state_key: str | None = None,
) -> tuple[dict[str, Any], set[str]]:
    row = copy.deepcopy(_mapping(raw_row, label))
    unit_id = _string(row.get(id_key), f"{label}.{id_key}")
    state_label = f"{label}.{state_key}" if state_key else label
    state = _mapping(row.get(state_key), state_label) if state_key else row
    capability_level = _string(
        state.get("capability_level"), f"{state_label}.capability_level"
    )
    delivery_state = _string(
        state.get("delivery_state"), f"{state_label}.delivery_state"
    )
    return (
        {
            "id": unit_id,
            "unit_kind": unit_kind,
            "capability_level": capability_level,
            "delivery_state": delivery_state,
            "contract": row,
        },
        _unit_fallback_ids(row, label),
    )


def _promotion_units(
    source: dict[str, Any], fallback_ids: set[str]
) -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    referenced_fallbacks: set[str] = set()
    sources = (
        ("exact_cells", 4, "cell_id", "exact_cell", None),
        (
            "compatibility_contracts",
            1,
            "contract_id",
            "compatibility_contract",
            None,
        ),
        ("later_promotion_roster", 34, "unit_id", "later_runtime", "initial_state"),
    )
    for source_key, count, id_key, unit_kind, state_key in sources:
        for index, raw_row in enumerate(
            _promotion_source_rows(source, source_key, count)
        ):
            unit, unit_fallbacks = _promotion_unit(
                raw_row,
                f"{source_key}[{index}]",
                id_key=id_key,
                unit_kind=unit_kind,
                state_key=state_key,
            )
            result.append(unit)
            referenced_fallbacks.update(unit_fallbacks)

    result.sort(key=lambda unit: unit["id"])
    unit_ids = [unit["id"] for unit in result]
    if len(result) != 39 or len(set(unit_ids)) != 39:
        raise ContractGenerationError(
            "promotion-unit IDs must close to 39 unique values"
        )
    if {unit["capability_level"] for unit in result} != {"unsupported"}:
        raise ContractGenerationError(
            "all generated promotion units must be unsupported"
        )
    if {unit["delivery_state"] for unit in result} != {"absent"}:
        raise ContractGenerationError("all generated promotion units must be absent")
    unknown_fallbacks = referenced_fallbacks - fallback_ids
    if unknown_fallbacks:
        raise ContractGenerationError(
            f"promotion units reference unknown fallbacks {sorted(unknown_fallbacks)!r}"
        )
    if referenced_fallbacks != fallback_ids:
        raise ContractGenerationError(
            "generated promotion units do not close the fallback registry"
        )
    return result


def _json_bytes(value: Any) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")


def _registry_mapping(registry: Mapping[str, Any], key: str) -> dict[str, Any]:
    return _mapping(registry.get(key), f"contract_registry.{key}")


def _reason_rows(registry: Mapping[str, Any]) -> list[dict[str, Any]]:
    reason_registry = _registry_mapping(registry, "reason_registry")
    presentations = _registry_mapping(registry, "presentation_registry")
    rows: list[dict[str, Any]] = []
    for code in sorted(reason_registry):
        reason = copy.deepcopy(
            _mapping(reason_registry[code], f"reason_registry.{code}")
        )
        category_id = _string(
            reason.get("category_id"), f"reason_registry.{code}.category_id"
        )
        presentation_id = _string(
            reason.get("presentation_id"),
            f"reason_registry.{code}.presentation_id",
        )
        detail_schema_id = _string(
            reason.get("detail_schema_id"),
            f"reason_registry.{code}.detail_schema_id",
        )
        presentation = copy.deepcopy(
            _mapping(
                presentations.get(presentation_id),
                f"presentation_registry.{presentation_id}",
            )
        )
        rows.append(
            {
                "code": code,
                "category_id": category_id,
                "presentation_id": presentation_id,
                "detail_schema_id": detail_schema_id,
                "presentation": presentation,
                "contract": reason,
            }
        )
    if len(rows) != 87:
        raise ContractGenerationError("reason_registry must contain 87 entries")
    return rows


def _schema_type(schema_key: str, row: Mapping[str, Any]) -> str:
    return _string(row.get("schema_type"), f"schema_registry.{schema_key}.schema_type")


def _schema_rows(registry: Mapping[str, Any]) -> dict[str, dict[str, Any]]:
    rows = _registry_mapping(registry, "schema_registry")
    if set(rows) != set(SCHEMA_KEYS):
        raise ContractGenerationError(
            "schema_registry must contain the exact 12 generated schema keys"
        )
    return {
        key: copy.deepcopy(_mapping(rows[key], f"schema_registry.{key}"))
        for key in SCHEMA_KEYS
    }


def _nonnegative_integer(value: Any, label: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        raise ContractGenerationError(f"{label} must be a nonnegative integer")
    return value


def _explanation_schema_metadata(
    registry: Mapping[str, Any],
    schema_rows: Mapping[str, Mapping[str, Any]],
    schemas: Mapping[str, Mapping[str, Any]],
) -> dict[str, Any]:
    schema_id = _string(registry.get("schema"), "contract_registry.schema")
    operation_schema = _mapping(
        schema_rows.get("operation_revision"),
        "contract_registry.schema_registry.operation_revision",
    )
    version = _mapping(
        operation_schema.get("version"),
        "contract_registry.schema_registry.operation_revision.version",
    )
    _require_exact_keys(version, {"major", "minor"}, "operation revision version")
    major = _nonnegative_integer(version.get("major"), "operation revision major")
    minor = _nonnegative_integer(version.get("minor"), "operation revision minor")
    if not schema_id.endswith(f"/{major}.{minor}"):
        raise ContractGenerationError(
            "explanation schema ID and operation revision version differ"
        )

    operation_fields = _mapping(
        operation_schema.get("fields"),
        "contract_registry.schema_registry.operation_revision.fields",
    )
    schema_field = _mapping(
        operation_fields.get("schema"),
        "contract_registry.schema_registry.operation_revision.fields.schema",
    )
    if schema_field.get("value") != schema_id:
        raise ContractGenerationError(
            "operation revision schema literal and explanation schema differ"
        )

    generated_operation_schema = _mapping(
        schemas.get("operation_revision"), "generated operation revision schema"
    )
    properties = _mapping(
        generated_operation_schema.get("properties"),
        "generated operation revision schema properties",
    )
    reasons = _mapping(
        properties.get("reasons"),
        "generated operation revision reasons schema",
    )
    max_reasons = _nonnegative_integer(
        reasons.get("maxItems"), "generated operation revision reason bound"
    )
    if max_reasons == 0:
        raise ContractGenerationError(
            "generated operation revision reason bound must be positive"
        )
    return {
        "id": schema_id,
        "major": major,
        "minor": minor,
        "max_reasons": max_reasons,
    }


def _operation_retention_policy(registry: Mapping[str, Any]) -> dict[str, Any]:
    retention_registry = _registry_mapping(registry, "retention_registry")
    operation = copy.deepcopy(
        _mapping(retention_registry.get("operation"), "retention_registry.operation")
    )
    expected = {
        "active_expires",
        "recovery_required_expires",
        "terminal_detail_seconds",
        "forgotten_after_terminal_seconds",
    }
    _require_exact_keys(operation, expected, "retention_registry.operation")
    for field in ("active_expires", "recovery_required_expires"):
        if not isinstance(operation.get(field), bool):
            raise ContractGenerationError(
                f"retention_registry.operation.{field} must be a boolean"
            )
    for field in (
        "terminal_detail_seconds",
        "forgotten_after_terminal_seconds",
    ):
        operation[field] = _nonnegative_integer(
            operation.get(field), f"retention_registry.operation.{field}"
        )
    return operation


def _operation_family_rows(registry: Mapping[str, Any]) -> list[dict[str, str]]:
    operation_registry = _registry_mapping(registry, "operation_registry")
    families = _mapping(
        operation_registry.get("families"), "operation_registry.families"
    )
    aliases = _mapping(operation_registry.get("aliases"), "operation_registry.aliases")
    all_operations = _string_list(
        aliases.get("all_operations"), "operation_registry.aliases.all_operations"
    )
    family_by_operation: dict[str, str] = {}
    for family in sorted(families):
        for operation in _string_list(
            families[family], f"operation_registry.families.{family}"
        ):
            if operation in family_by_operation:
                raise ContractGenerationError(
                    f"operation {operation!r} belongs to multiple families"
                )
            family_by_operation[operation] = family
    if set(family_by_operation) != set(all_operations):
        raise ContractGenerationError(
            "operation family mapping does not close all operations"
        )
    return [
        {"operation_kind": operation, "family": family_by_operation[operation]}
        for operation in all_operations
    ]


def _operation_legal_state_row(
    code: str,
    operation_kinds: list[str],
    legal_state: str,
    state_label: str,
    secondary_only: bool,
    known_phases: set[str],
    known_outcomes: set[str],
) -> dict[str, Any]:
    row = {
        "code": code,
        "operation_kinds": "|".join(operation_kinds),
    }
    if legal_state == "associated_primary_state:secondary":
        return {
            **row,
            "phases": "",
            "terminal_outcomes": "",
            "primary": False,
            "secondary": True,
            "associated_primary_state": True,
        }
    state_parts = legal_state.split(":")
    if len(state_parts) != 3:
        raise ContractGenerationError(
            f"{state_label} must have phase, outcome, and position"
        )
    phases = state_parts[0].split("|")
    outcomes = state_parts[1].split("|")
    positions = state_parts[2].split("|")
    if not set(phases).issubset(known_phases):
        raise ContractGenerationError(f"{state_label} has unknown phases")
    if not set(outcomes).issubset(known_outcomes | {"null"}):
        raise ContractGenerationError(f"{state_label} has unknown outcomes")
    if not set(positions).issubset({"primary", "secondary"}):
        raise ContractGenerationError(f"{state_label} has unknown positions")
    primary = "primary" in positions
    if secondary_only and primary:
        raise ContractGenerationError(
            f"{state_label} gives a secondary-only reason primary authority"
        )
    return {
        **row,
        "phases": "|".join(phases),
        "terminal_outcomes": "|".join(outcomes),
        "primary": primary,
        "secondary": "secondary" in positions,
        "associated_primary_state": False,
    }


def _operation_reason_context_rows(
    code: str,
    envelope: Mapping[str, Any],
    secondary_only: bool,
    known_operations: set[str],
    known_phases: set[str],
    known_outcomes: set[str],
) -> list[dict[str, Any]]:
    contexts: list[dict[str, Any]] = []
    raw_contexts = _list(
        envelope.get("contexts"),
        f"reason_registry.{code}.envelopes.operation_revision.contexts",
    )
    for context_index, raw_context in enumerate(raw_contexts):
        context_label = (
            f"reason_registry.{code}.envelopes.operation_revision."
            f"contexts[{context_index}]"
        )
        context = _mapping(raw_context, context_label)
        operation_kinds = _string_list(
            context.get("operation_kinds"), f"{context_label}.operation_kinds"
        )
        if not operation_kinds or not set(operation_kinds).issubset(known_operations):
            raise ContractGenerationError(
                f"{context_label}.operation_kinds is not a nonempty known subset"
            )
        legal_states = _string_list(
            context.get("legal_states"), f"{context_label}.legal_states"
        )
        if not legal_states:
            raise ContractGenerationError(
                f"{context_label}.legal_states must not be empty"
            )
        contexts.extend(
            _operation_legal_state_row(
                code,
                operation_kinds,
                legal_state,
                f"{context_label}.legal_states[{legal_state!r}]",
                secondary_only,
                known_phases,
                known_outcomes,
            )
            for legal_state in legal_states
        )
    return contexts


def _operation_reason_rows(
    registry: Mapping[str, Any],
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    envelope_registry = _registry_mapping(registry, "reason_envelope_registry")
    operation_envelope = _mapping(
        envelope_registry.get("operation_revision"),
        "reason_envelope_registry.operation_revision",
    )
    operation_reason_codes = _string_list(
        operation_envelope.get("reason_codes"),
        "reason_envelope_registry.operation_revision.reason_codes",
    )
    reason_registry = _registry_mapping(registry, "reason_registry")
    operation_registry = _registry_mapping(registry, "operation_registry")
    aliases = _mapping(operation_registry.get("aliases"), "operation_registry.aliases")
    known_operations = set(
        _string_list(
            aliases.get("all_operations"),
            "operation_registry.aliases.all_operations",
        )
    )
    known_phases = set(
        _string_list(operation_registry.get("phases"), "operation_registry.phases")
    )
    known_outcomes = set(
        _string_list(
            operation_registry.get("terminal_outcomes"),
            "operation_registry.terminal_outcomes",
        )
    )

    rules: list[dict[str, Any]] = []
    contexts: list[dict[str, Any]] = []
    for canonical_rank, code in enumerate(operation_reason_codes):
        reason = _mapping(reason_registry.get(code), f"reason_registry.{code}")
        envelopes = _mapping(
            reason.get("envelopes"), f"reason_registry.{code}.envelopes"
        )
        envelope = _mapping(
            envelopes.get("operation_revision"),
            f"reason_registry.{code}.envelopes.operation_revision",
        )
        priority_band = _nonnegative_integer(
            envelope.get("priority_band"),
            f"reason_registry.{code}.envelopes.operation_revision.priority_band",
        )
        secondary_only = envelope.get("secondary_only", False)
        if not isinstance(secondary_only, bool):
            raise ContractGenerationError(
                f"reason_registry.{code}.envelopes.operation_revision.secondary_only "
                "must be a boolean"
            )
        rules.append(
            {
                "code": code,
                "canonical_rank": canonical_rank,
                "priority_band": priority_band,
                "secondary_only": secondary_only,
            }
        )
        contexts.extend(
            _operation_reason_context_rows(
                code,
                envelope,
                secondary_only,
                known_operations,
                known_phases,
                known_outcomes,
            )
        )
    return rules, contexts


def _presentation_rows(registry: Mapping[str, Any]) -> list[dict[str, str]]:
    presentations = _registry_mapping(registry, "presentation_registry")
    rows: list[dict[str, str]] = []
    for presentation_id in sorted(presentations):
        presentation = _mapping(
            presentations[presentation_id],
            f"presentation_registry.{presentation_id}",
        )
        rows.append(
            {
                "id": presentation_id,
                "category_id": _string(
                    presentation.get("category_id"),
                    f"presentation_registry.{presentation_id}.category_id",
                ),
                "severity": _string(
                    presentation.get("severity"),
                    f"presentation_registry.{presentation_id}.severity",
                ),
                "title": _string(
                    presentation.get("title"),
                    f"presentation_registry.{presentation_id}.title",
                ),
                "default_message": _string(
                    presentation.get("default_message"),
                    f"presentation_registry.{presentation_id}.default_message",
                ),
            }
        )
    return rows


def _cpp_string(value: str) -> str:
    replacements = {
        "\\": "\\\\",
        '"': '\\"',
        "\n": "\\n",
        "\r": "\\r",
        "\t": "\\t",
    }
    literals: list[str] = []
    ascii_run: list[str] = []
    for character in value:
        if ord(character) < 0x80:
            ascii_run.append(replacements.get(character, character))
            continue
        if ascii_run:
            literals.append('"' + "".join(ascii_run) + '"')
            ascii_run.clear()
        literals.extend(f'"\\x{byte:02x}"' for byte in character.encode("utf-8"))
    if ascii_run or not literals:
        literals.append('"' + "".join(ascii_run) + '"')
    return "".join(literals)


def _cpp_array(values: Iterable[str], indent: str = "    ") -> str:
    return "\n".join(f"{indent}{_cpp_string(value)}," for value in values)


def _render_cpp_header(
    packaged_catalog_sha256: str,
    explanation_schema: Mapping[str, Any],
    operation_retention: Mapping[str, Any],
) -> bytes:
    return (
        b"#pragma once\n"
        b"\n"
        b'#include "lemon/residency/types.h"\n'
        b"\n"
        b"#include <cstddef>\n"
        b"#include <cstdint>\n"
        b"#include <optional>\n"
        b"#include <string_view>\n"
        b"\n"
        b"namespace lemon::residency {\n"
        b"\n"
        + f"inline constexpr std::string_view packaged_catalog_sha256 = {_cpp_string(packaged_catalog_sha256)};\n".encode()
        + f"inline constexpr std::string_view explanation_schema_id = {_cpp_string(_string(explanation_schema.get('id'), 'explanation schema ID'))};\n".encode()
        + "inline constexpr SchemaVersion explanation_schema_version{{{}, {}}};\n".format(
            _nonnegative_integer(
                explanation_schema.get("major"), "explanation schema major"
            ),
            _nonnegative_integer(
                explanation_schema.get("minor"), "explanation schema minor"
            ),
        ).encode()
        + "inline constexpr std::size_t max_explanation_reasons = {};\n".format(
            _nonnegative_integer(
                explanation_schema.get("max_reasons"),
                "maximum explanation reasons",
            )
        ).encode()
        + b"\n"
        + b"struct OperationRetentionPolicy {\n"
        b"    bool active_expires;\n"
        b"    bool recovery_required_expires;\n"
        b"    std::uint64_t terminal_detail_seconds;\n"
        b"    std::uint64_t forgotten_after_terminal_seconds;\n"
        b"};\n"
        b"\n"
        + "inline constexpr OperationRetentionPolicy operation_retention_policy{{{}, {}, {}, {}}};\n".format(
            str(bool(operation_retention.get("active_expires"))).lower(),
            str(bool(operation_retention.get("recovery_required_expires"))).lower(),
            _nonnegative_integer(
                operation_retention.get("terminal_detail_seconds"),
                "terminal detail retention",
            ),
            _nonnegative_integer(
                operation_retention.get("forgotten_after_terminal_seconds"),
                "terminal forgotten retention",
            ),
        ).encode()
        + b"\n"
        + b"struct ReasonMetadata {\n"
        b"    std::string_view code;\n"
        b"    std::string_view category_id;\n"
        b"    std::string_view presentation_id;\n"
        b"    std::string_view detail_schema_id;\n"
        b"    std::string_view severity;\n"
        b"    std::string_view title;\n"
        b"    std::string_view default_message;\n"
        b"};\n"
        b"\n"
        b"struct ReasonPresentationMetadata {\n"
        b"    std::string_view id;\n"
        b"    std::string_view category_id;\n"
        b"    std::string_view severity;\n"
        b"    std::string_view title;\n"
        b"    std::string_view default_message;\n"
        b"};\n"
        b"\n"
        b"struct OperationReasonRuleMetadata {\n"
        b"    std::string_view code;\n"
        b"    std::size_t canonical_rank;\n"
        b"    std::uint32_t priority_band;\n"
        b"    bool secondary_only;\n"
        b"};\n"
        b"\n"
        b"class GeneratedContractRegistry {\n"
        b"public:\n"
        b"    static DecodedValue<PromotionUnitId> decode_promotion_unit_id(std::string_view wire);\n"
        b"    static PromotionUnitKind promotion_unit_kind(const PromotionUnitId& id) noexcept;\n"
        b"    static ReasonCode decode_reason_code(std::string_view wire);\n"
        b"    static DecodedValue<FallbackId> decode_fallback_id(std::string_view wire);\n"
        b"    static DecodedValue<SchemaType> decode_schema_type(std::string_view wire);\n"
        b"    static const ReasonMetadata* reason_metadata(std::string_view code) noexcept;\n"
        b"};\n"
        b"\n"
        b"DecodedValue<PromotionUnitId> decode_promotion_unit_id(std::string_view wire);\n"
        b"PromotionUnitKind promotion_unit_kind(const PromotionUnitId& id) noexcept;\n"
        b"ReasonCode decode_reason_code(std::string_view wire);\n"
        b"DecodedValue<FallbackId> decode_fallback_id(std::string_view wire);\n"
        b"DecodedValue<SchemaType> decode_schema_type(std::string_view wire);\n"
        b"const ReasonMetadata* reason_metadata(std::string_view code) noexcept;\n"
        b"const ReasonMetadata* reason_metadata(const KnownReasonCode& code) noexcept;\n"
        b"const ReasonMetadata* reason_metadata(const ReasonCode& code) noexcept;\n"
        b"std::optional<OperationFamily> operation_family(OperationKind kind) noexcept;\n"
        b"const OperationReasonRuleMetadata* operation_reason_rule_metadata(std::string_view code) noexcept;\n"
        b"bool operation_reason_is_legal(std::string_view code, OperationKind kind, OperationPhase phase, std::optional<TerminalOutcome> terminal_outcome, bool secondary) noexcept;\n"
        b"const ReasonPresentationMetadata* reason_presentation_metadata(std::string_view presentation_id) noexcept;\n"
        b"bool reason_category_is_known(std::string_view category_id) noexcept;\n"
        b"const ReasonPresentationMetadata* unique_reason_presentation_for_category(std::string_view category_id) noexcept;\n"
        b"const ReasonPresentationMetadata* matching_reason_presentation_for_category(std::string_view category_id, std::string_view presentation_id) noexcept;\n"
        b"\n"
        b"}\n"
    )


def _render_cpp_source(
    promotion_units: list[dict[str, Any]],
    fallback_ids: list[str],
    schema_types: list[str],
    reason_rows: list[dict[str, Any]],
    operation_family_rows: list[dict[str, str]],
    operation_reason_rows: list[dict[str, Any]],
    operation_reason_context_rows: list[dict[str, Any]],
    presentation_rows: list[dict[str, str]],
) -> bytes:
    cpp_promotion_unit_kinds = {
        "exact_cell": "PromotionUnitKind::ExactCell",
        "compatibility_contract": "PromotionUnitKind::CompatibilityContract",
        "later_runtime": "PromotionUnitKind::LaterRuntime",
    }
    promotion_unit_rows = "\n".join(
        "    PromotionUnitMetadata{{{}, {}}},".format(
            _cpp_string(_string(unit.get("id"), "promotion unit ID")),
            cpp_promotion_unit_kinds[
                _string(unit.get("unit_kind"), "promotion unit kind")
            ],
        )
        for unit in promotion_units
    )
    metadata_rows = "\n".join(
        "    ReasonMetadata{{{}}},".format(
            ", ".join(
                _cpp_string(value)
                for value in (
                    row["code"],
                    row["category_id"],
                    row["presentation_id"],
                    row["detail_schema_id"],
                    _string(
                        row["presentation"].get("severity"),
                        f"presentation_registry.{row['presentation_id']}.severity",
                    ),
                    _string(
                        row["presentation"].get("title"),
                        f"presentation_registry.{row['presentation_id']}.title",
                    ),
                    _string(
                        row["presentation"].get("default_message"),
                        f"presentation_registry.{row['presentation_id']}.default_message",
                    ),
                )
            )
        )
        for row in reason_rows
    )
    operation_family_metadata_rows = "\n".join(
        "    OperationFamilyMetadata{{{}, {}}},".format(
            _cpp_string(row["operation_kind"]), _cpp_string(row["family"])
        )
        for row in operation_family_rows
    )
    operation_reason_metadata_rows = "\n".join(
        "    OperationReasonRuleMetadata{{{}, {}, {}, {}}},".format(
            _cpp_string(row["code"]),
            row["canonical_rank"],
            row["priority_band"],
            str(row["secondary_only"]).lower(),
        )
        for row in operation_reason_rows
    )
    operation_reason_context_metadata_rows = "\n".join(
        "    OperationReasonContextMetadata{{{}, {}, {}, {}, {}, {}, {}}},".format(
            _cpp_string(row["code"]),
            _cpp_string(row["operation_kinds"]),
            _cpp_string(row["phases"]),
            _cpp_string(row["terminal_outcomes"]),
            str(row["primary"]).lower(),
            str(row["secondary"]).lower(),
            str(row["associated_primary_state"]).lower(),
        )
        for row in operation_reason_context_rows
    )
    presentation_metadata_rows = "\n".join(
        "    ReasonPresentationMetadata{{{}}},".format(
            ", ".join(
                _cpp_string(row[field])
                for field in (
                    "id",
                    "category_id",
                    "severity",
                    "title",
                    "default_message",
                )
            )
        )
        for row in presentation_rows
    )
    source = f"""#include "lemon/residency/generated_contract.h"

#include <array>
#include <string>

namespace lemon::residency {{
namespace {{

struct PromotionUnitMetadata {{
    std::string_view id;
    PromotionUnitKind kind;
}};

struct OperationFamilyMetadata {{
    std::string_view operation_kind;
    std::string_view family;
}};

struct OperationReasonContextMetadata {{
    std::string_view code;
    std::string_view operation_kinds;
    std::string_view phases;
    std::string_view terminal_outcomes;
    bool primary;
    bool secondary;
    bool associated_primary_state;
}};

constexpr std::array<PromotionUnitMetadata, {len(promotion_units)}> promotion_units{{{{
{promotion_unit_rows}
}}}};

constexpr std::array<std::string_view, {len(fallback_ids)}> fallback_ids{{{{
{_cpp_array(fallback_ids)}
}}}};

constexpr std::array<std::string_view, {len(schema_types)}> schema_types{{{{
{_cpp_array(schema_types)}
}}}};

constexpr std::array<ReasonMetadata, {len(reason_rows)}> reasons{{{{
{metadata_rows}
}}}};

constexpr std::array<ReasonPresentationMetadata, {len(presentation_rows)}> presentations{{{{
{presentation_metadata_rows}
}}}};

constexpr std::array<OperationFamilyMetadata, {len(operation_family_rows)}> operation_families{{{{
{operation_family_metadata_rows}
}}}};

constexpr std::array<OperationReasonRuleMetadata, {len(operation_reason_rows)}> operation_reason_rules{{{{
{operation_reason_metadata_rows}
}}}};

constexpr std::array<OperationReasonContextMetadata, {len(operation_reason_context_rows)}> operation_reason_contexts{{{{
{operation_reason_context_metadata_rows}
}}}};

bool token_list_contains(std::string_view list, std::string_view token) noexcept {{
    if (token.empty()) {{
        return false;
    }}
    std::size_t start = 0;
    while (start <= list.size()) {{
        const auto end = list.find('|', start);
        const auto count = end == std::string_view::npos ? list.size() - start : end - start;
        if (list.substr(start, count) == token) {{
            return true;
        }}
        if (end == std::string_view::npos) {{
            return false;
        }}
        start = end + 1;
    }}
    return false;
}}

}}

DecodedValue<PromotionUnitId>
GeneratedContractRegistry::decode_promotion_unit_id(std::string_view wire) {{
    for (const auto& metadata : promotion_units) {{
        if (metadata.id == wire) {{
            return DecodedValue<PromotionUnitId>::known(PromotionUnitId(std::string(wire)));
        }}
    }}
    return DecodedValue<PromotionUnitId>::unknown(wire);
}}

PromotionUnitKind
GeneratedContractRegistry::promotion_unit_kind(const PromotionUnitId& id) noexcept {{
    for (const auto& metadata : promotion_units) {{
        if (metadata.id == id.token()) {{
            return metadata.kind;
        }}
    }}
    std::terminate();
}}

ReasonCode GeneratedContractRegistry::decode_reason_code(std::string_view wire) {{
    for (const auto& reason : reasons) {{
        if (reason.code == wire) {{
            return ReasonCode::known(KnownReasonCode(std::string(wire)));
        }}
    }}
    return ReasonCode::unknown(wire);
}}

DecodedValue<FallbackId>
GeneratedContractRegistry::decode_fallback_id(std::string_view wire) {{
    for (const auto value : fallback_ids) {{
        if (value == wire) {{
            return DecodedValue<FallbackId>::known(FallbackId(std::string(wire)));
        }}
    }}
    return DecodedValue<FallbackId>::unknown(wire);
}}

DecodedValue<SchemaType>
GeneratedContractRegistry::decode_schema_type(std::string_view wire) {{
    for (const auto value : schema_types) {{
        if (value == wire) {{
            return DecodedValue<SchemaType>::known(SchemaType(std::string(wire)));
        }}
    }}
    return DecodedValue<SchemaType>::unknown(wire);
}}

const ReasonMetadata*
GeneratedContractRegistry::reason_metadata(std::string_view code) noexcept {{
    for (const auto& reason : reasons) {{
        if (reason.code == code) {{
            return &reason;
        }}
    }}
    return nullptr;
}}

DecodedValue<PromotionUnitId> decode_promotion_unit_id(std::string_view wire) {{
    return GeneratedContractRegistry::decode_promotion_unit_id(wire);
}}

PromotionUnitKind promotion_unit_kind(const PromotionUnitId& id) noexcept {{
    return GeneratedContractRegistry::promotion_unit_kind(id);
}}

ReasonCode decode_reason_code(std::string_view wire) {{
    return GeneratedContractRegistry::decode_reason_code(wire);
}}

DecodedValue<FallbackId> decode_fallback_id(std::string_view wire) {{
    return GeneratedContractRegistry::decode_fallback_id(wire);
}}

DecodedValue<SchemaType> decode_schema_type(std::string_view wire) {{
    return GeneratedContractRegistry::decode_schema_type(wire);
}}

const ReasonMetadata* reason_metadata(std::string_view code) noexcept {{
    return GeneratedContractRegistry::reason_metadata(code);
}}

const ReasonMetadata* reason_metadata(const KnownReasonCode& code) noexcept {{
    return GeneratedContractRegistry::reason_metadata(code.token());
}}

const ReasonMetadata* reason_metadata(const ReasonCode& code) noexcept {{
    const auto* known = code.known_value();
    return known == nullptr ? nullptr : reason_metadata(*known);
}}

std::optional<OperationFamily> operation_family(OperationKind kind) noexcept {{
    const auto operation_kind = wire_name(kind);
    for (const auto& metadata : operation_families) {{
        if (metadata.operation_kind == operation_kind) {{
            const auto family = decode_operation_family(metadata.family);
            const auto* known = family.known_value();
            if (known != nullptr) {{
                return *known;
            }}
        }}
    }}
    return std::nullopt;
}}

const OperationReasonRuleMetadata*
operation_reason_rule_metadata(std::string_view code) noexcept {{
    for (const auto& metadata : operation_reason_rules) {{
        if (metadata.code == code) {{
            return &metadata;
        }}
    }}
    return nullptr;
}}

bool operation_reason_is_legal(
    std::string_view code, OperationKind kind, OperationPhase phase,
    std::optional<TerminalOutcome> terminal_outcome, bool secondary) noexcept {{
    const auto* rule = operation_reason_rule_metadata(code);
    if (rule == nullptr || (rule->secondary_only && !secondary)) {{
        return false;
    }}
    const auto operation_kind = wire_name(kind);
    const auto operation_phase = wire_name(phase);
    if (operation_kind.empty() || operation_phase.empty()) {{
        return false;
    }}
    if (terminal_outcome.has_value() && wire_name(*terminal_outcome).empty()) {{
        return false;
    }}
    const auto family = operation_family(kind);
    if (!family.has_value() ||
        !operation_state_is_valid(*family, phase, terminal_outcome)) {{
        return false;
    }}
    const auto outcome = terminal_outcome.has_value()
                             ? wire_name(*terminal_outcome)
                             : std::string_view{{"null"}};
    for (const auto& context : operation_reason_contexts) {{
        if (context.code != code ||
            !token_list_contains(context.operation_kinds, operation_kind) ||
            (secondary ? !context.secondary : !context.primary)) {{
            continue;
        }}
        if (context.associated_primary_state ||
            (token_list_contains(context.phases, operation_phase) &&
             token_list_contains(context.terminal_outcomes, outcome))) {{
            return true;
        }}
    }}
    return false;
}}

const ReasonPresentationMetadata*
reason_presentation_metadata(std::string_view presentation_id) noexcept {{
    for (const auto& presentation : presentations) {{
        if (presentation.id == presentation_id) {{
            return &presentation;
        }}
    }}
    return nullptr;
}}

bool reason_category_is_known(std::string_view category_id) noexcept {{
    for (const auto& presentation : presentations) {{
        if (presentation.category_id == category_id) {{
            return true;
        }}
    }}
    return false;
}}

const ReasonPresentationMetadata*
unique_reason_presentation_for_category(std::string_view category_id) noexcept {{
    const ReasonPresentationMetadata* match = nullptr;
    for (const auto& presentation : presentations) {{
        if (presentation.category_id != category_id) {{
            continue;
        }}
        if (match != nullptr) {{
            return nullptr;
        }}
        match = &presentation;
    }}
    return match;
}}

const ReasonPresentationMetadata* matching_reason_presentation_for_category(
    std::string_view category_id, std::string_view presentation_id) noexcept {{
    const auto* presentation = reason_presentation_metadata(presentation_id);
    return presentation != nullptr && presentation->category_id == category_id
               ? presentation
               : nullptr;
}}

}}
"""
    return source.encode("utf-8")


def generate_outputs(source_path: Path) -> dict[str, bytes]:
    source = load_source(source_path)
    projection = _validated_projection(source)
    registry = _validated_registry(projection)
    fallbacks = _fallbacks(projection)
    fallback_ids = {fallback["id"] for fallback in fallbacks}
    promotion_units = _promotion_units(projection, fallback_ids)
    reasons = _reason_rows(registry)
    schema_rows = _schema_rows(registry)
    operation_family_rows = _operation_family_rows(registry)
    operation_reason_rows, operation_reason_context_rows = _operation_reason_rows(
        registry
    )
    presentation_rows = _presentation_rows(registry)
    operation_retention = _operation_retention_policy(registry)
    catalog_schema = _schema_type(
        "residency_profiles", schema_rows["residency_profiles"]
    )
    if catalog_schema != "residency.profiles/1.0":
        raise ContractGenerationError(
            "residency_profiles schema type must be residency.profiles/1.0"
        )
    profile_fields = _mapping(
        schema_rows["residency_profiles"].get("fields"),
        "schema_registry.residency_profiles.fields",
    )
    profile_schema_field = _mapping(
        profile_fields.get("schema"),
        "schema_registry.residency_profiles.fields.schema",
    )
    if profile_schema_field.get("value") != catalog_schema:
        raise ContractGenerationError(
            "residency_profiles schema literal and schema type differ"
        )
    generator_version_field = _mapping(
        profile_fields.get("generator_version"),
        "schema_registry.residency_profiles.fields.generator_version",
    )
    generator_version = generator_version_field.get("value")
    if not isinstance(generator_version, int) or isinstance(generator_version, bool):
        raise ContractGenerationError(
            "residency_profiles generator version must be an integer"
        )
    source_support_baseline_field = _mapping(
        profile_fields.get("source_support_baseline"),
        "schema_registry.residency_profiles.fields.source_support_baseline",
    )
    if source_support_baseline_field != {
        "type": "git_commit_sha1",
        "required": True,
    }:
        raise ContractGenerationError(
            "residency_profiles source-support baseline field drifted"
        )
    selection_registry_sha256_field = _mapping(
        profile_fields.get("selection_registry_sha256"),
        "schema_registry.residency_profiles.fields.selection_registry_sha256",
    )
    if selection_registry_sha256_field != {
        "type": "sha256",
        "required": True,
    }:
        raise ContractGenerationError(
            "residency_profiles selection-registry digest field drifted"
        )
    source_support_baseline = _string(
        projection.get("source_support_baseline"), "source_support_baseline"
    )
    selection_registry_sha256 = hashlib.sha256(_json_bytes(projection)).hexdigest()
    catalog = {
        "schema": catalog_schema,
        "generator_version": generator_version,
        "source_support_baseline": source_support_baseline,
        "selection_registry_sha256": selection_registry_sha256,
        "promotion_units": promotion_units,
        "fallbacks": fallbacks,
        "contract_registry": registry,
    }
    catalog_bytes = _json_bytes(catalog)
    schemas, schema_examples = build_schemas(registry, reasons, catalog)
    explanation_schema = _explanation_schema_metadata(registry, schema_rows, schemas)
    http_auth = {
        "schema": registry["schema"],
        "request_context_registry": registry["request_context_registry"],
        "request_stage_registry": registry["request_stage_registry"],
        "reason_envelope_registry": registry["reason_envelope_registry"],
        "http_auth_registry": registry["http_auth_registry"],
    }
    reason_golden = {
        "schema": registry["schema"],
        "reasons": reasons,
    }
    schema_example_golden = {
        "schema": registry["schema"],
        "examples": schema_examples,
    }

    outputs = {
        "src/cpp/resources/residency_profiles.json": catalog_bytes,
        "src/cpp/include/lemon/residency/generated_contract.h": _render_cpp_header(
            hashlib.sha256(catalog_bytes).hexdigest(),
            explanation_schema,
            operation_retention,
        ),
        "src/cpp/server/residency/generated_contract.cpp": _render_cpp_source(
            promotion_units,
            sorted(fallback_ids),
            [_schema_type(key, schema_rows[key]) for key in SCHEMA_KEYS],
            reasons,
            operation_family_rows,
            operation_reason_rows,
            operation_reason_context_rows,
            presentation_rows,
        ),
        "test/residency/contract/generated/catalog.json": catalog_bytes,
        "test/residency/contract/generated/reasons.json": _json_bytes(reason_golden),
        "test/residency/contract/generated/http_auth.json": _json_bytes(http_auth),
        "test/residency/contract/generated/schema_examples.json": _json_bytes(
            schema_example_golden
        ),
    }
    for key in SCHEMA_KEYS:
        outputs[f"docs/api/schemas/residency/{key}.schema.json"] = _json_bytes(
            schemas[key]
        )
    if set(outputs) != set(OUTPUT_PATHS):
        raise ContractGenerationError("internal generated path set is incomplete")
    return {path: outputs[path] for path in sorted(outputs)}
