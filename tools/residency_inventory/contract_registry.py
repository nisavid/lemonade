"""Validate the frozen generated-contract registry and inactive catalog closure."""

from __future__ import annotations

import copy
import hashlib
import json
import re
import unicodedata
from typing import Any

from .contract import (
    fail,
    require_exact_keys,
    require_list,
    require_mapping,
    require_registry_keys,
    require_string,
    require_string_list,
)
from .path import _path_tokens

EXPECTED_REGISTRY_DIGEST = (
    "cb0fcefcd8fa46b4c600e19ae13144ff7192d43aedcb8cca0901eafd28f8d832"
)
EXPECTED_REGISTRY_KEYS = [
    "schema",
    "mode_registry",
    "operation_registry",
    "reason_envelope_registry",
    "request_context_registry",
    "request_stage_registry",
    "reason_registry",
    "presentation_registry",
    "detail_schema_registry",
    "retention_registry",
    "http_auth_registry",
    "schema_registry",
]
EXPECTED_ALIAS_SIZES = {
    "all_operations": 14,
    "all_resource_operations": 10,
    "recovery_capable_resource_operations": 9,
    "recovery_capable_operations": 13,
    "terminally_quarantinable_resource_operations": 9,
    "all_resident_state_operations": 4,
    "planned_resource_operations": 5,
    "capacity_consuming_operations": 2,
    "resource_action_operations": 10,
    "automatic_protected_operations": 3,
    "live_use_protected_operations": 6,
    "readiness_dependent_operations": 3,
}
EXPECTED_ENVELOPE_SIZES = {
    "operation_revision": 34,
    "request_error": 39,
    "resource_diagnostic": 9,
    "authority_transaction_result": 2,
    "coordinator_step_result": 10,
    "response_diagnostic": 2,
    "artifact_writer_job_revision": 6,
    "artifact_writer_request_result": 6,
}
EXPECTED_SCHEMA_KEYS = [
    "artifact_quarantine_record",
    "artifact_writer_job_revision",
    "artifact_writer_request_result",
    "authority_transaction_result",
    "coordinator_step_result",
    "deployment_local_overlay_object",
    "operation_revision",
    "overlay_activation_root",
    "profiling_input_envelope",
    "reason",
    "request_error",
    "residency_profiles",
    "resource_diagnostic",
    "response_diagnostic",
    "staged_import_session_record",
]
EXPECTED_MODE_REGISTRY = {
    "configured_intent_sources": [
        "fixed_policy",
        "default",
        "operator",
        "migration",
    ],
    "configured_intents": {
        "admission": {
            "values": ["predictive_memory_capacity"],
            "sources": ["fixed_policy"],
        },
        "pressure": {
            "values": ["automatic", "report_only", "disabled"],
            "sources": ["default", "operator", "migration"],
        },
        "startup": {
            "values": ["load_saved_pins", "disabled"],
            "sources": ["default", "operator", "migration"],
        },
        "recovery": {
            "values": ["restore_lifecycle_readiness"],
            "sources": ["fixed_policy"],
        },
    },
    "effective_modes": {
        "admission": [
            "capacity_planned",
            "backend_enforced",
            "count_limited",
            "refused",
        ],
        "pressure": ["automatic_reclamation", "report_only", "disabled"],
        "startup": ["automatic_grouped", "disabled", "blocked"],
        "recovery": ["ready", "cleanup_only", "blocked"],
    },
    "footprint_confidence": [
        "enforced_complete",
        "validated_predictor",
        "calibrated_instance",
        "incomplete",
        "unknown",
    ],
    "signal_evidence_states": [
        "valid",
        "missing",
        "stale",
        "unhealthy",
        "incoherent",
        "superseded",
    ],
}
EXPECTED_RESOURCE_VOCABULARIES = {
    "authority_transaction_results": ["succeeded", "failed", "conflict"],
    "coordinator_step_kinds": [
        "artifact_acquisition",
        "recipe_options_persistence",
        "artifact_writer_quiescence",
        "model_artifact_delete",
        "model_identity_delete",
    ],
    "coordinator_step_results": [
        "not_requested",
        "not_started",
        "succeeded",
        "failed",
        "superseded",
        "recovery_required",
    ],
    "effect_dispositions": ["no_effect", "verified_effect", "ambiguous"],
    "publish_dispositions": ["prevented", "published_expected", "unknown"],
    "writer_normalized_states": [
        "active",
        "terminal_success",
        "terminal_superseded",
        "terminal_failed",
    ],
    "writer_causes": [
        "none",
        "native_failed",
        "client_cancelled",
        "superseded_by_model_delete",
        "staged_import_expired",
        "staged_import_validation_failed",
        "staged_import_failed",
        "staged_import_cleanup_incomplete",
    ],
    "writer_request_results": [
        "completed",
        "failed",
        "cancelled",
        "expired",
        "quarantined",
        "superseded_by_model_delete",
    ],
    "staged_import_states": [
        "prepared",
        "uploading",
        "verifying",
        "publishing",
        "closing",
        "terminal",
    ],
    "staged_import_terminal_results": [
        "committed",
        "aborted",
        "expired",
        "failed",
        "quarantined",
    ],
    "quarantine_states": ["cleanup_pending", "attempt_in_progress", "cleared"],
    "quarantine_origins": [
        "load_acquisition",
        "model_delete",
        "staged_import_abort",
        "staged_import_publish",
        "staged_import_adopt",
    ],
    "quarantine_resolution_intents": [
        "preserve_baseline",
        "complete_publish",
        "complete_delete",
    ],
    "resolution_marker_kinds": [
        "none",
        "artifact_publish_irreversible",
        "model_delete_requested",
    ],
    "identity_targets": ["preserve_baseline", "publish_target", "delete_target"],
    "baseline_states": ["present", "absent"],
    "identity_artifact_dispositions": [
        "unknown",
        "baseline_present_verified",
        "baseline_absent_verified",
        "target_published",
        "target_deleted",
    ],
    "staging_dispositions": ["not_applicable", "unknown", "removed"],
}
EXPECTED_RETENTION_REGISTRY = {
    "operation": {
        "active_expires": False,
        "recovery_required_expires": False,
        "terminal_detail_seconds": 86400,
        "forgotten_after_terminal_seconds": 604800,
    },
    "artifact_quarantine": {
        "pending_expires": False,
        "attempt_expires": False,
        "cleared_detail_seconds": 86400,
        "forgotten_after_clear_seconds": 604800,
    },
    "staged_import": {
        "terminal_detail_seconds": 86400,
        "forgotten_after_terminal_seconds": 604800,
    },
    "status_capability": {
        "lifetime_seconds": 86400,
        "refresh_window_seconds": 3600,
        "overlap_seconds": 3600,
    },
}
CONTEXT_KEYS_BY_ENVELOPE = {
    "operation_revision": {
        "priority_band",
        "contexts",
        "http_mappings",
        "endpoint_success_overrides",
        "secondary_only",
    },
    "request_error": {
        "priority_band",
        "contexts",
        "staged_contexts",
        "http_status",
        "authority_states",
        "boundary",
        "compatibility_epoch",
        "endpoint_overrides",
        "receipt_replay",
        "required_headers",
    },
    "resource_diagnostic": {
        "priority_band",
        "contexts",
        "contexts_by_authority_kind",
        "authority_states",
        "subject_generation",
    },
    "authority_transaction_result": {
        "priority_band",
        "contexts",
        "result",
        "http_status",
        "primary_required",
    },
    "coordinator_step_result": {
        "priority_band",
        "step_kinds",
        "results",
        "contexts",
        "http_status",
        "primary_required",
        "effect_dispositions",
        "http_mappings",
    },
    "response_diagnostic": {
        "priority_band",
        "endpoint_alias",
        "endpoint_rows",
        "compatibility_epoch",
        "required_projection",
    },
    "artifact_writer_job_revision": {
        "priority_band",
        "causes",
        "normalized_states",
        "native_families",
        "primary_required",
        "resume_allowed",
        "effect_dispositions",
        "publish_dispositions",
        "quarantine_record_required",
    },
    "artifact_writer_request_result": {
        "priority_band",
        "results",
        "native_families",
        "primary_required",
        "http_status",
    },
}
DETAIL_FIELD_TYPES = {
    "boolean",
    "closed_action_kind",
    "closed_endpoint_boundary",
    "enum",
    "enum_array",
    "enum_ref",
    "opaque",
    "rfc3339",
    "uint64",
    "utf8",
}
SCHEMA_FIELD_TYPES = {
    "boolean",
    "detail_schema_join",
    "enum",
    "enum_ref",
    "fallback_array",
    "git_commit_sha1",
    "http_status",
    "identity_scope_array",
    "literal",
    "local_overlay_authority_status",
    "local_overlay_claim_closure",
    "local_overlay_confidence_basis_points",
    "local_overlay_decision_trace_reference",
    "local_overlay_deployment_identity",
    "local_overlay_expiry",
    "local_overlay_method_identity",
    "local_overlay_object_status",
    "local_overlay_previous_root_reference",
    "local_overlay_positive_uint64",
    "local_overlay_required_true",
    "local_overlay_root_transition",
    "local_overlay_safety_margin_claim_closure",
    "local_overlay_schema_version",
    "local_overlay_selector_identity",
    "local_overlay_source_generations",
    "local_overlay_timestamp",
    "nullable_enum_ref",
    "nullable_opaque",
    "nullable_reason_code",
    "nullable_rfc3339",
    "object",
    "opaque",
    "opaque_array",
    "promotion_unit_array",
    "reason",
    "reason_array",
    "reason_code",
    "reason_presentation",
    "rfc3339",
    "schema_literal",
    "schema_ref",
    "sha256",
    "strong_etag",
    "tagged_baseline",
    "uint64",
    "uint64_array",
    "utf8",
}
SCHEMA_FIELD_EXTRA_KEYS = {field_type: set() for field_type in SCHEMA_FIELD_TYPES}
SCHEMA_FIELD_EXTRA_KEYS.update(
    {
        "enum": {"values"},
        "enum_ref": {"ref"},
        "fallback_array": {"min_items", "max_items"},
        "http_status": {"minimum", "maximum"},
        "identity_scope_array": {"min_items", "max_items", "canonical_sorted"},
        "literal": {"value"},
        "nullable_enum_ref": {"ref"},
        "nullable_opaque": {"max_bytes"},
        "opaque": {"max_bytes"},
        "promotion_unit_array": {"min_items", "max_items"},
        "reason_array": {"ordered"},
        "reason_code": {"max_bytes"},
        "schema_literal": {"value"},
        "schema_ref": {"ref"},
        "utf8": {"max_bytes"},
    }
)


