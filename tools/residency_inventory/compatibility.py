"""Validate evidence-only NPU/FLM compatibility promotion contracts."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from .contract import (
    MEMORY_CONSTRAINTS,
    OperationApplicability,
    fail,
    require_exact_keys,
    require_list,
    require_mapping,
    require_string,
    require_string_list,
    require_unique,
)

EXPECTED_COMPATIBILITY_FALLBACKS = {
    "insufficient_displacement_authority": "residency_npu_conflict_preserve_refuse_v1"
}
EXPECTED_COMPATIBILITY_CONTRACT_ID = "H-NPU-FLM-CONFLICT-XDNA2-v1"
EXPECTED_NPU_CONSTRAINT_PROFILES = {
    "npu_flm": {"flm_type_slot", "npu_cross_family", "model_type_pool", "ownership"},
    "npu_exclusive": {
        "npu_exclusive",
        "npu_cross_family",
        "model_type_pool",
        "ownership",
    },
}
EXPECTED_COEXIST_VARIANTS = {"flm-npu"}
EXPECTED_EXCLUSIVE_VARIANTS = {"whispercpp-npu", "ryzenai-llm-npu"}
EXPECTED_COMPATIBILITY_CONSTRAINTS = set().union(
    *EXPECTED_NPU_CONSTRAINT_PROFILES.values()
)
EXPECTED_RELATION_CONSTRAINTS = {"npu_cross_family"}


@dataclass(frozen=True)
class CompatibilityValidation:
    """Validated compatibility contracts and their referenced registries."""

    contracts: list[dict[str, Any]]
    fallback_uses: set[str]


@dataclass(frozen=True)
class _CompatibilityContext:
    """Registries and derived expectations for one compatibility contract."""

    expected_cases: set[tuple[str, str, str]]
    enum_constraints: set[str]
    fallback_registry: dict[str, Any]
    fallback_operations: dict[str, OperationApplicability]
    normalized_suite_sets: dict[str, list[str]]
    fallback_uses: set[str]


def expected_platform_cases(
    variants: list[dict[str, Any]],
    constraint_profiles: dict[str, list[str]],
) -> tuple[set[tuple[str, str, str]], set[str]]:
    coexist_variants = [
        variant
        for variant in variants
        if "flm_type_slot" in constraint_profiles[variant["constraints"]]
    ]
    exclusive_variants = [
        variant
        for variant in variants
        if "npu_exclusive" in constraint_profiles[variant["constraints"]]
    ]
    coexist_ids = {variant["id"] for variant in coexist_variants}
    exclusive_ids = {variant["id"] for variant in exclusive_variants}
    if coexist_ids != EXPECTED_COEXIST_VARIANTS:
        fail("FLM compatibility coexist role must be exactly flm-npu")
    if exclusive_ids != EXPECTED_EXCLUSIVE_VARIANTS:
        fail(
            "exclusive NPU compatibility roles must be exactly whispercpp-npu "
            "and ryzenai-llm-npu"
        )
    expected: set[tuple[str, str, str]] = set()
    constraints: set[str] = set()
    for coexist_variant in coexist_variants:
        for exclusive_variant in exclusive_variants:
            common_platforms = set(coexist_variant["platforms"]) & set(
                exclusive_variant["platforms"]
            )
            for platform_id in common_platforms:
                expected.add(
                    (platform_id, coexist_variant["id"], exclusive_variant["id"])
                )
                constraints.update(constraint_profiles[coexist_variant["constraints"]])
                constraints.update(
                    constraint_profiles[exclusive_variant["constraints"]]
                )
    if len(expected) != 2:
        fail(
            "frozen variant intersections must derive exactly two NPU/FLM "
            "compatibility cases"
        )
    return expected, constraints


def _require_compatibility_scope(
    inventory: dict[str, Any],
    variants: list[dict[str, Any]],
    constraint_profiles: dict[str, list[str]],
) -> tuple[list[Any], set[tuple[str, str, str]]]:
    raw_contracts = require_list(
        inventory.get("compatibility_contracts"), "compatibility_contracts"
    )
    if len(raw_contracts) != 1:
        fail("compatibility_contracts must contain one derived NPU/FLM contract")
    for profile_id, expected_constraints in EXPECTED_NPU_CONSTRAINT_PROFILES.items():
        if set(constraint_profiles.get(profile_id, [])) != expected_constraints:
            fail(
                f"constraint profile {profile_id} must equal its accepted NPU contract"
            )
    expected_cases, expected_constraints = expected_platform_cases(
        variants, constraint_profiles
    )
    if expected_constraints != EXPECTED_COMPATIBILITY_CONSTRAINTS:
        fail("NPU compatibility constraints must equal the accepted closed union")
    if expected_constraints & MEMORY_CONSTRAINTS:
        fail("NPU compatibility profiles may not declare memory-capacity constraints")
    return raw_contracts, expected_cases


def _require_compatibility_contract(
    raw_contract: Any,
    index: int,
) -> tuple[dict[str, Any], str]:
    contract = require_mapping(raw_contract, f"compatibility_contracts[{index}]")
    contract_id = require_string(
        contract.get("contract_id"),
        f"compatibility_contracts[{index}].contract_id",
    )
    if contract_id != EXPECTED_COMPATIBILITY_CONTRACT_ID:
        fail(
            "compatibility contract ID must equal the accepted durable "
            f"ID {EXPECTED_COMPATIBILITY_CONTRACT_ID}"
        )
    require_exact_keys(
        contract,
        {
            "contract_id",
            "scope",
            "platform_cases",
            "directions",
            "incumbent_states",
            "model_type_coverage",
            "operation_template",
            "operation_leaf",
            "relation_constraints",
            "evidence_ceiling",
            "capability_level",
            "delivery_state",
            "runtime_authority",
            "evidence_mode",
            "suite_set",
            "campaign_gate_set",
            "promotion_target",
            "fallbacks",
        },
        f"compatibility contract {contract_id}",
    )
    require_string(contract["scope"], f"compatibility contract {contract_id}.scope")
    return contract, contract_id


def _validate_platform_cases(
    contract: dict[str, Any],
    contract_id: str,
    expected_cases: set[tuple[str, str, str]],
) -> None:
    platform_cases = require_list(
        contract["platform_cases"],
        f"compatibility contract {contract_id}.platform_cases",
    )
    normalized_cases: list[tuple[str, str, str]] = []
    for case_index, raw_case in enumerate(platform_cases):
        case = require_mapping(
            raw_case,
            f"compatibility contract {contract_id}.platform_cases[{case_index}]",
        )
        require_exact_keys(
            case,
            {"platform", "coexist_by_type_variant", "exclusive_variant"},
            f"compatibility contract {contract_id} platform case",
        )
        normalized_cases.append(
            (
                require_string(case["platform"], "compatibility platform"),
                require_string(
                    case["coexist_by_type_variant"], "coexist-by-type variant"
                ),
                require_string(case["exclusive_variant"], "exclusive variant"),
            )
        )
    if len(normalized_cases) != len(set(normalized_cases)):
        fail(f"compatibility contract {contract_id} repeats a platform case")
    if set(normalized_cases) != expected_cases:
        fail(
            f"compatibility contract {contract_id} cases do not match "
            "the source-supported variant intersections"
        )


def _validate_participant_coverage(
    contract: dict[str, Any],
    contract_id: str,
) -> None:
    directions = set(
        require_string_list(
            contract["directions"],
            f"compatibility contract {contract_id}.directions",
        )
    )
    if directions != {"coexist_by_type_incoming", "exclusive_incoming"}:
        fail(f"compatibility contract {contract_id} must cover both directions")
    incumbent_states = set(
        require_string_list(
            contract["incumbent_states"],
            f"compatibility contract {contract_id}.incumbent_states",
        )
    )
    if incumbent_states != {"unpinned_idle", "pinned", "in_use"}:
        fail(f"compatibility contract {contract_id} must cover all incumbent states")


def _validate_required_scalars(
    contract: dict[str, Any],
    contract_id: str,
) -> None:
    required_scalars = {
        "model_type_coverage": "all_declared_by_participant",
        "operation_template": "NPC",
        "operation_leaf": "admission",
        "evidence_ceiling": "modeled",
        "capability_level": "unsupported",
        "delivery_state": "absent",
        "runtime_authority": "none",
        "evidence_mode": "synthetic_only",
        "suite_set": "npu_compatibility",
        "promotion_target": "modeled",
    }
    for field, expected in required_scalars.items():
        actual = require_string(
            contract[field], f"compatibility contract {contract_id}.{field}"
        )
        if actual != expected:
            fail(f"compatibility contract {contract_id}.{field} must be {expected}")


def _validate_relation_constraints(
    context: _CompatibilityContext,
    contract: dict[str, Any],
    contract_id: str,
) -> None:
    relation_constraints = set(
        require_string_list(
            contract["relation_constraints"],
            f"compatibility contract {contract_id}.relation_constraints",
        )
    )
    if not relation_constraints <= context.enum_constraints:
        fail(f"compatibility contract {contract_id} has unknown relation constraints")
    if relation_constraints != EXPECTED_RELATION_CONSTRAINTS:
        fail(
            f"compatibility contract {contract_id} relation constraints must "
            "equal the accepted cross-family relation"
        )


def _validate_compatibility_fallbacks(
    context: _CompatibilityContext,
    contract: dict[str, Any],
    contract_id: str,
) -> None:
    fallbacks = require_mapping(
        contract["fallbacks"],
        f"compatibility contract {contract_id}.fallbacks",
    )
    if not fallbacks:
        fail(f"compatibility contract {contract_id} must define a fallback")
    if fallbacks != EXPECTED_COMPATIBILITY_FALLBACKS:
        fail(
            f"compatibility contract {contract_id} must use the accepted "
            "displacement-authority fallback"
        )
    fallback_ids = [
        require_string(value, f"compatibility contract {contract_id} fallback")
        for value in fallbacks.values()
    ]
    require_unique(fallback_ids, f"compatibility contract {contract_id} fallbacks")
    for fallback_id in fallback_ids:
        if fallback_id not in context.fallback_registry:
            fail(
                f"compatibility contract {contract_id} references unknown fallback "
                f"{fallback_id}"
            )
        if "NPC" not in context.fallback_operations[fallback_id].operations:
            fail(
                f"compatibility contract {contract_id} fallback {fallback_id} "
                "does not apply to NPC"
            )
        context.fallback_uses.add(fallback_id)


def _validate_compatibility_gate_bindings(
    context: _CompatibilityContext,
    contract: dict[str, Any],
    contract_id: str,
) -> None:
    require_string(
        contract["campaign_gate_set"],
        f"compatibility contract {contract_id}.campaign_gate_set",
    )
    suite_set_id = require_string(
        contract["suite_set"],
        f"compatibility contract {contract_id}.suite_set",
    )
    if suite_set_id not in context.normalized_suite_sets:
        fail(
            f"compatibility contract {contract_id} references unknown suite set "
            f"{suite_set_id}"
        )


def _validate_compatibility_contract(
    context: _CompatibilityContext,
    raw_contract: Any,
    index: int,
) -> tuple[dict[str, Any], str]:
    contract, contract_id = _require_compatibility_contract(raw_contract, index)
    _validate_platform_cases(contract, contract_id, context.expected_cases)
    _validate_participant_coverage(contract, contract_id)
    _validate_required_scalars(contract, contract_id)
    _validate_relation_constraints(context, contract, contract_id)
    _validate_compatibility_fallbacks(context, contract, contract_id)
    _validate_compatibility_gate_bindings(context, contract, contract_id)
    return contract, contract_id


def validate_compatibility_contracts(
    inventory: dict[str, Any],
    *,
    variants: list[dict[str, Any]],
    constraint_profiles: dict[str, list[str]],
    enum_constraints: set[str],
    fallback_registry: dict[str, Any],
    fallback_operations: dict[str, OperationApplicability],
    normalized_suite_sets: dict[str, list[str]],
    exact_cell_ids: set[str],
) -> CompatibilityValidation:
    raw_contracts, expected_cases = _require_compatibility_scope(
        inventory, variants, constraint_profiles
    )
    fallback_uses: set[str] = set()
    context = _CompatibilityContext(
        expected_cases,
        enum_constraints,
        fallback_registry,
        fallback_operations,
        normalized_suite_sets,
        fallback_uses,
    )
    validated = [
        _validate_compatibility_contract(context, raw_contract, index)
        for index, raw_contract in enumerate(raw_contracts)
    ]
    contracts = [contract for contract, _contract_id in validated]
    contract_ids = [contract_id for _contract, contract_id in validated]
    require_unique(contract_ids, "compatibility contract ids")
    overlap = exact_cell_ids & set(contract_ids)
    if overlap:
        fail(f"promotion IDs overlap across cells and contracts: {sorted(overlap)}")
    return CompatibilityValidation(contracts, fallback_uses)
