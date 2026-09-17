#include "lemon/residency/profiling_differential_input.h"

#include "residency_profiling_hash_failure_test_support.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using namespace lemon::residency;
using lemon::residency::profiling_internal::test::Sha256FailureMode;
using lemon::residency::profiling_internal::test::fail_sha256_after;
using namespace std::chrono_literals;

void require(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}

std::string digest(char value) { return std::string(64, value); }

ProfilingNoiseBindings noise_bindings() {
    ProfilingNoiseBindings bindings;
    bindings.deployment_id = digest('f');
    bindings.deployment_epoch_sha256 = digest('0');
    bindings.boot_id_sha256 = digest('1');
    bindings.device_identity_sha256 = digest('2');
    bindings.topology_sha256 = digest('3');
    bindings.kernel_identity_sha256 = digest('4');
    bindings.driver_identity_sha256 = digest('5');
    bindings.counter_source_id = "linux-amd-mem-info-gtt-used";
    bindings.counter_source_revision_sha256 = digest('6');
    bindings.counter_continuity_epoch_sha256 = digest('7');
    bindings.campaign_contract_sha256 = digest('8');
    bindings.procedure_revision_sha256 = digest('9');
    bindings.background_inventory_sha256 = digest('a');
    return bindings;
}

ParsedProfilingNoiseResult noise_result() {
    ProfilingNoTargetGttTrace trace;
    trace.started_at = std::chrono::steady_clock::time_point{1h};
    trace.exact_end = trace.started_at + profiling_noise_trace_duration;
    trace.bindings = noise_bindings();
    trace.read_skew_uncertainty_bytes = 8;
    for (auto scheduled = trace.started_at; scheduled < trace.exact_end;
         scheduled += profiling_noise_nominal_cadence) {
        trace.readings.push_back({
            scheduled,
            scheduled,
            scheduled + 1ms,
            8192,
            trace.bindings,
        });
    }
    auto produced = produce_no_target_gtt_noise(trace);
    require(produced.accepted(), "hash-failure fixture did not produce noise");
    return *produced.result;
}

ProfilingTransactionContext transaction_context(
    const ProfilingNoiseBindings &bindings) {
    ProfilingTransactionContext context;
    context.deployment_id = bindings.deployment_id;
    context.sequence = 1;
    context.profiling_transaction_id = "profiling-hash-failure";
    context.selector.catalog_sha256 = digest('b');
    context.selector.catalog_selector.source_support_baseline =
        std::string(40, 'a');
    context.selector.catalog_selector.base_variant = "llamacpp";
    context.selector.catalog_selector.platform = "linux-amd64";
    context.selector.catalog_selector.backend_channel = "llamacpp:rocm";
    context.selector.catalog_selector.model_type = "llm";
    context.selector.catalog_selector.operation_template =
        OperationTemplate::Adm;
    context.selector.catalog_selector.operation_kind = OperationKind::Admission;
    context.selector.catalog_selector.constraints = {
        ConstraintKind::GpuSharedResidency};
    context.selector.catalog_selector.recovery = "hereditary-containment";
    context.selector.catalog_selector.material_profiles = {
        {"residency", "hatchery"}};
    context.selector.canonical_model_id = "model-a";
    context.selector.model_artifact_sha256 = digest('c');
    context.selector.backend_build_sha256 = digest('d');
    context.selector.device_identity_sha256 =
        bindings.device_identity_sha256;
    context.selector.topology_sha256 = bindings.topology_sha256;
    context.selector.dependency_set_sha256 = digest('e');
    context.selector.driver_identity_sha256 =
        bindings.driver_identity_sha256;
    context.selector.configuration_sha256 = digest('f');
    context.selector.workload_sha256 = digest('0');
    context.selector.operation_contract_sha256 = digest('1');
    auto canonical = canonicalize_local_overlay_selector(context.selector);
    require(canonical.accepted(), "hash-failure selector was invalid");
    context.selector = std::move(*canonical.selector);
    context.selector_sha256 = canonical.selector_sha256;
    context.generations = {1, 2, 3, 4, 5, 6, 7};
    context.observation_contract_sha256 = digest('2');
    context.predictor_contract_sha256 = digest('3');
    context.ownership_recovery_evidence_sha256 = digest('4');
    context.action_lease_closure_sha256 = digest('5');
    return context;
}

FrozenProfilingDifferentialInput frozen_input(
    const ParsedProfilingNoiseResult &noise) {
    ProfilingDifferentialInputDraft draft;
    draft.identity.transaction = transaction_context(noise.bindings());
    draft.identity.target_client_identity_sha256 = digest('6');
    draft.identity.target_containment_identity_sha256 = digest('7');
    draft.identity.counter_continuity_epoch_sha256 =
        noise.bindings().counter_continuity_epoch_sha256;
    draft.identity.safety_contract_sha256 = digest('8');
    draft.identity.noise_trace_provenance_sha256 =
        std::string(noise.trace_provenance_sha256());
    draft.method_binding.method_id = "differential_retained_gtt";
    draft.method_binding.method_revision_sha256 = digest('8');
    draft.method_binding.constraint_id = "amd.shared_gtt.retained_bytes";
    draft.method_binding.constraint_revision_sha256 = digest('9');
    draft.method_binding.covered_effect = "retained_gtt";
    draft.noise_validity.noise_result_checksum_sha256 =
        std::string(noise.checksum_sha256());
    draft.noise_validity.state = ProfilingNoiseValidityState::Valid;
    draft.revision.calibration_revision_sha256 = digest('9');
    draft.revision.attempt_receipt_sha256 = digest('a');
    draft.revision.state = ProfilingDifferentialRevisionState::Fresh;
    draft.accounting.x_gtt_evidence_sha256 = digest('b');
    draft.accounting.m_gtt_policy_sha256 = digest('c');
    draft.accounting.partition_contract_sha256 = digest('d');
    draft.calibration_repetitions = 1;
    draft.validation_repetitions = 1;
    auto frozen = freeze_profiling_differential_input(noise, std::move(draft));
    require(frozen.accepted(), "hash-failure input did not freeze");
    return std::move(*frozen.input);
}