def _require_ordered_keys(
    value: dict[str, Any], expected: list[str], label: str
) -> None:
    require_exact_keys(value, set(expected), label)
    if list(value) != expected:
        fail(f"{label} fields are not in canonical order")


def _validate_modes_retention_and_http(registry: dict[str, Any]) -> None:
    if registry.get("mode_registry") != EXPECTED_MODE_REGISTRY:
        fail("contract_registry.mode_registry is not the accepted closed vocabulary")
    if registry.get("retention_registry") != EXPECTED_RETENTION_REGISTRY:
        fail("contract_registry.retention_registry is not the accepted retention table")
    http_auth = require_mapping(
        registry.get("http_auth_registry"), "contract_registry.http_auth_registry"
    )
    _require_ordered_keys(
        http_auth,
        [
            "authority_kinds",
            "authority_states",
            "authority_stages",
            "artifact_writer_origins",
            "native_families",
            "authorization_modes",
            "operation_http",
            "compatibility_epoch",
            "resource_vocabularies",
        ],
        "contract_registry.http_auth_registry",
    )
    expected_fixed = {
        "authority_kinds": ["configuration", "pin_preference"],
        "authority_states": ["healthy", "absent", "degraded_prior_root"],
        "authority_stages": [
            "configuration",
            "model_identity",
            "artifact_writer",
            "recipe_options",
            "lifecycle",
        ],
        "artifact_writer_origins": ["artifact_route", "load_coordinator"],
        "native_families": [
            "generic_job",
            "download_job",
            "ollama_pull_stream",
            "public_sse_writer",
            "public_blocking_writer",
            "staged_import_writer",
        ],
        "authorization_modes": [
            "regular",
            "admin",
            "residency_status_capability",
            "residency_quarantine_status_capability",
            "loopback_compatibility",
        ],
        "operation_http": {
            "accepted_nonterminal": 202,
            "recovery_required": 503,
            "known_read": 200,
            "tombstone": 410,
            "unknown_or_forgotten": 404,
        },
        "compatibility_epoch": "pre_announced_api_major_boundary",
    }
    for key, expected in expected_fixed.items():
        if http_auth.get(key) != expected:
            fail(f"contract_registry.http_auth_registry.{key} drifted")
    if http_auth.get("resource_vocabularies") != EXPECTED_RESOURCE_VOCABULARIES:
        fail("contract_registry resource vocabularies are not the accepted closed sets")


