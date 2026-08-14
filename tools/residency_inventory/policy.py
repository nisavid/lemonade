"""Validate closed operation, constraint, suite, fallback, and recovery policy."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Protocol

from .contract import (
    OperationApplicability,
    fail,
    require_exact_keys,
    require_mapping,
    require_registry_keys,
    require_string,
    require_string_list,
)
from .fallbacks import validate_fallback_semantics
from .recovery import validate_recovery_profiles


class Vocabulary(Protocol):
    """Closed vocabulary attributes consumed by policy validation."""

    operations: set[str]
    constraints: set[str]


EXPECTED_OPERATION_SETS = {
    "gpu_resource": {"ADM", "LFR", "PRE", "STA", "REC", "UNL", "PIN"},
    "npu_compatibility": {"ADM", "LFR", "STA", "REC", "UNL", "PIN", "NPC"},
}
EXPECTED_CONSTRAINT_PROFILES = {
    "apple_unified": {
        "gpu_shared_residency",
        "host_memavailable_floor",
        "model_type_pool",
        "ownership",
    },
    "provider_resolved_gpu": {
        "gpu_provider_resolved_capacity",
        "host_effects_provider_resolved",
        "model_type_pool",
        "ownership",
    },
    "npu_flm": {
        "flm_type_slot",
        "npu_cross_family",
        "model_type_pool",
        "ownership",
    },
    "npu_exclusive": {
        "npu_exclusive",
        "npu_cross_family",
        "model_type_pool",
        "ownership",
    },
}
EXPECTED_SUITE_OPERATIONS = {
    "PT-ID": {"*"},
    "PT-TOP": {"*"},
    "PT-FP": {"ADM", "STA"},
    "PT-SIG": {"ADM", "PRE", "STA"},
    "PT-ADM": {"ADM"},
    "PT-LFR": {"LFR"},
    "PT-PRE": {"PRE"},
    "PT-STA": {"STA"},
    "PT-NPU": {"ADM", "NPC"},
    "PT-REC": {"REC", "NPC"},
    "PT-UNL": {"UNL"},
    "PT-PIN": {"PIN"},
    "PT-CON": {"*"},
    "PT-ART": {"*"},
    "PT-EXP": {"*"},
    "PT-LIV": {"*"},
}
EXPECTED_SUITE_PROOFS = {
    "PT-ID": "identity, provenance, and immutable fingerprint",
    "PT-TOP": "topology and placement attribution",
    "PT-FP": "complete conservative lifetime footprint",
    "PT-SIG": "fresh coherent signals and uncertainty",
    "PT-ADM": "predictive admission and growth",
    "PT-LFR": "load-failure retry without unproved victims",
    "PT-PRE": "pressure reclamation and verified recovery",
    "PT-STA": "grouped startup admission",
    "PT-NPU": "NPU and FLM compatibility admission and conflict",
    "PT-REC": (
        "runtime ownership replay and verified release, or "
        "compatibility-synthetic fail-closed relation-state response"
    ),
    "PT-UNL": "explicit and forced unload protection",
    "PT-PIN": "saved and runtime pin mutation",
    "PT-CON": "atomic concurrency and stale-token rejection",
    "PT-ART": "artifact and native-writer ambiguity handoff",
    "PT-EXP": "generated explanation and reason conformance",
    "PT-LIV": "evidence-liveness rollback and restoration",
}
EXPECTED_SUITE_SETS = {
    "gpu_standard": {
        "PT-ID",
        "PT-TOP",
        "PT-FP",
        "PT-SIG",
        "PT-ADM",
        "PT-LFR",
        "PT-PRE",
        "PT-STA",
        "PT-REC",
        "PT-UNL",
        "PT-PIN",
        "PT-CON",
        "PT-ART",
        "PT-EXP",
        "PT-LIV",
    },
    "npu_compatibility": {
        "PT-ID",
        "PT-TOP",
        "PT-SIG",
        "PT-LFR",
        "PT-STA",
        "PT-NPU",
        "PT-REC",
        "PT-UNL",
        "PT-PIN",
        "PT-CON",
        "PT-EXP",
        "PT-LIV",
    },
}
EXPECTED_FALLBACK_SETS = {
    "gpu_standard": {
        "ADM": ["residency_admission_refuse_unknown_demand_v1"],
        "LFR": ["residency_load_retry_refuse_unproven_victim_set_v1"],
        "PRE": [
            "residency_pressure_report_only_unvalidated_v1",
            "residency_pressure_disabled_invalid_evidence_v1",
        ],
        "STA": ["residency_startup_block_group_v1"],
        "REC": ["residency_recovery_block_unproven_release_v1"],
        "UNL": ["residency_unload_preserve_live_use_v1"],
        "PIN": ["residency_pin_mutation_refuse_stale_or_unready_v1"],
    },
    "npu_compatibility": {
        "ADM": ["residency_admission_refuse_unknown_demand_v1"],
        "LFR": ["residency_load_retry_refuse_unproven_victim_set_v1"],
        "STA": ["residency_startup_block_group_v1"],
        "REC": ["residency_recovery_block_unproven_release_v1"],
        "UNL": ["residency_unload_preserve_live_use_v1"],
        "PIN": ["residency_pin_mutation_refuse_stale_or_unready_v1"],
        "NPC": ["residency_npu_conflict_preserve_refuse_v1"],
    },
}


def registry_member_operations(
    definition: Any,
    label: str,
    enum_operations: set[str],
    *,
    allow_wildcard: bool,
) -> OperationApplicability:
    entry = require_mapping(definition, label)
    operations = require_string_list(entry.get("operations"), f"{label}.operations")
    if "*" in operations:
        if not allow_wildcard:
            fail(f"{label} may not use wildcard operation applicability")
        if operations != ["*"]:
            fail(f"{label} may use '*' only as its sole operation applicability")
        return OperationApplicability(frozenset(enum_operations), is_wildcard=True)
    if not set(operations) <= enum_operations:
        fail(f"{label} references an unknown operation template")
    return OperationApplicability(frozenset(operations), is_wildcard=False)


@dataclass(frozen=True)
class PolicyRegistryValidation:
    """Normalized operation, constraint, proof, fallback, and recovery registries."""

    operation_sets: dict[str, set[str]]
    constraint_profiles: dict[str, list[str]]
    recovery_profiles: dict[str, dict[str, Any]]
    suite_registry: dict[str, Any]
    suite_sets: dict[str, list[str]]
    fallback_registry: dict[str, Any]
    fallback_sets: dict[str, dict[str, list[str]]]
    suite_operations: dict[str, OperationApplicability]
    fallback_operations: dict[str, OperationApplicability]


@dataclass(frozen=True)
class ResourcePolicyValidation:
    """Operation, constraint, and recovery policy registries."""

    operation_sets: dict[str, set[str]]
    constraint_profiles: dict[str, list[str]]
    recovery_profiles: dict[str, dict[str, Any]]


def validate_resource_policy_registries(
    inventory: dict[str, Any], vocabulary: Vocabulary
) -> ResourcePolicyValidation:
    raw_operation_sets = require_mapping(
        inventory.get("operation_sets"), "operation_sets"
    )
    require_registry_keys(raw_operation_sets, "operation_sets")
    operation_sets: dict[str, set[str]] = {}
    for set_id, raw_members in raw_operation_sets.items():
        members = set(require_string_list(raw_members, f"operation_sets.{set_id}"))
        if not members <= vocabulary.operations:
            fail(f"operation set {set_id} contains an unknown operation template")
        operation_sets[set_id] = members
    if operation_sets != EXPECTED_OPERATION_SETS:
        fail("operation_sets must equal the accepted GPU and NPU operation contracts")

    raw_constraint_profiles = require_mapping(
        inventory.get("constraint_profiles"), "constraint_profiles"
    )
    require_registry_keys(raw_constraint_profiles, "constraint_profiles")
    constraint_profiles: dict[str, list[str]] = {}
    for profile_id, raw_members in raw_constraint_profiles.items():
        normalized_members = require_string_list(
            raw_members, f"constraint_profiles.{profile_id}"
        )
        if not set(normalized_members) <= vocabulary.constraints:
            fail(f"constraint profile {profile_id} contains an unknown constraint")
        constraint_profiles[profile_id] = normalized_members
    if {
        profile_id: set(members) for profile_id, members in constraint_profiles.items()
    } != EXPECTED_CONSTRAINT_PROFILES:
        fail("constraint_profiles must equal the accepted closed profiles")
    return ResourcePolicyValidation(
        operation_sets=operation_sets,
        constraint_profiles=constraint_profiles,
        recovery_profiles=validate_recovery_profiles(inventory).profiles,
    )


@dataclass(frozen=True)
class SuitePolicyValidation:
    """Proof suite registry and normalized suite sets."""

    registry: dict[str, Any]
    sets: dict[str, list[str]]
    operations: dict[str, OperationApplicability]


def validate_suite_policy_registries(
    inventory: dict[str, Any], vocabulary: Vocabulary
) -> SuitePolicyValidation:
    registry = require_mapping(inventory.get("suite_registry"), "suite_registry")
    raw_sets = require_mapping(inventory.get("suite_sets"), "suite_sets")
    require_registry_keys(registry, "suite_registry")
    require_registry_keys(raw_sets, "suite_sets")
    for suite_id, definition in registry.items():
        entry = require_mapping(definition, f"suite_registry.{suite_id}")
        require_exact_keys(entry, {"operations", "proof"}, f"suite_registry.{suite_id}")
        require_string(entry["proof"], f"suite_registry.{suite_id}.proof")
    operations = {
        suite_id: registry_member_operations(
            definition,
            f"suite_registry.{suite_id}",
            vocabulary.operations,
            allow_wildcard=True,
        )
        for suite_id, definition in registry.items()
    }
    declared_operations = {
        suite_id: (
            {"*"} if applicability.is_wildcard else set(applicability.operations)
        )
        for suite_id, applicability in operations.items()
    }
    if declared_operations != EXPECTED_SUITE_OPERATIONS:
        fail("suite_registry operations must equal the accepted suite applicability")
    declared_proofs = {suite_id: entry["proof"] for suite_id, entry in registry.items()}
    if declared_proofs != EXPECTED_SUITE_PROOFS:
        fail("suite_registry proofs must equal the accepted proof semantics")
    sets: dict[str, list[str]] = {}
    for set_id, raw_members in raw_sets.items():
        members = require_string_list(raw_members, f"suite_sets.{set_id}")
        if not set(members) <= set(registry):
            fail(f"suite set {set_id} references an unknown suite")
        sets[set_id] = members
    if {set_id: set(members) for set_id, members in sets.items()} != (
        EXPECTED_SUITE_SETS
    ):
        fail("suite_sets must equal the accepted GPU and NPU proof memberships")
    return SuitePolicyValidation(registry=registry, sets=sets, operations=operations)


@dataclass(frozen=True)
class FallbackPolicyValidation:
    """Fail-safe fallback registry and normalized operation sets."""

    registry: dict[str, Any]
    sets: dict[str, dict[str, list[str]]]
    operations: dict[str, OperationApplicability]


def validate_fallback_policy_registries(
    inventory: dict[str, Any], vocabulary: Vocabulary
) -> FallbackPolicyValidation:
    registry = require_mapping(inventory.get("fallback_registry"), "fallback_registry")
    raw_sets = require_mapping(inventory.get("fallback_sets"), "fallback_sets")
    require_registry_keys(registry, "fallback_registry")
    require_registry_keys(raw_sets, "fallback_sets")
    for fallback_id, definition in registry.items():
        entry = require_mapping(definition, f"fallback_registry.{fallback_id}")
        require_exact_keys(
            entry,
            {"operations", "guard", "effect"},
            f"fallback_registry.{fallback_id}",
        )
        require_string(entry["guard"], f"fallback_registry.{fallback_id}.guard")
        require_string(entry["effect"], f"fallback_registry.{fallback_id}.effect")
    validate_fallback_semantics(registry)
    operations = {
        fallback_id: registry_member_operations(
            definition,
            f"fallback_registry.{fallback_id}",
            vocabulary.operations,
            allow_wildcard=False,
        )
        for fallback_id, definition in registry.items()
    }
    sets: dict[str, dict[str, list[str]]] = {}
    for set_id, raw_mapping in raw_sets.items():
        mapping = require_mapping(raw_mapping, f"fallback_sets.{set_id}")
        normalized: dict[str, list[str]] = {}
        for operation, raw_fallback_ids in mapping.items():
            if operation not in vocabulary.operations:
                fail(f"fallback set {set_id} has unknown operation {operation}")
            fallback_ids = require_string_list(
                raw_fallback_ids, f"fallback_sets.{set_id}.{operation}"
            )
            if not set(fallback_ids) <= set(registry):
                fail(f"fallback set {set_id} references an unknown fallback")
            for fallback_id in fallback_ids:
                if operation not in operations[fallback_id].operations:
                    fail(
                        f"fallback {fallback_id} does not apply to operation {operation}"
                    )
            normalized[operation] = fallback_ids
        sets[set_id] = normalized
    if sets != EXPECTED_FALLBACK_SETS:
        fail("fallback_sets must equal the accepted fail-safe memberships")
    return FallbackPolicyValidation(registry=registry, sets=sets, operations=operations)


def validate_policy_registry_stage(
    inventory: dict[str, Any], vocabulary: Vocabulary
) -> PolicyRegistryValidation:
    """Validate closed policy registries and normalize their references."""

    resources = validate_resource_policy_registries(inventory, vocabulary)
    suites = validate_suite_policy_registries(inventory, vocabulary)
    fallbacks = validate_fallback_policy_registries(inventory, vocabulary)
    return PolicyRegistryValidation(
        operation_sets=resources.operation_sets,
        constraint_profiles=resources.constraint_profiles,
        recovery_profiles=resources.recovery_profiles,
        suite_registry=suites.registry,
        suite_sets=suites.sets,
        fallback_registry=fallbacks.registry,
        fallback_sets=fallbacks.sets,
        suite_operations=suites.operations,
        fallback_operations=fallbacks.operations,
    )