ProfilingDifferentialRevalidationObservation observation_for(
    const FrozenProfilingDifferentialInput &input) {
    ProfilingDifferentialRevalidationObservation observation;
    observation.checked_at = std::chrono::steady_clock::time_point{2h};
    observation.observed_bindings = input.noise().bindings();
    observation.observed_noise_result_checksum_sha256 =
        std::string(input.noise().checksum_sha256());
    observation.observed_counter_continuity_epoch_sha256 =
        input.identity().counter_continuity_epoch_sha256;
    observation.observed_non_target_gtt_range_bytes =
        input.noise().n_gtt_bytes();
    return observation;
}

void require_failure(
    const ParsedProfilingNoiseResult &noise,
    std::size_t successful_hashes_before_failure,
    Sha256FailureMode mode,
    bool reset,
    bool target_mismatch,
    ProfilingDifferentialRevalidationStatus expected_status,
    ProfilingDifferentialRevalidationDisposition expected_disposition,
    std::string_view label) {
    auto input = frozen_input(noise);
    ProfilingDifferentialAttemptState attempt(input);
    auto observation = observation_for(input);
    observation.counter_reset_detected = reset;
    if (target_mismatch) {
        observation.target_activity = ProfilingDifferentialTargetActivity{
            digest('0'),
            input.identity().target_containment_identity_sha256,
        };
    }
    fail_sha256_after(successful_hashes_before_failure, mode);
    auto result = revalidate_profiling_differential_input(
        input, attempt, ProfilingDifferentialRepetitionPhase::Calibration, 0,
        observation);
    const bool expected_invalidation =
        expected_disposition ==
        ProfilingDifferentialRevalidationDisposition::InvalidateNoiseResult;
    require(!result.accepted() && !result.receipt.has_value() &&
                result.status == expected_status &&
                result.disposition == expected_disposition &&
                attempt.revision_rejected() &&
                attempt.noise_result_invalidated() == expected_invalidation,
            std::string(label));
}

void require_nonexception_hash_failures_preserve_disposition() {
    const auto noise = noise_result();
    require_failure(
        noise, 0, Sha256FailureMode::ReturnUnavailable, false, false,
        ProfilingDifferentialRevalidationStatus::DigestUnavailable,
        ProfilingDifferentialRevalidationDisposition::RejectRevision,
        "observation hash unavailability was reported as binding drift");
    require_failure(
        noise, 0, Sha256FailureMode::ReturnUnavailable, true, false,
        ProfilingDifferentialRevalidationStatus::DigestUnavailable,
        ProfilingDifferentialRevalidationDisposition::InvalidateNoiseResult,
        "observation hash unavailability erased latched invalidation");
    require_failure(
        noise, 1, Sha256FailureMode::ReturnUnavailable, true, false,
        ProfilingDifferentialRevalidationStatus::DigestUnavailable,
        ProfilingDifferentialRevalidationDisposition::InvalidateNoiseResult,
        "receipt hash unavailability downgraded latched invalidation");
    require_failure(
        noise, 1, Sha256FailureMode::ReturnUnavailable, false, true,
        ProfilingDifferentialRevalidationStatus::DigestUnavailable,
        ProfilingDifferentialRevalidationDisposition::RejectRevision,
        "receipt hash unavailability promoted target mismatch to invalidation");
}

void require_hash_exceptions_keep_the_latched_authority_boundary() {
    const auto noise = noise_result();
    require_failure(
        noise, 0, Sha256FailureMode::ThrowException, true, false,
        ProfilingDifferentialRevalidationStatus::RevisionRejected,
        ProfilingDifferentialRevalidationDisposition::InvalidateNoiseResult,
        "observation hash exception erased latched invalidation");
    require_failure(
        noise, 1, Sha256FailureMode::ThrowException, true, false,
        ProfilingDifferentialRevalidationStatus::RevisionRejected,
        ProfilingDifferentialRevalidationDisposition::InvalidateNoiseResult,
        "receipt hash exception erased latched invalidation");
    require_failure(
        noise, 1, Sha256FailureMode::ThrowException, false, true,
        ProfilingDifferentialRevalidationStatus::RevisionRejected,
        ProfilingDifferentialRevalidationDisposition::RejectRevision,
        "receipt hash exception granted target mismatch invalidation authority");
}

} // namespace

int main() {
    try {
        require_nonexception_hash_failures_preserve_disposition();
        require_hash_exceptions_keep_the_latched_authority_boundary();
        std::cout << "PASS: residency profiling hash-failure tests\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
