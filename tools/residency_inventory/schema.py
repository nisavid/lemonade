"""Validate the normalized residency inventory and its cross-field closure."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Any

from .contract import (
    EXPECTED_CAPABILITY_LEVELS,
    EXPECTED_DELIVERY_STATES,
    EXPECTED_OPERATION_TEMPLATES,
    OPERATION_LEAVES_BY_TEMPLATE,
    fail,
    require_exact_keys,
    require_mapping,
    require_registry_keys,
    require_string_list,
)
from .platforms import validate_provider_and_topology_rules
from .policy import validate_policy_registry_stage
from .profiles import validate_profile_stage
from .promotions import (
    validate_closure_stage,
    validate_promotion_stage,
)
from .source import FrozenSourceClosure, validate_source_closure
from .variants import (
    validate_exclusion_stage,
    validate_variant_stage,
)

EXPECTED_TOP_LEVEL_KEYS = {
    "schema_version",
    "source_support_baseline",
    "campaign_base_binding",
    "source_file_blobs",
    "enums",
    "operation_sets",
    "constraint_profiles",
    "recovery_profiles",
    "suite_registry",
    "suite_sets",
    "fallback_registry",
    "fallback_sets",
    "provider_keys",
    "topology_rules",
    "recipe_model_type_contracts",
    "hardware_profiles",
    "configuration_profiles",
    "workload_profiles",
    "predictor_rules",
    "observation_contracts",
    "runtime_binding_kinds",
    "gate_sources",
    "gate_registry",
    "gate_sets",
    "platform_predicates",
    "variants",
    "exact_cells",
    "compatibility_contracts",
    "promotion_roster",
    "exclusions",
    "coverage_policy",
}

EXPECTED_MODEL_TYPES = {
    "llm",
    "embedding",
    "reranking",
    "transcription",
    "image",
    "tts",
    "audio-generation",
    "classification",
    "mesh",
}
EXPECTED_CONSTRAINT_KINDS = {
    "gpu_shared_residency",
    "gpu_provider_resolved_capacity",
    "host_memavailable_floor",
    "host_effects_provider_resolved",
    "model_type_pool",
    "ownership",
    "flm_type_slot",
    "npu_cross_family",
    "npu_exclusive",
}


@dataclass(frozen=True)
class VocabularyValidation:
    """Frozen source facts plus the closed schema vocabularies they constrain."""

    source: FrozenSourceClosure
    provider_keys: dict[str, str]
    topology_rules: dict[str, str]
    capabilities: set[str]
    delivery_states: set[str]
    operations: set[str]
    operation_leaves: set[str]
    model_types: set[str]
    constraints: set[str]
    platforms: dict[str, dict[str, Any]]


def validate_vocabulary_stage(
    repo: Path, inventory: dict[str, Any]
) -> VocabularyValidation:
    """Validate the envelope, frozen inputs, enums, and platform registry shape."""

    require_exact_keys(inventory, EXPECTED_TOP_LEVEL_KEYS, "inventory")
    if inventory.get("schema_version") != 4:
        fail("schema_version must be 4")

    source = validate_source_closure(repo, inventory)
    provider_keys, topology_rules = validate_provider_and_topology_rules(
        inventory, source
    )

    enums = require_mapping(inventory.get("enums"), "enums")
    require_exact_keys(
        enums,
        {
            "capability_level",
            "delivery_state",
            "operation_template",
            "operation_leaf",
            "model_type",
            "constraint_kind",
        },
        "enums",
    )
    capabilities = set(
        require_string_list(enums.get("capability_level"), "enums.capability_level")
    )
    delivery_states = set(
        require_string_list(enums.get("delivery_state"), "enums.delivery_state")
    )
    operations = set(
        require_string_list(enums.get("operation_template"), "enums.operation_template")
    )
    operation_leaves = set(
        require_string_list(enums.get("operation_leaf"), "enums.operation_leaf")
    )
    model_types = set(require_string_list(enums.get("model_type"), "enums.model_type"))
    constraints = set(
        require_string_list(enums.get("constraint_kind"), "enums.constraint_kind")
    )
    if capabilities != EXPECTED_CAPABILITY_LEVELS:
        fail("capability_level enum is not the accepted machine closed set")
    if delivery_states != EXPECTED_DELIVERY_STATES:
        fail("delivery_state enum is not the accepted closed set")
    if operations != EXPECTED_OPERATION_TEMPLATES:
        fail("operation_template enum is not the accepted closed set")
    expected_leaves = set().union(*OPERATION_LEAVES_BY_TEMPLATE.values())
    if operation_leaves != expected_leaves:
        fail("operation_leaf enum is not the accepted closed set")
    if model_types != EXPECTED_MODEL_TYPES:
        fail("model_type enum is not the accepted closed set")
    if constraints != EXPECTED_CONSTRAINT_KINDS:
        fail("constraint_kind enum is not the accepted closed set")

    raw_platforms = require_mapping(
        inventory.get("platform_predicates"), "platform_predicates"
    )
    require_registry_keys(raw_platforms, "platform_predicates")
    platforms = {
        platform_id: require_mapping(raw_platform, f"platform_predicates.{platform_id}")
        for platform_id, raw_platform in raw_platforms.items()
    }
    return VocabularyValidation(
        source=source,
        provider_keys=provider_keys,
        topology_rules=topology_rules,
        capabilities=capabilities,
        delivery_states=delivery_states,
        operations=operations,
        operation_leaves=operation_leaves,
        model_types=model_types,
        constraints=constraints,
        platforms=platforms,
    )


def validate_inventory(repo: Path, inventory: dict[str, Any]) -> dict[str, Any]:
    """Run the schema-v4 stages and return the renderer's normalized projection."""

    vocabulary = validate_vocabulary_stage(repo, inventory)
    policy = validate_policy_registry_stage(inventory, vocabulary)
    variants = validate_variant_stage(inventory, vocabulary, policy)
    exclusions = validate_exclusion_stage(
        inventory, vocabulary.source, variants.dispositions
    )
    profiles = validate_profile_stage(repo, inventory, vocabulary)
    promotions = validate_promotion_stage(
        repo,
        inventory,
        vocabulary,
        policy,
        variants,
        profiles,
    )
    projection = validate_closure_stage(
        inventory,
        vocabulary,
        policy,
        variants,
        exclusions,
        profiles,
        promotions,
    )
    return projection.as_mapping()
