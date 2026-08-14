"""Closed semantic identities for accepted material residency profiles."""

from __future__ import annotations

from typing import Any

from .contract import SourceSupportKey

HATCHERY_SHARED_GTT_ATOM: SourceSupportKey = (
    "llamacpp",
    "rocm",
    "linux",
    "amd_gpu",
    "gfx1151",
    "stable",
)

ACCEPTED_HARDWARE_PROFILE_IDENTITIES: dict[str, dict[str, Any]] = {
    "hatchery-gfx1151-shared-gtt-v1": {
        "base_topology": "provider_resolved",
        "topology": "shared_gtt",
        "source_support": HATCHERY_SHARED_GTT_ATOM,
        "evidence_document": {
            "locator": (
                "docs/research/hatchery-campaign-parameters.md"
                "#live-hatchery-observations"
            ),
            "sha256": (
                "c43642e3ef985ee02d2e2e42d6a8ee8ac3e6f40703b88de97931fb91b43e501f"
            ),
        },
        "required_runtime_bindings": frozenset(
            {"device_identity", "driver_runtime_closure"}
        ),
    }
}

ACCEPTED_CONFIGURATION_PROFILE_IDENTITIES: dict[str, dict[str, Any]] = {
    "profile-free-residency-estimation-v1-text-only": {
        "model_types": frozenset({"llm"}),
        "document": {
            "locator": (
                "docs/research/profile-free-residency-estimation.md"
                "#v1-configuration-predicate"
            ),
            "sha256": (
                "fb8da2962ff9070cc172995806f81ccf6c1ada267bd75f4edab9a29d7fb3cdda"
            ),
        },
    }
}

ACCEPTED_WORKLOAD_PROFILE_IDENTITIES: dict[str, dict[str, Any]] = {
    "hatchery-text-generation-campaign-v1": {
        "document": {
            "locator": (
                "docs/research/hatchery-campaign-parameters.md"
                "#workload-partition-for-the-first-predictor-campaign"
            ),
            "sha256": (
                "27840de0b38f113c13edcee700f4c27a7916a8d6888e10d966ffe39b11124aea"
            ),
        }
    }
}

ACCEPTED_PREDICTOR_RULE_IDENTITIES: dict[str, dict[str, Any]] = {
    "hatchery-llamacpp-rocm-profile-free-v1": {
        "confidence_target": "validated_predictor",
        "document": {
            "locator": (
                "docs/research/profile-free-residency-estimation.md"
                "#validation-and-promotion-threshold"
            ),
            "sha256": (
                "a3184cf7a6c3e436ce41e11dee30ca1c7c780ac01374125d3880f6c46f46aef9"
            ),
        },
    }
}

ACCEPTED_OBSERVATION_CONTRACT_IDENTITIES: dict[str, dict[str, Any]] = {
    "hatchery-gtt-host-observation-v1": {
        "constraints": frozenset({"gpu_shared_residency", "host_memavailable_floor"}),
        "document": {
            "locator": (
                "docs/research/hatchery-campaign-parameters.md"
                "#live-hatchery-observations"
            ),
            "sha256": (
                "c43642e3ef985ee02d2e2e42d6a8ee8ac3e6f40703b88de97931fb91b43e501f"
            ),
        },
    }
}

ACCEPTED_PROFILE_IDENTITIES = {
    "hardware_profiles": ACCEPTED_HARDWARE_PROFILE_IDENTITIES,
    "configuration_profiles": ACCEPTED_CONFIGURATION_PROFILE_IDENTITIES,
    "workload_profiles": ACCEPTED_WORKLOAD_PROFILE_IDENTITIES,
    "predictor_rules": ACCEPTED_PREDICTOR_RULE_IDENTITIES,
    "observation_contracts": ACCEPTED_OBSERVATION_CONTRACT_IDENTITIES,
}

PROFILE_KIND_LABELS = {
    "hardware_profiles": "hardware profile",
    "configuration_profiles": "configuration profile",
    "workload_profiles": "workload profile",
    "predictor_rules": "predictor rule",
    "observation_contracts": "observation contract",
}