def _require_reference_list(
    value: Any, label: str, allowed: set[str], *, nonempty: bool = True
) -> list[str]:
    members = require_string_list(value, label, nonempty=nonempty)
    unknown = set(members) - allowed
    if unknown:
        fail(f"{label} contains unknown references: {sorted(unknown)}")
    return members


def _validate_http_status(value: Any, label: str, *, nullable: bool = False) -> None:
    if nullable and value is None:
        return
    if isinstance(value, bool) or not isinstance(value, int) or not 100 <= value <= 599:
        fail(f"{label} must be a registered HTTP status")


def _validate_operation_state(
    state: Any, label: str, phases: set[str], outcomes: set[str]
) -> None:
    state = require_string(state, label)
    if state == "associated_primary_state:secondary":
        return
    match = re.fullmatch(
        r"([^:]+):([^:]+):(primary|secondary|primary\|secondary)", state
    )
    if match is None:
        fail(f"{label} has invalid phase/outcome/role grammar")
    selected_phases = set(match.group(1).split("|"))
    selected_outcomes = set(match.group(2).split("|"))
    if not selected_phases <= phases:
        fail(f"{label} references an unknown operation phase")
    if selected_phases == {"terminal"}:
        if "null" in selected_outcomes or not selected_outcomes <= outcomes:
            fail(f"{label} has an invalid terminal outcome")
    elif "terminal" in selected_phases or selected_outcomes != {"null"}:
        fail(f"{label} gives a nonterminal phase a terminal outcome")


def _validate_operation_context(
    context: dict[str, Any], registry: dict[str, Any], label: str
) -> None:
    require_exact_keys(context, {"operation_kinds", "legal_states"}, label)
    operations = registry["operation_registry"]
    _require_reference_list(
        context.get("operation_kinds"),
        f"{label}.operation_kinds",
        set(operations["aliases"]["all_operations"]),
    )
    states = require_list(context.get("legal_states"), f"{label}.legal_states")
    if not states:
        fail(f"{label}.legal_states must not be empty")
    for index, state in enumerate(states):
        _validate_operation_state(
            state,
            f"{label}.legal_states[{index}]",
            set(operations["phases"]),
            set(operations["terminal_outcomes"]),
        )


def _validate_operation_rule(
    rule: dict[str, Any], registry: dict[str, Any], label: str
) -> None:
    contexts = require_list(rule.get("contexts"), f"{label}.contexts")
    if not contexts:
        fail(f"{label}.contexts must not be empty")
    for index, raw_context in enumerate(contexts):
        _validate_operation_context(
            require_mapping(raw_context, f"{label}.contexts[{index}]"),
            registry,
            f"{label}.contexts[{index}]",
        )
    mappings = require_list(rule.get("http_mappings"), f"{label}.http_mappings")
    outcomes = set(registry["operation_registry"]["terminal_outcomes"])
    phases = set(registry["operation_registry"]["phases"])
    for index, raw_mapping in enumerate(mappings):
        mapping = require_mapping(raw_mapping, f"{label}.http_mappings[{index}]")
        if set(mapping) == {"terminal_outcomes", "status"}:
            _require_reference_list(
                mapping["terminal_outcomes"],
                f"{label}.http_mappings[{index}].terminal_outcomes",
                outcomes,
            )
        elif set(mapping) == {"phases", "status"}:
            _require_reference_list(
                mapping["phases"],
                f"{label}.http_mappings[{index}].phases",
                phases,
            )
        else:
            fail(f"{label}.http_mappings[{index}] has invalid fields")
        _validate_http_status(mapping.get("status"), f"{label}.http_mappings[{index}]")
    for index, status in enumerate(rule.get("endpoint_success_overrides", [])):
        _validate_http_status(status, f"{label}.endpoint_success_overrides[{index}]")


def _validate_request_rule(
    rule: dict[str, Any], registry: dict[str, Any], label: str
) -> None:
    contexts = set(registry["request_context_registry"])
    stages = set(registry["request_stage_registry"])
    _require_reference_list(
        rule.get("contexts"), f"{label}.contexts", contexts, nonempty=False
    )
    staged = require_mapping(rule.get("staged_contexts"), f"{label}.staged_contexts")
    for context, raw_stages in staged.items():
        if context not in contexts:
            fail(f"{label}.staged_contexts references unknown context {context}")
        _require_reference_list(
            raw_stages, f"{label}.staged_contexts.{context}", stages
        )
    if not rule["contexts"] and not staged:
        fail(f"{label} must own a context or staged context")
    _validate_http_status(rule.get("http_status"), f"{label}.http_status")
    if "authority_states" in rule:
        _require_reference_list(
            rule["authority_states"],
            f"{label}.authority_states",
            set(registry["http_auth_registry"]["authority_states"]),
        )
    for index, raw_override in enumerate(rule.get("endpoint_overrides", [])):
        override = require_mapping(raw_override, f"{label}.endpoint_overrides[{index}]")
        if override.get("context") not in contexts:
            fail(f"{label}.endpoint_overrides[{index}] has unknown context")
        if "stage" in override and override["stage"] not in stages:
            fail(f"{label}.endpoint_overrides[{index}] has unknown stage")
        _validate_http_status(
            override.get("status"), f"{label}.endpoint_overrides[{index}].status"
        )


