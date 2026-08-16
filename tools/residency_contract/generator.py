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


def _cpp_string(value: str) -> str:
    replacements = {
        "\\": "\\\\",
        '"': '\\"',
        "\n": "\\n",
        "\r": "\\r",
        "\t": "\\t",
    }
    return (
        '"'
        + "".join(replacements.get(character, character) for character in value)
        + '"'
    )


def _cpp_array(values: Iterable[str], indent: str = "    ") -> str:
    return "\n".join(f"{indent}{_cpp_string(value)}," for value in values)


def _render_cpp_header(packaged_catalog_sha256: str) -> bytes:
    return (
        b"#pragma once\n"
        b"\n"
        b'#include "lemon/residency/types.h"\n'
        b"\n"
        b"#include <string_view>\n"
        b"\n"
        b"namespace lemon::residency {\n"
        b"\n"
        + f"inline constexpr std::string_view kPackagedCatalogSha256 = {_cpp_string(packaged_catalog_sha256)};\n".encode()
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
        b"\n"
        b"}\n"
    )


def _render_cpp_source(
    promotion_units: list[dict[str, Any]],
    fallback_ids: list[str],
    schema_types: list[str],
    reason_rows: list[dict[str, Any]],
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
    source = f"""#include "lemon/residency/generated_contract.h"

#include <array>
#include <exception>
#include <string>

namespace lemon::residency {{
namespace {{

struct PromotionUnitMetadata {{
    std::string_view id;
    PromotionUnitKind kind;
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
            hashlib.sha256(catalog_bytes).hexdigest()
        ),
        "src/cpp/server/residency/generated_contract.cpp": _render_cpp_source(
            promotion_units,
            sorted(fallback_ids),
            [_schema_type(key, schema_rows[key]) for key in SCHEMA_KEYS],
            reasons,
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
