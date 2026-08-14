"""Validate broad variants, exclusions, and their frozen-source closure."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Protocol

from .contract import (
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
from .source import (
    FrozenSourceClosure,
    platform_source_support,
    source_support_from_mapping,
    source_support_text,
)

EXPECTED_RETAINED_CONTRACTS = {"model_type_pool", "ownership", "verified_release"}
EXPECTED_VARIANT_REGISTRY_ASSIGNMENTS = {
    "apple_unified": {
        "operations": "gpu_resource",
        "suites": "gpu_standard",
        "fallbacks": "gpu_standard",
    },
    "provider_resolved_gpu": {
        "operations": "gpu_resource",
        "suites": "gpu_standard",
        "fallbacks": "gpu_standard",
    },
    "npu_flm": {
        "operations": "npu_compatibility",
        "suites": "npu_compatibility",
        "fallbacks": "npu_compatibility",
    },
    "npu_exclusive": {
        "operations": "npu_compatibility",
        "suites": "npu_compatibility",
        "fallbacks": "npu_compatibility",
    },
}
EXPECTED_GPU_CONSTRAINT_BY_TOPOLOGY = {
    "unified": "apple_unified",
    "provider_resolved": "provider_resolved_gpu",
}
EXPECTED_NPU_VARIANT_CONSTRAINTS = {
    "flm-npu": "npu_flm",
    "whispercpp-npu": "npu_exclusive",
    "ryzenai-llm-npu": "npu_exclusive",
}
EXPECTED_RECOVERY_BY_RECIPE = {
    "llamacpp": "native_subprocess_tree",
    "whispercpp": "native_subprocess_tree",
    "sd-cpp": "native_subprocess_tree",
    "kokoro": "native_subprocess_tree",
    "vllm": "python_or_async_job_tree",
    "thenoise": "native_subprocess_tree",
    "thinksound": "python_or_async_job_tree",
    "acestep": "python_or_async_job_tree",
    "trellis": "python_or_async_job_tree",
    "openmoss": "native_subprocess_tree",
    "flm": "flm_system_managed",
    "ryzenai-llm": "native_subprocess_tree",
}


class Vocabulary(Protocol):
    """Vocabulary and source attributes consumed by variant validation."""

    source: FrozenSourceClosure
    platforms: dict[str, dict[str, Any]]
    model_types: set[str]
    capabilities: set[str]


class Policy(Protocol):
    """Normalized policy registries consumed by variant validation."""

    operation_sets: dict[str, set[str]]
    constraint_profiles: dict[str, list[str]]
    recovery_profiles: dict[str, dict[str, Any]]
    suite_sets: dict[str, list[str]]
    fallback_sets: dict[str, dict[str, list[str]]]
    suite_operations: dict[str, OperationApplicability]


@dataclass(frozen=True)
class VariantValidation:
    """Broad variants and the frozen source rows they dispose."""

    variants: list[dict[str, Any]]
    by_id: dict[str, dict[str, Any]]
    dispositions: dict[SourceSupportKey, list[str]]


@dataclass(frozen=True)
class VariantEntryValidation:
    """One normalized variant and its frozen source dispositions."""

    variant_id: str
    recipe_backend: str
    variant: dict[str, Any]
    dispositions: list[tuple[SourceSupportKey, str]]


def variant_source_dispositions(
    variant_id: str,
    recipe: str,
    backend: str,
    platform_ids: list[str],
    vocabulary: Vocabulary,
) -> list[tuple[SourceSupportKey, str]]:
    dispositions: list[tuple[SourceSupportKey, str]] = []
    for platform_id in platform_ids:
        if platform_id not in vocabulary.platforms:
            fail(f"variant {variant_id} references unknown platform {platform_id}")
        dispositions.extend(
            (source_key, platform_id)
            for source_key in platform_source_support(
                recipe,
                backend,
                vocabulary.platforms[platform_id],
                f"platform_predicates.{platform_id}",
            )
        )
    return dispositions


def validate_variant_policy_bindings(
    variant_id: str,
    recipe: str,
    variant: dict[str, Any],
    vocabulary: Vocabulary,
    policy: Policy,
) -> None:
    model_types = set(
        require_string_list(
            variant.get("model_types"), f"variant {variant_id}.model_types"
        )
    )
    if not model_types <= vocabulary.model_types:
        fail(f"variant {variant_id} references an unknown model type")
    for field, registry in (
        ("constraints", policy.constraint_profiles),
        ("operations", policy.operation_sets),
        ("recovery", policy.recovery_profiles),
        ("suites", policy.suite_sets),
        ("fallbacks", policy.fallback_sets),
    ):
        reference = require_string(variant.get(field), f"variant {variant_id}.{field}")
        if reference not in registry:
            fail(f"variant {variant_id} references unknown {field} {reference}")
    expected_assignment = EXPECTED_VARIANT_REGISTRY_ASSIGNMENTS.get(
        variant["constraints"]
    )
    actual_assignment = {
        field: variant[field] for field in ("operations", "suites", "fallbacks")
    }
    if expected_assignment != actual_assignment:
        fail(
            f"variant {variant_id} operation, suite, and fallback sets must "
            "match its constraint profile"
        )
    if EXPECTED_RECOVERY_BY_RECIPE.get(recipe) != variant["recovery"]:
        fail(
            f"variant {variant_id} recovery profile must match its recipe "
            "ownership model"
        )
    ceiling = require_string(
        variant.get("evidence_ceiling"), f"variant {variant_id}.evidence_ceiling"
    )
    if ceiling not in vocabulary.capabilities:
        fail(f"variant {variant_id} has an unknown evidence ceiling")
    if ceiling != "modeled":
        fail(f"variant {variant_id} evidence ceiling must remain modeled")
    covered_suite_operations = set().union(
        *(
            policy.suite_operations[suite_id].operations
            for suite_id in policy.suite_sets[variant["suites"]]
        )
    )
    operations = policy.operation_sets[variant["operations"]]
    if not operations <= covered_suite_operations:
        missing = sorted(operations - covered_suite_operations)
        fail(f"variant {variant_id} suite set does not cover operations {missing}")
    if set(policy.fallback_sets[variant["fallbacks"]]) != operations:
        fail(f"variant {variant_id} fallback set must exactly cover its operation set")


def validate_variant_entry(
    raw_variant: Any,
    index: int,
    vocabulary: Vocabulary,
    policy: Policy,
) -> VariantEntryValidation:
    variant = require_mapping(raw_variant, f"variants[{index}]")
    variant_id = require_string(variant.get("id"), f"variants[{index}].id")
    require_exact_keys(
        variant,
        {
            "id",
            "recipe",
            "backend",
            "platforms",
            "model_types",
            "constraints",
            "operations",
            "recovery",
            "suites",
            "fallbacks",
            "evidence_ceiling",
        },
        f"variant {variant_id}",
    )
    recipe = require_string(variant.get("recipe"), f"variant {variant_id}.recipe")
    backend = require_string(variant.get("backend"), f"variant {variant_id}.backend")
    platform_ids = require_string_list(
        variant.get("platforms"), f"variant {variant_id}.platforms"
    )
    dispositions = variant_source_dispositions(
        variant_id, recipe, backend, platform_ids, vocabulary
    )
    validate_variant_policy_bindings(variant_id, recipe, variant, vocabulary, policy)
    return VariantEntryValidation(
        variant_id=variant_id,
        recipe_backend=f"{recipe}:{backend}",
        variant=variant,
        dispositions=dispositions,
    )


def validate_variant_stage(
    inventory: dict[str, Any], vocabulary: Vocabulary, policy: Policy
) -> VariantValidation:
    """Validate broad variant contracts and collect their source dispositions."""

    variants = require_list(inventory.get("variants"), "variants")
    variant_ids: list[str] = []
    recipe_backend_pairs: list[str] = []
    variants_by_id: dict[str, dict[str, Any]] = {}
    dispositions: dict[SourceSupportKey, list[str]] = {}
    for index, raw_variant in enumerate(variants):
        entry = validate_variant_entry(raw_variant, index, vocabulary, policy)
        variant_ids.append(entry.variant_id)
        recipe_backend_pairs.append(entry.recipe_backend)
        variants_by_id[entry.variant_id] = entry.variant
        for source_key, platform_id in entry.dispositions:
            dispositions.setdefault(source_key, []).append(
                f"variant:{entry.variant_id}:{platform_id}"
            )
    require_unique(variant_ids, "variant ids")
    require_unique(recipe_backend_pairs, "variant recipe/backend pairs")
    return VariantValidation(
        variants=variants,
        by_id=variants_by_id,
        dispositions=dispositions,
    )


@dataclass(frozen=True)
class ExclusionValidation:
    """Explicit exclusions closing every non-variant frozen source row."""

    exclusions: list[dict[str, Any]]


@dataclass(frozen=True)
class ExclusionEntryValidation:
    """One normalized exclusion and the dispositions it contributes."""

    exclusion_id: str
    source_dispositions: list[SourceSupportKey]
    empty_recipes: list[str]
    non_descriptor_recipes: list[str]


def validate_exclusion_entry(
    raw_exclusion: Any, index: int
) -> ExclusionEntryValidation:
    exclusion = require_mapping(raw_exclusion, f"exclusions[{index}]")
    exclusion_id = require_string(exclusion.get("id"), f"exclusions[{index}].id")
    allowed_fields = {
        "id",
        "behavior",
        "retained_contracts",
        "source_support",
        "empty_support_recipes",
        "non_descriptor_recipes",
    }
    extra_fields = set(exclusion) - allowed_fields
    if extra_fields:
        fail(f"exclusion {exclusion_id} has unknown fields {sorted(extra_fields)}")
    require_string(exclusion.get("behavior"), f"exclusion {exclusion_id}.behavior")
    raw_source_support = require_list(
        exclusion.get("source_support", []),
        f"exclusion {exclusion_id}.source_support",
    )
    if raw_source_support and "retained_contracts" not in exclusion:
        fail(
            f"exclusion {exclusion_id} with local source support must declare "
            "the accepted retained contracts"
        )
    if not raw_source_support and "retained_contracts" in exclusion:
        fail(
            f"exclusion {exclusion_id} without local source support must not "
            "declare retained contracts"
        )
    if "retained_contracts" in exclusion:
        retained_contracts = set(
            require_string_list(
                exclusion["retained_contracts"],
                f"exclusion {exclusion_id}.retained_contracts",
            )
        )
        if retained_contracts != EXPECTED_RETAINED_CONTRACTS:
            fail(
                f"exclusion {exclusion_id} retained_contracts must equal the "
                "accepted closed set including verified_release"
            )
    source_dispositions = [
        source_key
        for support_index, raw_support in enumerate(raw_source_support)
        for source_key in source_support_from_mapping(
            raw_support, f"exclusion {exclusion_id}.source_support[{support_index}]"
        )
    ]
    empty_recipes = require_string_list(
        exclusion.get("empty_support_recipes", []),
        f"exclusion {exclusion_id}.empty_support_recipes",
        nonempty=False,
    )
    non_descriptor_recipes = require_string_list(
        exclusion.get("non_descriptor_recipes", []),
        f"exclusion {exclusion_id}.non_descriptor_recipes",
        nonempty=False,
    )
    if not source_dispositions and not empty_recipes and not non_descriptor_recipes:
        fail(f"exclusion {exclusion_id} must dispose at least one source item")
    return ExclusionEntryValidation(
        exclusion_id=exclusion_id,
        source_dispositions=source_dispositions,
        empty_recipes=empty_recipes,
        non_descriptor_recipes=non_descriptor_recipes,
    )


def validate_non_descriptor_disposition_closure(
    source: FrozenSourceClosure, non_descriptor_recipes: list[str]
) -> None:
    """Require each frozen collection recipe to have one non-descriptor exclusion."""

    descriptor_recipes = {
        key[0] for key in source.support
    } | source.empty_support_recipes
    overlap = descriptor_recipes & set(non_descriptor_recipes)
    if overlap:
        fail(
            f"non-descriptor exclusions name registered descriptors: {sorted(overlap)}"
        )
    configured_collections = set(non_descriptor_recipes)
    for recipe in sorted(source.collection_recipes | configured_collections):
        if recipe not in source.collection_recipes:
            fail(f"non-descriptor recipe {recipe} is not a frozen collection recipe")
        matches = non_descriptor_recipes.count(recipe)
        if matches != 1:
            fail(
                f"frozen collection recipe {recipe} must have exactly one "
                f"disposition, got {matches}"
            )


def validate_source_disposition_closure(
    source: FrozenSourceClosure,
    dispositions: dict[SourceSupportKey, list[str]],
    empty_dispositions: dict[str, list[str]],
    non_descriptor_recipes: list[str],
) -> None:
    for key in sorted(source.support | set(dispositions)):
        matches = dispositions.get(key, [])
        if key not in source.support:
            fail(
                f"configured selector has no frozen source row: {source_support_text(key)}"
            )
        if len(matches) != 1:
            fail(
                f"source support {source_support_text(key)} must have exactly one "
                f"disposition, got {matches}"
            )
    for recipe in sorted(source.empty_support_recipes | set(empty_dispositions)):
        matches = empty_dispositions.get(recipe, [])
        if recipe not in source.empty_support_recipes:
            fail(f"empty-support selector {recipe} is not an empty-support descriptor")
        if len(matches) != 1:
            fail(
                f"empty-support descriptor {recipe} must have exactly one disposition, "
                f"got {matches}"
            )
    validate_non_descriptor_disposition_closure(source, non_descriptor_recipes)


def validate_exclusion_stage(
    inventory: dict[str, Any],
    source: FrozenSourceClosure,
    variant_dispositions: dict[SourceSupportKey, list[str]],
) -> ExclusionValidation:
    """Validate exclusion contracts and exact source-disposition closure."""

    dispositions = {
        source_key: list(matches)
        for source_key, matches in variant_dispositions.items()
    }
    exclusions = require_list(inventory.get("exclusions"), "exclusions")
    exclusion_ids: list[str] = []
    empty_dispositions: dict[str, list[str]] = {}
    non_descriptor_recipes: list[str] = []
    for index, raw_exclusion in enumerate(exclusions):
        entry = validate_exclusion_entry(raw_exclusion, index)
        exclusion_ids.append(entry.exclusion_id)
        for source_key in entry.source_dispositions:
            dispositions.setdefault(source_key, []).append(
                f"exclusion:{entry.exclusion_id}"
            )
        for recipe in entry.empty_recipes:
            empty_dispositions.setdefault(recipe, []).append(entry.exclusion_id)
        non_descriptor_recipes.extend(entry.non_descriptor_recipes)
    require_unique(exclusion_ids, "exclusion ids")
    require_unique(non_descriptor_recipes, "non-descriptor exclusion recipes")
    validate_source_disposition_closure(
        source,
        dispositions,
        empty_dispositions,
        non_descriptor_recipes,
    )
    return ExclusionValidation(exclusions=exclusions)


def validate_variant_topology_bindings(
    vocabulary: Vocabulary, variants: list[dict[str, Any]]
) -> None:
    for variant in variants:
        variant_id = variant["id"]
        platform_topologies = {
            vocabulary.platforms[platform_id]["topology"]
            for platform_id in variant["platforms"]
        }
        if len(platform_topologies) != 1:
            fail(f"variant {variant_id} spans incompatible platform topologies")
        platform_topology = next(iter(platform_topologies))
        expected_constraint = EXPECTED_GPU_CONSTRAINT_BY_TOPOLOGY.get(platform_topology)
        if platform_topology == "npu_compatibility":
            expected_constraint = EXPECTED_NPU_VARIANT_CONSTRAINTS.get(variant_id)
        if expected_constraint != variant["constraints"]:
            fail(
                f"variant {variant_id} constraint profile must match its platform "
                "topology and accepted NPU role"
            )
