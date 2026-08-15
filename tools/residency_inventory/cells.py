"""Validate exact promoted-cell selectors and collect their registry uses."""

from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Any

from .contract import (
    CAPABILITY_RANK,
    MEMORY_CONSTRAINTS,
    MEMORY_CONSTRAINTS_BY_HARDWARE_TOPOLOGY,
    OPERATION_LEAVES_BY_TEMPLATE,
    OperationApplicability,
    SourceSupportKey,
    fail,
    require_exact_keys,
    require_list,
    require_mapping,
    require_string,
    require_string_list,
    require_unique,
)

EXPECTED_EXACT_FALLBACK_BY_GUARD = {
    "insufficient_capacity_authority": "hatchery_rocm_admission_refuse_unknown_capacity_v1",
    "valid_reporting_without_action_authority": "hatchery_rocm_pressure_report_only_v1",
    "invalid_reporting_evidence": "hatchery_rocm_pressure_disabled_invalid_evidence_v1",
    "insufficient_startup_authority": "hatchery_rocm_startup_block_group_v1",
    "unproven_release": "hatchery_rocm_recovery_block_readiness_v1",
}
EXPECTED_EXACT_CELL_COMMON_IDENTITY = {
    "base_variant": "llamacpp-rocm",
    "platform": "linux-amd-rocm-llamacpp",
    "backend_channel": "stable",
    "hardware_profile": "hatchery-gfx1151-shared-gtt-v1",
    "model_types": {"llm"},
    "configuration_profile": "profile-free-residency-estimation-v1-text-only",
    "workload_profile": "hatchery-text-generation-campaign-v1",
    "predictor_rule": "hatchery-llamacpp-rocm-profile-free-v1",
    "observation_contract": "hatchery-gtt-host-observation-v1",
    "recovery": "native_subprocess_tree",
    "constraints": {
        "gpu_shared_residency",
        "host_memavailable_floor",
        "model_type_pool",
        "ownership",
    },
    "evidence_ceiling": "validated",
    "capability_level": "unsupported",
    "delivery_state": "absent",
    "runtime_bindings": {
        "device_identity",
        "backend_artifact_digest",
        "source_build_dependency_closure",
        "driver_runtime_closure",
        "model_manifest_digest",
        "normalized_configuration_digest",
        "evidence_index_digest",
        "evidence_liveness_lease",
    },
}
EXPECTED_EXACT_CELL_IDENTITIES = {
    "H-ROCM-ADM-GTT-HOST-v1": {
        **EXPECTED_EXACT_CELL_COMMON_IDENTITY,
        "operation_template": "ADM",
        "operation_leaf": "admission",
        "campaign_gate_set": "hatchery_rocm_adm_v1",
        "fallbacks": {
            "insufficient_capacity_authority": (
                "hatchery_rocm_admission_refuse_unknown_capacity_v1"
            )
        },
    },
    "H-ROCM-PRE-GTT-HOST-v1": {
        **EXPECTED_EXACT_CELL_COMMON_IDENTITY,
        "operation_template": "PRE",
        "operation_leaf": "pressure_reclamation",
        "campaign_gate_set": "hatchery_rocm_pre_v1",
        "fallbacks": {
            "valid_reporting_without_action_authority": (
                "hatchery_rocm_pressure_report_only_v1"
            ),
            "invalid_reporting_evidence": (
                "hatchery_rocm_pressure_disabled_invalid_evidence_v1"
            ),
        },
    },
    "H-ROCM-STA-GTT-HOST-v1": {
        **EXPECTED_EXACT_CELL_COMMON_IDENTITY,
        "operation_template": "STA",
        "operation_leaf": "startup_load",
        "campaign_gate_set": "hatchery_rocm_sta_v1",
        "fallbacks": {
            "insufficient_startup_authority": ("hatchery_rocm_startup_block_group_v1")
        },
    },
    "H-ROCM-REC-GTT-HOST-OWN-v1": {
        **EXPECTED_EXACT_CELL_COMMON_IDENTITY,
        "operation_template": "REC",
        "operation_leaf": "prior_epoch_owner_cleanup",
        "campaign_gate_set": "hatchery_rocm_rec_v1",
        "fallbacks": {"unproven_release": "hatchery_rocm_recovery_block_readiness_v1"},
    },
}


@dataclass(frozen=True)
class ExactCellValidation:
    """Validated exact cells and the registries they consume."""

    exact_cells: list[dict[str, Any]]
    profile_uses: dict[str, set[str]]
    runtime_binding_uses: set[str]
    fallback_uses: set[str]


