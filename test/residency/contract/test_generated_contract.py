import hashlib
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from importlib import import_module
from pathlib import Path
from typing import Any

import jsonschema
from referencing import Registry, Resource

REPO_ROOT = Path(__file__).resolve().parents[3]
GENERATOR = REPO_ROOT / "tools/generate_residency_contract.py"
SOURCE = REPO_ROOT / "docs/research/portable-residency-capability-inventory.json"

GENERATED_PATHS = {
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
}


def strict_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def load_json(content: bytes) -> Any:
    return json.loads(content.decode("utf-8"), object_pairs_hook=strict_object)


def collect(root: Path) -> dict[str, bytes]:
    return {
        path.relative_to(root).as_posix(): path.read_bytes()
        for path in sorted(path for path in root.rglob("*") if path.is_file())
    }


def run_generator(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(GENERATOR), *arguments],
        cwd=REPO_ROOT,
        check=False,
        text=True,
        capture_output=True,
    )


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def require_canonical_bytes(path: str, content: bytes) -> None:
    require(not content.startswith(b"\xef\xbb\xbf"), f"{path} has a UTF-8 BOM")
    require(b"\r" not in content, f"{path} is not LF-normalized")
    require(content.endswith(b"\n"), f"{path} lacks a terminal newline")
    require(not content.endswith(b"\n\n"), f"{path} has multiple terminal newlines")
    content.decode("utf-8")


