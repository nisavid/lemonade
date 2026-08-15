"""Validate the closed later-cell implementation and qualification roster."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import PurePosixPath
from typing import Any

from .contract import (
    OPERATION_LEAVES_BY_TEMPLATE,
    OperationApplicability,
    fail,
    require_exact_keys,
    require_list,
    require_mapping,
    require_string,
    require_string_list,
    require_unique,
)

VULKAN_MATERIAL_PROFILES = {
    "hardware_profile": "hatchery-gfx1151-vulkan-shared-gtt-v1",
    "configuration_profile": "profile-free-residency-estimation-vulkan-v1-text-only",
    "workload_profile": "hatchery-vulkan-text-generation-campaign-v1",
    "predictor_rule": "hatchery-llamacpp-vulkan-profile-free-v1",
    "observation_contract": "hatchery-vulkan-gtt-host-observation-v1",
}
PROFILE_REGISTRY_BY_FIELD = {
    "hardware_profile": "hardware_profiles",
    "configuration_profile": "configuration_profiles",
    "workload_profile": "workload_profiles",
    "predictor_rule": "predictor_rules",
    "observation_contract": "observation_contracts",
}
VULKAN_CONSTRAINTS = [
    "gpu_shared_residency",
    "host_memavailable_floor",
    "model_type_pool",
    "ownership",
]
NPU_RELATION_CONTRACTS = ["H-NPU-FLM-CONFLICT-XDNA2-v1"]
INITIAL_STATE = {
    "capability_level": "unsupported",
    "delivery_state": "absent",
}
WINDOWS_OPERATION_LEAVES = {
    "ADM": ["admission"],
    "LFR": ["admission"],
    "STA": ["startup_load"],
    "REC": [
        "service_termination",
        "dead_backend_pruning",
        "same_epoch_recovery_cleanup",
        "prior_epoch_owner_cleanup",
        "artifact_scope_recovery_cleanup",
    ],
    "UNL": ["explicit_unload", "force_unload"],
    "PIN": [
        "saved_pin_mutation",
        "runtime_pin_mutation",
        "legacy_pin_batch",
        "resident_state_recovery_cleanup",
    ],
}
FALLBACKS_BY_OPERATION = {
    "ADM": {
        "insufficient_capacity_authority": (
            "residency_admission_refuse_unknown_demand_v1"
        )
    },
    "LFR": {
        "unproven_victim_set": ("residency_load_retry_refuse_unproven_victim_set_v1")
    },
    "PRE": {
        "valid_reporting_without_action_authority": (
            "residency_pressure_report_only_unvalidated_v1"
        ),
        "invalid_reporting_evidence": (
            "residency_pressure_disabled_invalid_evidence_v1"
        ),
    },
    "STA": {"insufficient_startup_authority": "residency_startup_block_group_v1"},
    "REC": {"unproven_release": "residency_recovery_block_unproven_release_v1"},
    "UNL": {"live_use_unfenced": "residency_unload_preserve_live_use_v1"},
    "PIN": {"stale_or_unready": "residency_pin_mutation_refuse_stale_or_unready_v1"},
}
WINDOWS_PARTICIPANTS = (
    (
        "FLM-NPU-LLM",
        "flm-npu",
        "llm",
        "flm_system_managed",
        "npu_flm",
        "flm_npu",
    ),
    (
        "FLM-NPU-EMBEDDING",
        "flm-npu",
        "embedding",
        "flm_system_managed",
        "npu_flm",
        "flm_npu",
    ),
    (
        "FLM-NPU-TRANSCRIPTION",
        "flm-npu",
        "transcription",
        "flm_system_managed",
        "npu_flm",
        "flm_npu",
    ),
    (
        "WHISPERCPP-NPU-TRANSCRIPTION",
        "whispercpp-npu",
        "transcription",
        "native_subprocess_tree",
        "npu_exclusive",
        "whispercpp_npu",
    ),
    (
        "RYZENAI-LLM-NPU-LLM",
        "ryzenai-llm-npu",
        "llm",
        "native_subprocess_tree",
        "npu_exclusive",
        "ryzenai_llm_npu",
    ),
)


def _expected_roots(unit_id: str, stem: str) -> dict[str, str]:
    return {
        "implementation": f"src/cpp/server/residency/later/{stem}",
        "tests": f"test/residency/later/{stem}",
        "outputs": f"plan/evidence/later-promotion/{unit_id}",
    }


def _base_identity(
    unit_id: str,
    *,
    selector: dict[str, Any],
    material_profiles: dict[str, str],
    constraints: list[str],
    recovery: str,
    fallbacks: dict[str, str],
    evidence_gate_set: str,
    compatibility_contracts: list[str],
    expected_roots: dict[str, str],
) -> dict[str, Any]:
    return {
        "unit_id": unit_id,
        "initial_state": dict(INITIAL_STATE),
        "evidence_ceiling": "modeled",
        "selector": selector,
        "material_profiles": material_profiles,
        "constraints": constraints,
        "recovery": recovery,
        "fallbacks": fallbacks,
        "delivery_gate": f"release_verified:{unit_id}",
        "evidence_gate_set": evidence_gate_set,
        "compatibility_contracts": compatibility_contracts,
        "expected_roots": expected_roots,
    }


def _accepted_vulkan_identities() -> list[dict[str, Any]]:
    identities: list[dict[str, Any]] = []
    for operation, leaf in (
        ("ADM", "admission"),
        ("PRE", "pressure_reclamation"),
        ("STA", "startup_load"),
        ("REC", "prior_epoch_owner_cleanup"),
    ):
        ownership_suffix = "-OWN" if operation == "REC" else ""
        unit_id = f"H-VULKAN-{operation}-GTT-HOST{ownership_suffix}-v1"
        operation_path = operation.casefold()
        identities.append(
            _base_identity(
                unit_id,
                selector={
                    "base_variant": "llamacpp-vulkan",
                    "platform": "linux-amd-vulkan",
                    "backend_channel": "single",
                    "model_type": "llm",
                    "operation_template": operation,
                    "operation_leaves": [leaf],
                },
                material_profiles=dict(VULKAN_MATERIAL_PROFILES),
                constraints=list(VULKAN_CONSTRAINTS),
                recovery="native_subprocess_tree",
                fallbacks=dict(FALLBACKS_BY_OPERATION[operation]),
                evidence_gate_set=f"hatchery_vulkan_{operation_path}_v1",
                compatibility_contracts=[],
                expected_roots=_expected_roots(
                    unit_id, f"hatchery_vulkan/{operation_path}"
                ),
            )
        )
    return identities


def _accepted_windows_identities(
    constraint_profiles: dict[str, list[str]],
) -> list[dict[str, Any]]:
    identities: list[dict[str, Any]] = []
    for (
        unit_stem,
        base_variant,
        model_type,
        recovery,
        constraint_profile,
        path_participant,
    ) in WINDOWS_PARTICIPANTS:
        for operation, operation_leaves in WINDOWS_OPERATION_LEAVES.items():
            unit_id = f"W-XDNA2-{unit_stem}-{operation}-v1"
            operation_path = operation.casefold()
            stem = f"windows_xdna2/{path_participant}/{model_type}/{operation_path}"
            identities.append(
                _base_identity(
                    unit_id,
                    selector={
                        "base_variant": base_variant,
                        "platform": "windows-xdna2",
                        "backend_channel": "single",
                        "model_type": model_type,
                        "operation_template": operation,
                        "operation_leaves": list(operation_leaves),
                    },
                    material_profiles={},
                    constraints=list(constraint_profiles[constraint_profile]),
                    recovery=recovery,
                    fallbacks=dict(FALLBACKS_BY_OPERATION[operation]),
                    evidence_gate_set=(
                        f"windows_xdna2_{path_participant}_{model_type}_"
                        f"{operation_path}_v1"
                    ),
                    compatibility_contracts=list(NPU_RELATION_CONTRACTS),
                    expected_roots=_expected_roots(unit_id, stem),
                )
            )
    return identities


@dataclass(frozen=True)
class LaterRosterValidation:
    """Closed later promotion units and their registry uses."""

    units: list[dict[str, Any]]
    profile_uses: dict[str, set[str]]
    fallback_uses: set[str]


def _require_safe_root(value: Any, label: str) -> str:
    root = require_string(value, label)
    path = PurePosixPath(root)
    if (
        "\\" in root
        or path.is_absolute()
        or any(part in {"", ".", ".."} for part in path.parts)
    ):
        fail(f"{label} must be a safe repository-relative root")
    return root


def _normalize_later_unit(raw_unit: Any, index: int) -> tuple[dict[str, Any], Any]:
    label = f"later_promotion_roster[{index}]"
    unit = require_mapping(raw_unit, label)
    require_exact_keys(
        unit,
        {
            "unit_id",
            "issue_id",
            "selector",
            "material_profiles",
            "constraints",
            "recovery",
            "fallbacks",
            "initial_state",
            "evidence_ceiling",
            "delivery_gate",
            "evidence_gate_set",
            "compatibility_contracts",
            "expected_roots",
        },
        label,
    )
    unit_id = require_string(unit["unit_id"], f"{label}.unit_id")
    issue_id = unit["issue_id"]
    if issue_id is not None and (
        isinstance(issue_id, bool) or not isinstance(issue_id, int) or issue_id <= 0
    ):
        fail(
            f"later promotion unit {unit_id}.issue_id must be null or a positive integer"
        )

    selector = require_mapping(
        unit["selector"], f"later promotion unit {unit_id}.selector"
    )
    require_exact_keys(
        selector,
        {
            "base_variant",
            "platform",
            "backend_channel",
            "model_type",
            "operation_template",
            "operation_leaves",
        },
        f"later promotion unit {unit_id}.selector",
    )
    normalized_selector = {
        field: require_string(
            selector[field], f"later promotion unit {unit_id}.selector.{field}"
        )
        for field in (
            "base_variant",
            "platform",
            "backend_channel",
            "model_type",
            "operation_template",
        )
    }
    normalized_selector["operation_leaves"] = require_string_list(
        selector["operation_leaves"],
        f"later promotion unit {unit_id}.selector.operation_leaves",
    )

    material_profiles = require_mapping(
        unit["material_profiles"], f"later promotion unit {unit_id}.material_profiles"
    )
    normalized_material_profiles = {
        key: require_string(
            value, f"later promotion unit {unit_id}.material_profiles.{key}"
        )
        for key, value in material_profiles.items()
    }
    constraints = require_string_list(
        unit["constraints"], f"later promotion unit {unit_id}.constraints"
    )
    recovery = require_string(
        unit["recovery"], f"later promotion unit {unit_id}.recovery"
    )
    fallbacks = require_mapping(
        unit["fallbacks"], f"later promotion unit {unit_id}.fallbacks"
    )
    normalized_fallbacks = {
        require_string(
            guard, f"later promotion unit {unit_id} fallback guard"
        ): require_string(
            fallback_id,
            f"later promotion unit {unit_id}.fallbacks.{guard}",
        )
        for guard, fallback_id in fallbacks.items()
    }
    if not normalized_fallbacks:
        fail(f"later promotion unit {unit_id}.fallbacks must not be empty")

    initial_state = require_mapping(
        unit["initial_state"], f"later promotion unit {unit_id}.initial_state"
    )
    require_exact_keys(
        initial_state,
        {"capability_level", "delivery_state"},
        f"later promotion unit {unit_id}.initial_state",
    )
    normalized_initial_state = {
        key: require_string(
            initial_state[key], f"later promotion unit {unit_id}.initial_state.{key}"
        )
        for key in ("capability_level", "delivery_state")
    }
    compatibility_contracts = require_string_list(
        unit["compatibility_contracts"],
        f"later promotion unit {unit_id}.compatibility_contracts",
        nonempty=False,
    )
    expected_roots = require_mapping(
        unit["expected_roots"], f"later promotion unit {unit_id}.expected_roots"
    )
    require_exact_keys(
        expected_roots,
        {"implementation", "tests", "outputs"},
        f"later promotion unit {unit_id}.expected_roots",
    )
    normalized_roots = {
        kind: _require_safe_root(
            expected_roots[kind],
            f"later promotion unit {unit_id}.expected_roots.{kind}",
        )
        for kind in ("implementation", "tests", "outputs")
    }
    return (
        {
            "unit_id": unit_id,
            "initial_state": normalized_initial_state,
            "evidence_ceiling": require_string(
                unit["evidence_ceiling"],
                f"later promotion unit {unit_id}.evidence_ceiling",
            ),
            "selector": normalized_selector,
            "material_profiles": normalized_material_profiles,
            "constraints": constraints,
            "recovery": recovery,
            "fallbacks": normalized_fallbacks,
            "delivery_gate": require_string(
                unit["delivery_gate"], f"later promotion unit {unit_id}.delivery_gate"
            ),
            "evidence_gate_set": require_string(
                unit["evidence_gate_set"],
                f"later promotion unit {unit_id}.evidence_gate_set",
            ),
            "compatibility_contracts": compatibility_contracts,
            "expected_roots": normalized_roots,
        },
        issue_id,
    )


def validate_later_promotion_roster(
    inventory: dict[str, Any],
    *,
    variants_by_id: dict[str, dict[str, Any]],
    platforms: dict[str, dict[str, Any]],
    operation_sets: dict[str, set[str]],
    constraint_profiles: dict[str, list[str]],
    recoveries: dict[str, dict[str, Any]],
    fallback_registry: dict[str, dict[str, Any]],
    fallback_operations: dict[str, OperationApplicability],
    profile_registries: dict[str, dict[str, Any]],
    compatibility_contract_ids: set[str],
) -> LaterRosterValidation:
    """Validate every deferred physical unit without granting current authority."""

    units = require_list(
        inventory.get("later_promotion_roster"), "later_promotion_roster"
    )
    expected = _accepted_vulkan_identities() + _accepted_windows_identities(
        constraint_profiles
    )
    normalized_with_issues = [
        _normalize_later_unit(raw_unit, index) for index, raw_unit in enumerate(units)
    ]
    normalized = [identity for identity, _issue_id in normalized_with_issues]
    issue_ids = [
        issue_id
        for _identity, issue_id in normalized_with_issues
        if issue_id is not None
    ]
    require_unique(
        [str(issue_id) for issue_id in issue_ids], "later promotion issue IDs"
    )
    if len(normalized) != 34 or [unit["unit_id"] for unit in normalized] != [
        unit["unit_id"] for unit in expected
    ]:
        fail("later_promotion_roster must contain exactly the accepted 34 units")

    profile_uses = {
        "hardware_profiles": set(),
        "configuration_profiles": set(),
        "workload_profiles": set(),
        "predictor_rules": set(),
        "observation_contracts": set(),
    }
    fallback_uses: set[str] = set()
    for actual, accepted in zip(normalized, expected, strict=True):
        unit_id = actual["unit_id"]
        if actual != accepted:
            fail(f"later promotion unit {unit_id} must equal its accepted identity")
        selector = actual["selector"]
        variant = variants_by_id[selector["base_variant"]]
        if selector["platform"] not in variant["platforms"]:
            fail(f"later promotion unit {unit_id} is outside its variant platform")
        if (
            selector["backend_channel"]
            not in platforms[selector["platform"]]["channels"]
        ):
            fail(f"later promotion unit {unit_id} is outside its platform channel")
        if selector["model_type"] not in variant["model_types"]:
            fail(f"later promotion unit {unit_id} is outside its variant model types")
        operation = selector["operation_template"]
        if operation not in operation_sets[variant["operations"]]:
            fail(f"later promotion unit {unit_id} is outside its variant operations")
        if (
            not set(selector["operation_leaves"])
            <= OPERATION_LEAVES_BY_TEMPLATE[operation]
        ):
            fail(f"later promotion unit {unit_id} has an invalid operation leaf")
        if (
            actual["recovery"] not in recoveries
            or actual["recovery"] != variant["recovery"]
        ):
            fail(f"later promotion unit {unit_id} has an invalid recovery profile")
        for registry_field, profile_id in actual["material_profiles"].items():
            registry_name = PROFILE_REGISTRY_BY_FIELD[registry_field]
            if profile_id not in profile_registries[registry_name]:
                fail(
                    f"later promotion unit {unit_id} references unknown material profile"
                )
            profile_uses[registry_name].add(profile_id)
        for fallback_id in actual["fallbacks"].values():
            if fallback_id not in fallback_registry:
                fail(f"later promotion unit {unit_id} references unknown fallback")
            if operation not in fallback_operations[fallback_id].operations:
                fail(f"later promotion unit {unit_id} fallback does not apply")
            fallback_uses.add(fallback_id)
        if not set(actual["compatibility_contracts"]) <= compatibility_contract_ids:
            fail(
                f"later promotion unit {unit_id} references unknown compatibility contract"
            )

    return LaterRosterValidation(
        units=units,
        profile_uses=profile_uses,
        fallback_uses=fallback_uses,
    )
