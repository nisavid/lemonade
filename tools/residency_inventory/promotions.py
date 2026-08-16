"""Validate exact promotions, registry closure, and renderer projection."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any, Protocol

from .cells import validate_exact_cells
from .compatibility import validate_compatibility_contracts
from .contract import fail, require_exact_keys, require_mapping, require_string_list
from .contract_registry import validate_contract_promotion_closure
from .gates import PromotionGateRequirement, validate_gates
from .later_roster import validate_later_promotion_roster
from .platforms import validate_platform_contracts
from .policy import PolicyRegistryValidation
from .profiles import ProfileValidation
from .source import FrozenSourceClosure
from .variants import (
    ExclusionValidation,
    VariantValidation,
    validate_variant_topology_bindings,
)


class Vocabulary(Protocol):
    """Closed source and enum attributes consumed by promotion validation."""

    source: FrozenSourceClosure
    contract_registry: dict[str, Any]
    provider_keys: dict[str, str]
    topology_rules: dict[str, str]
    capabilities: set[str]
    delivery_states: set[str]
    operation_leaves: set[str]
    model_types: set[str]
    constraints: set[str]
    platforms: dict[str, dict[str, Any]]


@dataclass(frozen=True)
class PromotionValidation:
    """Exact runtime and compatibility promotions plus their registry uses."""

    exact_cells: list[dict[str, Any]]
    compatibility_contracts: list[dict[str, Any]]
    promotion_roster: dict[str, Any]
    later_promotion_roster: list[dict[str, Any]]
    flattened_gate_sets: dict[str, list[str]]
    gate_sources: dict[str, dict[str, Any]]
    gate_registry: dict[str, dict[str, Any]]
    campaign_base_binding: dict[str, str]
    profile_uses: dict[str, set[str]]
    runtime_binding_uses: set[str]
    fallback_uses: set[str]


def validate_fallback_operation_uses(
    policy: PolicyRegistryValidation,
    exact_cells: list[dict[str, Any]],
    compatibility_contracts: list[dict[str, Any]],
    later_promotion_roster: list[dict[str, Any]],
) -> None:
    uses = {fallback_id: set() for fallback_id in policy.fallback_registry}
    for mapping in policy.fallback_sets.values():
        for operation, fallback_ids in mapping.items():
            for fallback_id in fallback_ids:
                uses[fallback_id].add(operation)
    for cell in exact_cells:
        for fallback_id in cell["fallbacks"].values():
            uses[fallback_id].add(cell["operation_template"])
    for contract in compatibility_contracts:
        for fallback_id in contract["fallbacks"].values():
            uses[fallback_id].add(contract["operation_template"])
    for unit in later_promotion_roster:
        for fallback_id in unit["fallbacks"].values():
            uses[fallback_id].add(unit["selector"]["operation_template"])
    for fallback_id, applicability in policy.fallback_operations.items():
        if set(applicability.operations) != uses[fallback_id]:
            fail(
                f"fallback_registry.{fallback_id}.operations must exactly equal "
                "its referenced operation templates"
            )


def validate_promotion_roster(
    inventory: dict[str, Any],
    exact_cells: list[dict[str, Any]],
    compatibility_contracts: list[dict[str, Any]],
) -> dict[str, Any]:
    roster = require_mapping(inventory.get("promotion_roster"), "promotion_roster")
    require_exact_keys(
        roster,
        {"exact_cells", "compatibility_contracts"},
        "promotion_roster",
    )
    roster_cell_ids = require_string_list(
        roster["exact_cells"], "promotion_roster.exact_cells"
    )
    roster_contract_ids = require_string_list(
        roster["compatibility_contracts"],
        "promotion_roster.compatibility_contracts",
    )
    actual_cell_ids = {cell["cell_id"] for cell in exact_cells}
    actual_contract_ids = {
        contract["contract_id"] for contract in compatibility_contracts
    }
    if set(roster_cell_ids) != actual_cell_ids:
        fail("promotion_roster.exact_cells must exactly list the exact cell IDs")
    if set(roster_contract_ids) != actual_contract_ids:
        fail(
            "promotion_roster.compatibility_contracts must exactly list the "
            "compatibility contract IDs"
        )
    return roster


def promotion_gate_requirements(
    variants: VariantValidation,
    exact_cells: list[dict[str, Any]],
    compatibility_contracts: list[dict[str, Any]],
    later_promotion_roster: list[dict[str, Any]],
) -> list[PromotionGateRequirement]:
    requirements = [
        PromotionGateRequirement(
            unit_id=cell["cell_id"],
            unit_kind="exact_cell",
            operation=cell["operation_template"],
            suite_set_id=variants.by_id[cell["base_variant"]]["suites"],
            gate_set_id=cell["campaign_gate_set"],
        )
        for cell in exact_cells
    ]
    requirements.extend(
        PromotionGateRequirement(
            unit_id=contract["contract_id"],
            unit_kind="compatibility_contract",
            operation=contract["operation_template"],
            suite_set_id=contract["suite_set"],
            gate_set_id=contract["campaign_gate_set"],
        )
        for contract in compatibility_contracts
    )
    requirements.extend(
        PromotionGateRequirement(
            unit_id=unit["unit_id"],
            unit_kind="later_runtime",
            operation=unit["selector"]["operation_template"],
            suite_set_id=variants.by_id[unit["selector"]["base_variant"]]["suites"],
            gate_set_id=unit["evidence_gate_set"],
        )
        for unit in later_promotion_roster
    )
    return requirements


def validate_promotion_stage(
    repo: Path,
    inventory: dict[str, Any],
    vocabulary: Vocabulary,
    policy: PolicyRegistryValidation,
    variants: VariantValidation,
    profiles: ProfileValidation,
) -> PromotionValidation:
    """Validate exact cells, compatibility contracts, fallbacks, roster, and gates."""

    platform_validation = validate_platform_contracts(
        inventory,
        vocabulary.source,
        vocabulary.platforms,
        variants.variants,
        vocabulary.model_types,
        profiles.runtime_binding_kinds,
        vocabulary.provider_keys,
        vocabulary.topology_rules,
    )
    validate_variant_topology_bindings(vocabulary, variants.variants)
    cell_validation = validate_exact_cells(
        inventory,
        variants_by_id=variants.by_id,
        platform_support=vocabulary.platforms,
        normalized_operation_sets=policy.operation_sets,
        recoveries=policy.recovery_profiles,
        fallback_registry=policy.fallback_registry,
        fallback_operations=policy.fallback_operations,
        registries=profiles.registries,
        runtime_binding_kinds=profiles.runtime_binding_kinds,
        enum_capabilities=vocabulary.capabilities,
        enum_delivery=vocabulary.delivery_states,
        enum_operation_leaves=vocabulary.operation_leaves,
        enum_constraints=vocabulary.constraints,
        hardware_atoms=platform_validation.hardware_atoms,
    )
    exact_cells = cell_validation.exact_cells
    compatibility_validation = validate_compatibility_contracts(
        inventory,
        variants=variants.variants,
        constraint_profiles=policy.constraint_profiles,
        enum_constraints=vocabulary.constraints,
        fallback_registry=policy.fallback_registry,
        fallback_operations=policy.fallback_operations,
        normalized_suite_sets=policy.suite_sets,
        exact_cell_ids={cell["cell_id"] for cell in exact_cells},
    )
    compatibility_contracts = compatibility_validation.contracts
    later_validation = validate_later_promotion_roster(
        inventory,
        variants_by_id=variants.by_id,
        platforms=vocabulary.platforms,
        operation_sets=policy.operation_sets,
        constraint_profiles=policy.constraint_profiles,
        recoveries=policy.recovery_profiles,
        fallback_registry=policy.fallback_registry,
        fallback_operations=policy.fallback_operations,
        profile_registries=profiles.registries,
        compatibility_contract_ids={
            contract["contract_id"] for contract in compatibility_contracts
        },
    )
    later_promotion_roster = later_validation.units
    validate_contract_promotion_closure(
        exact_cells=exact_cells,
        compatibility_contracts=compatibility_contracts,
        later_promotion_roster=later_promotion_roster,
        fallback_ids=set(policy.fallback_registry),
    )
    validate_fallback_operation_uses(
        policy,
        exact_cells,
        compatibility_contracts,
        later_promotion_roster,
    )
    promotion_roster = validate_promotion_roster(
        inventory,
        exact_cells,
        compatibility_contracts,
    )
    gate_validation = validate_gates(
        repo,
        inventory,
        promotion_gate_requirements(
            variants,
            exact_cells,
            compatibility_contracts,
            later_promotion_roster,
        ),
        suite_operations=policy.suite_operations,
        normalized_suite_sets=policy.suite_sets,
    )
    return PromotionValidation(
        exact_cells=exact_cells,
        compatibility_contracts=compatibility_contracts,
        promotion_roster=promotion_roster,
        later_promotion_roster=later_promotion_roster,
        flattened_gate_sets=gate_validation.flattened_sets,
        gate_sources=gate_validation.source_bindings,
        gate_registry=gate_validation.registry,
        campaign_base_binding=gate_validation.campaign_base_binding,
        profile_uses={
            registry_name: references | later_validation.profile_uses[registry_name]
            for registry_name, references in cell_validation.profile_uses.items()
        },
        runtime_binding_uses=cell_validation.runtime_binding_uses,
        fallback_uses=(
            cell_validation.fallback_uses
            | compatibility_validation.fallback_uses
            | later_validation.fallback_uses
        ),
    )


@dataclass(frozen=True)
class ProjectionValidation:
    """Validated public renderer projection after orphan closure."""

    source_support_baseline: str
    source_tree_objects: dict[str, str]
    contract_registry: dict[str, Any]
    fallback_registry: dict[str, Any]
    variants: list[dict[str, Any]]
    exclusions: list[dict[str, Any]]
    profile_semantics: dict[str, dict[str, Any]]
    profile_bindings: dict[str, dict[str, str]]
    coverage_policy: dict[str, str]
    suite_registry: dict[str, Any]
    suite_sets: dict[str, list[str]]
    campaign_base_binding: dict[str, str]
    gate_sources: dict[str, dict[str, Any]]
    gate_registry: dict[str, Any]
    gate_sets: dict[str, list[str]]
    exact_cells: list[dict[str, Any]]
    compatibility_contracts: list[dict[str, Any]]
    promotion_roster: dict[str, Any]
    later_promotion_roster: list[dict[str, Any]]

    def as_mapping(self) -> dict[str, Any]:
        """Return the stable machine projection consumed by both renderers."""

        return {
            "source_support_baseline": self.source_support_baseline,
            "source_tree_objects": self.source_tree_objects,
            "contract_registry": self.contract_registry,
            "fallback_registry": self.fallback_registry,
            "variants": self.variants,
            "exclusions": self.exclusions,
            "profile_semantics": self.profile_semantics,
            "profile_bindings": self.profile_bindings,
            "coverage_policy": self.coverage_policy,
            "suite_registry": self.suite_registry,
            "suite_sets": self.suite_sets,
            "campaign_base_binding": self.campaign_base_binding,
            "gate_sources": self.gate_sources,
            "gate_registry": self.gate_registry,
            "gate_sets": self.gate_sets,
            "exact_cells": self.exact_cells,
            "compatibility_contracts": self.compatibility_contracts,
            "promotion_roster": self.promotion_roster,
            "later_promotion_roster": self.later_promotion_roster,
        }


def validate_closure_stage(
    inventory: dict[str, Any],
    vocabulary: Vocabulary,
    policy: PolicyRegistryValidation,
    variants: VariantValidation,
    exclusions: ExclusionValidation,
    profiles: ProfileValidation,
    promotions: PromotionValidation,
) -> ProjectionValidation:
    """Reject orphan registry entries and assemble the public projection."""

    used_platforms = {
        platform_id
        for variant in variants.variants
        for platform_id in variant["platforms"]
    }
    if used_platforms != set(vocabulary.platforms):
        fail(
            "platform_predicates contains orphan selectors: "
            f"{sorted(set(vocabulary.platforms) - used_platforms)}"
        )
    variant_registry_references = {
        "operation_sets": {variant["operations"] for variant in variants.variants},
        "constraint_profiles": {
            variant["constraints"] for variant in variants.variants
        },
        "recovery_profiles": {variant["recovery"] for variant in variants.variants},
        "suite_sets": {variant["suites"] for variant in variants.variants},
        "fallback_sets": {variant["fallbacks"] for variant in variants.variants},
    }
    for registry_name, references in variant_registry_references.items():
        registry = inventory[registry_name]
        if references != set(registry):
            fail(
                f"{registry_name} contains orphan entries: "
                f"{sorted(set(registry) - references)}"
            )

    used_suites = {
        suite_id for members in policy.suite_sets.values() for suite_id in members
    }
    if used_suites != set(policy.suite_registry):
        fail(
            "suite_registry contains orphan entries: "
            f"{sorted(set(policy.suite_registry) - used_suites)}"
        )
    used_fallbacks = promotions.fallback_uses | {
        fallback_id
        for mapping in policy.fallback_sets.values()
        for fallback_ids in mapping.values()
        for fallback_id in fallback_ids
    }
    if used_fallbacks != set(policy.fallback_registry):
        fail(
            "fallback_registry contains orphan entries: "
            f"{sorted(set(policy.fallback_registry) - used_fallbacks)}"
        )
    for registry_name, references in promotions.profile_uses.items():
        if references != set(profiles.registries[registry_name]):
            fail(
                f"{registry_name} contains orphan entries: "
                f"{sorted(set(profiles.registries[registry_name]) - references)}"
            )
    if promotions.runtime_binding_uses != profiles.runtime_binding_kinds:
        fail(
            "runtime_binding_kinds contains orphan entries: "
            f"{sorted(profiles.runtime_binding_kinds - promotions.runtime_binding_uses)}"
        )

    return ProjectionValidation(
        source_support_baseline=vocabulary.source.baseline,
        source_tree_objects=vocabulary.source.tree_objects,
        contract_registry=vocabulary.contract_registry,
        fallback_registry=policy.fallback_registry,
        variants=variants.variants,
        exclusions=exclusions.exclusions,
        profile_semantics=profiles.registries,
        profile_bindings=profiles.document_bindings,
        coverage_policy=profiles.coverage_policy,
        suite_registry=policy.suite_registry,
        suite_sets=policy.suite_sets,
        campaign_base_binding=promotions.campaign_base_binding,
        gate_sources=promotions.gate_sources,
        gate_registry=promotions.gate_registry,
        gate_sets=promotions.flattened_gate_sets,
        exact_cells=promotions.exact_cells,
        compatibility_contracts=promotions.compatibility_contracts,
        promotion_roster=promotions.promotion_roster,
        later_promotion_roster=promotions.later_promotion_roster,
    )