def require_catalog_contract(files: dict[str, bytes]) -> None:
    catalog = load_json(files["src/cpp/resources/residency_profiles.json"])
    require(catalog["schema"] == "residency.profiles/1.0", "catalog schema drifted")
    source = load_json(SOURCE.read_bytes())
    require(
        catalog["source_support_baseline"] == source["source_support_baseline"],
        "catalog source-support baseline drifted",
    )
    tools_root = str(REPO_ROOT / "tools")
    if tools_root not in sys.path:
        sys.path.insert(0, tools_root)
    validate_inventory = import_module("residency_inventory.schema").validate_inventory
    projection = validate_inventory(REPO_ROOT, source)
    projection_bytes = (
        json.dumps(projection, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")
    require(
        catalog["selection_registry_sha256"]
        == hashlib.sha256(projection_bytes).hexdigest(),
        "catalog selection-registry digest drifted",
    )

    units = catalog["promotion_units"]
    require(len(units) == 39, "catalog promotion-unit count drifted")
    require(
        len({unit["id"] for unit in units}) == 39, "promotion-unit IDs are not unique"
    )
    require(
        {unit["capability_level"] for unit in units} == {"unsupported"},
        "generated catalog raised capability",
    )
    require(
        {unit["delivery_state"] for unit in units} == {"absent"},
        "generated catalog raised delivery",
    )
    kind_counts = {
        kind: sum(unit["unit_kind"] == kind for unit in units)
        for kind in ("exact_cell", "compatibility_contract", "later_runtime")
    }
    require(
        kind_counts
        == {"exact_cell": 4, "compatibility_contract": 1, "later_runtime": 34},
        "promotion-unit kind counts drifted",
    )

    registry = catalog["contract_registry"]
    expected_counts = {
        "detail_schema_registry": 15,
        "presentation_registry": 27,
        "reason_envelope_registry": 8,
        "reason_registry": 87,
        "request_context_registry": 37,
        "schema_registry": 12,
    }
    for key, expected in expected_counts.items():
        require(len(registry[key]) == expected, f"{key} count drifted")

    aliases = registry["operation_registry"]["aliases"]
    expected_alias_sizes = {
        "all_operations": 14,
        "all_resource_operations": 10,
        "recovery_capable_resource_operations": 9,
        "recovery_capable_operations": 13,
        "terminally_quarantinable_resource_operations": 9,
        "all_resident_state_operations": 4,
        "planned_resource_operations": 5,
        "capacity_consuming_operations": 2,
        "resource_action_operations": 10,
        "automatic_protected_operations": 3,
        "live_use_protected_operations": 6,
        "readiness_dependent_operations": 3,
    }
    require(
        {alias: len(members) for alias, members in aliases.items()}
        == expected_alias_sizes,
        "operation alias closure drifted",
    )

    envelope_memberships = {
        envelope: len(definition["reason_codes"])
        for envelope, definition in registry["reason_envelope_registry"].items()
    }
    require(
        envelope_memberships
        == {
            "operation_revision": 34,
            "request_error": 39,
            "resource_diagnostic": 9,
            "authority_transaction_result": 2,
            "coordinator_step_result": 10,
            "response_diagnostic": 2,
            "artifact_writer_job_revision": 6,
            "artifact_writer_request_result": 6,
        },
        "reason envelope closure drifted",
    )

    require(len(catalog["fallbacks"]) == 14, "fallback registry count drifted")

    catalog_digest = hashlib.sha256(
        files["src/cpp/resources/residency_profiles.json"]
    ).hexdigest()
    header = files["src/cpp/include/lemon/residency/generated_contract.h"].decode()
    generated_source = files["src/cpp/server/residency/generated_contract.cpp"].decode()
    for symbol in (
        "class GeneratedContractRegistry",
        "decode_promotion_unit_id",
        "decode_reason_code",
        "decode_fallback_id",
        "decode_schema_type",
        "promotion_unit_kind",
        "reason_metadata",
        "kExplanationSchemaId",
        "kExplanationSchemaVersion",
        "kMaxExplanationReasons",
        "kOperationRetentionPolicy",
        "operation_family",
        "operation_reason_rule_metadata",
        "operation_reason_is_legal",
        "reason_presentation_metadata",
        "reason_category_is_known",
        "unique_reason_presentation_for_category",
        "matching_reason_presentation_for_category",
    ):
        require(symbol in header, f"generated header lacks {symbol}")
    require(
        f'inline constexpr std::string_view kPackagedCatalogSha256 = "{catalog_digest}";'
        in header,
        "generated header lacks the exact packaged catalog digest",
    )
    require(
        '"residency_operation_succeeded"' in generated_source,
        "generated source lacks the closed reason registry",
    )

    profile_schema = load_json(
        files["docs/api/schemas/residency/residency_profiles.schema.json"]
    )
    require(
        {
            "source_support_baseline",
            "selection_registry_sha256",
        }.issubset(profile_schema["required"]),
        "catalog identity fields are not required by the source-owned schema",
    )
    require(
        profile_schema["properties"]["source_support_baseline"]["maxLength"] == 40,
        "source-support baseline schema bound drifted",
    )
    require(
        profile_schema["properties"]["source_support_baseline"]["minLength"] == 40,
        "source-support baseline schema width drifted",
    )
    require(
        profile_schema["properties"]["source_support_baseline"]["pattern"]
        == "^[0-9a-f]{40}$",
        "source-support baseline schema pattern drifted",
    )
    require(
        profile_schema["properties"]["selection_registry_sha256"]["maxLength"] == 64,
        "selection-registry digest schema bound drifted",
    )
    require(
        profile_schema["properties"]["selection_registry_sha256"]["minLength"] == 64,
        "selection-registry digest schema width drifted",
    )
    require(
        profile_schema["properties"]["selection_registry_sha256"]["pattern"]
        == "^[0-9a-f]{64}$",
        "selection-registry digest schema pattern drifted",
    )
    profile_validator = jsonschema.Draft202012Validator(profile_schema)
    for field, invalid in (
        ("source_support_baseline", "A" * 40),
        ("source_support_baseline", "0" * 39),
        ("selection_registry_sha256", "G" * 64),
        ("selection_registry_sha256", "0" * 63),
    ):
        invalid_catalog = dict(catalog)
        invalid_catalog[field] = invalid
        require(
            not profile_validator.is_valid(invalid_catalog),
            f"catalog schema accepted invalid {field}",
        )


LegalityQuery = tuple[str, str, str, str | None, bool]
OperationState = tuple[str, str, str, str | None]


def cpp_enum(enum_name: str, wire: str) -> str:
    member = "".join(part.title() for part in wire.split("_"))
    return f"{enum_name}::{member}"


def operation_reason_metadata_checks(registry: dict[str, Any]) -> str:
    operation_reason_codes = registry["reason_envelope_registry"]["operation_revision"][
        "reason_codes"
    ]
    checks = []
    for rank, code in enumerate(operation_reason_codes):
        envelope = registry["reason_registry"][code]["envelopes"]["operation_revision"]
        secondary_only = str(envelope.get("secondary_only", False)).lower()
        checks.append(
            f"    const auto* operation_reason_{rank} = "
            f"operation_reason_rule_metadata({json.dumps(code)});\n"
            f"    if (operation_reason_{rank} == nullptr || "
            f"operation_reason_{rank}->canonical_rank != {rank} || "
            f"operation_reason_{rank}->priority_band != "
            f"{envelope['priority_band']} || "
            f"operation_reason_{rank}->secondary_only != {secondary_only}) "
            "return 142;"
        )
    return "\n".join(checks)


def record_operation_legal_state(
    code: str,
    kind: str,
    state: str,
    legal_queries: set[LegalityQuery],
    covered_states: set[OperationState],
    associated_contexts: list[tuple[str, str]],
) -> None:
    if state == "associated_primary_state:secondary":
        associated_contexts.append((code, kind))
        return
    phases, outcomes, positions = state.split(":")
    for phase in phases.split("|"):
        for outcome in outcomes.split("|"):
            normalized_outcome = None if outcome == "null" else outcome
            covered_states.add((code, kind, phase, normalized_outcome))
            legal_queries.update(
                (
                    code,
                    kind,
                    phase,
                    normalized_outcome,
                    position == "secondary",
                )
                for position in positions.split("|")
            )


def operation_legality_contract(
    registry: dict[str, Any],
) -> tuple[set[LegalityQuery], set[OperationState], list[tuple[str, str]]]:
    legal_queries: set[LegalityQuery] = set()
    covered_states: set[OperationState] = set()
    associated_contexts: list[tuple[str, str]] = []
    operation_reason_codes = registry["reason_envelope_registry"]["operation_revision"][
        "reason_codes"
    ]
    for code in operation_reason_codes:
        envelope = registry["reason_registry"][code]["envelopes"]["operation_revision"]
        for context in envelope["contexts"]:
            for kind in context["operation_kinds"]:
                for state in context["legal_states"]:
                    record_operation_legal_state(
                        code,
                        kind,
                        state,
                        legal_queries,
                        covered_states,
                        associated_contexts,
                    )
    return legal_queries, covered_states, associated_contexts


def operation_legality_call(
    code: str,
    kind: str,
    phase: str,
    outcome: str | None,
    secondary: bool,
) -> str:
    outcome_expression = (
        "std::nullopt" if outcome is None else cpp_enum("TerminalOutcome", outcome)
    )
    return (
        "operation_reason_is_legal("
        f"{json.dumps(code)}, {cpp_enum('OperationKind', kind)}, "
        f"{cpp_enum('OperationPhase', phase)}, {outcome_expression}, "
        f"{str(secondary).lower()})"
    )


def operation_legality_checks(registry: dict[str, Any]) -> str:
    legal_queries, covered_states, associated_contexts = operation_legality_contract(
        registry
    )
    checks = [
        f"    if (!{operation_legality_call(*query)}) return 143;"
        for query in sorted(
            legal_queries,
            key=lambda row: (row[0], row[1], row[2], row[3] or "", row[4]),
        )
    ]
    for state in sorted(
        covered_states,
        key=lambda row: (row[0], row[1], row[2], row[3] or ""),
    ):
        for secondary in (False, True):
            query = (*state, secondary)
            if query not in legal_queries:
                checks.append(f"    if ({operation_legality_call(*query)}) return 144;")
    for code, kind in associated_contexts:
        secondary = operation_legality_call(code, kind, "evaluating", None, True)
        primary = operation_legality_call(code, kind, "evaluating", None, False)
        checks.extend(
            (
                f"    if (!{secondary}) return 145;",
                f"    if ({primary}) return 146;",
            )
        )
    return "\n".join(checks)


def require_generated_cpp_compiles(output_root: Path, files: dict[str, bytes]) -> None:
    configured = shlex.split(os.environ.get("CXX", "c++"))
    require(
        bool(configured) and shutil.which(configured[0]) is not None,
        f"C++ compiler is unavailable: {configured!r}",
    )
    include_root = output_root / "src/cpp/include"
    generated_source = output_root / "src/cpp/server/residency/generated_contract.cpp"
    with tempfile.TemporaryDirectory(prefix="residency-contract-cpp-") as directory:
        build_root = Path(directory)
        seam = build_root / "generated_contract_public_seam.cpp"
        catalog = load_json(files["src/cpp/resources/residency_profiles.json"])
        schemas = catalog["contract_registry"]["schema_registry"]
        known_checks = "\n".join(
            f'    if (!decode_schema_type("{row["schema_type"]}").is_known()) return 1;'
            for row in schemas.values()
        )
        internal_key_checks = "\n".join(
            f'    if (decode_schema_type("{key}").is_known()) return 2;'
            for key in schemas
        )
        cpp_promotion_kinds = {
            "exact_cell": "ExactCell",
            "compatibility_contract": "CompatibilityContract",
            "later_runtime": "LaterRuntime",
        }
        promotion_kind_checks = "\n".join(
            (
                f'    const auto promotion_unit_{index} = decode_promotion_unit_id("{unit["id"]}");\n'
                f"    if (!promotion_unit_{index}.is_known()) return {4 + index * 2};\n"
                f"    if (promotion_unit_kind(*promotion_unit_{index}.known_value()) != "
                f"PromotionUnitKind::{cpp_promotion_kinds[unit['unit_kind']]}) "
                f"return {5 + index * 2};"
            )
            for index, unit in enumerate(catalog["promotion_units"])
        )
        operation_family_checks = "\n".join(
            (
                f"    if (operation_family(OperationKind::{kind}) != "
                f"OperationFamily::{family}) return {100 + index};"
            )
            for index, (kind, family) in enumerate(
                (
                    ("Admission", "ResourceLifecycle"),
                    ("ExplicitUnload", "ResourceLifecycle"),
                    ("ForceUnload", "ResourceLifecycle"),
                    ("PressureReclamation", "ResourceLifecycle"),
                    ("StartupLoad", "ResourceLifecycle"),
                    ("ServiceTermination", "ResourceLifecycle"),
                    ("DeadBackendPruning", "ResourceLifecycle"),
                    ("SameEpochRecoveryCleanup", "ResourceLifecycle"),
                    ("PriorEpochOwnerCleanup", "ResourceLifecycle"),
                    ("ArtifactScopeRecoveryCleanup", "ResourceLifecycle"),
                    ("SavedPinMutation", "ResidentState"),
                    ("RuntimePinMutation", "ResidentState"),
                    ("LegacyPinBatch", "ResidentState"),
                    ("ResidentStateRecoveryCleanup", "ResidentState"),
                )
            )
        )
        registry = catalog["contract_registry"]
        operation_reason_checks_text = operation_reason_metadata_checks(registry)
        operation_legality_checks_text = operation_legality_checks(registry)
        seam.write_text(
            '#include "lemon/residency/generated_contract.h"\n\n'
            "using namespace lemon::residency;\n\n"
            "int main() {\n"
            f'    if (kPackagedCatalogSha256 != "{hashlib.sha256(files["src/cpp/resources/residency_profiles.json"]).hexdigest()}") return 3;\n'
            f"{known_checks}\n"
            f"{internal_key_checks}\n"
            f"{promotion_kind_checks}\n"
            f"{operation_family_checks}\n"
            f"{operation_reason_checks_text}\n"
            f"{operation_legality_checks_text}\n"
            '    if (kExplanationSchemaId != "residency.explanation/1.0") return 120;\n'
            "    if (kExplanationSchemaVersion.major != 1 || kExplanationSchemaVersion.minor != 0) return 121;\n"
            "    if (kMaxExplanationReasons != 16) return 122;\n"
            "    if (kOperationRetentionPolicy.active_expires || kOperationRetentionPolicy.recovery_required_expires) return 123;\n"
            "    if (kOperationRetentionPolicy.terminal_detail_seconds != 86400 || kOperationRetentionPolicy.forgotten_after_terminal_seconds != 604800) return 124;\n"
            '    const auto* succeeded = operation_reason_rule_metadata("residency_operation_succeeded");\n'
            "    if (succeeded == nullptr || succeeded->canonical_rank != 0 || succeeded->priority_band != 0 || succeeded->secondary_only) return 125;\n"
            '    const auto* cancelled = operation_reason_rule_metadata("residency_cancelled");\n'
            "    if (cancelled == nullptr || cancelled->canonical_rank != 8 || cancelled->priority_band != 1 || cancelled->secondary_only) return 126;\n"
            '    const auto* deprecated_pin = operation_reason_rule_metadata("residency_unconditional_pin_write_deprecated");\n'
            "    if (deprecated_pin == nullptr || deprecated_pin->canonical_rank != 33 || deprecated_pin->priority_band != 6 || !deprecated_pin->secondary_only) return 127;\n"
            '    if (operation_reason_rule_metadata("future_reason") != nullptr) return 128;\n'
            '    if (!operation_reason_is_legal("residency_cancelled", OperationKind::Admission, OperationPhase::Evaluating, std::nullopt, false)) return 129;\n'
            '    if (!operation_reason_is_legal("residency_capability_unsupported", OperationKind::Admission, OperationPhase::Evaluating, std::nullopt, false)) return 130;\n'
            '    if (operation_reason_is_legal("residency_capability_unsupported", OperationKind::ExplicitUnload, OperationPhase::Evaluating, std::nullopt, false)) return 131;\n'
            '    if (!operation_reason_is_legal("residency_operation_succeeded", OperationKind::Admission, OperationPhase::Terminal, TerminalOutcome::Succeeded, false)) return 132;\n'
            '    if (operation_reason_is_legal("residency_operation_succeeded", OperationKind::Admission, OperationPhase::Evaluating, std::nullopt, false)) return 133;\n'
            '    if (!operation_reason_is_legal("residency_unconditional_pin_write_deprecated", OperationKind::SavedPinMutation, OperationPhase::Evaluating, std::nullopt, true)) return 134;\n'
            '    if (operation_reason_is_legal("residency_unconditional_pin_write_deprecated", OperationKind::SavedPinMutation, OperationPhase::Evaluating, std::nullopt, false)) return 135;\n'
            '    const auto* capacity = reason_presentation_metadata("p_capacity");\n'
            '    if (capacity == nullptr || capacity->category_id != "capacity" || capacity->severity != "warning") return 136;\n'
            '    if (!reason_category_is_known("capacity") || reason_category_is_known("future-category")) return 137;\n'
            '    if (unique_reason_presentation_for_category("capacity") != capacity) return 138;\n'
            '    if (unique_reason_presentation_for_category("authentication") != nullptr) return 139;\n'
            '    const auto* status_auth = matching_reason_presentation_for_category("authentication", "p_status_authentication");\n'
            '    if (status_auth == nullptr || status_auth->title != "Status authorization required") return 140;\n'
            '    if (matching_reason_presentation_for_category("capacity", "p_action") != nullptr) return 141;\n'
            "    return 0;\n"
            "}\n",
            encoding="utf-8",
            newline="\n",
        )
        executable = build_root / (
            "generated_contract_public_seam.exe"
            if os.name == "nt"
            else "generated_contract_public_seam"
        )
        compiler = Path(configured[0]).name.lower()
        if compiler in {"cl", "cl.exe"}:
            command = [
                *configured,
                "/nologo",
                "/std:c++17",
                "/W4",
                "/WX",
                "/EHsc",
                f"/I{include_root}",
                f"/I{REPO_ROOT / 'src/cpp/include'}",
                f"/Fo{build_root}{os.sep}",
                f"/Fd{build_root / 'generated_contract.pdb'}",
                str(generated_source),
                str(seam),
                f"/Fe:{executable}",
            ]
        else:
            command = [
                *configured,
                "-std=c++17",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pedantic",
                "-I",
                str(include_root),
                "-I",
                str(REPO_ROOT / "src/cpp/include"),
                str(generated_source),
                str(seam),
                "-o",
                str(executable),
            ]
        compiled = subprocess.run(
            command,
            cwd=REPO_ROOT,
            check=False,
            text=True,
            capture_output=True,
        )
        require(compiled.returncode == 0, compiled.stdout + compiled.stderr)
        executed = subprocess.run(
            [str(executable)],
            cwd=REPO_ROOT,
            check=False,
            text=True,
            capture_output=True,
        )
        require(executed.returncode == 0, executed.stdout + executed.stderr)


REASON_BEARING_SCHEMAS = (
    "artifact_quarantine_record",
    "artifact_writer_job_revision",
    "artifact_writer_request_result",
    "authority_transaction_result",
    "coordinator_step_result",
    "operation_revision",
    "request_error",
    "resource_diagnostic",
    "response_diagnostic",
    "staged_import_session_record",
)


def load_schema_contract(
    files: dict[str, bytes],
) -> tuple[dict[str, Any], Registry, Any]:
    tools_root = str(REPO_ROOT / "tools")
    if tools_root not in sys.path:
        sys.path.insert(0, tools_root)
    contract_validator = import_module(
        "residency_contract.validation"
    ).contract_validator
    schema_prefix = "docs/api/schemas/residency/"
    schemas = {
        path.removeprefix(schema_prefix).removesuffix(".schema.json"): load_json(
            content
        )
        for path, content in files.items()
        if path.startswith(schema_prefix)
    }
    resources = Registry().with_resources(
        [(schema["$id"], Resource.from_contents(schema)) for schema in schemas.values()]
    )
    for schema in schemas.values():
        jsonschema.Draft202012Validator.check_schema(schema)
    return schemas, resources, contract_validator


def require_schema_examples(
    files: dict[str, bytes],
    schemas: dict[str, Any],
    resources: Registry,
    contract_validator: Any,
) -> dict[str, Any]:
    examples = load_json(
        files["test/residency/contract/generated/schema_examples.json"]
    )["examples"]
    require(set(examples) == set(schemas), "schema example closure drifted")
    for name, example in examples.items():
        contract_validator(schemas[name], registry=resources).validate(example)
    return examples


def require_transitive_keywords(schemas: dict[str, Any]) -> None:
    required_keywords = ["x-max-utf8-bytes", "x-nfc"]
    for name in ("reason", *REASON_BEARING_SCHEMAS):
        expected_keywords = (
            [*required_keywords, "x-residency-assert"]
            if name == "operation_revision"
            else required_keywords
        )
        require(
            schemas[name]["x-residency-required-keywords"] == expected_keywords,
            f"{name} does not declare its transitive boundary validator",
        )


def collect_annotations(value: Any, keyword: str) -> list[Any]:
    if isinstance(value, dict):
        found = [value[keyword]] if keyword in value else []
        for child in value.values():
            found.extend(collect_annotations(child, keyword))
        return found
    if isinstance(value, list):
        found = []
        for child in value:
            found.extend(collect_annotations(child, keyword))
        return found
    return []


def require_quarantine_provenance(schemas: dict[str, Any]) -> None:
    require(
        collect_annotations(
            schemas["artifact_quarantine_record"], "x-residency-derived-by"
        )
        == [
            {
                "field": "resolution_intent",
                "selected_by": ["origin", "resolution_evidence"],
            }
        ],
        "quarantine derivation provenance drifted",
    )


def envelope_with_reason(
    name: str, reason: dict[str, Any], examples: dict[str, Any]
) -> dict[str, Any]:
    candidate = json.loads(json.dumps(examples[name]))
    if name == "artifact_quarantine_record":
        candidate["cause"] = reason
    elif name in {"request_error", "resource_diagnostic", "response_diagnostic"}:
        candidate["reason"] = reason
    elif name == "staged_import_session_record":
        writer_status = json.loads(json.dumps(examples["artifact_writer_job_revision"]))
        writer_status["reasons"] = [reason]
        candidate["writer_status"] = writer_status
    else:
        candidate["reasons"] = [reason]
        if name == "operation_revision":
            candidate["primary_reason_code"] = reason["code"]
        if name == "artifact_writer_request_result":
            candidate["result"] = "failed"
    return candidate


def require_operation_reason_equality(
    validator: Any, unknown_reason: dict[str, Any], examples: dict[str, Any]
) -> None:
    mismatched = envelope_with_reason("operation_revision", unknown_reason, examples)
    mismatched["primary_reason_code"] = "different_reason"
    require(
        not validator.is_valid(mismatched),
        "operation primary reason diverged from reasons[0]",
    )
    missing = envelope_with_reason("operation_revision", unknown_reason, examples)
    missing["primary_reason_code"] = None
    require(
        not validator.is_valid(missing),
        "operation reasons were accepted without a primary reason",
    )


def require_unsafe_reasons_rejected(
    reason_validator: Any,
    envelope_validators: dict[str, Any],
    unknown_reason: dict[str, Any],
    examples: dict[str, Any],
) -> None:
    unsafe_mutations = (
        ("category_id", "unknown_category"),
        ("severity", "sudo"),
        ("title", "<b>unsafe</b>"),
        ("title", "unsafe\u202etext"),
        ("default_message", "unsafe\u0001text"),
        ("title", "Cafe\u0301"),
        ("title", "é" * 41),
        ("title", "\ud800"),
    )
    for field, value in unsafe_mutations:
        unsafe_reason = json.loads(json.dumps(unknown_reason))
        unsafe_reason["presentation"][field] = value
        require(
            not reason_validator.is_valid(unsafe_reason),
            f"unknown reason accepted unsafe {field}",
        )
        for name, validator in envelope_validators.items():
            candidate = envelope_with_reason(name, unsafe_reason, examples)
            require(
                not validator.is_valid(candidate),
                f"{name} accepted unsafe nested reason {field}",
            )


def require_reason_boundaries(
    schemas: dict[str, Any],
    resources: Registry,
    contract_validator: Any,
    examples: dict[str, Any],
) -> None:
    unknown_reason = {
        "code": "future_reason",
        "presentation": {
            "presentation_id": "future_reason",
            "category_id": "compatibility",
            "severity": "warning",
            "title": "Future reason",
            "default_message": "A future reason was preserved for display.",
        },
    }
    reason_validator = contract_validator(schemas["reason"], registry=resources)
    reason_validator.validate(unknown_reason)

    envelope_validators = {
        name: contract_validator(schemas[name], registry=resources)
        for name in REASON_BEARING_SCHEMAS
    }
    for name, validator in envelope_validators.items():
        validator.validate(envelope_with_reason(name, unknown_reason, examples))
    require_operation_reason_equality(
        envelope_validators["operation_revision"], unknown_reason, examples
    )
    require_unsafe_reasons_rejected(
        reason_validator, envelope_validators, unknown_reason, examples
    )


def require_schemas_and_examples(files: dict[str, bytes]) -> None:
    schemas, resources, contract_validator = load_schema_contract(files)
    examples = require_schema_examples(files, schemas, resources, contract_validator)
    require_transitive_keywords(schemas)
    require_quarantine_provenance(schemas)
    require_reason_boundaries(schemas, resources, contract_validator, examples)


def require_duplicate_key_rejected() -> None:
    with tempfile.TemporaryDirectory(
        prefix="residency-contract-duplicate-"
    ) as directory:
        root = Path(directory)
        source_text = SOURCE.read_text(encoding="utf-8")
        marker = '  "schema_version": 7,'
        require(marker in source_text, "source schema version is not 7")
        duplicate_source = root / "duplicate.json"
        duplicate_source.write_text(
            source_text.replace(marker, f"{marker}\n{marker}", 1),
            encoding="utf-8",
            newline="\n",
        )
        output_root = root / "output"
        rejected = run_generator(
            "--source", str(duplicate_source), "--output-root", str(output_root)
        )
        require(rejected.returncode != 0, "duplicate source key was accepted")
        require(
            "duplicate" in rejected.stderr.lower(), "duplicate diagnostic was unclear"
        )
        require(
            "traceback" not in rejected.stderr.lower(),
            "duplicate rejection leaked traceback",
        )
        require(not output_root.exists(), "failed generation wrote output")


def require_output_root_symlink_rejected() -> None:
    with tempfile.TemporaryDirectory(prefix="residency-contract-symlink-") as directory:
        base = Path(directory)
        output_root = base / "output"
        outside = base / "outside"
        (output_root / "docs/api/schemas").mkdir(parents=True)
        outside.mkdir()
        link = output_root / "docs/api/schemas/residency"
        try:
            link.symlink_to(outside, target_is_directory=True)
        except (NotImplementedError, OSError):
            return
        rejected = run_generator(
            "--source", str(SOURCE), "--output-root", str(output_root)
        )
        require(rejected.returncode != 0, "output-root symlink was accepted")
        require(
            "symlink" in rejected.stderr.lower()
            or "outside" in rejected.stderr.lower(),
            "symlink rejection diagnostic was unclear",
        )
        require(not collect(outside), "generator wrote outside output root")


def require_check_rejects_generated_drift() -> None:
    def run_case(label: str, mutate: Any, expected: tuple[str, ...]) -> None:
        with tempfile.TemporaryDirectory(
            prefix=f"residency-contract-check-{label}-"
        ) as directory:
            output_root = Path(directory)
            generated = run_generator(
                "--source", str(SOURCE), "--output-root", str(output_root)
            )
            require(generated.returncode == 0, generated.stderr or generated.stdout)
            mutate(output_root)
            before = collect(output_root)
            checked = run_generator(
                "--source",
                str(SOURCE),
                "--output-root",
                str(output_root),
                "--check",
            )
            require(checked.returncode != 0, f"--check accepted {label}")
            for expected_path in expected:
                require(
                    expected_path in checked.stderr,
                    f"--check misclassified {label}: {expected_path}",
                )
            require(collect(output_root) == before, f"--check rewrote {label}")

    def remove_output(root: Path) -> None:
        (root / "src/cpp/resources/residency_profiles.json").unlink()

    def mutate_output(root: Path) -> None:
        path = root / "src/cpp/include/lemon/residency/generated_contract.h"
        path.write_bytes(path.read_bytes() + b"drift\n")

    def add_unexpected_outputs(root: Path) -> None:
        for relative in (
            "docs/api/schemas/residency/unexpected.schema.json",
            "test/residency/contract/generated/unexpected.json",
        ):
            path = root / relative
            path.write_text("{}\n", encoding="utf-8", newline="\n")

    run_case("missing", remove_output, ("missing=['src/cpp/resources/",))
    run_case("drifted", mutate_output, ("drifted=['src/cpp/include/",))
    run_case(
        "unexpected",
        add_unexpected_outputs,
        (
            "docs/api/schemas/residency/unexpected.schema.json",
            "test/residency/contract/generated/unexpected.json",
        ),
    )


def main() -> int:
    if not GENERATOR.is_file():
        sys.stderr.write("TASK-009 residency contract generator is unavailable\n")
        return 1

    with (
        tempfile.TemporaryDirectory(prefix="residency-contract-a-") as first_dir,
        tempfile.TemporaryDirectory(prefix="residency-contract-b-") as second_dir,
    ):
        first = Path(first_dir)
        second = Path(second_dir)
        for output_root in (first, second):
            generated = run_generator(
                "--source", str(SOURCE), "--output-root", str(output_root)
            )
            require(generated.returncode == 0, generated.stderr or generated.stdout)

        first_files = collect(first)
        second_files = collect(second)
        require(set(first_files) == GENERATED_PATHS, "generated path set drifted")
        require(first_files == second_files, "double generation was not byte-identical")
        for path, content in first_files.items():
            require_canonical_bytes(path, content)
            if path.endswith(".json"):
                load_json(content)
        require_catalog_contract(first_files)
        require_schemas_and_examples(first_files)
        require_generated_cpp_compiles(first, first_files)

    checked = run_generator("--source", str(SOURCE), "--check")
    require(checked.returncode == 0, checked.stderr or checked.stdout)
    require_duplicate_key_rejected()
    require_output_root_symlink_rejected()
    require_check_rejects_generated_drift()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
