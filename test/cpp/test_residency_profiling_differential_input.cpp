#include "lemon/residency/profiling_differential_input.h"

#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using namespace lemon::residency;
using namespace std::chrono_literals;

void require(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}

std::string digest(char value) {
    return std::string(64, value);
}

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
    bindings.campaign_contract_sha256 = digest('7');
    bindings.procedure_revision_sha256 = digest('8');
    bindings.background_inventory_sha256 = digest('9');
    return bindings;
}

ProfilingNoTargetGttTrace stable_noise_trace() {
    ProfilingNoTargetGttTrace trace;
    trace.started_at = std::chrono::steady_clock::time_point{1h};
    trace.exact_end =
        trace.started_at + profiling_noise_trace_duration;
    trace.bindings = noise_bindings();
    trace.read_skew_uncertainty_bytes = 8;

    std::uint64_t index = 0;
    for (auto scheduled = trace.started_at; scheduled < trace.exact_end;
         scheduled += profiling_noise_nominal_cadence, ++index) {
        ProfilingNoTargetGttReading reading;
        reading.scheduled_at = scheduled;
        reading.read_started_at = scheduled;
        reading.read_finished_at = scheduled + 1ms;
        reading.gtt_used_bytes = 8192 + (index % 2) * 64;
        reading.observed_bindings = trace.bindings;
        trace.readings.push_back(std::move(reading));
    }
    return trace;
}

ProfilingTransactionContext transaction_context(
    const ProfilingNoiseBindings &bindings) {
    ProfilingTransactionContext context;
    context.deployment_id = bindings.deployment_id;
    context.sequence = 41;
    context.profiling_transaction_id = "profiling-transaction-a";
    context.selector.catalog_sha256 = digest('a');
    context.selector.catalog_selector.source_support_baseline =
        std::string(40, 'a');
    context.selector.catalog_selector.base_variant = "llamacpp";
    context.selector.catalog_selector.platform = "linux-amd64";
    context.selector.catalog_selector.backend_channel = "llamacpp:rocm";
    context.selector.catalog_selector.model_type = "llm";
    context.selector.catalog_selector.operation_template =
        OperationTemplate::Adm;
    context.selector.catalog_selector.operation_kind =
        OperationKind::Admission;
    context.selector.catalog_selector.constraints = {
        ConstraintKind::GpuSharedResidency};
    context.selector.catalog_selector.recovery = "hereditary-containment";
    context.selector.catalog_selector.material_profiles = {
        {"residency", "hatchery"}};
    context.selector.canonical_model_id = "model-a";
    context.selector.model_artifact_sha256 = digest('b');
    context.selector.backend_build_sha256 = digest('c');
    context.selector.device_identity_sha256 =
        bindings.device_identity_sha256;
    context.selector.topology_sha256 = bindings.topology_sha256;
    context.selector.dependency_set_sha256 = digest('d');
    context.selector.driver_identity_sha256 =
        bindings.driver_identity_sha256;
    context.selector.configuration_sha256 = digest('e');
    context.selector.workload_sha256 = digest('f');
    context.selector.operation_contract_sha256 = digest('0');
    context.selector_sha256 = digest('1');
    context.generations.model = 1;
    context.generations.backend = 2;
    context.generations.device = 3;
    context.generations.topology = 4;
    context.generations.driver = 5;
    context.generations.configuration = 6;
    context.generations.workload = 7;
    context.observation_contract_sha256 = digest('2');
    context.predictor_contract_sha256 = digest('3');
    context.ownership_recovery_evidence_sha256 = digest('4');
    context.action_lease_closure_sha256 = digest('5');
    return context;
}