@dataclass(frozen=True)
class _ExactCellContext:
    """Registries and collection state shared while validating exact cells."""

    variants_by_id: dict[str, dict[str, Any]]
    platform_support: dict[str, dict[str, Any]]
    normalized_operation_sets: dict[str, set[str]]
    recoveries: dict[str, Any]
    fallback_registry: dict[str, Any]
    fallback_operations: dict[str, OperationApplicability]
    registries: dict[str, dict[str, Any]]
    runtime_binding_kinds: set[str]
    enum_capabilities: set[str]
    enum_delivery: set[str]
    enum_operation_leaves: set[str]
    enum_constraints: set[str]
    hardware_atoms: dict[str, SourceSupportKey]
    profile_uses: dict[str, set[str]]
    runtime_binding_uses: set[str]
    fallback_uses: set[str]


@dataclass(frozen=True)
class _ExactCellMatch:
    """Normalized identity values established by an exact-cell match."""

    base_variant_id: str
    base_variant: dict[str, Any]
    match: dict[str, Any]
    platform_id: str
    platform: dict[str, Any]
    backend_channel: str
    model_types: set[str]
    hardware_profile: dict[str, Any]
    recovery: str


@dataclass(frozen=True)
class _ExactCellOperation:
    """Normalized operation and constraint values for an exact cell."""

    template: str
    leaf: str
    constraints: set[str]


@dataclass(frozen=True)
class _ExactCellCapability:
    """Normalized capability and delivery values for an exact cell."""

    ceiling: str
    capability: str
    delivery: str


def _require_exact_cell(raw_cell: Any, index: int) -> tuple[dict[str, Any], str]:
    cell = require_mapping(raw_cell, f"exact_cells[{index}]")
    cell_id = require_string(cell.get("cell_id"), f"exact_cells[{index}].cell_id")
    require_exact_keys(
        cell,
        {
            "cell_id",
            "base_variant",
            "match",
            "operation_template",
            "operation_leaf",
            "constraints",
            "evidence_ceiling",
            "capability_level",
            "delivery_state",
            "runtime_bindings",
            "campaign_gate_set",
            "fallbacks",
            "scope",
            "promotion_target",
        },
        f"exact cell {cell_id}",
    )
    require_string(cell["scope"], f"exact cell {cell_id}.scope")
    require_string(cell["promotion_target"], f"exact cell {cell_id}.promotion_target")
    return cell, cell_id


def _validate_profile_references(
    context: _ExactCellContext,
    cell_id: str,
    match: dict[str, Any],
) -> None:
    for field, registry_name in (
        ("hardware_profile", "hardware_profiles"),
        ("configuration_profile", "configuration_profiles"),
        ("workload_profile", "workload_profiles"),
        ("predictor_rule", "predictor_rules"),
        ("observation_contract", "observation_contracts"),
    ):
        reference = require_string(match[field], f"exact cell {cell_id}.match.{field}")
        if reference not in context.registries[registry_name]:
            fail(f"exact cell {cell_id} references unknown {field} {reference}")
        context.profile_uses[registry_name].add(reference)


def _validate_hardware_match(
    context: _ExactCellContext,
    cell_id: str,
    match: dict[str, Any],
    base_variant: dict[str, Any],
    platform: dict[str, Any],
    backend_channel: str,
) -> dict[str, Any]:
    hardware_profile = context.registries["hardware_profiles"][
        match["hardware_profile"]
    ]
    hardware_atom = context.hardware_atoms[match["hardware_profile"]]
    atom_recipe, atom_backend, atom_os, atom_accelerator, atom_arch, atom_channel = (
        hardware_atom
    )
    if (atom_recipe, atom_backend) != (
        base_variant["recipe"],
        base_variant["backend"],
    ):
        fail(f"exact cell {cell_id} hardware atom differs from its base variant")
    if atom_channel != backend_channel:
        fail(f"exact cell {cell_id} backend channel differs from its hardware atom")
    if (
        platform["os"] != atom_os
        or platform["accelerator"] != atom_accelerator
        or atom_channel not in platform["channels"]
        or (
            "*" not in platform["architectures"]
            and atom_arch not in platform["architectures"]
        )
    ):
        fail(f"exact cell {cell_id} hardware atom is outside its platform predicate")
    return hardware_profile


