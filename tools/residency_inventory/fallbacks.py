"""Validate the closed fail-safe meanings of residency fallbacks."""

from __future__ import annotations

from typing import Any

from .contract import fail

EXPECTED_FALLBACK_SEMANTICS = {
    "residency_admission_refuse_unknown_demand_v1": {
        "guard": "complete demand or capacity authority unavailable",
        "effect": (
            "refuse before victim selection, reclamation, discovery spawn, or "
            "backend spawn"
        ),
    },
    "residency_load_retry_refuse_unproven_victim_set_v1": {
        "guard": "complete retry victim and release proof unavailable",
        "effect": "refuse retry without changing residency",
    },
    "residency_pressure_report_only_unvalidated_v1": {
        "guard": "reporting evidence valid but action authority unavailable",
        "effect": "report only and preserve residency",
    },
    "residency_pressure_disabled_invalid_evidence_v1": {
        "guard": "reporting evidence missing, stale, unhealthy, or incoherent",
        "effect": "disable pressure automation and preserve residency",
    },
    "residency_startup_block_group_v1": {
        "guard": "complete grouped startup authority unavailable",
        "effect": "load no group member and preserve saved preferences",
    },
    "residency_recovery_block_unproven_release_v1": {
        "guard": "ownership or verified release unavailable",
        "effect": "block lifecycle readiness and retain maximum plausible claims",
    },
    "residency_unload_preserve_live_use_v1": {
        "guard": "live-use fencing unavailable",
        "effect": "refuse unload and preserve resident",
    },
    "residency_pin_mutation_refuse_stale_or_unready_v1": {
        "guard": "generation stale or lifecycle unready",
        "effect": "refuse pin mutation",
    },
    "residency_npu_conflict_preserve_refuse_v1": {
        "guard": "NPU compatibility conflict lacks validated displacement authority",
        "effect": "preserve every incumbent and refuse incoming load",
    },
    "hatchery_rocm_admission_refuse_unknown_capacity_v1": {
        "guard": "Hatchery capacity authority insufficient",
        "effect": (
            "refuse before victim selection, reclamation, discovery spawn, or "
            "backend spawn"
        ),
    },
    "hatchery_rocm_pressure_report_only_v1": {
        "guard": "Hatchery reporting evidence valid but action authority unavailable",
        "effect": "report only and preserve residency",
    },
    "hatchery_rocm_pressure_disabled_invalid_evidence_v1": {
        "guard": "Hatchery reporting evidence invalid",
        "effect": "disable pressure automation and preserve residency",
    },
    "hatchery_rocm_startup_block_group_v1": {
        "guard": "Hatchery grouped startup authority insufficient",
        "effect": "load no group member and preserve saved preferences",
    },
    "hatchery_rocm_recovery_block_readiness_v1": {
        "guard": "Hatchery ownership or release unproved",
        "effect": "block lifecycle readiness and retain maximum plausible claims",
    },
}


def validate_fallback_semantics(fallback_registry: dict[str, Any]) -> None:
    """Require every fallback ID to retain its accepted guard and effect."""

    actual = {
        fallback_id: {
            "guard": definition["guard"],
            "effect": definition["effect"],
        }
        for fallback_id, definition in fallback_registry.items()
    }
    if actual != EXPECTED_FALLBACK_SEMANTICS:
        fail("fallback_registry guard/effect values must equal the accepted contracts")