def _validate_context_references(
    rule: dict[str, Any], registry: dict[str, Any], label: str
) -> None:
    contexts = set(registry["request_context_registry"])
    if "contexts" in rule:
        _require_reference_list(
            rule["contexts"], f"{label}.contexts", contexts, nonempty=False
        )
    for authority_kind, raw_contexts in rule.get(
        "contexts_by_authority_kind", {}
    ).items():
        if authority_kind not in registry["http_auth_registry"]["authority_kinds"]:
            fail(f"{label} references unknown authority kind {authority_kind}")
        _require_reference_list(
            raw_contexts, f"{label}.{authority_kind}", contexts, nonempty=False
        )


def _validate_coordinator_rule(
    rule: dict[str, Any], registry: dict[str, Any], label: str
) -> None:
    vocabulary = registry["http_auth_registry"]["resource_vocabularies"]
    _require_reference_list(
        rule.get("step_kinds"),
        f"{label}.step_kinds",
        set(vocabulary["coordinator_step_kinds"]),
    )
    _require_reference_list(
        rule.get("results"),
        f"{label}.results",
        set(vocabulary["coordinator_step_results"]),
    )
    _validate_context_references(rule, registry, label)
    _validate_http_status(
        rule.get("http_status"), f"{label}.http_status", nullable=True
    )
    if "effect_dispositions" in rule:
        _require_reference_list(
            rule["effect_dispositions"],
            f"{label}.effect_dispositions",
            set(vocabulary["effect_dispositions"]),
        )
    for index, raw_mapping in enumerate(rule.get("http_mappings", [])):
        mapping = require_mapping(raw_mapping, f"{label}.http_mappings[{index}]")
        _require_reference_list(
            mapping.get("contexts"),
            f"{label}.http_mappings[{index}].contexts",
            set(registry["request_context_registry"]),
        )
        if "accumulated_effect" in mapping:
            _require_reference_list(
                mapping["accumulated_effect"],
                f"{label}.http_mappings[{index}].accumulated_effect",
                set(vocabulary["effect_dispositions"]),
            )
        _validate_http_status(
            mapping.get("status"), f"{label}.http_mappings[{index}].status"
        )


def _validate_writer_rule(
    envelope: str, rule: dict[str, Any], registry: dict[str, Any], label: str
) -> None:
    vocabulary = registry["http_auth_registry"]["resource_vocabularies"]
    _require_reference_list(
        rule.get("native_families"),
        f"{label}.native_families",
        set(registry["http_auth_registry"]["native_families"]),
    )
    if envelope == "artifact_writer_job_revision":
        _require_reference_list(
            rule.get("causes"), f"{label}.causes", set(vocabulary["writer_causes"])
        )
        _require_reference_list(
            rule.get("normalized_states"),
            f"{label}.normalized_states",
            set(vocabulary["writer_normalized_states"]),
        )
        for key, vocabulary_key in (
            ("effect_dispositions", "effect_dispositions"),
            ("publish_dispositions", "publish_dispositions"),
        ):
            if key in rule:
                _require_reference_list(
                    rule[key], f"{label}.{key}", set(vocabulary[vocabulary_key])
                )
    else:
        _require_reference_list(
            rule.get("results"),
            f"{label}.results",
            set(vocabulary["writer_request_results"]),
        )
        if "http_status" in rule:
            _validate_http_status(rule["http_status"], f"{label}.http_status")


def _validate_context_or_status_rule(
    rule: dict[str, Any], registry: dict[str, Any], label: str
) -> None:
    _validate_context_references(rule, registry, label)
    if "http_status" in rule:
        _validate_http_status(rule["http_status"], f"{label}.http_status")


def _validate_response_rule(
    rule: dict[str, Any], registry: dict[str, Any], label: str
) -> None:
    for index, raw_row in enumerate(rule.get("endpoint_rows", [])):
        row = require_mapping(raw_row, f"{label}.endpoint_rows[{index}]")
        if row.get("context") not in registry["request_context_registry"]:
            fail(f"{label}.endpoint_rows[{index}] has unknown context")
        if "stage" in row and row["stage"] not in registry["request_stage_registry"]:
            fail(f"{label}.endpoint_rows[{index}] has unknown stage")


def _validate_contextual_rule(
    envelope: str, rule: dict[str, Any], registry: dict[str, Any], label: str
) -> None:
    unknown = set(rule) - CONTEXT_KEYS_BY_ENVELOPE[envelope]
    if unknown:
        fail(f"{label} has unknown fields: {sorted(unknown)}")
    validator = {
        "operation_revision": _validate_operation_rule,
        "request_error": _validate_request_rule,
        "resource_diagnostic": _validate_context_or_status_rule,
        "authority_transaction_result": _validate_context_or_status_rule,
        "coordinator_step_result": _validate_coordinator_rule,
        "response_diagnostic": _validate_response_rule,
    }.get(envelope)
    if validator is None:
        _validate_writer_rule(envelope, rule, registry, label)
        return
    validator(rule, registry, label)