def _validate_exact_cell_match(
    context: _ExactCellContext,
    cell: dict[str, Any],
    cell_id: str,
) -> _ExactCellMatch:
    base_variant_id = require_string(
        cell.get("base_variant"), f"exact cell {cell_id}.base_variant"
    )
    if base_variant_id not in context.variants_by_id:
        fail(f"exact cell {cell_id} references unknown base variant")
    base_variant = context.variants_by_id[base_variant_id]
    match = require_mapping(cell.get("match"), f"exact cell {cell_id}.match")
    require_exact_keys(
        match,
        {
            "platform",
            "backend_channel",
            "hardware_profile",
            "model_types",
            "configuration_profile",
            "workload_profile",
            "predictor_rule",
            "observation_contract",
            "recovery",
        },
        f"exact cell {cell_id}.match",
    )
    platform_id = require_string(match["platform"], f"exact cell {cell_id}.platform")
    if platform_id not in base_variant["platforms"]:
        fail(f"exact cell {cell_id} platform is outside its base variant")
    platform = context.platform_support[platform_id]
    backend_channel = require_string(
        match["backend_channel"], f"exact cell {cell_id}.backend_channel"
    )
    if backend_channel not in platform["channels"]:
        fail(f"exact cell {cell_id} backend channel is outside its platform")
    model_types = set(
        require_string_list(
            match["model_types"], f"exact cell {cell_id}.match.model_types"
        )
    )
    if not model_types <= set(base_variant["model_types"]):
        fail(f"exact cell {cell_id} model types exceed its base variant")
    _validate_profile_references(context, cell_id, match)
    hardware_profile = _validate_hardware_match(
        context,
        cell_id,
        match,
        base_variant,
        platform,
        backend_channel,
    )
    configuration_profile = context.registries["configuration_profiles"][
        match["configuration_profile"]
    ]
    if model_types != set(configuration_profile["model_types"]):
        fail(f"exact cell {cell_id} model types must equal its configuration profile")
    recovery = require_string(match["recovery"], f"exact cell {cell_id}.match.recovery")
    if recovery not in context.recoveries:
        fail(f"exact cell {cell_id} references unknown recovery {recovery}")
    if recovery != base_variant["recovery"]:
        fail(f"exact cell {cell_id} recovery differs from its base variant")
    return _ExactCellMatch(
        base_variant_id,
        base_variant,
        match,
        platform_id,
        platform,
        backend_channel,
        model_types,
        hardware_profile,
        recovery,
    )


def _validate_exact_cell_operation(
    context: _ExactCellContext,
    cell: dict[str, Any],
    cell_id: str,
    match: _ExactCellMatch,
) -> _ExactCellOperation:
    operation_template = require_string(
        cell.get("operation_template"), f"exact cell {cell_id}.operation_template"
    )
    if (
        operation_template
        not in context.normalized_operation_sets[match.base_variant["operations"]]
    ):
        fail(f"exact cell {cell_id} operation is outside its base variant")
    operation_leaf = require_string(
        cell.get("operation_leaf"), f"exact cell {cell_id}.operation_leaf"
    )
    if operation_leaf not in context.enum_operation_leaves:
        fail(f"exact cell {cell_id} references unknown operation leaf")
    if operation_leaf not in OPERATION_LEAVES_BY_TEMPLATE[operation_template]:
        fail(
            f"exact cell {cell_id} operation leaf does not belong to "
            f"template {operation_template}"
        )
    constraints = set(
        require_string_list(
            cell.get("constraints"), f"exact cell {cell_id}.constraints"
        )
    )
    if not constraints <= context.enum_constraints:
        fail(f"exact cell {cell_id} references an unknown constraint")
    _validate_exact_cell_constraints(context, cell_id, match, constraints)
    return _ExactCellOperation(operation_template, operation_leaf, constraints)


def _validate_exact_cell_constraints(
    context: _ExactCellContext,
    cell_id: str,
    match: _ExactCellMatch,
    constraints: set[str],
) -> None:
    observation_contract = context.registries["observation_contracts"][
        match.match["observation_contract"]
    ]
    expected_constraints = set(observation_contract["constraints"]) | {
        "model_type_pool",
        "ownership",
    }
    if constraints != expected_constraints:
        fail(
            f"exact cell {cell_id} constraints must equal its observation "
            "constraints plus model_type_pool and ownership"
        )
    hardware_topology = match.hardware_profile["topology"]
    if hardware_topology not in MEMORY_CONSTRAINTS_BY_HARDWARE_TOPOLOGY:
        fail(
            f"exact cell {cell_id} hardware topology has no closed "
            "memory-constraint mapping"
        )
    if (
        constraints & MEMORY_CONSTRAINTS
        != MEMORY_CONSTRAINTS_BY_HARDWARE_TOPOLOGY[hardware_topology]
    ):
        fail(
            f"exact cell {cell_id} memory constraints do not match its "
            "hardware topology"
        )


