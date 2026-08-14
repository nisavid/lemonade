"""Validate material profile registries and their bound documentation."""

from __future__ import annotations

import hashlib
import re
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any, Protocol

from .contract import (
    fail,
    require_exact_keys,
    require_mapping,
    require_registry_keys,
    require_string,
    require_string_list,
)
from .markdown_sections import scan_markdown_headings, slice_markdown_section
from .profile_contracts import (
    ACCEPTED_HARDWARE_PROFILE_IDENTITIES,
    ACCEPTED_PROFILE_IDENTITIES,
    PROFILE_KIND_LABELS,
)

EXPECTED_RUNTIME_BINDING_KINDS = {
    "device_identity",
    "backend_artifact_digest",
    "source_build_dependency_closure",
    "driver_runtime_closure",
    "model_manifest_digest",
    "normalized_configuration_digest",
    "evidence_index_digest",
    "evidence_liveness_lease",
}
EXPECTED_COVERAGE_POLICY = {
    "classification_disposition": "exclude_cpu_onnxruntime_compatibility",
    "host_floor_rule": "constraint_profile_only",
    "multi_device_rule": "separate_exact_topology_placement_cell",
}
PROFILE_REGISTRIES = (
    "hardware_profiles",
    "configuration_profiles",
    "workload_profiles",
    "predictor_rules",
    "observation_contracts",
)
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
ANCHOR_PATTERN = re.compile(r"[a-z0-9]+(?:-[a-z0-9]+)*")


class Vocabulary(Protocol):
    """Vocabulary attributes consumed by profile validation."""

    model_types: set[str]
    constraints: set[str]


@dataclass(frozen=True)
class ProfileValidation:
    """Exact-cell profile registries and their material binding vocabulary."""

    registries: dict[str, dict[str, Any]]
    runtime_binding_kinds: set[str]
    document_bindings: dict[str, dict[str, str]]
    coverage_policy: dict[str, str]


def _require_accepted_profile_ids(registry_name: str, profiles: dict[str, Any]) -> None:
    expected_ids = set(ACCEPTED_PROFILE_IDENTITIES[registry_name])
    actual_ids = set(profiles)
    if actual_ids != expected_ids:
        fail(
            f"{registry_name} must equal the accepted profile ID set; "
            f"missing={sorted(expected_ids - actual_ids)}, "
            f"extra={sorted(actual_ids - expected_ids)}"
        )


def _require_accepted_profile_identity(
    registry_name: str, profile_id: str, identity: dict[str, Any]
) -> None:
    expected = ACCEPTED_PROFILE_IDENTITIES[registry_name][profile_id]
    if identity != expected:
        fail(
            f"{PROFILE_KIND_LABELS[registry_name]} {profile_id} must equal its "
            "accepted semantic identity"
        )


def _heading_slug(heading: str) -> str:
    normalized = re.sub(r"[^a-z0-9 _-]", "", heading.casefold())
    return re.sub(r"[ _]+", "-", normalized).strip("-")


def _markdown_section(document: str, anchor: str, label: str) -> str:
    headings = scan_markdown_headings(document)
    matches = [
        heading for heading in headings if _heading_slug(heading.title) == anchor
    ]
    if not matches:
        fail(f"{label} heading #{anchor} does not exist")
    if len(matches) != 1:
        fail(f"{label} heading #{anchor} is ambiguous")
    return slice_markdown_section(document, headings, matches[0], include_heading=True)


def _safe_document_path(repo: Path, raw_path: str, label: str) -> Path:
    posix_path = PurePosixPath(raw_path)
    if (
        not raw_path
        or "\\" in raw_path
        or posix_path.is_absolute()
        or any(part in {"", ".", ".."} for part in posix_path.parts)
    ):
        fail(f"{label}.locator must use a safe repository-relative path")
    repository = repo.resolve()
    path = (repository / Path(*posix_path.parts)).resolve()
    if not path.is_relative_to(repository):
        fail(f"{label}.locator escapes the repository")
    if not path.is_file():
        fail(f"{label}.locator document does not exist")
    return path


def _read_canonical_document(path: Path, label: str) -> str:
    try:
        with path.open(encoding="utf-8", newline=None) as stream:
            return stream.read()
    except UnicodeDecodeError as error:
        fail(f"{label}.locator document is not UTF-8: {error}")
    except OSError as error:
        fail(f"cannot read {label}.locator document: {error}")


def validate_document_binding(repo: Path, value: Any, label: str) -> dict[str, str]:
    """Validate one content-addressed repository document or heading section."""

    binding = require_mapping(value, label)
    require_exact_keys(binding, {"locator", "sha256"}, label)
    locator = require_string(binding["locator"], f"{label}.locator")
    sha256 = require_string(binding["sha256"], f"{label}.sha256")
    if SHA256_PATTERN.fullmatch(sha256) is None:
        fail(f"{label}.sha256 must be a full lowercase SHA-256")
    if locator.count("#") > 1:
        fail(f"{label}.locator may contain at most one heading anchor")
    raw_path, separator, anchor = locator.partition("#")
    path = _safe_document_path(repo, raw_path, label)
    document = _read_canonical_document(path, label)
    if separator:
        if ANCHOR_PATTERN.fullmatch(anchor) is None:
            fail(f"{label}.locator has an invalid heading anchor")
        bound_document = _markdown_section(document, anchor, label)
    else:
        bound_document = document
    bound_content = bound_document.encode("utf-8")
    if hashlib.sha256(bound_content).hexdigest() != sha256:
        fail(f"{label}.sha256 does not match its bound document content")
    return {"locator": locator, "sha256": sha256}


