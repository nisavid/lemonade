"""Validate source-derived providers, topology, model types, and refinements."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from .contract import (
    SourceSupportKey,
    fail,
    require_exact_keys,
    require_mapping,
    require_registry_keys,
    require_string,
    require_string_list,
)
from .profile_contracts import (
    ACCEPTED_HARDWARE_PROFILE_IDENTITIES,
    HATCHERY_SHARED_GTT_ATOM,
    HATCHERY_VULKAN_SHARED_GTT_ATOM,
)
from .source import FrozenSourceClosure, source_support_from_mapping

ACCEPTED_PROVIDER_KEYS = {
    "cpu": "cpu",
    "system": "system",
    "metal": "metal",
    "cuda": "cuda",
    "vulkan": "vulkan",
    "rocm": "rocm",
    "npu": "xdna2",
}
ACCEPTED_TOPOLOGY_RULES = {
    "cpu": "host_compatibility",
    "metal": "unified",
    "amd_gpu": "provider_resolved",
    "nvidia_gpu": "provider_resolved",
    "amd_npu": "npu_compatibility",
}


@dataclass(frozen=True)
class PlatformContractValidation:
    """Source atoms selected by each material hardware profile."""

    hardware_atoms: dict[str, SourceSupportKey]


def validate_provider_and_topology_rules(
    inventory: dict[str, Any], source: FrozenSourceClosure
) -> tuple[dict[str, str], dict[str, str]]:
    provider_keys = require_mapping(inventory.get("provider_keys"), "provider_keys")
    require_registry_keys(provider_keys, "provider_keys")
    source_backends = {atom[1] for atom in source.support}
    if set(provider_keys) != source_backends:
        fail(
            "provider_keys must exactly cover frozen source backends; "
            f"missing={sorted(source_backends - set(provider_keys))}, "
            f"extra={sorted(set(provider_keys) - source_backends)}"
        )
    normalized_providers = {
        backend: require_string(provider, f"provider_keys.{backend}")
        for backend, provider in provider_keys.items()
    }
    if normalized_providers != ACCEPTED_PROVIDER_KEYS:
        fail("provider_keys must equal the accepted backend-to-provider mapping")

    topology_rules = require_mapping(inventory.get("topology_rules"), "topology_rules")
    require_registry_keys(topology_rules, "topology_rules")
    source_accelerators = {atom[3] for atom in source.support}
    if set(topology_rules) != source_accelerators:
        fail(
            "topology_rules must exactly cover frozen source accelerators; "
            f"missing={sorted(source_accelerators - set(topology_rules))}, "
            f"extra={sorted(set(topology_rules) - source_accelerators)}"
        )
    normalized_topologies = {
        accelerator: require_string(topology, f"topology_rules.{accelerator}")
        for accelerator, topology in topology_rules.items()
    }
    if normalized_topologies != ACCEPTED_TOPOLOGY_RULES:
        fail("topology_rules must equal the accepted accelerator-to-topology mapping")
    return normalized_providers, normalized_topologies


def validate_recipe_model_type_contracts(
    inventory: dict[str, Any],
    source: FrozenSourceClosure,
    variants: list[dict[str, Any]],
    enum_model_types: set[str],
) -> None:
    contracts = require_mapping(
        inventory.get("recipe_model_type_contracts"),
        "recipe_model_type_contracts",
    )
    require_registry_keys(contracts, "recipe_model_type_contracts")
    expected_recipes = source.descriptor_recipes - source.empty_support_recipes
    if set(contracts) != expected_recipes:
        fail(
            "recipe_model_type_contracts must exactly cover local descriptor "
            f"recipes; missing={sorted(expected_recipes - set(contracts))}, "
            f"extra={sorted(set(contracts) - expected_recipes)}"
        )
    for recipe, raw_contract in contracts.items():
        _validate_recipe_model_type_contract(
            recipe,
            raw_contract,
            source,
            enum_model_types,
        )
    _validate_variant_model_type_contracts(variants, contracts)


def _validate_recipe_model_type_contract(
    recipe: str,
    raw_contract: Any,
    source: FrozenSourceClosure,
    enum_model_types: set[str],
) -> None:
    contract = require_mapping(raw_contract, f"recipe_model_type_contracts.{recipe}")
    require_exact_keys(
        contract,
        {"derivation", "model_types"},
        f"recipe_model_type_contracts.{recipe}",
    )
    derivation = require_string(
        contract["derivation"],
        f"recipe_model_type_contracts.{recipe}.derivation",
    )
    expected_derivation = "flm_slot_policy" if recipe == "flm" else "server_models"
    if derivation != expected_derivation:
        fail(
            f"recipe_model_type_contracts.{recipe}.derivation must be "
            f"{expected_derivation}"
        )
    model_types = set(
        require_string_list(
            contract["model_types"],
            f"recipe_model_type_contracts.{recipe}.model_types",
        )
    )
    if not model_types <= enum_model_types:
        fail(f"recipe model-type contract {recipe} uses an unknown model type")
    expected_model_types = source.recipe_model_types.get(recipe, set())
    if not expected_model_types:
        fail(f"frozen sources derive no model types for recipe {recipe}")
    if model_types != expected_model_types:
        fail(
            f"recipe model-type contract {recipe} does not match frozen source; "
            f"expected={sorted(expected_model_types)}, got={sorted(model_types)}"
        )
    if recipe == "flm" and "reranking" in model_types:
        fail("FLM model-type contract may not declare a reranking slot")


def _validate_variant_model_type_contracts(
    variants: list[dict[str, Any]],
    contracts: dict[str, Any],
) -> None:
    for variant in variants:
        recipe = variant["recipe"]
        if recipe not in contracts:
            fail(f"variant {variant['id']} has no recipe model-type contract")
        expected = set(contracts[recipe]["model_types"])
        actual = set(variant["model_types"])
        if actual != expected:
            fail(f"variant {variant['id']} model types must equal its recipe contract")


def validate_platform_predicates(
    platforms: dict[str, dict[str, Any]],
    variants: list[dict[str, Any]],
    provider_keys: dict[str, str],
    topology_rules: dict[str, str],
) -> None:
    for variant in variants:
        backend = variant["backend"]
        if backend not in provider_keys:
            fail(f"variant {variant['id']} has no derived provider key")
        for platform_id in variant["platforms"]:
            platform = platforms[platform_id]
            expected_provider = provider_keys[backend]
            if platform["provider"] != expected_provider:
                fail(
                    f"platform {platform_id} provider must be derived from backend "
                    f"{backend} as {expected_provider}"
                )
            accelerator = platform["accelerator"]
            if accelerator not in topology_rules:
                fail(f"platform {platform_id} accelerator has no topology rule")
            expected_topology = topology_rules[accelerator]
            if platform["topology"] != expected_topology:
                fail(
                    f"platform {platform_id} topology must be derived from accelerator "
                    f"{accelerator} as {expected_topology}"
                )


def validate_hardware_profiles(
    inventory: dict[str, Any],
    source: FrozenSourceClosure,
    topology_rules: dict[str, str],
    runtime_binding_kinds: set[str],
) -> dict[str, SourceSupportKey]:
    profiles = require_mapping(inventory.get("hardware_profiles"), "hardware_profiles")
    require_registry_keys(profiles, "hardware_profiles")
    atoms: dict[str, SourceSupportKey] = {}
    for profile_id, raw_profile in profiles.items():
        profile = require_mapping(raw_profile, f"hardware_profiles.{profile_id}")
        require_exact_keys(
            profile,
            {
                "base_topology",
                "topology",
                "source_support",
                "evidence_document",
                "required_runtime_bindings",
            },
            f"hardware_profiles.{profile_id}",
        )
        selected_atoms = source_support_from_mapping(
            profile["source_support"],
            f"hardware_profiles.{profile_id}.source_support",
        )
        if len(selected_atoms) != 1:
            fail(f"hardware profile {profile_id} must select exactly one source atom")
        atom = next(iter(selected_atoms))
        if atom not in source.support:
            fail(f"hardware profile {profile_id} selects no frozen source atom")
        accelerator = atom[3]
        base_topology = require_string(
            profile["base_topology"], f"hardware_profiles.{profile_id}.base_topology"
        )
        if base_topology != topology_rules[accelerator]:
            fail(
                f"hardware profile {profile_id} base_topology disagrees with its "
                "source accelerator"
            )
        topology = require_string(
            profile["topology"], f"hardware_profiles.{profile_id}.topology"
        )
        if topology != base_topology and not (
            atom in {HATCHERY_SHARED_GTT_ATOM, HATCHERY_VULKAN_SHARED_GTT_ATOM}
            and topology == "shared_gtt"
        ):
            fail(
                f"hardware profile {profile_id} uses an unauthorized topology refinement"
            )
        required_bindings = set(
            require_string_list(
                profile["required_runtime_bindings"],
                f"hardware_profiles.{profile_id}.required_runtime_bindings",
            )
        )
        if not required_bindings <= runtime_binding_kinds:
            fail(f"hardware profile {profile_id} references an unknown runtime binding")
        identity = {
            "base_topology": base_topology,
            "topology": topology,
            "source_support": atom,
            "evidence_document": profile["evidence_document"],
            "required_runtime_bindings": required_bindings,
        }
        if identity != ACCEPTED_HARDWARE_PROFILE_IDENTITIES[profile_id]:
            fail(
                f"hardware profile {profile_id} must equal its accepted semantic identity"
            )
        atoms[profile_id] = atom
    return atoms


def validate_platform_contracts(
    inventory: dict[str, Any],
    source: FrozenSourceClosure,
    platforms: dict[str, dict[str, Any]],
    variants: list[dict[str, Any]],
    enum_model_types: set[str],
    runtime_binding_kinds: set[str],
    provider_keys: dict[str, str],
    topology_rules: dict[str, str],
) -> PlatformContractValidation:
    validate_recipe_model_type_contracts(inventory, source, variants, enum_model_types)
    validate_platform_predicates(platforms, variants, provider_keys, topology_rules)
    hardware_atoms = validate_hardware_profiles(
        inventory, source, topology_rules, runtime_binding_kinds
    )
    return PlatformContractValidation(hardware_atoms)