ProfilingDifferentialInputDraft input_draft(
    const ProfilingNoiseBindings &bindings,
    const ParsedProfilingNoiseResult &noise) {
    ProfilingDifferentialInputDraft draft;
    draft.identity.transaction = transaction_context(bindings);
    draft.identity.target_client_identity_sha256 = digest('6');
    draft.identity.target_containment_identity_sha256 = digest('7');
    draft.identity.counter_continuity_epoch_sha256 = digest('8');
    draft.identity.safety_contract_sha256 = digest('9');
    draft.identity.noise_trace_provenance_sha256 =
        std::string(noise.trace_provenance_sha256());
    draft.revision.calibration_revision_sha256 = digest('b');
    draft.revision.attempt_receipt_sha256 = digest('c');
    draft.revision.state = ProfilingDifferentialRevisionState::Fresh;
    draft.accounting.x_gtt_bytes = 16;
    draft.accounting.m_gtt_bytes = 32;
    draft.accounting.x_gtt_evidence_sha256 = digest('d');
    draft.accounting.m_gtt_policy_sha256 = digest('e');
    draft.accounting.partition_contract_sha256 = digest('f');
    draft.calibration_repetitions = 2;
    draft.validation_repetitions = 1;
    return draft;
}

ProfilingDifferentialRevalidationObservation observation_for(
    const FrozenProfilingDifferentialInput &input,
    std::chrono::steady_clock::time_point checked_at) {
    ProfilingDifferentialRevalidationObservation observation;
    observation.checked_at = checked_at;
    observation.observed_bindings = input.noise().bindings();
    observation.observed_noise_result_checksum_sha256 =
        std::string(input.noise().checksum_sha256());
    observation.observed_counter_continuity_epoch_sha256 =
        input.identity().counter_continuity_epoch_sha256;
    observation.observed_non_target_gtt_range_bytes =
        input.noise().n_gtt_bytes();
    return observation;
}

FrozenProfilingDifferentialInput freeze_valid_input(
    const ParsedProfilingNoiseResult &noise,
    const ProfilingNoiseBindings &bindings) {
    auto frozen = freeze_profiling_differential_input(
        noise, input_draft(bindings, noise));
    require(frozen.accepted(),
            "consumer failure fixture did not freeze valid input");
    return std::move(*frozen.input);
}

void require_freeze_rejected(
    const ParsedProfilingNoiseResult &noise,
    ProfilingDifferentialInputDraft draft,
    ProfilingDifferentialInputFreezeStatus expected,
    const std::string &message) {
    auto rejected =
        freeze_profiling_differential_input(noise, std::move(draft));
    require(!rejected.accepted() && rejected.status == expected &&
                !rejected.input.has_value(),
            message);
}

void require_revalidation_rejected(
    FrozenProfilingDifferentialInput &input,
    ProfilingDifferentialRevalidationObservation observation,
    ProfilingDifferentialRevalidationStatus expected,
    const std::string &message) {
    auto rejected =
        revalidate_profiling_differential_input(input, observation);
    require(!rejected.accepted() && rejected.status == expected &&
                input.revision_rejected(),
            message);
}