def _validate_configuration_profiles(
    repo: Path,
    profiles: dict[str, Any],
    vocabulary: Vocabulary,
    bindings: dict[str, dict[str, str]],
) -> None:
    for profile_id, raw_profile in profiles.items():
        label = f"configuration_profiles.{profile_id}"
        profile = require_mapping(raw_profile, label)
        require_exact_keys(profile, {"document", "model_types"}, label)
        document = validate_document_binding(
            repo, profile["document"], f"{label}.document"
        )
        bindings[f"{label}.document"] = document
        model_types = set(
            require_string_list(profile["model_types"], f"{label}.model_types")
        )
        if not model_types <= vocabulary.model_types:
            fail(f"configuration profile {profile_id} references an unknown model type")
        _require_accepted_profile_identity(
            "configuration_profiles",
            profile_id,
            {"document": document, "model_types": model_types},
        )


def _validate_simple_document_profiles(
    repo: Path,
    registry_name: str,
    profiles: dict[str, Any],
    bindings: dict[str, dict[str, str]],
) -> None:
    for profile_id, raw_profile in profiles.items():
        label = f"{registry_name}.{profile_id}"
        profile = require_mapping(raw_profile, label)
        require_exact_keys(profile, {"document"}, label)
        document = validate_document_binding(
            repo, profile["document"], f"{label}.document"
        )
        bindings[f"{label}.document"] = document
        _require_accepted_profile_identity(
            registry_name, profile_id, {"document": document}
        )


def _validate_predictor_rules(
    repo: Path,
    rules: dict[str, Any],
    bindings: dict[str, dict[str, str]],
) -> None:
    for rule_id, raw_rule in rules.items():
        label = f"predictor_rules.{rule_id}"
        rule = require_mapping(raw_rule, label)
        require_exact_keys(rule, {"confidence_target", "document"}, label)
        confidence_target = require_string(
            rule["confidence_target"], f"{label}.confidence_target"
        )
        document = validate_document_binding(
            repo, rule["document"], f"{label}.document"
        )
        bindings[f"{label}.document"] = document
        _require_accepted_profile_identity(
            "predictor_rules",
            rule_id,
            {"confidence_target": confidence_target, "document": document},
        )


def _validate_observation_contracts(
    repo: Path,
    contracts: dict[str, Any],
    vocabulary: Vocabulary,
    bindings: dict[str, dict[str, str]],
) -> None:
    for contract_id, raw_contract in contracts.items():
        label = f"observation_contracts.{contract_id}"
        contract = require_mapping(raw_contract, label)
        require_exact_keys(contract, {"constraints", "document"}, label)
        constraints = set(
            require_string_list(contract["constraints"], f"{label}.constraints")
        )
        if not constraints <= vocabulary.constraints:
            fail(f"observation contract {contract_id} references an unknown constraint")
        document = validate_document_binding(
            repo, contract["document"], f"{label}.document"
        )
        bindings[f"{label}.document"] = document
        _require_accepted_profile_identity(
            "observation_contracts",
            contract_id,
            {"constraints": constraints, "document": document},
        )


def _validate_hardware_document_bindings(
    repo: Path,
    profiles: dict[str, Any],
    bindings: dict[str, dict[str, str]],
) -> None:
    for profile_id, raw_profile in profiles.items():
        label = f"hardware_profiles.{profile_id}"
        profile = require_mapping(raw_profile, label)
        document = validate_document_binding(
            repo, profile.get("evidence_document"), f"{label}.evidence_document"
        )
        bindings[f"{label}.evidence_document"] = document
        if (
            document
            != ACCEPTED_HARDWARE_PROFILE_IDENTITIES[profile_id]["evidence_document"]
        ):
            fail(
                f"hardware profile {profile_id} must equal its accepted semantic identity"
            )


def validate_profile_stage(
    repo: Path, inventory: dict[str, Any], vocabulary: Vocabulary
) -> ProfileValidation:
    """Validate exact-cell profiles, material document bindings, and policy."""

    registries: dict[str, dict[str, Any]] = {}
    for registry_name in PROFILE_REGISTRIES:
        registry = require_mapping(inventory.get(registry_name), registry_name)
        require_registry_keys(registry, registry_name)
        _require_accepted_profile_ids(registry_name, registry)
        registries[registry_name] = registry
    runtime_binding_kinds = set(
        require_string_list(
            inventory.get("runtime_binding_kinds"), "runtime_binding_kinds"
        )
    )
    if runtime_binding_kinds != EXPECTED_RUNTIME_BINDING_KINDS:
        fail("runtime_binding_kinds must equal the accepted material identity set")

    bindings: dict[str, dict[str, str]] = {}
    _validate_hardware_document_bindings(
        repo, registries["hardware_profiles"], bindings
    )
    _validate_configuration_profiles(
        repo, registries["configuration_profiles"], vocabulary, bindings
    )
    _validate_simple_document_profiles(
        repo, "workload_profiles", registries["workload_profiles"], bindings
    )
    _validate_predictor_rules(repo, registries["predictor_rules"], bindings)
    _validate_observation_contracts(
        repo, registries["observation_contracts"], vocabulary, bindings
    )

    coverage_policy = require_mapping(
        inventory.get("coverage_policy"), "coverage_policy"
    )
    require_exact_keys(
        coverage_policy, set(EXPECTED_COVERAGE_POLICY), "coverage_policy"
    )
    for field, value in coverage_policy.items():
        require_string(value, f"coverage_policy.{field}")
    if coverage_policy != EXPECTED_COVERAGE_POLICY:
        fail("coverage_policy must equal the accepted policy enum values")
    return ProfileValidation(
        registries=registries,
        runtime_binding_kinds=runtime_binding_kinds,
        document_bindings=bindings,
        coverage_policy=dict(coverage_policy),
    )