def _validate_exact_cell_capability(
    context: _ExactCellContext,
    cell: dict[str, Any],
    cell_id: str,
) -> _ExactCellCapability:
    ceiling = require_string(
        cell.get("evidence_ceiling"), f"exact cell {cell_id}.evidence_ceiling"
    )
    capability = require_string(
        cell.get("capability_level"), f"exact cell {cell_id}.capability_level"
    )
    delivery = require_string(
        cell.get("delivery_state"), f"exact cell {cell_id}.delivery_state"
    )
    if (
        ceiling not in context.enum_capabilities
        or capability not in context.enum_capabilities
    ):
        fail(f"exact cell {cell_id} has an unknown capability value")
    if delivery not in context.enum_delivery:
        fail(f"exact cell {cell_id} has an unknown delivery state")
    if CAPABILITY_RANK[capability] > CAPABILITY_RANK[ceiling]:
        fail(f"exact cell {cell_id} capability exceeds its evidence ceiling")
    if delivery == "absent" and capability != "unsupported":
        fail(f"exact cell {cell_id} cannot accept capability while delivery is absent")
    return _ExactCellCapability(ceiling, capability, delivery)


def _validate_exact_cell_runtime_bindings(
    context: _ExactCellContext,
    cell: dict[str, Any],
    cell_id: str,
    hardware_profile: dict[str, Any],
) -> list[str]:
    runtime_bindings = require_string_list(
        cell.get("runtime_bindings"), f"exact cell {cell_id}.runtime_bindings"
    )
    if set(runtime_bindings) != context.runtime_binding_kinds:
        fail(f"exact cell {cell_id} must bind every required runtime kind")
    context.runtime_binding_uses.update(runtime_bindings)
    if not set(hardware_profile["required_runtime_bindings"]) <= set(runtime_bindings):
        fail(f"exact cell {cell_id} omits a hardware-profile runtime binding")
    return runtime_bindings


def _validate_exact_cell_fallbacks(
    context: _ExactCellContext,
    cell: dict[str, Any],
    cell_id: str,
    operation_template: str,
) -> tuple[str, dict[str, Any]]:
    gate_set = require_string(
        cell.get("campaign_gate_set"), f"exact cell {cell_id}.campaign_gate_set"
    )
    cell_fallbacks = require_mapping(
        cell.get("fallbacks"), f"exact cell {cell_id}.fallbacks"
    )
    if not cell_fallbacks:
        fail(f"exact cell {cell_id} must define guarded fallbacks")
    normalized_fallbacks: dict[str, str] = {}
    for guard, fallback_value in cell_fallbacks.items():
        guard_id = require_string(guard, f"exact cell {cell_id} fallback guard")
        fallback_id = require_string(
            fallback_value, f"exact cell {cell_id}.fallbacks.{guard_id}"
        )
        normalized_fallbacks[guard_id] = fallback_id
    require_unique(
        list(normalized_fallbacks.values()), f"exact cell {cell_id} fallback IDs"
    )
    for guard, fallback_id in normalized_fallbacks.items():
        if fallback_id not in context.fallback_registry:
            fail(f"exact cell {cell_id} references unknown fallback {fallback_id}")
        if EXPECTED_EXACT_FALLBACK_BY_GUARD.get(guard) != fallback_id:
            fail(
                f"exact cell {cell_id} fallback guard {guard} must use its "
                "accepted fallback"
            )
        context.fallback_uses.add(fallback_id)
        if (
            operation_template
            not in context.fallback_operations[fallback_id].operations
        ):
            fail(
                f"exact cell {cell_id} fallback {fallback_id} does not apply "
                f"to {operation_template}"
            )
    return gate_set, normalized_fallbacks