def _validate_operations(registry: dict[str, Any]) -> None:
    operations = require_mapping(
        registry.get("operation_registry"), "contract_registry.operation_registry"
    )
    _require_ordered_keys(
        operations,
        [
            "families",
            "aliases",
            "phases",
            "terminal_outcomes",
            "action_results",
            "target_dispositions",
        ],
        "contract_registry.operation_registry",
    )
    families = require_mapping(
        operations.get("families"), "contract_registry.operation_registry.families"
    )
    _require_ordered_keys(
        families,
        ["resource_lifecycle", "resident_state"],
        "contract_registry.operation_registry.families",
    )
    family_members = []
    for family, raw_members in families.items():
        members = require_string_list(
            raw_members, f"contract_registry.operation_registry.families.{family}"
        )
        family_members.extend(members)
    if len(family_members) != 14 or len(set(family_members)) != 14:
        fail("contract_registry operation families must close 14 unique kinds")

    aliases = require_mapping(
        operations.get("aliases"), "contract_registry.operation_registry.aliases"
    )
    _require_ordered_keys(
        aliases,
        list(EXPECTED_ALIAS_SIZES),
        "contract_registry.operation_registry.aliases",
    )
    for alias, expected_size in EXPECTED_ALIAS_SIZES.items():
        members = require_string_list(
            aliases[alias], f"contract_registry.operation_registry.aliases.{alias}"
        )
        if len(members) != expected_size or not set(members) <= set(family_members):
            fail(f"contract_registry.operation_registry.aliases.{alias} is not closed")
    if aliases["all_operations"] != family_members:
        fail("all_operations must preserve canonical family order")
    for key, size in (
        ("phases", 8),
        ("terminal_outcomes", 7),
        ("action_results", 4),
        ("target_dispositions", 3),
    ):
        if (
            len(
                require_string_list(
                    operations.get(key), f"contract_registry.operation_registry.{key}"
                )
            )
            != size
        ):
            fail(f"contract_registry.operation_registry.{key} count drifted")


def _validate_envelopes(registry: dict[str, Any]) -> None:
    envelopes = require_mapping(
        registry.get("reason_envelope_registry"),
        "contract_registry.reason_envelope_registry",
    )
    _require_ordered_keys(
        envelopes,
        list(EXPECTED_ENVELOPE_SIZES),
        "contract_registry.reason_envelope_registry",
    )
    reasons = require_mapping(
        registry.get("reason_registry"), "contract_registry.reason_registry"
    )
    if len(reasons) != 87:
        fail("contract_registry.reason_registry must contain 87 entries")
    for envelope, expected_size in EXPECTED_ENVELOPE_SIZES.items():
        definition = require_mapping(
            envelopes[envelope],
            f"contract_registry.reason_envelope_registry.{envelope}",
        )
        _require_ordered_keys(
            definition,
            ["schema_type", "reason_codes"],
            f"contract_registry.reason_envelope_registry.{envelope}",
        )
        if definition.get("schema_type") != envelope:
            fail(f"reason envelope {envelope} must use its matching schema type")
        codes = require_string_list(
            definition.get("reason_codes"),
            f"contract_registry.reason_envelope_registry.{envelope}.reason_codes",
        )
        if len(codes) != expected_size or not set(codes) <= set(reasons):
            fail(f"reason envelope {envelope} membership is not closed")


def _validate_reason_joins(registry: dict[str, Any]) -> None:
    reasons = require_mapping(
        registry.get("reason_registry"), "contract_registry.reason_registry"
    )
    envelopes = require_mapping(
        registry.get("reason_envelope_registry"),
        "contract_registry.reason_envelope_registry",
    )
    presentations = require_mapping(
        registry.get("presentation_registry"),
        "contract_registry.presentation_registry",
    )
    details = require_mapping(
        registry.get("detail_schema_registry"),
        "contract_registry.detail_schema_registry",
    )
    reverse_memberships = {code: [] for code in reasons}
    for envelope, definition in envelopes.items():
        for code in definition["reason_codes"]:
            reverse_memberships[code].append(envelope)

    for code, raw_reason in reasons.items():
        require_string(code, "contract_registry reason code")
        reason = require_mapping(
            raw_reason, f"contract_registry.reason_registry.{code}"
        )
        _require_ordered_keys(
            reason,
            ["category_id", "presentation_id", "detail_schema_id", "envelopes"],
            f"contract_registry.reason_registry.{code}",
        )
        presentation_id = require_string(
            reason.get("presentation_id"),
            f"contract_registry.reason_registry.{code}.presentation_id",
        )
        detail_id = require_string(
            reason.get("detail_schema_id"),
            f"contract_registry.reason_registry.{code}.detail_schema_id",
        )
        if presentation_id not in presentations or detail_id not in details:
            fail(f"contract_registry.reason_registry.{code} has an unknown join")
        category_id = require_string(
            reason.get("category_id"),
            f"contract_registry.reason_registry.{code}.category_id",
        )
        if presentations[presentation_id].get("category_id") != category_id:
            fail(
                f"contract_registry.reason_registry.{code} presentation join disagrees"
            )
        contextual = require_mapping(
            reason.get("envelopes"),
            f"contract_registry.reason_registry.{code}.envelopes",
        )
        if list(contextual) != reverse_memberships[code]:
            fail(f"contract_registry.reason_registry.{code} envelope join disagrees")
        for envelope, raw_context in contextual.items():
            context = require_mapping(
                raw_context,
                f"contract_registry.reason_registry.{code}.envelopes.{envelope}",
            )
            band = context.get("priority_band")
            if (
                isinstance(band, bool)
                or not isinstance(band, int)
                or not 0 <= band <= 6
            ):
                fail(f"{code}@{envelope} has an invalid priority_band")
            _validate_contextual_rule(
                envelope,
                context,
                registry,
                f"contract_registry.reason_registry.{code}.envelopes.{envelope}",
            )


def _resolve_registry_ref(registry: dict[str, Any], ref: str, label: str) -> Any:
    value: Any = registry
    for component in ref.split("."):
        if not isinstance(value, dict) or component not in value:
            fail(f"{label} references unknown registry path {ref}")
        value = value[component]
    return value


