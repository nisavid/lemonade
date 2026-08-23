import copy
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
    "docs/api/schemas/residency/deployment_local_overlay_object.schema.json",
    "docs/api/schemas/residency/operation_revision.schema.json",
    "docs/api/schemas/residency/overlay_activation_root.schema.json",
    "docs/api/schemas/residency/profiling_input_envelope.schema.json",
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
        "schema_registry": 15,
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
        "explanation_schema_id",
        "explanation_schema_version",
        "max_explanation_reasons",
        "operation_retention_policy",
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
        f'inline constexpr std::string_view packaged_catalog_sha256 = "{catalog_digest}";'
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
    cpp_string = import_module("residency_contract.generator")._cpp_string
    unicode_literal = cpp_string("éA")
    require(
        unicode_literal == r'"\xc3""\xa9""A"',
        "C++ string literal did not isolate UTF-8 byte escapes",
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
                f"    const auto operation_family_{index} = "
                f"operation_family(OperationKind::{kind});\n"
                f"    if (!operation_family_{index}.has_value() || "
                f"*operation_family_{index} != OperationFamily::{family}) "
                f"return {100 + index};"
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
            "#include <string>\n"
            "#include <type_traits>\n\n"
            "using namespace lemon::residency;\n\n"
            "int main() {\n"
            "    static_assert(std::is_same_v<decltype(operation_family(OperationKind::Admission)), std::optional<OperationFamily>>);\n"
            f"    const std::string unicode_literal = {unicode_literal};\n"
            "    if (unicode_literal.size() != 3 || static_cast<unsigned char>(unicode_literal[0]) != 0xc3 || static_cast<unsigned char>(unicode_literal[1]) != 0xa9 || unicode_literal[2] != 'A') return 151;\n"
            f'    if (packaged_catalog_sha256 != "{hashlib.sha256(files["src/cpp/resources/residency_profiles.json"]).hexdigest()}") return 3;\n'
            f"{known_checks}\n"
            f"{internal_key_checks}\n"
            f"{promotion_kind_checks}\n"
            f"{operation_family_checks}\n"
            f"{operation_reason_checks_text}\n"
            f"{operation_legality_checks_text}\n"
            '    if (explanation_schema_id != "residency.explanation/1.0") return 120;\n'
            "    if (explanation_schema_version.major != 1 || explanation_schema_version.minor != 0) return 121;\n"
            "    if (max_explanation_reasons != 16) return 122;\n"
            "    if (operation_retention_policy.active_expires || operation_retention_policy.recovery_required_expires) return 123;\n"
            "    if (operation_retention_policy.terminal_detail_seconds != 86400 || operation_retention_policy.forgotten_after_terminal_seconds != 604800) return 124;\n"
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
            "    if (operation_family(static_cast<OperationKind>(0x7fff)).has_value()) return 147;\n"
            '    if (operation_reason_is_legal("residency_cancelled", static_cast<OperationKind>(0x7fff), OperationPhase::Evaluating, std::nullopt, false)) return 148;\n'
            '    if (operation_reason_is_legal("residency_cancelled", OperationKind::Admission, static_cast<OperationPhase>(0x7fff), std::nullopt, false)) return 149;\n'
            '    if (operation_reason_is_legal("residency_operation_succeeded", OperationKind::Admission, OperationPhase::Terminal, static_cast<TerminalOutcome>(0x7fff), false)) return 150;\n'
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


def require_cpp_codec_schema_seam(
    schemas: dict[str, Any], resources: Registry, contract_validator: Any
) -> None:
    configured = shlex.split(os.environ.get("CXX", "c++"))
    require(
        bool(configured) and shutil.which(configured[0]) is not None,
        f"C++ compiler is unavailable: {configured!r}",
    )
    compiler = Path(configured[0]).name.lower()
    explicit_crypto_flags = os.environ.get("RESIDENCY_CONTRACT_MBEDCRYPTO_FLAGS")
    if explicit_crypto_flags is not None:
        crypto_flags = shlex.split(explicit_crypto_flags)
    elif compiler in {"cl", "cl.exe"}:
        crypto_flags = ["mbedcrypto.lib"]
    else:
        pkg_config = shutil.which("pkg-config")
        discovered = (
            subprocess.run(
                [pkg_config, "--cflags", "--libs", "mbedcrypto"],
                cwd=REPO_ROOT,
                check=False,
                text=True,
                capture_output=True,
            )
            if pkg_config is not None
            else None
        )
        crypto_flags = (
            shlex.split(discovered.stdout)
            if discovered is not None and discovered.returncode == 0
            else ["-lmbedcrypto"]
        )

    with tempfile.TemporaryDirectory(prefix="residency-overlay-codec-") as directory:
        build_root = Path(directory)
        executable = build_root / (
            "local_overlay_schema_seam.exe"
            if os.name == "nt"
            else "local_overlay_schema_seam"
        )
        sources = [
            REPO_ROOT / "test/cpp/test_residency_local_overlay.cpp",
            REPO_ROOT / "src/cpp/server/residency/claims.cpp",
            REPO_ROOT / "src/cpp/server/residency/local_overlay.cpp",
        ]
        if compiler in {"cl", "cl.exe"}:
            command = [
                *configured,
                "/nologo",
                "/std:c++17",
                "/W4",
                "/WX",
                "/EHsc",
                f"/I{REPO_ROOT / 'src/cpp/include'}",
                f"/Fo{build_root}{os.sep}",
                f"/Fd{build_root / 'local_overlay_schema_seam.pdb'}",
                *(str(source) for source in sources),
                f"/Fe:{executable}",
                "/link",
                *crypto_flags,
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
                str(REPO_ROOT / "src/cpp/include"),
                *(str(source) for source in sources),
                *crypto_flags,
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
        emitted = subprocess.run(
            [str(executable), "--emit-schema-validation-corpus"],
            cwd=REPO_ROOT,
            check=False,
            text=True,
            capture_output=True,
        )
        require(emitted.returncode == 0, emitted.stdout + emitted.stderr)

    corpus = load_json(emitted.stdout.encode("utf-8"))
    require(
        corpus["confidence_basis_points_boundary"]
        == {"maximum_accepted": 10000, "first_rejected": 10001},
        "C++ codec confidence boundary drifted",
    )
    documents = corpus["accepted_documents"]
    expected_documents = {
        "deployment_local_overlay_object",
        "overlay_activation_root",
        "profiling_input_envelope",
    }
    require(
        set(documents) == expected_documents,
        "C++ codec schema corpus is incomplete",
    )
    parsed_documents: dict[str, Any] = {}
    for name, canonical_bytes in documents.items():
        require(isinstance(canonical_bytes, str), f"{name} codec output is not text")
        document = load_json(canonical_bytes.encode("utf-8"))
        require(
            json.dumps(
                document,
                ensure_ascii=False,
                separators=(",", ":"),
                sort_keys=True,
            )
            == canonical_bytes,
            f"{name} codec output is not canonical JSON",
        )
        contract_validator(schemas[name], registry=resources).validate(document)
        parsed_documents[name] = document

    require(
        parsed_documents["deployment_local_overlay_object"]["confidence_basis_points"]
        == 10000,
        "C++ codec corpus omitted the accepted confidence maximum",
    )
    above_maximum = copy.deepcopy(parsed_documents["deployment_local_overlay_object"])
    above_maximum["confidence_basis_points"] = 10001
    require(
        not contract_validator(
            schemas["deployment_local_overlay_object"], registry=resources
        ).is_valid(above_maximum),
        "overlay schema accepted confidence above 10000 basis points",
    )


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


def require_closed_schema_conditionals(
    schemas: dict[str, Any],
    resources: Registry,
    contract_validator: Any,
    examples: dict[str, Any],
) -> None:
    def mappings(value: Any) -> Any:
        if isinstance(value, dict):
            yield value
            for child in value.values():
                yield from mappings(child)
        elif isinstance(value, list):
            for child in value:
                yield from mappings(child)

    for name, schema in schemas.items():
        for mapping in mappings(schema):
            require(
                mapping.get("then") != {},
                f"{name} schema contains an empty conditional consequence",
            )

    authority = schemas["authority_transaction_result"]
    require(
        len(authority.get("allOf", [])) == 1,
        "explicit allow-empty authority condition was emitted as a no-op",
    )
    authority_validator = contract_validator(authority, registry=resources)
    require(
        authority_validator.is_valid(examples["authority_transaction_result"]),
        "successful authority transaction no longer permits empty reasons",
    )


def require_local_overlay_authoritative_vocabulary(
    schemas: dict[str, Any],
) -> None:
    vocabulary = import_module("residency_inventory.contract")
    expected_templates = {
        template: sorted(operations)
        for template, operations in sorted(
            vocabulary.OPERATION_LEAVES_BY_TEMPLATE.items()
        )
    }
    expected_constraints = sorted(vocabulary.EXPECTED_CONSTRAINT_KINDS)
    for name in (
        "profiling_input_envelope",
        "deployment_local_overlay_object",
    ):
        catalog = schemas[name]["$defs"]["selector_identity"]["properties"]["catalog"]
        require(
            catalog["properties"]["operation_template"]["enum"]
            == list(expected_templates),
            f"{name} operation templates drifted from inventory vocabulary",
        )
        require(
            catalog["properties"]["constraints"]["items"]["enum"]
            == expected_constraints,
            f"{name} constraints drifted from inventory vocabulary",
        )
        rendered_templates = {
            conditional["if"]["properties"]["operation_template"]["const"]: conditional[
                "then"
            ]["properties"]["operation_kind"]["enum"]
            for conditional in catalog["allOf"]
        }
        require(
            rendered_templates == expected_templates,
            f"{name} template operations drifted from inventory vocabulary",
        )


def require_local_overlay_schemas(
    schemas: dict[str, Any],
    resources: Registry,
    contract_validator: Any,
    examples: dict[str, Any],
) -> None:
    names = {
        "profiling_input_envelope",
        "deployment_local_overlay_object",
        "overlay_activation_root",
    }
    require(names <= set(schemas), "local-overlay schema set is incomplete")
    for name in names:
        schema = schemas[name]
        require(
            schema.get("additionalProperties") is False,
            f"{name} is not a closed document",
        )
        require(
            set(schema["properties"]) == set(schema["required"]),
            f"{name} contains optional top-level wire fields",
        )

    profiling = contract_validator(
        schemas["profiling_input_envelope"], registry=resources
    )
    overlay = contract_validator(
        schemas["deployment_local_overlay_object"], registry=resources
    )
    root = contract_validator(schemas["overlay_activation_root"], registry=resources)

    timestamp_validators = {
        "profiling_input_envelope": (
            profiling,
            ("observed_at", "fresh_until"),
        ),
        "deployment_local_overlay_object": (
            overlay,
            ("qualified_at", "expires_at"),
        ),
        "overlay_activation_root": (
            root,
            ("activated_at", "expires_at"),
        ),
    }
    for schema_name, (validator, fields) in timestamp_validators.items():
        for field in fields:
            for invalid_timestamp in (
                "2026-09-23T10:01:00+00:00",
                "2026-02-29T10:01:00Z",
                "2026-04-31T10:01:00Z",
            ):
                candidate = copy.deepcopy(examples[schema_name])
                candidate[field] = invalid_timestamp
                require(
                    not validator.is_valid(candidate),
                    f"{schema_name}.{field} accepted a noncanonical timestamp",
                )
            leap_day = copy.deepcopy(examples[schema_name])
            leap_day[field] = "2028-02-29T23:59:59Z"
            companion = fields[1] if field == fields[0] else fields[0]
            leap_day[companion] = (
                "2028-03-01T00:00:00Z" if field == fields[0] else "2028-02-29T23:59:58Z"
            )
            require(
                validator.is_valid(leap_day),
                f"{schema_name}.{field} rejected a canonical leap-day timestamp",
            )
        start_field, end_field = fields
        advancing = copy.deepcopy(examples[schema_name])
        advancing[start_field] = "2026-09-23T10:01:00Z"
        advancing[end_field] = "2026-09-23T10:01:01Z"
        require(
            validator.is_valid(advancing),
            f"{schema_name} rejected an advancing timestamp interval",
        )
        reverse = copy.deepcopy(advancing)
        reverse[start_field], reverse[end_field] = (
            reverse[end_field],
            reverse[start_field],
        )
        require(
            not validator.is_valid(reverse),
            f"{schema_name} accepted a reverse timestamp interval",
        )
        equal = copy.deepcopy(advancing)
        equal[end_field] = equal[start_field]
        require(
            not validator.is_valid(equal),
            f"{schema_name} accepted an equal timestamp interval",
        )

    ordered_values = contract_validator(
        {
            "$schema": "https://json-schema.org/draft/2020-12/schema",
            "$id": "urn:lemonade:test:residency-ordered-values",
            "type": "object",
            "additionalProperties": False,
            "properties": {"left": {}, "right": {}},
            "required": ["left", "right"],
            "x-residency-assert": {
                "field": "left",
                "less_than_path": "right",
            },
            "x-residency-required-keywords": ["x-residency-assert"],
        }
    )
    require(
        ordered_values.is_valid({"left": 1, "right": 2}),
        "ordered assertion rejected comparable numeric values",
    )
    for incomparable in (
        {"left": "1", "right": 2},
        {"left": 1, "right": "2"},
        {"left": {}, "right": []},
    ):
        require(
            not ordered_values.is_valid(incomparable),
            "ordered assertion accepted incomparable values",
        )

    incomplete = copy.deepcopy(examples["profiling_input_envelope"])
    incomplete["attribution_complete"] = False
    require(
        not profiling.is_valid(incomplete),
        "profiling schema accepted incomplete attribution",
    )
    zero_clock_skew = copy.deepcopy(examples["profiling_input_envelope"])
    zero_clock_skew["max_clock_skew_milliseconds"] = 0
    require(
        not profiling.is_valid(zero_clock_skew),
        "profiling schema accepted zero maximum clock skew",
    )
    unknown_selector = copy.deepcopy(examples["profiling_input_envelope"])
    unknown_selector["selector"]["future"] = True
    require(
        not profiling.is_valid(unknown_selector),
        "profiling schema accepted an open selector identity",
    )
    mismatched_template = copy.deepcopy(examples["profiling_input_envelope"])
    mismatched_template["selector"]["catalog"]["operation_template"] = "UNL"
    require(
        not profiling.is_valid(mismatched_template),
        "profiling schema accepted a mismatched operation template and kind",
    )
    unknown_claim = copy.deepcopy(examples["profiling_input_envelope"])
    unknown_claim["attributed_claims"][0]["completeness"] = "unknown"
    require(
        not profiling.is_valid(unknown_claim),
        "profiling schema accepted an incomplete claim closure",
    )

    wrong_status = copy.deepcopy(examples["deployment_local_overlay_object"])
    wrong_status["status"] = "stale"
    require(
        not overlay.is_valid(wrong_status),
        "overlay schema accepted a status owned by later invalidation work",
    )
    zero_safety_margin = copy.deepcopy(examples["deployment_local_overlay_object"])
    for family in zero_safety_margin["safety_margin_claims"]:
        family["completeness"] = "known_zero"
        family["entries"] = []
    require(
        not overlay.is_valid(zero_safety_margin),
        "qualified overlay accepted a zero safety margin",
    )
    exact_with_predicate = copy.deepcopy(examples["deployment_local_overlay_object"])
    exact_with_predicate["method"]["architecture_predicate_sha256"] = "a" * 64
    require(
        not overlay.is_valid(exact_with_predicate),
        "deployment-exact method accepted an architecture predicate",
    )
    architecture_without_predicate = copy.deepcopy(
        examples["deployment_local_overlay_object"]
    )
    architecture_without_predicate["method"]["scope"] = "architecture_predicate"
    require(
        not overlay.is_valid(architecture_without_predicate),
        "architecture method accepted a missing predicate",
    )
    wrong_method_operation = copy.deepcopy(examples["deployment_local_overlay_object"])
    wrong_method_operation["method"]["operation_kind"] = "pressure_reclamation"
    require(
        not overlay.is_valid(wrong_method_operation),
        "overlay method operation diverged from its selector operation",
    )

    fallback = copy.deepcopy(examples["overlay_activation_root"])
    fallback["transition"] = "fallback"
    require(
        not root.is_valid(fallback),
        "activation root accepted a fallback transition",
    )
    wrong_authority = copy.deepcopy(examples["overlay_activation_root"])
    wrong_authority["authority_status"] = "fallback"
    require(
        not root.is_valid(wrong_authority),
        "activation root accepted non-active authority",
    )
    genesis_with_predecessor = copy.deepcopy(examples["overlay_activation_root"])
    genesis_with_predecessor["previous_root_sha256"] = "b" * 64
    require(
        not root.is_valid(genesis_with_predecessor),
        "generation-one root accepted a predecessor",
    )
    skipped_genesis_sequence = copy.deepcopy(examples["overlay_activation_root"])
    skipped_genesis_sequence["selected_overlay_sequence"] = 2
    skipped_genesis_sequence["sequence_high_water"] = 2
    require(
        not root.is_valid(skipped_genesis_sequence),
        "generation-one root accepted a skipped overlay sequence",
    )
    successor_qualification = copy.deepcopy(examples["overlay_activation_root"])
    successor_qualification["generation"] = 2
    successor_qualification["previous_root_sha256"] = "b" * 64
    successor_qualification["selected_overlay_sequence"] = 2
    successor_qualification["sequence_high_water"] = 2
    require(
        root.is_valid(successor_qualification),
        "activation-root schema rejected a successor qualification",
    )
    rollback_without_predecessor = copy.deepcopy(examples["overlay_activation_root"])
    rollback_without_predecessor["generation"] = 2
    rollback_without_predecessor["transition"] = "rollback"
    require(
        not root.is_valid(rollback_without_predecessor),
        "rollback root accepted a missing predecessor",
    )
    successor_without_predecessor = copy.deepcopy(examples["overlay_activation_root"])
    successor_without_predecessor["generation"] = 2
    require(
        not root.is_valid(successor_without_predecessor),
        "activation-root successor accepted a missing predecessor",
    )
    sequence_above_high_water = copy.deepcopy(examples["overlay_activation_root"])
    sequence_above_high_water["generation"] = 2
    sequence_above_high_water["previous_root_sha256"] = "b" * 64
    sequence_above_high_water["transition"] = "rollback"
    sequence_above_high_water["selected_overlay_sequence"] = 3
    sequence_above_high_water["sequence_high_water"] = 2
    require(
        not root.is_valid(sequence_above_high_water),
        "activation root accepted a selected sequence above its high-water",
    )
    qualification_below_high_water = copy.deepcopy(examples["overlay_activation_root"])
    qualification_below_high_water["generation"] = 2
    qualification_below_high_water["previous_root_sha256"] = "b" * 64
    qualification_below_high_water["selected_overlay_sequence"] = 1
    qualification_below_high_water["sequence_high_water"] = 2
    require(
        not root.is_valid(qualification_below_high_water),
        "qualification root selected an overlay below its high-water",
    )
    rollback_at_high_water = copy.deepcopy(examples["overlay_activation_root"])
    rollback_at_high_water["generation"] = 2
    rollback_at_high_water["previous_root_sha256"] = "b" * 64
    rollback_at_high_water["transition"] = "rollback"
    rollback_at_high_water["selected_overlay_sequence"] = 2
    rollback_at_high_water["sequence_high_water"] = 2
    require(
        not root.is_valid(rollback_at_high_water),
        "rollback root selected the current sequence high-water",
    )


def require_schema_translation_mutations_rejected(files: dict[str, bytes]) -> None:
    tools_root = str(REPO_ROOT / "tools")
    if tools_root not in sys.path:
        sys.path.insert(0, tools_root)
    schemas_module = import_module("residency_contract.schemas")
    registry_module = import_module("residency_inventory.contract_registry")
    catalog = load_json(files["src/cpp/resources/residency_profiles.json"])
    source_registry = load_json(SOURCE.read_bytes())["contract_registry"]
    reasons = load_json(files["test/residency/contract/generated/reasons.json"])[
        "reasons"
    ]

    def conditional(document: dict[str, Any]) -> dict[str, Any]:
        registry = document.get("contract_registry", document)
        return registry["schema_registry"]["authority_transaction_result"][
            "conditionals"
        ][0]

    def check_registry(document: dict[str, Any]) -> None:
        registry_module.validate_contract_registry(document)

    def check_schemas(document: dict[str, Any]) -> None:
        registry = document["contract_registry"]
        schemas_module.build_schemas(registry, reasons, document)

    def set_untranslatable(target: dict[str, Any]) -> None:
        target["if"] = {"contexts": ["health_read"]}

    def drop_consequence(target: dict[str, Any]) -> None:
        target.pop("require_nonempty")

    def empty_values(target: dict[str, Any]) -> None:
        target.pop("require_nonempty")
        target["require_values"] = {}

    def add_unless(target: dict[str, Any]) -> None:
        target["unless"] = target["if"]

    def assert_with_consequence(target: dict[str, Any]) -> None:
        target["assert"] = target.pop("if")

    cases = (
        (
            source_registry,
            set_untranslatable,
            check_registry,
            ValueError,
            "predicate is not translatable",
            "non-translatable schema predicate",
        ),
        (
            catalog,
            drop_consequence,
            check_schemas,
            schemas_module.SchemaRenderError,
            "consequence is empty",
            "empty schema consequence",
        ),
        (
            catalog,
            empty_values,
            check_schemas,
            schemas_module.SchemaRenderError,
            "require_values is empty",
            "empty require-values consequence",
        ),
        (
            source_registry,
            add_unless,
            check_registry,
            ValueError,
            "exactly one predicate mode",
            "mixed schema predicate modes",
        ),
        (
            catalog,
            add_unless,
            check_schemas,
            schemas_module.SchemaRenderError,
            "exactly one predicate mode",
            "renderer mixed schema predicate modes",
        ),
        (
            source_registry,
            assert_with_consequence,
            check_registry,
            ValueError,
            "assert cannot include consequences",
            "schema assertion consequence",
        ),
        (
            catalog,
            assert_with_consequence,
            check_schemas,
            schemas_module.SchemaRenderError,
            "assert cannot include consequences",
            "renderer schema assertion consequence",
        ),
    )
    for document, mutate, check, error_type, expected, label in cases:
        mutated = copy.deepcopy(document)
        mutate(conditional(mutated))
        try:
            check(mutated)
        except error_type as error:
            require(expected in str(error), f"{label} diagnostic drifted")
        else:
            raise AssertionError(f"{label} was accepted")


def require_schemas_and_examples(files: dict[str, bytes]) -> None:
    schemas, resources, contract_validator = load_schema_contract(files)
    examples = require_schema_examples(files, schemas, resources, contract_validator)
    require_transitive_keywords(schemas)
    require_quarantine_provenance(schemas)
    require_reason_boundaries(schemas, resources, contract_validator, examples)
    require_closed_schema_conditionals(schemas, resources, contract_validator, examples)
    require_local_overlay_authoritative_vocabulary(schemas)
    require_local_overlay_schemas(schemas, resources, contract_validator, examples)
    require_cpp_codec_schema_seam(schemas, resources, contract_validator)
    require_schema_translation_mutations_rejected(files)


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