def _require_accepted_exact_cell_identity(
    cell_id: str,
    match: _ExactCellMatch,
    operation: _ExactCellOperation,
    capability: _ExactCellCapability,
    runtime_bindings: list[str],
    gate_set: str,
    fallbacks: dict[str, Any],
) -> None:
    identity = {
        "base_variant": match.base_variant_id,
        "platform": match.platform_id,
        "backend_channel": match.backend_channel,
        "hardware_profile": match.match["hardware_profile"],
        "model_types": match.model_types,
        "configuration_profile": match.match["configuration_profile"],
        "workload_profile": match.match["workload_profile"],
        "predictor_rule": match.match["predictor_rule"],
        "observation_contract": match.match["observation_contract"],
        "recovery": match.recovery,
        "operation_template": operation.template,
        "operation_leaf": operation.leaf,
        "constraints": operation.constraints,
        "evidence_ceiling": capability.ceiling,
        "capability_level": capability.capability,
        "delivery_state": capability.delivery,
        "runtime_bindings": set(runtime_bindings),
        "campaign_gate_set": gate_set,
        "fallbacks": fallbacks,
    }
    if EXPECTED_EXACT_CELL_IDENTITIES.get(cell_id) != identity:
        fail(f"exact cell {cell_id} must equal its accepted identity selector")


def _exact_cell_match_key(
    match: _ExactCellMatch,
    operation: _ExactCellOperation,
) -> str:
    return json.dumps(
        {
            "base_variant": match.base_variant_id,
            "match": {**match.match, "model_types": sorted(match.model_types)},
            "operation_template": operation.template,
            "operation_leaf": operation.leaf,
            "constraints": sorted(operation.constraints),
        },
        sort_keys=True,
        separators=(",", ":"),
    )


def _validate_exact_cell(
    context: _ExactCellContext,
    raw_cell: Any,
    index: int,
) -> tuple[str, str]:
    cell, cell_id = _require_exact_cell(raw_cell, index)
    match = _validate_exact_cell_match(context, cell, cell_id)
    operation = _validate_exact_cell_operation(context, cell, cell_id, match)
    capability = _validate_exact_cell_capability(context, cell, cell_id)
    runtime_bindings = _validate_exact_cell_runtime_bindings(
        context, cell, cell_id, match.hardware_profile
    )
    gate_set, fallbacks = _validate_exact_cell_fallbacks(
        context, cell, cell_id, operation.template
    )
    _require_accepted_exact_cell_identity(
        cell_id,
        match,
        operation,
        capability,
        runtime_bindings,
        gate_set,
        fallbacks,
    )
    return cell_id, _exact_cell_match_key(match, operation)


def validate_exact_cells(
    inventory: dict[str, Any],
    *,
    variants_by_id: dict[str, dict[str, Any]],
    platform_support: dict[str, dict[str, Any]],
    normalized_operation_sets: dict[str, set[str]],
    recoveries: dict[str, Any],
    fallback_registry: dict[str, Any],
    fallback_operations: dict[str, OperationApplicability],
    registries: dict[str, dict[str, Any]],
    runtime_binding_kinds: set[str],
    enum_capabilities: set[str],
    enum_delivery: set[str],
    enum_operation_leaves: set[str],
    enum_constraints: set[str],
    hardware_atoms: dict[str, SourceSupportKey],
) -> ExactCellValidation:
    exact_cells = require_list(inventory.get("exact_cells"), "exact_cells")
    profile_uses = {
        "hardware_profiles": set(),
        "configuration_profiles": set(),
        "workload_profiles": set(),
        "predictor_rules": set(),
        "observation_contracts": set(),
    }
    runtime_binding_uses: set[str] = set()
    fallback_uses: set[str] = set()
    context = _ExactCellContext(
        variants_by_id,
        platform_support,
        normalized_operation_sets,
        recoveries,
        fallback_registry,
        fallback_operations,
        registries,
        runtime_binding_kinds,
        enum_capabilities,
        enum_delivery,
        enum_operation_leaves,
        enum_constraints,
        hardware_atoms,
        profile_uses,
        runtime_binding_uses,
        fallback_uses,
    )
    validated = [
        _validate_exact_cell(context, raw_cell, index)
        for index, raw_cell in enumerate(exact_cells)
    ]
    cell_ids = [cell_id for cell_id, _match_key in validated]
    match_keys = [match_key for _cell_id, match_key in validated]
    require_unique(cell_ids, "exact cell ids")
    require_unique(match_keys, "exact cell match keys")
    if set(cell_ids) != set(EXPECTED_EXACT_CELL_IDENTITIES):
        fail("exact_cells must contain exactly the accepted durable cell IDs")
    return ExactCellValidation(
        exact_cells=exact_cells,
        profile_uses=profile_uses,
        runtime_binding_uses=runtime_binding_uses,
        fallback_uses=fallback_uses,
    )