void require_input_freezes_and_revalidates_before_repetitions() {
    auto trace = stable_noise_trace();
    auto produced = produce_no_target_gtt_noise(trace);
    require(produced.accepted(),
            "consumer fixture did not produce canonical no-target noise");

    auto mismatched_draft =
        input_draft(trace.bindings, *produced.result);
    auto mismatched_provenance =
        mismatched_draft.identity.noise_trace_provenance_sha256;
    mismatched_provenance.front() =
        mismatched_provenance.front() == '0' ? '1' : '0';
    mismatched_draft.identity.noise_trace_provenance_sha256 =
        std::move(mismatched_provenance);
    auto mismatched = freeze_profiling_differential_input(
        *produced.result, std::move(mismatched_draft));
    require(!mismatched.accepted() &&
                mismatched.status ==
                    ProfilingDifferentialInputFreezeStatus::
                        TraceProvenanceMismatch,
            "a caller-supplied trace provenance digest replaced the "
            "producer result");

    auto draft = input_draft(trace.bindings, *produced.result);
    const auto expected_noise_checksum =
        std::string(produced.result->checksum_sha256());
    const auto expected_trace_provenance =
        std::string(produced.result->trace_provenance_sha256());
    auto frozen =
        freeze_profiling_differential_input(*produced.result, draft);
    require(frozen.accepted(),
            "a valid differential input freezes before target observation");

    auto input = std::move(*frozen.input);
    require(input.noise().n_gtt_bytes() == 72 &&
                input.noise().checksum_sha256() ==
                    expected_noise_checksum &&
                input.identity().noise_trace_provenance_sha256 ==
                    expected_trace_provenance &&
                input.accounting().x_gtt_bytes == 16 &&
                input.accounting().m_gtt_bytes == 32 &&
                input.calibration_repetitions() == 2 &&
                input.validation_repetitions() == 1 &&
                input.frozen_input_sha256().size() == 64,
            "the frozen input changed its noise or accounting partition");

    draft.accounting.x_gtt_bytes = 4096;
    draft.identity.target_client_identity_sha256 = digest('0');
    require(input.accounting().x_gtt_bytes == 16 &&
                input.identity().target_client_identity_sha256 ==
                    digest('6'),
            "the frozen input retained mutable caller state");

    auto baseline =
        observation_for(input, trace.exact_end + 1h);
    auto baseline_check =
        revalidate_profiling_differential_input(input, baseline);
    require(baseline_check.accepted(),
            "matching no-target bindings do not begin a repetition");

    auto loaded =
        observation_for(input, trace.exact_end + std::chrono::hours(24 * 30));
    loaded.target_activity = ProfilingDifferentialTargetActivity{
        input.identity().target_client_identity_sha256,
        input.identity().target_containment_identity_sha256};
    auto loaded_check =
        revalidate_profiling_differential_input(input, loaded);
    require(loaded_check.accepted(),
            "elapsed time or the declared contained target invalidated the "
            "input");

    auto unrelated = loaded;
    unrelated.target_activity->client_identity_sha256 = digest('1');
    auto unrelated_check =
        revalidate_profiling_differential_input(input, unrelated);
    require(!unrelated_check.accepted() &&
                unrelated_check.status ==
                    ProfilingDifferentialRevalidationStatus::TargetMismatch &&
                input.revision_rejected(),
            "unrelated GTT client drift did not reject the revision");

    auto same_revision_retry =
        revalidate_profiling_differential_input(input, loaded);
    require(!same_revision_retry.accepted() &&
                same_revision_retry.status ==
                    ProfilingDifferentialRevalidationStatus::RevisionRejected,
            "a rejected revision was allowed to refit");

    auto rejected_draft =
        input_draft(trace.bindings, *produced.result);
    rejected_draft.revision.state =
        ProfilingDifferentialRevisionState::PreviouslyRejected;
    auto rejected_refit = freeze_profiling_differential_input(
        *produced.result, std::move(rejected_draft));
    require(!rejected_refit.accepted() &&
                rejected_refit.status ==
                    ProfilingDifferentialInputFreezeStatus::
                        RevisionAlreadyRejected,
            "a journal-rejected calibration revision froze again");
}