def _validate_detail_field(
    field: dict[str, Any], registry: dict[str, Any], label: str
) -> None:
    field_type = require_string(field.get("type"), f"{label}.type")
    if field_type not in DETAIL_FIELD_TYPES:
        fail(f"{label}.type is not a closed detail field type")
    if field.get("audience") not in {"regular_admin", "admin"}:
        fail(f"{label}.audience is not a closed projection audience")
    allowed_keys = {"type", "audience"}
    if field_type in {"enum", "enum_array"}:
        allowed_keys.add("values")
        values = require_string_list(field.get("values"), f"{label}.values")
        if not values:
            fail(f"{label}.values must not be empty")
    if field_type == "enum_ref":
        allowed_keys.add("ref")
        ref = require_string(field.get("ref"), f"{label}.ref")
        resolved = _resolve_registry_ref(registry, ref, label)
        require_string_list(resolved, f"{label} resolved enum")
    if field_type in {"opaque", "utf8"}:
        allowed_keys.add("max_bytes")
        bound = field.get("max_bytes")
        if isinstance(bound, bool) or not isinstance(bound, int) or bound <= 0:
            fail(f"{label}.max_bytes must be a positive integer")
    if field_type == "enum_array":
        allowed_keys.update({"min_items", "max_items"})
        minimum = field.get("min_items")
        maximum = field.get("max_items")
        if (
            isinstance(minimum, bool)
            or isinstance(maximum, bool)
            or not isinstance(minimum, int)
            or not isinstance(maximum, int)
            or minimum < 0
            or maximum < minimum
        ):
            fail(f"{label} has invalid array bounds")
    require_exact_keys(field, allowed_keys, label)


def _validate_ascii_identifier(identifier: str, label: str) -> None:
    if len(identifier.encode("utf-8")) > 64 or not all(
        0x21 <= ord(character) <= 0x7E for character in identifier
    ):
        fail(f"{label} {identifier} must be bounded printable ASCII")


def _validate_presentation_text(value: str, label: str, byte_bound: int) -> None:
    if unicodedata.normalize("NFC", value) != value:
        fail(f"{label} must be NFC")
    if len(value.encode("utf-8")) > byte_bound:
        fail(f"{label} exceeds its byte bound")
    if any(
        unicodedata.category(character) == "Cc"
        or "\u202a" <= character <= "\u202e"
        or "\u2066" <= character <= "\u2069"
        for character in value
    ):
        fail(f"{label} contains unsafe controls")
    if re.search(r"[<>*_`\[\]{}]", value):
        fail(f"{label} contains markup")


def _validate_presentation(
    presentation_id: str, raw_presentation: Any, categories: set[str]
) -> None:
    label = f"contract_registry.presentation_registry.{presentation_id}"
    presentation = require_mapping(raw_presentation, label)
    _require_ordered_keys(
        presentation,
        ["category_id", "severity", "title", "default_message"],
        label,
    )
    require_string(presentation_id, "presentation ID")
    for field in ("category_id", "severity", "title", "default_message"):
        require_string(presentation.get(field), f"{label}.{field}")
    category_id = presentation["category_id"]
    categories.add(category_id)
    _validate_ascii_identifier(presentation_id, "presentation ID")
    _validate_ascii_identifier(category_id, "category ID")
    if presentation["severity"] not in {"info", "warning", "error", "critical"}:
        fail(f"presentation {presentation_id} has unknown severity")
    _validate_presentation_text(presentation["title"], f"{label}.title", 80)
    _validate_presentation_text(
        presentation["default_message"], f"{label}.default_message", 256
    )


def _validate_presentations(registry: dict[str, Any]) -> None:
    presentations = require_mapping(
        registry.get("presentation_registry"),
        "contract_registry.presentation_registry",
    )
    if len(presentations) != 27:
        fail("contract_registry.presentation_registry must contain 27 entries")
    categories: set[str] = set()
    for presentation_id, raw_presentation in presentations.items():
        _validate_presentation(presentation_id, raw_presentation, categories)
    if len(categories) != 24:
        fail("contract_registry presentation categories must close to 24 IDs")


def _validate_detail_schema(
    detail_id: str, raw_detail: Any, registry: dict[str, Any]
) -> None:
    label = f"contract_registry.detail_schema_registry.{detail_id}"
    detail = require_mapping(raw_detail, label)
    _require_ordered_keys(
        detail,
        ["required_fields", "optional_fields", "fields"],
        label,
    )
    required = require_string_list(
        detail.get("required_fields"), f"{label}.required_fields", nonempty=False
    )
    optional = require_string_list(
        detail.get("optional_fields"), f"{label}.optional_fields", nonempty=False
    )
    fields = require_mapping(detail.get("fields"), f"{label}.fields")
    if set(required) & set(optional) or set(required + optional) != set(fields):
        fail(f"detail schema {detail_id} field closure disagrees")
    for field_name, raw_field in fields.items():
        _validate_detail_field(
            require_mapping(raw_field, f"detail schema {detail_id}.{field_name}"),
            registry,
            f"detail schema {detail_id}.{field_name}",
        )


def _validate_details(registry: dict[str, Any]) -> None:
    details = require_mapping(
        registry.get("detail_schema_registry"),
        "contract_registry.detail_schema_registry",
    )
    if len(details) != 15:
        fail("contract_registry.detail_schema_registry must contain 15 entries")
    for detail_id, raw_detail in details.items():
        _validate_detail_schema(detail_id, raw_detail, registry)


def _validate_presentations_and_details(registry: dict[str, Any]) -> None:
    _validate_presentations(registry)
    _validate_details(registry)


def _validate_schema_field_reference(
    field_type: str, field: dict[str, Any], registry: dict[str, Any], label: str
) -> None:
    if "ref" not in field:
        return
    ref = require_string(field["ref"], f"{label}.ref")
    resolved = _resolve_registry_ref(registry, ref, label)
    if field_type in {"enum_ref", "nullable_enum_ref"}:
        if isinstance(resolved, dict):
            require_registry_keys(resolved, f"{label} resolved enum")
        else:
            require_string_list(resolved, f"{label} resolved enum")
        return
    if field_type != "schema_ref" or not isinstance(resolved, dict):
        fail(f"{label}.ref is incompatible with {field_type}")


def _validate_schema_field_literal(
    field_type: str, field: dict[str, Any], label: str
) -> None:
    if "value" not in field:
        return
    value = field["value"]
    if field_type == "schema_literal":
        require_string(value, f"{label}.value")
        return
    if isinstance(value, bool) or not isinstance(value, (int, str)):
        fail(f"{label}.value must be a string or integer literal")


