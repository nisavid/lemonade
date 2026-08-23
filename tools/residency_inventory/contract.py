"""Closed inventory vocabulary and structural validation primitives."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, NoReturn

EXPECTED_CAPABILITY_LEVELS = {
    "unsupported",
    "fallback_only",
    "modeled",
    "validated",
}
EXPECTED_DELIVERY_STATES = {
    "absent",
    "implemented_unverified",
    "release_verified",
}
EXPECTED_OPERATION_TEMPLATES = {
    "ADM",
    "LFR",
    "PRE",
    "STA",
    "REC",
    "UNL",
    "PIN",
    "NPC",
}
OPERATION_LEAVES_BY_TEMPLATE = {
    "ADM": {"admission"},
    "LFR": {"admission"},
    "PRE": {"pressure_reclamation"},
    "STA": {"startup_load"},
    "REC": {
        "service_termination",
        "dead_backend_pruning",
        "same_epoch_recovery_cleanup",
        "prior_epoch_owner_cleanup",
        "artifact_scope_recovery_cleanup",
    },
    "UNL": {"explicit_unload", "force_unload"},
    "PIN": {
        "saved_pin_mutation",
        "runtime_pin_mutation",
        "legacy_pin_batch",
        "resident_state_recovery_cleanup",
    },
    "NPC": {"admission"},
}
EXPECTED_CONSTRAINT_KINDS = {
    "flm_type_slot",
    "gpu_provider_resolved_capacity",
    "gpu_shared_residency",
    "host_effects_provider_resolved",
    "host_memavailable_floor",
    "model_type_pool",
    "npu_cross_family",
    "npu_exclusive",
    "ownership",
}
CAPABILITY_RANK = {
    "unsupported": 0,
    "fallback_only": 1,
    "modeled": 2,
    "validated": 3,
}
MEMORY_CONSTRAINTS = {
    "gpu_shared_residency",
    "gpu_provider_resolved_capacity",
    "host_memavailable_floor",
    "host_effects_provider_resolved",
}
MEMORY_CONSTRAINTS_BY_HARDWARE_TOPOLOGY = {
    "shared_gtt": {
        "gpu_shared_residency",
        "host_memavailable_floor",
    },
}

SourceSupportKey = tuple[str, str, str, str, str, str]


class ResidencyInventoryError(ValueError):
    """One bounded domain error for every public validator failure."""


@dataclass(frozen=True)
class OperationApplicability:
    """Resolved operations while retaining whether applicability was wildcarded."""

    operations: frozenset[str]
    is_wildcard: bool


def fail(message: str) -> NoReturn:
    raise ResidencyInventoryError(message)


def require_mapping(value: Any, label: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        fail(f"{label} must be an object")
    return value


def require_list(value: Any, label: str) -> list[Any]:
    if not isinstance(value, list):
        fail(f"{label} must be an array")
    return value


def require_string(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value:
        fail(f"{label} must be a nonempty string")
    return value


def require_string_list(value: Any, label: str, *, nonempty: bool = True) -> list[str]:
    """Return a string list after enforcing nonemptiness and unique members."""

    members = require_list(value, label)
    if nonempty and not members:
        fail(f"{label} must not be empty")
    if not all(isinstance(member, str) and member for member in members):
        fail(f"{label} must contain only nonempty strings")
    result = list(members)
    require_unique(result, label)
    return result


def require_unique(values: list[str], label: str) -> None:
    if len(values) != len(set(values)):
        fail(f"{label} contains duplicate values")


def require_exact_keys(value: dict[str, Any], expected: set[str], label: str) -> None:
    actual = set(value)
    if actual != expected:
        missing = sorted(expected - actual)
        extra = sorted(actual - expected)
        fail(f"{label} has wrong fields; missing={missing}, extra={extra}")


def require_registry_keys(registry: dict[str, Any], label: str) -> None:
    if not registry:
        fail(f"{label} must not be empty")
    for registry_id in registry:
        require_string(registry_id, f"{label} ID")
