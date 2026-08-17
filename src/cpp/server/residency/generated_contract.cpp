#include "lemon/residency/generated_contract.h"

#include <array>
#include <string>

namespace lemon::residency {
namespace {

struct PromotionUnitMetadata {
    std::string_view id;
    PromotionUnitKind kind;
};

struct OperationFamilyMetadata {
    std::string_view operation_kind;
    std::string_view family;
};

struct OperationReasonContextMetadata {
    std::string_view code;
    std::string_view operation_kinds;
    std::string_view phases;
    std::string_view terminal_outcomes;
    bool primary;
    bool secondary;
    bool associated_primary_state;
};

constexpr std::array<PromotionUnitMetadata, 39> promotion_units{{
    PromotionUnitMetadata{"H-NPU-FLM-CONFLICT-XDNA2-v1", PromotionUnitKind::CompatibilityContract},
    PromotionUnitMetadata{"H-ROCM-ADM-GTT-HOST-v1", PromotionUnitKind::ExactCell},
    PromotionUnitMetadata{"H-ROCM-PRE-GTT-HOST-v1", PromotionUnitKind::ExactCell},
    PromotionUnitMetadata{"H-ROCM-REC-GTT-HOST-OWN-v1", PromotionUnitKind::ExactCell},
    PromotionUnitMetadata{"H-ROCM-STA-GTT-HOST-v1", PromotionUnitKind::ExactCell},
    PromotionUnitMetadata{"H-VULKAN-ADM-GTT-HOST-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"H-VULKAN-PRE-GTT-HOST-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"H-VULKAN-REC-GTT-HOST-OWN-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"H-VULKAN-STA-GTT-HOST-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-FLM-NPU-EMBEDDING-ADM-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-FLM-NPU-EMBEDDING-LFR-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-FLM-NPU-EMBEDDING-PIN-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-FLM-NPU-EMBEDDING-REC-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-FLM-NPU-EMBEDDING-STA-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-FLM-NPU-EMBEDDING-UNL-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-FLM-NPU-LLM-ADM-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-FLM-NPU-LLM-LFR-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-FLM-NPU-LLM-PIN-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-FLM-NPU-LLM-REC-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-FLM-NPU-LLM-STA-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-FLM-NPU-LLM-UNL-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-FLM-NPU-TRANSCRIPTION-ADM-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-FLM-NPU-TRANSCRIPTION-LFR-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-FLM-NPU-TRANSCRIPTION-PIN-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-FLM-NPU-TRANSCRIPTION-REC-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-FLM-NPU-TRANSCRIPTION-STA-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-FLM-NPU-TRANSCRIPTION-UNL-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-RYZENAI-LLM-NPU-LLM-ADM-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-RYZENAI-LLM-NPU-LLM-LFR-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-RYZENAI-LLM-NPU-LLM-PIN-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-RYZENAI-LLM-NPU-LLM-REC-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-RYZENAI-LLM-NPU-LLM-STA-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-RYZENAI-LLM-NPU-LLM-UNL-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-WHISPERCPP-NPU-TRANSCRIPTION-ADM-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-WHISPERCPP-NPU-TRANSCRIPTION-LFR-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-WHISPERCPP-NPU-TRANSCRIPTION-PIN-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-WHISPERCPP-NPU-TRANSCRIPTION-REC-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-WHISPERCPP-NPU-TRANSCRIPTION-STA-v1", PromotionUnitKind::LaterRuntime},
    PromotionUnitMetadata{"W-XDNA2-WHISPERCPP-NPU-TRANSCRIPTION-UNL-v1", PromotionUnitKind::LaterRuntime},
}};

constexpr std::array<std::string_view, 14> fallback_ids{{
    "hatchery_rocm_admission_refuse_unknown_capacity_v1",
    "hatchery_rocm_pressure_disabled_invalid_evidence_v1",
    "hatchery_rocm_pressure_report_only_v1",
    "hatchery_rocm_recovery_block_readiness_v1",
    "hatchery_rocm_startup_block_group_v1",
    "residency_admission_refuse_unknown_demand_v1",
    "residency_load_retry_refuse_unproven_victim_set_v1",
    "residency_npu_conflict_preserve_refuse_v1",
    "residency_pin_mutation_refuse_stale_or_unready_v1",
    "residency_pressure_disabled_invalid_evidence_v1",
    "residency_pressure_report_only_unvalidated_v1",
    "residency_recovery_block_unproven_release_v1",
    "residency_startup_block_group_v1",
    "residency_unload_preserve_live_use_v1",
}};

constexpr std::array<std::string_view, 12> schema_types{{
    "residency.artifact_quarantine_record/1.0",
    "residency.artifact_writer_job_revision/1.0",
    "residency.artifact_writer_request_result/1.0",
    "residency.authority_transaction_result/1.0",
    "residency.coordinator_step_result/1.0",
    "residency.operation_revision/1.0",
    "residency.reason/1.0",
    "residency.request_error/1.0",
    "residency.profiles/1.0",
    "residency.resource_diagnostic/1.0",
    "residency.response_diagnostic/1.0",
    "residency.staged_import_session_record/1.0",
}};

constexpr std::array<ReasonMetadata, 87> reasons{{
    ReasonMetadata{"model_not_found", "model_identity", "p_model_identity", "d_model", "warning", "Model not found", "The requested model is not known."},
    ReasonMetadata{"model_not_loaded_or_loading", "resident_state", "p_resident_state", "d_model", "warning", "Resident unavailable", "The requested live resident state is unavailable."},
    ReasonMetadata{"residency_action_failed", "action", "p_action", "d_action", "error", "Action incomplete", "A residency action did not complete successfully."},
    ReasonMetadata{"residency_artifact_acquisition_failed", "action", "p_action", "d_action", "error", "Action incomplete", "A residency action did not complete successfully."},
    ReasonMetadata{"residency_artifact_import_cleanup_incomplete", "artifact_import", "p_artifact_import", "d_import", "warning", "Artifact import unavailable", "The staged artifact import could not complete in its current state."},
    ReasonMetadata{"residency_artifact_import_conflict", "artifact_import", "p_artifact_import", "d_import", "warning", "Artifact import unavailable", "The staged artifact import could not complete in its current state."},
    ReasonMetadata{"residency_artifact_import_expired", "artifact_import", "p_artifact_import", "d_lookup", "warning", "Artifact import unavailable", "The staged artifact import could not complete in its current state."},
    ReasonMetadata{"residency_artifact_import_failed", "artifact_import", "p_artifact_import", "d_import", "warning", "Artifact import unavailable", "The staged artifact import could not complete in its current state."},
    ReasonMetadata{"residency_artifact_import_invalid", "artifact_import", "p_artifact_import", "d_import", "warning", "Artifact import unavailable", "The staged artifact import could not complete in its current state."},
    ReasonMetadata{"residency_artifact_import_not_found", "artifact_import", "p_artifact_import", "d_lookup", "warning", "Artifact import unavailable", "The staged artifact import could not complete in its current state."},
    ReasonMetadata{"residency_artifact_import_storage_unavailable", "artifact_import", "p_artifact_import", "d_storage", "warning", "Artifact import unavailable", "The staged artifact import could not complete in its current state."},
    ReasonMetadata{"residency_artifact_quarantine_expired", "lookup", "p_record_lookup", "d_lookup", "warning", "Residency record unavailable", "The requested residency request record is no longer available."},
    ReasonMetadata{"residency_artifact_quarantine_not_found", "lookup", "p_record_lookup", "d_lookup", "warning", "Residency record unavailable", "The requested residency request record is no longer available."},
    ReasonMetadata{"residency_artifact_writer_failed", "action", "p_action", "d_writer", "error", "Action incomplete", "A residency action did not complete successfully."},
    ReasonMetadata{"residency_artifact_writer_fenced", "protection", "p_protection", "d_fence", "warning", "Resident protected", "A resident protection rule prevents the requested action."},
    ReasonMetadata{"residency_artifact_writer_quiescence_failed", "action", "p_action", "d_action", "error", "Action incomplete", "A residency action did not complete successfully."},
    ReasonMetadata{"residency_artifact_writer_superseded_by_model_delete", "action", "p_action", "d_none", "error", "Action incomplete", "A residency action did not complete successfully."},
    ReasonMetadata{"residency_attempt_budget_exhausted", "scheduling", "p_scheduling", "d_action", "warning", "Scheduling limit reached", "The residency coordinator could not proceed within its bound."},
    ReasonMetadata{"residency_authority_degraded", "persistence", "p_persistence", "d_authority", "error", "Persistence unavailable", "Required residency state could not be persisted."},
    ReasonMetadata{"residency_authority_persistence_failed", "persistence", "p_persistence", "d_authority", "error", "Persistence unavailable", "Required residency state could not be persisted."},
    ReasonMetadata{"residency_authority_storage_unavailable", "persistence", "p_persistence", "d_authority", "error", "Persistence unavailable", "Required residency state could not be persisted."},
    ReasonMetadata{"residency_authority_unavailable", "persistence", "p_persistence", "d_authority", "error", "Persistence unavailable", "Required residency state could not be persisted."},
    ReasonMetadata{"residency_cancelled", "lifecycle", "p_lifecycle", "d_none", "warning", "Lifecycle changed", "The residency lifecycle operation was interrupted or superseded."},
    ReasonMetadata{"residency_capability_unsupported", "capability", "p_capability", "d_none", "error", "Capability unavailable", "The required residency capability is unavailable."},
    ReasonMetadata{"residency_capacity_insufficient", "capacity", "p_capacity", "d_none", "warning", "Capacity unavailable", "The requested residency capacity is unavailable."},
    ReasonMetadata{"residency_configuration_conflict", "configuration", "p_configuration", "d_generation", "error", "Configuration conflict", "The residency configuration request conflicts with current authority."},
    ReasonMetadata{"residency_configuration_scope_mismatch", "configuration", "p_configuration", "d_none", "error", "Configuration conflict", "The residency configuration request conflicts with current authority."},
    ReasonMetadata{"residency_coordinator_expired", "lookup", "p_record_lookup", "d_lookup", "warning", "Residency record unavailable", "The requested residency request record is no longer available."},
    ReasonMetadata{"residency_coordinator_source_changed", "lifecycle", "p_lifecycle", "d_generation", "warning", "Lifecycle changed", "The residency lifecycle operation was interrupted or superseded."},
    ReasonMetadata{"residency_count_limit_exceeded_after_reconfiguration", "capacity", "p_capacity", "d_none", "warning", "Capacity unavailable", "The requested residency capacity is unavailable."},
    ReasonMetadata{"residency_critical_pressure_refusal", "pressure", "p_pressure", "d_none", "error", "Pressure unresolved", "Memory pressure could not be safely resolved."},
    ReasonMetadata{"residency_dead_backend_prune_incomplete", "recovery", "p_recovery", "d_action", "error", "Recovery required", "Residency recovery is required before normal lifecycle work can continue."},
    ReasonMetadata{"residency_deprecated_configuration_key", "configuration", "p_configuration", "d_migration", "error", "Configuration conflict", "The residency configuration request conflicts with current authority."},
    ReasonMetadata{"residency_evidence_incoherent", "evidence", "p_evidence", "d_none", "error", "Evidence unavailable", "Required residency evidence is not currently usable."},
    ReasonMetadata{"residency_evidence_missing", "evidence", "p_evidence", "d_none", "error", "Evidence unavailable", "Required residency evidence is not currently usable."},
    ReasonMetadata{"residency_evidence_stale", "evidence", "p_evidence", "d_none", "error", "Evidence unavailable", "Required residency evidence is not currently usable."},
    ReasonMetadata{"residency_evidence_superseded", "evidence", "p_evidence", "d_none", "error", "Evidence unavailable", "Required residency evidence is not currently usable."},
    ReasonMetadata{"residency_evidence_unhealthy", "evidence", "p_evidence", "d_none", "error", "Evidence unavailable", "Required residency evidence is not currently usable."},
    ReasonMetadata{"residency_explanation_storage_unavailable", "persistence", "p_persistence", "d_storage", "error", "Persistence unavailable", "Required residency state could not be persisted."},
    ReasonMetadata{"residency_fallback_selected", "policy", "p_policy", "d_none", "info", "Fallback selected", "A declared residency fallback was selected."},
    ReasonMetadata{"residency_footprint_confidence_insufficient", "capability", "p_capability", "d_none", "error", "Capability unavailable", "The required residency capability is unavailable."},
    ReasonMetadata{"residency_force_fence_failed", "protection", "p_protection", "d_action", "warning", "Resident protected", "A resident protection rule prevents the requested action."},
    ReasonMetadata{"residency_idempotency_conflict", "compatibility", "p_compatibility", "d_idempotency", "warning", "Compatibility constraint", "A compatibility rule affects the requested operation."},
    ReasonMetadata{"residency_idempotency_key_required", "precondition", "p_precondition", "d_idempotency", "warning", "Precondition failed", "The request precondition is missing or no longer current."},
    ReasonMetadata{"residency_legacy_admin_key_fallback_deprecated", "authentication", "p_authentication", "d_compatibility", "warning", "Legacy authorization", "A deprecated authorization compatibility rule was used."},
    ReasonMetadata{"residency_legacy_eviction_options_require_review", "migration", "p_migration", "d_migration", "warning", "Migration attention required", "Residency migration state requires review or resolution."},
    ReasonMetadata{"residency_legacy_gtt_hint_ignored", "migration", "p_migration", "d_migration", "warning", "Migration attention required", "Residency migration state requires review or resolution."},
    ReasonMetadata{"residency_legacy_memory_limit_ambiguous", "migration", "p_migration", "d_migration", "warning", "Migration attention required", "Residency migration state requires review or resolution."},
    ReasonMetadata{"residency_limit_exceeded_after_reconfiguration", "capacity", "p_capacity", "d_none", "warning", "Capacity unavailable", "The requested residency capacity is unavailable."},
    ReasonMetadata{"residency_model_artifact_delete_failed", "action", "p_action", "d_action", "error", "Action incomplete", "A residency action did not complete successfully."},
    ReasonMetadata{"residency_model_identity_conflict", "model_identity", "p_model_identity_conflict", "d_identity", "warning", "Model identity unavailable", "The requested model identity cannot be published in its current state."},
    ReasonMetadata{"residency_model_identity_delete_failed", "action", "p_action", "d_action", "error", "Action incomplete", "A residency action did not complete successfully."},
    ReasonMetadata{"residency_model_identity_fenced", "protection", "p_protection", "d_fence", "warning", "Resident protected", "A resident protection rule prevents the requested action."},
    ReasonMetadata{"residency_model_identity_source_ineligible", "model_identity", "p_model_identity_conflict", "d_identity", "warning", "Model identity unavailable", "The requested model identity cannot be published in its current state."},
    ReasonMetadata{"residency_model_replacement_requires_release", "model_identity", "p_model_identity_conflict", "d_identity", "warning", "Model identity unavailable", "The requested model identity cannot be published in its current state."},
    ReasonMetadata{"residency_operation_budget_exhausted", "scheduling", "p_scheduling", "d_action", "warning", "Scheduling limit reached", "The residency coordinator could not proceed within its bound."},
    ReasonMetadata{"residency_operation_expired", "lookup", "p_lookup", "d_lookup", "warning", "Operation unavailable", "The requested residency operation record is unavailable."},
    ReasonMetadata{"residency_operation_not_found", "lookup", "p_lookup", "d_lookup", "warning", "Operation unavailable", "The requested residency operation record is unavailable."},
    ReasonMetadata{"residency_operation_rate_limited", "rate_limit", "p_rate_limit", "d_rate", "warning", "Operation rate limited", "The residency operation allowance is temporarily exhausted."},
    ReasonMetadata{"residency_operation_succeeded", "success", "p_success", "d_none", "info", "Operation succeeded", "The residency operation succeeded."},
    ReasonMetadata{"residency_partial_outcome", "action", "p_action", "d_action", "error", "Action incomplete", "A residency action did not complete successfully."},
    ReasonMetadata{"residency_persistence_failed", "persistence", "p_persistence", "d_storage", "error", "Persistence unavailable", "Required residency state could not be persisted."},
    ReasonMetadata{"residency_pin_migration_conflict", "migration", "p_migration", "d_migration", "warning", "Migration attention required", "Residency migration state requires review or resolution."},
    ReasonMetadata{"residency_plan_infeasible", "feasibility", "p_feasibility", "d_none", "warning", "Plan infeasible", "No complete safe residency plan is currently feasible."},
    ReasonMetadata{"residency_plan_invalidated", "lifecycle", "p_lifecycle", "d_generation", "warning", "Lifecycle changed", "The residency lifecycle operation was interrupted or superseded."},
    ReasonMetadata{"residency_planning_deadline_exceeded", "scheduling", "p_scheduling", "d_action", "warning", "Scheduling limit reached", "The residency coordinator could not proceed within its bound."},
    ReasonMetadata{"residency_precondition_failed", "precondition", "p_precondition", "d_generation", "warning", "Precondition failed", "The request precondition is missing or no longer current."},
    ReasonMetadata{"residency_precondition_required", "precondition", "p_precondition", "d_generation", "warning", "Precondition failed", "The request precondition is missing or no longer current."},
    ReasonMetadata{"residency_pressure_unresolved", "pressure", "p_pressure", "d_action", "error", "Pressure unresolved", "Memory pressure could not be safely resolved."},
    ReasonMetadata{"residency_protected_in_use", "protection", "p_protection", "d_none", "warning", "Resident protected", "A resident protection rule prevents the requested action."},
    ReasonMetadata{"residency_protected_pinned", "protection", "p_protection", "d_none", "warning", "Resident protected", "A resident protection rule prevents the requested action."},
    ReasonMetadata{"residency_quarantined", "integrity", "p_integrity", "d_none", "critical", "Residency quarantined", "Residency state is quarantined pending recovery."},
    ReasonMetadata{"residency_recovery_not_ready", "recovery", "p_recovery", "d_none", "error", "Recovery required", "Residency recovery is required before normal lifecycle work can continue."},
    ReasonMetadata{"residency_recovery_required", "recovery", "p_recovery", "d_none", "error", "Recovery required", "Residency recovery is required before normal lifecycle work can continue."},
    ReasonMetadata{"residency_reserved_registration_name", "model_identity", "p_model_identity_conflict", "d_identity", "warning", "Model identity unavailable", "The requested model identity cannot be published in its current state."},
    ReasonMetadata{"residency_resident_unavailable", "resident_state", "p_resident_state", "d_model", "warning", "Resident unavailable", "The requested live resident state is unavailable."},
    ReasonMetadata{"residency_service_termination_incomplete", "recovery", "p_recovery", "d_action", "error", "Recovery required", "Residency recovery is required before normal lifecycle work can continue."},
    ReasonMetadata{"residency_startup_rollback_incomplete", "recovery", "p_recovery", "d_action", "error", "Recovery required", "Residency recovery is required before normal lifecycle work can continue."},
    ReasonMetadata{"residency_startup_set_infeasible", "feasibility", "p_feasibility", "d_none", "warning", "Plan infeasible", "No complete safe residency plan is currently feasible."},
    ReasonMetadata{"residency_status_authorization_forbidden", "authentication", "p_status_authentication", "d_none", "warning", "Status authorization required", "A valid residency status capability or admin authorization is required."},
    ReasonMetadata{"residency_status_capability_expired", "authentication", "p_status_authentication", "d_none", "warning", "Status authorization required", "A valid residency status capability or admin authorization is required."},
    ReasonMetadata{"residency_status_capability_invalid", "authentication", "p_status_authentication", "d_none", "warning", "Status authorization required", "A valid residency status capability or admin authorization is required."},
    ReasonMetadata{"residency_status_capability_required", "authentication", "p_status_authentication", "d_none", "warning", "Status authorization required", "A valid residency status capability or admin authorization is required."},
    ReasonMetadata{"residency_unconditional_pin_write_deprecated", "compatibility", "p_compatibility", "d_compatibility", "warning", "Compatibility constraint", "A compatibility rule affects the requested operation."},
    ReasonMetadata{"residency_unmanaged_artifact_compatibility", "compatibility", "p_compatibility", "d_compatibility", "warning", "Compatibility constraint", "A compatibility rule affects the requested operation."},
    ReasonMetadata{"router_residency_conflict", "compatibility", "p_compatibility", "d_compatibility", "warning", "Compatibility constraint", "A compatibility rule affects the requested operation."},
    ReasonMetadata{"slots_pinned_error", "protection", "p_protection", "d_compatibility", "warning", "Resident protected", "A resident protection rule prevents the requested action."},
}};

constexpr std::array<ReasonPresentationMetadata, 27> presentations{{
    ReasonPresentationMetadata{"p_action", "action", "error", "Action incomplete", "A residency action did not complete successfully."},
    ReasonPresentationMetadata{"p_artifact_import", "artifact_import", "warning", "Artifact import unavailable", "The staged artifact import could not complete in its current state."},
    ReasonPresentationMetadata{"p_authentication", "authentication", "warning", "Legacy authorization", "A deprecated authorization compatibility rule was used."},
    ReasonPresentationMetadata{"p_capability", "capability", "error", "Capability unavailable", "The required residency capability is unavailable."},
    ReasonPresentationMetadata{"p_capacity", "capacity", "warning", "Capacity unavailable", "The requested residency capacity is unavailable."},
    ReasonPresentationMetadata{"p_compatibility", "compatibility", "warning", "Compatibility constraint", "A compatibility rule affects the requested operation."},
    ReasonPresentationMetadata{"p_configuration", "configuration", "error", "Configuration conflict", "The residency configuration request conflicts with current authority."},
    ReasonPresentationMetadata{"p_evidence", "evidence", "error", "Evidence unavailable", "Required residency evidence is not currently usable."},
    ReasonPresentationMetadata{"p_feasibility", "feasibility", "warning", "Plan infeasible", "No complete safe residency plan is currently feasible."},
    ReasonPresentationMetadata{"p_integrity", "integrity", "critical", "Residency quarantined", "Residency state is quarantined pending recovery."},
    ReasonPresentationMetadata{"p_lifecycle", "lifecycle", "warning", "Lifecycle changed", "The residency lifecycle operation was interrupted or superseded."},
    ReasonPresentationMetadata{"p_lookup", "lookup", "warning", "Operation unavailable", "The requested residency operation record is unavailable."},
    ReasonPresentationMetadata{"p_migration", "migration", "warning", "Migration attention required", "Residency migration state requires review or resolution."},
    ReasonPresentationMetadata{"p_model_identity", "model_identity", "warning", "Model not found", "The requested model is not known."},
    ReasonPresentationMetadata{"p_model_identity_conflict", "model_identity", "warning", "Model identity unavailable", "The requested model identity cannot be published in its current state."},
    ReasonPresentationMetadata{"p_persistence", "persistence", "error", "Persistence unavailable", "Required residency state could not be persisted."},
    ReasonPresentationMetadata{"p_policy", "policy", "info", "Fallback selected", "A declared residency fallback was selected."},
    ReasonPresentationMetadata{"p_precondition", "precondition", "warning", "Precondition failed", "The request precondition is missing or no longer current."},
    ReasonPresentationMetadata{"p_pressure", "pressure", "error", "Pressure unresolved", "Memory pressure could not be safely resolved."},
    ReasonPresentationMetadata{"p_protection", "protection", "warning", "Resident protected", "A resident protection rule prevents the requested action."},
    ReasonPresentationMetadata{"p_rate_limit", "rate_limit", "warning", "Operation rate limited", "The residency operation allowance is temporarily exhausted."},
    ReasonPresentationMetadata{"p_record_lookup", "lookup", "warning", "Residency record unavailable", "The requested residency request record is no longer available."},
    ReasonPresentationMetadata{"p_recovery", "recovery", "error", "Recovery required", "Residency recovery is required before normal lifecycle work can continue."},
    ReasonPresentationMetadata{"p_resident_state", "resident_state", "warning", "Resident unavailable", "The requested live resident state is unavailable."},
    ReasonPresentationMetadata{"p_scheduling", "scheduling", "warning", "Scheduling limit reached", "The residency coordinator could not proceed within its bound."},
    ReasonPresentationMetadata{"p_status_authentication", "authentication", "warning", "Status authorization required", "A valid residency status capability or admin authorization is required."},
    ReasonPresentationMetadata{"p_success", "success", "info", "Operation succeeded", "The residency operation succeeded."},
}};

constexpr std::array<OperationFamilyMetadata, 14> operation_families{{
    OperationFamilyMetadata{"admission", "resource_lifecycle"},
    OperationFamilyMetadata{"explicit_unload", "resource_lifecycle"},
    OperationFamilyMetadata{"force_unload", "resource_lifecycle"},
    OperationFamilyMetadata{"pressure_reclamation", "resource_lifecycle"},
    OperationFamilyMetadata{"startup_load", "resource_lifecycle"},
    OperationFamilyMetadata{"service_termination", "resource_lifecycle"},
    OperationFamilyMetadata{"dead_backend_pruning", "resource_lifecycle"},
    OperationFamilyMetadata{"same_epoch_recovery_cleanup", "resource_lifecycle"},
    OperationFamilyMetadata{"prior_epoch_owner_cleanup", "resource_lifecycle"},
    OperationFamilyMetadata{"artifact_scope_recovery_cleanup", "resource_lifecycle"},
    OperationFamilyMetadata{"saved_pin_mutation", "resident_state"},
    OperationFamilyMetadata{"runtime_pin_mutation", "resident_state"},
    OperationFamilyMetadata{"legacy_pin_batch", "resident_state"},
    OperationFamilyMetadata{"resident_state_recovery_cleanup", "resident_state"},
}};

constexpr std::array<OperationReasonRuleMetadata, 34> operation_reason_rules{{
    OperationReasonRuleMetadata{"residency_operation_succeeded", 0, 0, false},
    OperationReasonRuleMetadata{"residency_persistence_failed", 1, 0, false},
    OperationReasonRuleMetadata{"residency_recovery_required", 2, 0, false},
    OperationReasonRuleMetadata{"residency_quarantined", 3, 0, false},
    OperationReasonRuleMetadata{"residency_startup_rollback_incomplete", 4, 0, false},
    OperationReasonRuleMetadata{"residency_service_termination_incomplete", 5, 0, false},
    OperationReasonRuleMetadata{"residency_dead_backend_prune_incomplete", 6, 0, false},
    OperationReasonRuleMetadata{"residency_recovery_not_ready", 7, 0, false},
    OperationReasonRuleMetadata{"residency_cancelled", 8, 1, false},
    OperationReasonRuleMetadata{"residency_plan_invalidated", 9, 1, false},
    OperationReasonRuleMetadata{"residency_footprint_confidence_insufficient", 10, 2, false},
    OperationReasonRuleMetadata{"residency_capability_unsupported", 11, 2, false},
    OperationReasonRuleMetadata{"residency_evidence_missing", 12, 2, false},
    OperationReasonRuleMetadata{"residency_evidence_stale", 13, 2, false},
    OperationReasonRuleMetadata{"residency_evidence_unhealthy", 14, 2, false},
    OperationReasonRuleMetadata{"residency_evidence_incoherent", 15, 2, false},
    OperationReasonRuleMetadata{"residency_evidence_superseded", 16, 2, false},
    OperationReasonRuleMetadata{"slots_pinned_error", 17, 3, false},
    OperationReasonRuleMetadata{"router_residency_conflict", 18, 3, false},
    OperationReasonRuleMetadata{"residency_protected_pinned", 19, 3, false},
    OperationReasonRuleMetadata{"residency_protected_in_use", 20, 3, false},
    OperationReasonRuleMetadata{"residency_force_fence_failed", 21, 3, false},
    OperationReasonRuleMetadata{"residency_capacity_insufficient", 22, 4, false},
    OperationReasonRuleMetadata{"residency_plan_infeasible", 23, 4, false},
    OperationReasonRuleMetadata{"residency_startup_set_infeasible", 24, 4, false},
    OperationReasonRuleMetadata{"residency_planning_deadline_exceeded", 25, 4, false},
    OperationReasonRuleMetadata{"residency_critical_pressure_refusal", 26, 4, false},
    OperationReasonRuleMetadata{"residency_action_failed", 27, 5, false},
    OperationReasonRuleMetadata{"residency_partial_outcome", 28, 5, false},
    OperationReasonRuleMetadata{"residency_pressure_unresolved", 29, 5, false},
    OperationReasonRuleMetadata{"residency_operation_budget_exhausted", 30, 5, false},
    OperationReasonRuleMetadata{"residency_attempt_budget_exhausted", 31, 5, false},
    OperationReasonRuleMetadata{"residency_fallback_selected", 32, 6, true},
    OperationReasonRuleMetadata{"residency_unconditional_pin_write_deprecated", 33, 6, true},
}};

constexpr std::array<OperationReasonContextMetadata, 80> operation_reason_contexts{{
    OperationReasonContextMetadata{"residency_operation_succeeded", "admission|explicit_unload|force_unload|pressure_reclamation|startup_load|service_termination|dead_backend_pruning|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup|artifact_scope_recovery_cleanup|saved_pin_mutation|runtime_pin_mutation|legacy_pin_batch|resident_state_recovery_cleanup", "terminal", "succeeded", true, false, false},
    OperationReasonContextMetadata{"residency_persistence_failed", "admission|explicit_unload|force_unload|pressure_reclamation|startup_load|service_termination|dead_backend_pruning|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup", "closing", "null", true, true, false},
    OperationReasonContextMetadata{"residency_persistence_failed", "admission|explicit_unload|force_unload|pressure_reclamation|startup_load|service_termination|dead_backend_pruning|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup", "recovery_required", "null", true, true, false},
    OperationReasonContextMetadata{"residency_persistence_failed", "admission|explicit_unload|force_unload|pressure_reclamation|startup_load|service_termination|dead_backend_pruning|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup", "terminal", "failed|quarantined", true, true, false},
    OperationReasonContextMetadata{"residency_persistence_failed", "artifact_scope_recovery_cleanup", "closing", "null", true, true, false},
    OperationReasonContextMetadata{"residency_persistence_failed", "artifact_scope_recovery_cleanup", "terminal", "failed|partially_succeeded", true, true, false},
    OperationReasonContextMetadata{"residency_persistence_failed", "saved_pin_mutation|runtime_pin_mutation|legacy_pin_batch|resident_state_recovery_cleanup", "closing", "null", true, true, false},
    OperationReasonContextMetadata{"residency_persistence_failed", "saved_pin_mutation|runtime_pin_mutation|legacy_pin_batch|resident_state_recovery_cleanup", "recovery_required", "null", true, true, false},
    OperationReasonContextMetadata{"residency_persistence_failed", "saved_pin_mutation|runtime_pin_mutation|legacy_pin_batch|resident_state_recovery_cleanup", "terminal", "failed", true, true, false},
    OperationReasonContextMetadata{"residency_recovery_required", "admission|explicit_unload|force_unload|pressure_reclamation|startup_load|service_termination|dead_backend_pruning|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup|saved_pin_mutation|runtime_pin_mutation|legacy_pin_batch|resident_state_recovery_cleanup", "recovery_required", "null", true, true, false},
    OperationReasonContextMetadata{"residency_quarantined", "admission|explicit_unload|force_unload|pressure_reclamation|startup_load|service_termination|dead_backend_pruning|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup", "recovery_required", "null", true, true, false},
    OperationReasonContextMetadata{"residency_quarantined", "admission|explicit_unload|force_unload|pressure_reclamation|startup_load|service_termination|dead_backend_pruning|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup", "terminal", "quarantined", true, true, false},
    OperationReasonContextMetadata{"residency_startup_rollback_incomplete", "startup_load", "closing", "null", true, true, false},
    OperationReasonContextMetadata{"residency_startup_rollback_incomplete", "startup_load", "recovery_required", "null", true, true, false},
    OperationReasonContextMetadata{"residency_startup_rollback_incomplete", "startup_load", "terminal", "partially_succeeded|quarantined", true, true, false},
    OperationReasonContextMetadata{"residency_service_termination_incomplete", "service_termination", "closing", "null", true, true, false},
    OperationReasonContextMetadata{"residency_service_termination_incomplete", "service_termination", "recovery_required", "null", true, true, false},
    OperationReasonContextMetadata{"residency_service_termination_incomplete", "service_termination", "terminal", "failed|partially_succeeded|quarantined", true, true, false},
    OperationReasonContextMetadata{"residency_dead_backend_prune_incomplete", "dead_backend_pruning", "closing", "null", true, true, false},
    OperationReasonContextMetadata{"residency_dead_backend_prune_incomplete", "dead_backend_pruning", "recovery_required", "null", true, true, false},
    OperationReasonContextMetadata{"residency_dead_backend_prune_incomplete", "dead_backend_pruning", "terminal", "failed|partially_succeeded|quarantined", true, true, false},
    OperationReasonContextMetadata{"residency_recovery_not_ready", "admission|pressure_reclamation|startup_load", "evaluating", "null", true, true, false},
    OperationReasonContextMetadata{"residency_recovery_not_ready", "admission|pressure_reclamation|startup_load", "terminal", "refused", true, true, false},
    OperationReasonContextMetadata{"residency_cancelled", "admission|explicit_unload|force_unload|pressure_reclamation|startup_load|service_termination|dead_backend_pruning|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup|artifact_scope_recovery_cleanup", "evaluating|waiting_for_evidence|waiting_for_in_use|reserved|executing|closing", "null", true, true, false},
    OperationReasonContextMetadata{"residency_cancelled", "admission|explicit_unload|force_unload|pressure_reclamation|startup_load|service_termination|dead_backend_pruning|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup|artifact_scope_recovery_cleanup", "terminal", "cancelled|partially_succeeded", true, true, false},
    OperationReasonContextMetadata{"residency_cancelled", "admission|explicit_unload|force_unload|pressure_reclamation|startup_load|service_termination|dead_backend_pruning|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup", "recovery_required", "null", false, true, false},
    OperationReasonContextMetadata{"residency_cancelled", "saved_pin_mutation|runtime_pin_mutation|legacy_pin_batch|resident_state_recovery_cleanup", "evaluating|closing", "null", true, true, false},
    OperationReasonContextMetadata{"residency_cancelled", "saved_pin_mutation|runtime_pin_mutation|legacy_pin_batch|resident_state_recovery_cleanup", "recovery_required", "null", false, true, false},
    OperationReasonContextMetadata{"residency_cancelled", "saved_pin_mutation|runtime_pin_mutation|legacy_pin_batch|resident_state_recovery_cleanup", "terminal", "cancelled", true, true, false},
    OperationReasonContextMetadata{"residency_plan_invalidated", "admission|pressure_reclamation|startup_load|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup", "evaluating|waiting_for_evidence|waiting_for_in_use|reserved|executing|closing", "null", true, true, false},
    OperationReasonContextMetadata{"residency_plan_invalidated", "admission|pressure_reclamation|startup_load|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup", "terminal", "superseded|partially_succeeded", true, true, false},
    OperationReasonContextMetadata{"residency_footprint_confidence_insufficient", "admission|pressure_reclamation|startup_load", "evaluating", "null", true, true, false},
    OperationReasonContextMetadata{"residency_footprint_confidence_insufficient", "admission|pressure_reclamation|startup_load", "terminal", "refused", true, true, false},
    OperationReasonContextMetadata{"residency_capability_unsupported", "admission|pressure_reclamation|startup_load", "evaluating", "null", true, true, false},
    OperationReasonContextMetadata{"residency_capability_unsupported", "admission|pressure_reclamation|startup_load", "terminal", "refused", true, true, false},
    OperationReasonContextMetadata{"residency_evidence_missing", "admission|pressure_reclamation|startup_load|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup", "evaluating|waiting_for_evidence", "null", true, true, false},
    OperationReasonContextMetadata{"residency_evidence_missing", "admission|pressure_reclamation|startup_load|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup", "terminal", "refused|superseded", true, true, false},
    OperationReasonContextMetadata{"residency_evidence_stale", "admission|pressure_reclamation|startup_load|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup", "evaluating|waiting_for_evidence", "null", true, true, false},
    OperationReasonContextMetadata{"residency_evidence_stale", "admission|pressure_reclamation|startup_load|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup", "terminal", "refused|superseded", true, true, false},
    OperationReasonContextMetadata{"residency_evidence_unhealthy", "admission|pressure_reclamation|startup_load|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup", "evaluating|waiting_for_evidence", "null", true, true, false},
    OperationReasonContextMetadata{"residency_evidence_unhealthy", "admission|pressure_reclamation|startup_load|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup", "terminal", "refused|superseded", true, true, false},
    OperationReasonContextMetadata{"residency_evidence_incoherent", "admission|pressure_reclamation|startup_load|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup", "evaluating|waiting_for_evidence", "null", true, true, false},
    OperationReasonContextMetadata{"residency_evidence_incoherent", "admission|pressure_reclamation|startup_load|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup", "terminal", "refused|superseded", true, true, false},
    OperationReasonContextMetadata{"residency_evidence_superseded", "admission|pressure_reclamation|startup_load|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup", "evaluating|waiting_for_evidence", "null", true, true, false},
    OperationReasonContextMetadata{"residency_evidence_superseded", "admission|pressure_reclamation|startup_load|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup", "terminal", "refused|superseded", true, true, false},
    OperationReasonContextMetadata{"slots_pinned_error", "admission", "evaluating", "null", true, true, false},
    OperationReasonContextMetadata{"slots_pinned_error", "admission", "terminal", "refused", true, true, false},
    OperationReasonContextMetadata{"router_residency_conflict", "admission", "evaluating", "null", true, true, false},
    OperationReasonContextMetadata{"router_residency_conflict", "admission", "terminal", "refused", true, true, false},
    OperationReasonContextMetadata{"residency_protected_pinned", "admission|pressure_reclamation|startup_load", "evaluating", "null", true, true, false},
    OperationReasonContextMetadata{"residency_protected_pinned", "admission|pressure_reclamation|startup_load", "terminal", "refused", true, true, false},
    OperationReasonContextMetadata{"residency_protected_in_use", "admission|explicit_unload|pressure_reclamation|startup_load|service_termination|same_epoch_recovery_cleanup", "evaluating|waiting_for_in_use", "null", true, true, false},
    OperationReasonContextMetadata{"residency_protected_in_use", "admission|explicit_unload|pressure_reclamation|startup_load|service_termination|same_epoch_recovery_cleanup", "terminal", "refused", true, true, false},
    OperationReasonContextMetadata{"residency_force_fence_failed", "force_unload", "evaluating|waiting_for_in_use", "null", true, true, false},
    OperationReasonContextMetadata{"residency_force_fence_failed", "force_unload", "terminal", "refused|failed", true, true, false},
    OperationReasonContextMetadata{"residency_capacity_insufficient", "admission|startup_load", "evaluating", "null", true, true, false},
    OperationReasonContextMetadata{"residency_capacity_insufficient", "admission|startup_load", "terminal", "refused", true, true, false},
    OperationReasonContextMetadata{"residency_plan_infeasible", "admission|pressure_reclamation|startup_load|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup", "evaluating", "null", true, true, false},
    OperationReasonContextMetadata{"residency_plan_infeasible", "admission|pressure_reclamation|startup_load|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup", "terminal", "refused", true, true, false},
    OperationReasonContextMetadata{"residency_startup_set_infeasible", "startup_load", "evaluating", "null", true, true, false},
    OperationReasonContextMetadata{"residency_startup_set_infeasible", "startup_load", "terminal", "refused", true, true, false},
    OperationReasonContextMetadata{"residency_planning_deadline_exceeded", "admission|pressure_reclamation|startup_load|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup", "evaluating|waiting_for_in_use", "null", true, true, false},
    OperationReasonContextMetadata{"residency_planning_deadline_exceeded", "admission|pressure_reclamation|startup_load|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup", "terminal", "refused|failed", true, true, false},
    OperationReasonContextMetadata{"residency_critical_pressure_refusal", "admission|pressure_reclamation|startup_load", "evaluating", "null", true, true, false},
    OperationReasonContextMetadata{"residency_critical_pressure_refusal", "admission|pressure_reclamation|startup_load", "terminal", "refused", true, true, false},
    OperationReasonContextMetadata{"residency_action_failed", "admission|explicit_unload|force_unload|pressure_reclamation|startup_load|service_termination|dead_backend_pruning|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup|artifact_scope_recovery_cleanup", "executing|closing", "null", true, true, false},
    OperationReasonContextMetadata{"residency_action_failed", "admission|explicit_unload|force_unload|pressure_reclamation|startup_load|service_termination|dead_backend_pruning|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup|artifact_scope_recovery_cleanup", "terminal", "failed|partially_succeeded", true, true, false},
    OperationReasonContextMetadata{"residency_partial_outcome", "admission|explicit_unload|force_unload|pressure_reclamation|startup_load|service_termination|dead_backend_pruning|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup|artifact_scope_recovery_cleanup", "closing", "null", true, true, false},
    OperationReasonContextMetadata{"residency_partial_outcome", "admission|explicit_unload|force_unload|pressure_reclamation|startup_load|service_termination|dead_backend_pruning|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup|artifact_scope_recovery_cleanup", "terminal", "partially_succeeded", true, true, false},
    OperationReasonContextMetadata{"residency_pressure_unresolved", "pressure_reclamation", "evaluating|closing", "null", true, true, false},
    OperationReasonContextMetadata{"residency_pressure_unresolved", "pressure_reclamation", "terminal", "failed|refused", true, true, false},
    OperationReasonContextMetadata{"residency_operation_budget_exhausted", "admission|explicit_unload|force_unload|pressure_reclamation|startup_load|service_termination|dead_backend_pruning|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup|artifact_scope_recovery_cleanup|saved_pin_mutation|runtime_pin_mutation|legacy_pin_batch|resident_state_recovery_cleanup", "closing", "null", true, true, false},
    OperationReasonContextMetadata{"residency_operation_budget_exhausted", "admission|explicit_unload|force_unload|pressure_reclamation|startup_load|service_termination|dead_backend_pruning|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup|artifact_scope_recovery_cleanup|saved_pin_mutation|runtime_pin_mutation|legacy_pin_batch|resident_state_recovery_cleanup", "terminal", "refused|failed|superseded", true, true, false},
    OperationReasonContextMetadata{"residency_attempt_budget_exhausted", "same_epoch_recovery_cleanup|prior_epoch_owner_cleanup|artifact_scope_recovery_cleanup", "closing", "null", true, true, false},
    OperationReasonContextMetadata{"residency_attempt_budget_exhausted", "same_epoch_recovery_cleanup|prior_epoch_owner_cleanup|artifact_scope_recovery_cleanup", "terminal", "failed|partially_succeeded", true, true, false},
    OperationReasonContextMetadata{"residency_attempt_budget_exhausted", "resident_state_recovery_cleanup", "closing", "null", true, true, false},
    OperationReasonContextMetadata{"residency_attempt_budget_exhausted", "resident_state_recovery_cleanup", "terminal", "failed", true, true, false},
    OperationReasonContextMetadata{"residency_fallback_selected", "admission|pressure_reclamation|startup_load|same_epoch_recovery_cleanup|prior_epoch_owner_cleanup", "", "", false, true, true},
    OperationReasonContextMetadata{"residency_unconditional_pin_write_deprecated", "saved_pin_mutation", "evaluating|closing", "null", false, true, false},
    OperationReasonContextMetadata{"residency_unconditional_pin_write_deprecated", "saved_pin_mutation", "terminal", "succeeded", false, true, false},
}};

bool token_list_contains(std::string_view list, std::string_view token) noexcept {
    if (token.empty()) {
        return false;
    }
    std::size_t start = 0;
    while (start <= list.size()) {
        const auto end = list.find('|', start);
        const auto count = end == std::string_view::npos ? list.size() - start : end - start;
        if (list.substr(start, count) == token) {
            return true;
        }
        if (end == std::string_view::npos) {
            return false;
        }
        start = end + 1;
    }
    return false;
}

}

DecodedValue<PromotionUnitId>
GeneratedContractRegistry::decode_promotion_unit_id(std::string_view wire) {
    for (const auto& metadata : promotion_units) {
        if (metadata.id == wire) {
            return DecodedValue<PromotionUnitId>::known(PromotionUnitId(std::string(wire)));
        }
    }
    return DecodedValue<PromotionUnitId>::unknown(wire);
}

PromotionUnitKind
GeneratedContractRegistry::promotion_unit_kind(const PromotionUnitId& id) noexcept {
    for (const auto& metadata : promotion_units) {
        if (metadata.id == id.token()) {
            return metadata.kind;
        }
    }
    std::terminate();
}

ReasonCode GeneratedContractRegistry::decode_reason_code(std::string_view wire) {
    for (const auto& reason : reasons) {
        if (reason.code == wire) {
            return ReasonCode::known(KnownReasonCode(std::string(wire)));
        }
    }
    return ReasonCode::unknown(wire);
}

DecodedValue<FallbackId>
GeneratedContractRegistry::decode_fallback_id(std::string_view wire) {
    for (const auto value : fallback_ids) {
        if (value == wire) {
            return DecodedValue<FallbackId>::known(FallbackId(std::string(wire)));
        }
    }
    return DecodedValue<FallbackId>::unknown(wire);
}

DecodedValue<SchemaType>
GeneratedContractRegistry::decode_schema_type(std::string_view wire) {
    for (const auto value : schema_types) {
        if (value == wire) {
            return DecodedValue<SchemaType>::known(SchemaType(std::string(wire)));
        }
    }
    return DecodedValue<SchemaType>::unknown(wire);
}

const ReasonMetadata*
GeneratedContractRegistry::reason_metadata(std::string_view code) noexcept {
    for (const auto& reason : reasons) {
        if (reason.code == code) {
            return &reason;
        }
    }
    return nullptr;
}

DecodedValue<PromotionUnitId> decode_promotion_unit_id(std::string_view wire) {
    return GeneratedContractRegistry::decode_promotion_unit_id(wire);
}

PromotionUnitKind promotion_unit_kind(const PromotionUnitId& id) noexcept {
    return GeneratedContractRegistry::promotion_unit_kind(id);
}

ReasonCode decode_reason_code(std::string_view wire) {
    return GeneratedContractRegistry::decode_reason_code(wire);
}

DecodedValue<FallbackId> decode_fallback_id(std::string_view wire) {
    return GeneratedContractRegistry::decode_fallback_id(wire);
}

DecodedValue<SchemaType> decode_schema_type(std::string_view wire) {
    return GeneratedContractRegistry::decode_schema_type(wire);
}

const ReasonMetadata* reason_metadata(std::string_view code) noexcept {
    return GeneratedContractRegistry::reason_metadata(code);
}

const ReasonMetadata* reason_metadata(const KnownReasonCode& code) noexcept {
    return GeneratedContractRegistry::reason_metadata(code.token());
}

const ReasonMetadata* reason_metadata(const ReasonCode& code) noexcept {
    const auto* known = code.known_value();
    return known == nullptr ? nullptr : reason_metadata(*known);
}

std::optional<OperationFamily> operation_family(OperationKind kind) noexcept {
    const auto operation_kind = wire_name(kind);
    for (const auto& metadata : operation_families) {
        if (metadata.operation_kind == operation_kind) {
            const auto family = decode_operation_family(metadata.family);
            const auto* known = family.known_value();
            if (known != nullptr) {
                return *known;
            }
        }
    }
    return std::nullopt;
}

const OperationReasonRuleMetadata*
operation_reason_rule_metadata(std::string_view code) noexcept {
    for (const auto& metadata : operation_reason_rules) {
        if (metadata.code == code) {
            return &metadata;
        }
    }
    return nullptr;
}

bool operation_reason_is_legal(
    std::string_view code, OperationKind kind, OperationPhase phase,
    std::optional<TerminalOutcome> terminal_outcome, bool secondary) noexcept {
    const auto* rule = operation_reason_rule_metadata(code);
    if (rule == nullptr || (rule->secondary_only && !secondary)) {
        return false;
    }
    const auto operation_kind = wire_name(kind);
    const auto operation_phase = wire_name(phase);
    if (operation_kind.empty() || operation_phase.empty()) {
        return false;
    }
    if (terminal_outcome.has_value() && wire_name(*terminal_outcome).empty()) {
        return false;
    }
    const auto family = operation_family(kind);
    if (!family.has_value() ||
        !operation_state_is_valid(*family, phase, terminal_outcome)) {
        return false;
    }
    const auto outcome = terminal_outcome.has_value()
                             ? wire_name(*terminal_outcome)
                             : std::string_view{"null"};
    for (const auto& context : operation_reason_contexts) {
        if (context.code != code ||
            !token_list_contains(context.operation_kinds, operation_kind) ||
            (secondary ? !context.secondary : !context.primary)) {
            continue;
        }
        if (context.associated_primary_state ||
            (token_list_contains(context.phases, operation_phase) &&
             token_list_contains(context.terminal_outcomes, outcome))) {
            return true;
        }
    }
    return false;
}

const ReasonPresentationMetadata*
reason_presentation_metadata(std::string_view presentation_id) noexcept {
    for (const auto& presentation : presentations) {
        if (presentation.id == presentation_id) {
            return &presentation;
        }
    }
    return nullptr;
}

bool reason_category_is_known(std::string_view category_id) noexcept {
    for (const auto& presentation : presentations) {
        if (presentation.category_id == category_id) {
            return true;
        }
    }
    return false;
}

const ReasonPresentationMetadata*
unique_reason_presentation_for_category(std::string_view category_id) noexcept {
    const ReasonPresentationMetadata* match = nullptr;
    for (const auto& presentation : presentations) {
        if (presentation.category_id != category_id) {
            continue;
        }
        if (match != nullptr) {
            return nullptr;
        }
        match = &presentation;
    }
    return match;
}

const ReasonPresentationMetadata* matching_reason_presentation_for_category(
    std::string_view category_id, std::string_view presentation_id) noexcept {
    const auto* presentation = reason_presentation_metadata(presentation_id);
    return presentation != nullptr && presentation->category_id == category_id
               ? presentation
               : nullptr;
}

}
