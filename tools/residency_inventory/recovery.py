"""Validate closed lifecycle recovery and release contracts."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from .contract import (
    fail,
    require_exact_keys,
    require_mapping,
    require_registry_keys,
    require_string,
    require_string_list,
)

EXPECTED_RECOVERY_PROFILES = {
    "native_subprocess_tree": {
        "launch": "prepared",
        "containment": "complete_descendant_process_tree",
        "ownership": "lemonade_spawned_process_tree",
        "verified_release": ["process", "resource", "constraint"],
    },
    "python_or_async_job_tree": {
        "launch": "prepared",
        "containment": "complete_worker_or_async_job_membership",
        "ownership": "lemonade_worker_or_async_job",
        "verified_release": [
            "native_completion_or_cancellation_join",
            "resource",
        ],
    },
    "flm_system_managed": {
        "launch": "prepared",
        "containment": (
            "serving_process_or_service_membership_excluding_external_package_and_"
            "model_store"
        ),
        "ownership": "lemonade_serving_process_or_service_only",
        "verified_release": [
            "serving_process_or_service_membership",
            "device_claim",
        ],
    },
}


@dataclass(frozen=True)
class RecoveryProfileValidation:
    """Typed recovery profiles available to variants and exact cells."""

    profiles: dict[str, dict[str, Any]]


def validate_recovery_profiles(inventory: dict[str, Any]) -> RecoveryProfileValidation:
    """Require exact containment, ownership, and verified-release semantics."""

    raw_profiles = require_mapping(
        inventory.get("recovery_profiles"), "recovery_profiles"
    )
    require_registry_keys(raw_profiles, "recovery_profiles")
    normalized: dict[str, dict[str, Any]] = {}
    for profile_id, raw_profile in raw_profiles.items():
        label = f"recovery profile {profile_id}"
        profile = require_mapping(raw_profile, label)
        require_exact_keys(
            profile,
            {"launch", "containment", "ownership", "verified_release"},
            label,
        )
        normalized[profile_id] = {
            "launch": require_string(profile["launch"], f"{label}.launch"),
            "containment": require_string(
                profile["containment"], f"{label}.containment"
            ),
            "ownership": require_string(profile["ownership"], f"{label}.ownership"),
            "verified_release": require_string_list(
                profile["verified_release"], f"{label}.verified_release"
            ),
        }
    if normalized != EXPECTED_RECOVERY_PROFILES:
        fail("recovery_profiles must equal the accepted closed recovery semantics")
    return RecoveryProfileValidation(profiles=normalized)
