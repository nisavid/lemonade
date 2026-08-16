#include "lemon/residency/generated_contract.h"

#include <array>
#include <string>

namespace lemon::residency {
namespace {

constexpr std::array<std::string_view, 39> promotion_unit_ids{{
    "H-NPU-FLM-CONFLICT-XDNA2-v1",
    "H-ROCM-ADM-GTT-HOST-v1",
    "H-ROCM-PRE-GTT-HOST-v1",
    "H-ROCM-REC-GTT-HOST-OWN-v1",
    "H-ROCM-STA-GTT-HOST-v1",
    "H-VULKAN-ADM-GTT-HOST-v1",
    "H-VULKAN-PRE-GTT-HOST-v1",
    "H-VULKAN-REC-GTT-HOST-OWN-v1",
    "H-VULKAN-STA-GTT-HOST-v1",
    "W-XDNA2-FLM-NPU-EMBEDDING-ADM-v1",
    "W-XDNA2-FLM-NPU-EMBEDDING-LFR-v1",
    "W-XDNA2-FLM-NPU-EMBEDDING-PIN-v1",
    "W-XDNA2-FLM-NPU-EMBEDDING-REC-v1",
    "W-XDNA2-FLM-NPU-EMBEDDING-STA-v1",
    "W-XDNA2-FLM-NPU-EMBEDDING-UNL-v1",
    "W-XDNA2-FLM-NPU-LLM-ADM-v1",
    "W-XDNA2-FLM-NPU-LLM-LFR-v1",
    "W-XDNA2-FLM-NPU-LLM-PIN-v1",
    "W-XDNA2-FLM-NPU-LLM-REC-v1",
    "W-XDNA2-FLM-NPU-LLM-STA-v1",
    "W-XDNA2-FLM-NPU-LLM-UNL-v1",
    "W-XDNA2-FLM-NPU-TRANSCRIPTION-ADM-v1",
    "W-XDNA2-FLM-NPU-TRANSCRIPTION-LFR-v1",
    "W-XDNA2-FLM-NPU-TRANSCRIPTION-PIN-v1",
    "W-XDNA2-FLM-NPU-TRANSCRIPTION-REC-v1",
    "W-XDNA2-FLM-NPU-TRANSCRIPTION-STA-v1",
    "W-XDNA2-FLM-NPU-TRANSCRIPTION-UNL-v1",
    "W-XDNA2-RYZENAI-LLM-NPU-LLM-ADM-v1",
    "W-XDNA2-RYZENAI-LLM-NPU-LLM-LFR-v1",
    "W-XDNA2-RYZENAI-LLM-NPU-LLM-PIN-v1",
    "W-XDNA2-RYZENAI-LLM-NPU-LLM-REC-v1",
    "W-XDNA2-RYZENAI-LLM-NPU-LLM-STA-v1",
    "W-XDNA2-RYZENAI-LLM-NPU-LLM-UNL-v1",
    "W-XDNA2-WHISPERCPP-NPU-TRANSCRIPTION-ADM-v1",
    "W-XDNA2-WHISPERCPP-NPU-TRANSCRIPTION-LFR-v1",
    "W-XDNA2-WHISPERCPP-NPU-TRANSCRIPTION-PIN-v1",
    "W-XDNA2-WHISPERCPP-NPU-TRANSCRIPTION-REC-v1",
    "W-XDNA2-WHISPERCPP-NPU-TRANSCRIPTION-STA-v1",
    "W-XDNA2-WHISPERCPP-NPU-TRANSCRIPTION-UNL-v1",
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

}

DecodedValue<PromotionUnitId>
GeneratedContractRegistry::decode_promotion_unit_id(std::string_view wire) {
    for (const auto value : promotion_unit_ids) {
        if (value == wire) {
            return DecodedValue<PromotionUnitId>::known(PromotionUnitId(std::string(wire)));
        }
    }
    return DecodedValue<PromotionUnitId>::unknown(wire);
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

}