void require_consumer_failure_contract() {
    auto trace = stable_noise_trace();
    auto produced = produce_no_target_gtt_noise(trace);
    require(produced.accepted(),
            "consumer failure fixture did not produce canonical noise");
    const auto &noise = *produced.result;

    auto no_calibration = input_draft(trace.bindings, noise);
    no_calibration.calibration_repetitions = 0;
    require_freeze_rejected(
        noise, std::move(no_calibration),
        ProfilingDifferentialInputFreezeStatus::InvalidRepetitionCount,
        "zero calibration repetitions were accepted");

    auto no_validation = input_draft(trace.bindings, noise);
    no_validation.validation_repetitions = 0;
    require_freeze_rejected(
        noise, std::move(no_validation),
        ProfilingDifferentialInputFreezeStatus::InvalidRepetitionCount,
        "zero validation repetitions were accepted");

    auto accounting_overflow = input_draft(trace.bindings, noise);
    accounting_overflow.accounting.x_gtt_bytes =
        std::numeric_limits<std::uint64_t>::max();
    accounting_overflow.accounting.m_gtt_bytes = 0;
    require_freeze_rejected(
        noise, std::move(accounting_overflow),
        ProfilingDifferentialInputFreezeStatus::InvalidAccountingPartition,
        "overflowing differential accounting was accepted");

    auto mismatched_identity = input_draft(trace.bindings, noise);
    mismatched_identity.identity.transaction.selector
        .device_identity_sha256 = digest('a');
    require_freeze_rejected(
        noise, std::move(mismatched_identity),
        ProfilingDifferentialInputFreezeStatus::InvalidIdentity,
        "a target transaction bound to another device was accepted");

    auto reset_input = freeze_valid_input(noise, trace.bindings);
    auto reset = observation_for(reset_input, trace.exact_end + 1h);
    reset.counter_reset_detected = true;
    require_revalidation_rejected(
        reset_input, std::move(reset),
        ProfilingDifferentialRevalidationStatus::CounterReset,
        "a global GTT counter reset did not invalidate the input");

    auto discontinuity_input = freeze_valid_input(noise, trace.bindings);
    auto discontinuity =
        observation_for(discontinuity_input, trace.exact_end + 2h);
    discontinuity.counter_discontinuity_detected = true;
    require_revalidation_rejected(
        discontinuity_input, std::move(discontinuity),
        ProfilingDifferentialRevalidationStatus::CounterDiscontinuity,
        "a global GTT counter discontinuity did not invalidate the input");

    auto reboot_input = freeze_valid_input(noise, trace.bindings);
    auto reboot = observation_for(reboot_input, trace.exact_end + 3h);
    reboot.observed_bindings.boot_id_sha256 = digest('a');
    require_revalidation_rejected(
        reboot_input, std::move(reboot),
        ProfilingDifferentialRevalidationStatus::BindingMismatch,
        "a changed boot identity did not invalidate the input");

    auto background_input = freeze_valid_input(noise, trace.bindings);
    auto background =
        observation_for(background_input, trace.exact_end + 4h);
    background.observed_bindings.background_inventory_sha256 =
        digest('a');
    require_revalidation_rejected(
        background_input, std::move(background),
        ProfilingDifferentialRevalidationStatus::BackgroundDrift,
        "changed non-target background inventory did not invalidate the "
        "input");

    auto variation_input = freeze_valid_input(noise, trace.bindings);
    auto variation =
        observation_for(variation_input, trace.exact_end + 5h);
    variation.observed_non_target_gtt_range_bytes =
        noise.n_gtt_bytes() + 1;
    require_revalidation_rejected(
        variation_input, std::move(variation),
        ProfilingDifferentialRevalidationStatus::ExcessVariation,
        "non-target variation above N_gtt did not invalidate the input");

    auto checksum_input = freeze_valid_input(noise, trace.bindings);
    auto checksum =
        observation_for(checksum_input, trace.exact_end + 6h);
    auto mismatched_checksum =
        checksum.observed_noise_result_checksum_sha256;
    mismatched_checksum.front() =
        mismatched_checksum.front() == '0' ? '1' : '0';
    checksum.observed_noise_result_checksum_sha256 =
        std::move(mismatched_checksum);
    require_revalidation_rejected(
        checksum_input, std::move(checksum),
        ProfilingDifferentialRevalidationStatus::NoiseResultMismatch,
        "a changed immutable noise-result checksum did not invalidate the "
        "input");
}

} // namespace

int main() {
    try {
        require_input_freezes_and_revalidates_before_repetitions();
        require_consumer_failure_contract();
        std::cout << "PASS: residency profiling differential input tests\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << "\n";
        return 1;
    }
}