def _validate_nonnegative_integer(value: Any, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        fail(f"{label} must be a nonnegative integer")
    return value


def _validate_schema_field_bounds(field: dict[str, Any], label: str) -> None:
    bounds = {
        key: _validate_nonnegative_integer(value, f"{label}.{key}")
        for key, value in field.items()
        if key in {"max_bytes", "max_items", "maximum", "min_items", "minimum"}
    }
    if "max_bytes" in bounds and bounds["max_bytes"] == 0:
        fail(f"{label}.max_bytes must be positive")
    if bounds.get("min_items", 0) > bounds.get("max_items", bounds.get("min_items", 0)):
        fail(f"{label} has inverted item bounds")
    if bounds.get("minimum", 0) > bounds.get("maximum", bounds.get("minimum", 0)):
        fail(f"{label} has inverted numeric bounds")


def _validate_schema_field(
    field: dict[str, Any], registry: dict[str, Any], label: str
) -> None:
    field_type = require_string(field.get("type"), f"{label}.type")
    if field_type not in SCHEMA_FIELD_TYPES:
        fail(f"{label}.type is not a closed schema field type")
    require_exact_keys(
        field, {"type", "required"} | SCHEMA_FIELD_EXTRA_KEYS[field_type], label
    )
    if not isinstance(field.get("required"), bool):
        fail(f"{label}.required must be boolean")
    _validate_schema_field_reference(field_type, field, registry, label)
    if "values" in field:
        require_string_list(field["values"], f"{label}.values")
    _validate_schema_field_literal(field_type, field, label)
    _validate_schema_field_bounds(field, label)
    for flag in ("canonical_sorted", "ordered"):
        if flag in field and not isinstance(field[flag], bool):
            fail(f"{label}.{flag} must be boolean")


def _validate_conditional_fields(
    value: Any, fields: dict[str, Any], label: str
) -> None:
    names = require_string_list(value, label)
    unknown = set(names) - set(fields)
    if unknown:
        fail(f"{label} references unknown schema fields: {sorted(unknown)}")


def _validate_schema_predicate(
    predicate: dict[str, Any],
    fields: dict[str, Any],
    registry: dict[str, Any],
    label: str,
    *,
    allow_field_path: bool = False,
) -> None:
    def require_schema_path_root(path: str, path_label: str) -> str:
        if _path_tokens(path) is None:
            fail(f"{path_label} is not a valid schema path")
        root = path.split(".", maxsplit=1)[0].split("[", maxsplit=1)[0]
        if root not in fields and root != "detail_schema_id":
            fail(f"{path_label} references unknown schema field {path}")
        return root

    allowed = {
        "cause",
        "contexts",
        "effect_disposition",
        "equals",
        "equals_path",
        "field",
        "in",
        "less_than_or_equal_path",
        "less_than_path",
        "not_equals",
        "not_in",
        "publish_disposition",
        "reason_code",
        "result_not_in",
        "resume_allowed",
        "selected_by",
    }
    unknown = set(predicate) - allowed
    if unknown:
        fail(f"{label} has unknown predicate fields: {sorted(unknown)}")
    if "field" in predicate:
        field_name = require_string(predicate["field"], f"{label}.field")
        field_root = field_name
        if allow_field_path:
            field_root = require_schema_path_root(field_name, f"{label}.field")
        if field_root not in fields and field_root != "detail_schema_id":
            fail(f"{label}.field references unknown schema field {field_name}")
    if "selected_by" in predicate:
        _validate_conditional_fields(
            predicate["selected_by"], fields, f"{label}.selected_by"
        )
    if "contexts" in predicate:
        _require_reference_list(
            predicate["contexts"],
            f"{label}.contexts",
            set(registry["request_context_registry"]),
        )
    if (
        "reason_code" in predicate
        and predicate["reason_code"] not in registry["reason_registry"]
    ):
        fail(f"{label}.reason_code references an unknown reason")
    for key in ("in", "not_in", "result_not_in"):
        if key in predicate:
            require_string_list(predicate[key], f"{label}.{key}")
    for key in ("equals_path", "less_than_path", "less_than_or_equal_path"):
        if key in predicate:
            path = require_string(predicate[key], f"{label}.{key}")
            require_schema_path_root(path, f"{label}.{key}")


def _schema_predicate_is_translatable(predicate: dict[str, Any]) -> bool:
    return (
        "reason_code" in predicate
        or any(key.endswith("_not_in") for key in predicate)
        or (
            "field" in predicate
            and any(
                operator in predicate
                for operator in ("equals", "not_equals", "in", "not_in")
            )
        )
    )


def _validate_schema_conditional(
    conditional: dict[str, Any],
    fields: dict[str, Any],
    registry: dict[str, Any],
    label: str,
) -> None:
    allowed = {
        "allow_empty",
        "assert",
        "forbid",
        "if",
        "require",
        "require_empty",
        "require_nonempty",
        "require_nonnull",
        "require_null",
        "require_values",
        "unless",
    }
    unknown = set(conditional) - allowed
    if unknown:
        fail(f"{label} has unknown fields: {sorted(unknown)}")
    predicate_modes = set(conditional) & {"if", "unless"}
    if not predicate_modes and set(conditional) != {"assert"}:
        if "assert" in conditional:
            fail(f"{label}.assert cannot include consequences")
        fail(f"{label} must contain exactly one predicate mode")
    if len(predicate_modes) > 1:
        fail(f"{label} must contain exactly one predicate mode")
    for key in ("if", "unless", "assert"):
        if key in conditional:
            predicate = require_mapping(conditional[key], f"{label}.{key}")
            _validate_schema_predicate(
                predicate,
                fields,
                registry,
                f"{label}.{key}",
                allow_field_path=key == "assert",
            )
            if key in {"if", "unless"} and not _schema_predicate_is_translatable(
                predicate
            ):
                fail(f"{label}.{key} predicate is not translatable")
    for key in (
        "allow_empty",
        "forbid",
        "require",
        "require_empty",
        "require_nonempty",
        "require_nonnull",
        "require_null",
    ):
        if key in conditional:
            _validate_conditional_fields(conditional[key], fields, f"{label}.{key}")
    if "require_values" in conditional:
        values = require_mapping(
            conditional["require_values"], f"{label}.require_values"
        )
        if not values:
            fail(f"{label}.require_values is empty")
        unknown_fields = set(values) - set(fields)
        if unknown_fields:
            fail(f"{label}.require_values references unknown fields")


def _validate_schema_registry(registry: dict[str, Any]) -> None:
    schemas = require_mapping(
        registry.get("schema_registry"), "contract_registry.schema_registry"
    )
    _require_ordered_keys(
        schemas, EXPECTED_SCHEMA_KEYS, "contract_registry.schema_registry"
    )
    for schema_key, raw_schema in schemas.items():
        schema = require_mapping(
            raw_schema, f"contract_registry.schema_registry.{schema_key}"
        )
        _require_ordered_keys(
            schema,
            ["version", "output", "schema_type", "fields", "conditionals"],
            f"contract_registry.schema_registry.{schema_key}",
        )
        version = require_mapping(
            schema.get("version"),
            f"contract_registry.schema_registry.{schema_key}.version",
        )
        if version != {"major": 1, "minor": 0}:
            fail(f"schema {schema_key} must remain at version 1.0")
        expected_output = f"docs/api/schemas/residency/{schema_key}.schema.json"
        if schema.get("output") != expected_output:
            fail(f"schema {schema_key} output path drifted")
        expected_type = (
            "residency.profiles/1.0"
            if schema_key == "residency_profiles"
            else f"residency.{schema_key}/1.0"
        )
        if schema.get("schema_type") != expected_type:
            fail(f"schema {schema_key} type drifted")
        fields = require_mapping(
            schema.get("fields"),
            f"contract_registry.schema_registry.{schema_key}.fields",
        )
        require_registry_keys(
            fields, f"contract_registry.schema_registry.{schema_key}.fields"
        )
        for field_name, raw_field in fields.items():
            _validate_schema_field(
                require_mapping(raw_field, f"schema {schema_key} field {field_name}"),
                registry,
                f"schema {schema_key} field {field_name}",
            )
        conditionals = require_list(
            schema.get("conditionals"),
            f"contract_registry.schema_registry.{schema_key}.conditionals",
        )
        for index, raw_conditional in enumerate(conditionals):
            _validate_schema_conditional(
                require_mapping(
                    raw_conditional, f"schema {schema_key} conditional {index}"
                ),
                fields,
                registry,
                f"schema {schema_key} conditional {index}",
            )


def _validate_registry_digest(registry: dict[str, Any]) -> None:
    encoded = json.dumps(
        registry,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=False,
    ).encode("utf-8")
    if hashlib.sha256(encoded).hexdigest() != EXPECTED_REGISTRY_DIGEST:
        fail("contract_registry does not match the accepted schema-v7 contract")


def validate_contract_registry(raw: Any) -> dict[str, Any]:
    """Return a strict normalized copy of the repository-independent registry."""

    registry = require_mapping(raw, "contract_registry")
    _require_ordered_keys(registry, EXPECTED_REGISTRY_KEYS, "contract_registry")
    if registry.get("schema") != "residency.explanation/1.0":
        fail("contract_registry.schema must be residency.explanation/1.0")
    if (
        len(
            require_string_list(
                registry.get("request_context_registry"),
                "contract_registry.request_context_registry",
            )
        )
        != 37
    ):
        fail("contract_registry.request_context_registry must contain 37 entries")
    if (
        len(
            require_string_list(
                registry.get("request_stage_registry"),
                "contract_registry.request_stage_registry",
            )
        )
        != 7
    ):
        fail("contract_registry.request_stage_registry must contain 7 entries")
    _validate_modes_retention_and_http(registry)
    _validate_operations(registry)
    _validate_envelopes(registry)
    _validate_reason_joins(registry)
    _validate_presentations_and_details(registry)
    _validate_schema_registry(registry)
    _validate_registry_digest(registry)
    return copy.deepcopy(registry)


def validate_contract_promotion_closure(
    *,
    exact_cells: list[dict[str, Any]],
    compatibility_contracts: list[dict[str, Any]],
    later_promotion_roster: list[dict[str, Any]],
    fallback_ids: set[str],
) -> None:
    """Keep every generated catalog unit inactive and close all fallback references."""

    if (
        len(exact_cells),
        len(compatibility_contracts),
        len(later_promotion_roster),
    ) != (
        4,
        1,
        34,
    ):
        fail(
            "contract promotion kinds must close to 4 exact, 1 compatibility, 34 later"
        )
    unit_states = []
    unit_ids = []
    fallback_references = []
    for cell in exact_cells:
        unit_ids.append(cell["cell_id"])
        unit_states.append((cell["capability_level"], cell["delivery_state"]))
        fallback_references.extend(cell["fallbacks"].values())
    for contract in compatibility_contracts:
        unit_ids.append(contract["contract_id"])
        unit_states.append((contract["capability_level"], contract["delivery_state"]))
        fallback_references.extend(contract["fallbacks"].values())
    for unit in later_promotion_roster:
        unit_ids.append(unit["unit_id"])
        state = unit["initial_state"]
        unit_states.append((state["capability_level"], state["delivery_state"]))
        fallback_references.extend(unit["fallbacks"].values())
    if len(unit_ids) != 39 or len(set(unit_ids)) != 39:
        fail("contract promotion roster must contain 39 unique unit IDs")
    if set(unit_states) != {("unsupported", "absent")}:
        fail("contract promotion units must remain unsupported and absent")
    if len(fallback_references) != 41:
        fail("contract promotion units must contain exactly 41 fallback references")
    if set(fallback_references) != fallback_ids or len(fallback_ids) != 14:
        fail("contract promotion units must close all 14 fallback IDs")
