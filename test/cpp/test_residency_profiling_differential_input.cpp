#include "lemon/residency/profiling_differential_input.h"

#include <mbedtls/md.h>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
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

std::string raw_sha256(std::string_view bytes) {
    const auto *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == nullptr) throw std::runtime_error("SHA-256 is unavailable");

    mbedtls_md_context_t context;
    mbedtls_md_init(&context);
    std::array<unsigned char, 32> output{};
    const bool failed =
        mbedtls_md_setup(&context, info, 0) != 0 ||
        mbedtls_md_starts(&context) != 0 ||
        mbedtls_md_update(
            &context,
            reinterpret_cast<const unsigned char *>(bytes.data()),
            bytes.size()) != 0 ||
        mbedtls_md_finish(&context, output.data()) != 0;
    mbedtls_md_free(&context);
    if (failed) throw std::runtime_error("SHA-256 failed");

    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const auto byte : output) {
        result.push_back(hex[(byte >> 4) & 0x0f]);
        result.push_back(hex[byte & 0x0f]);
    }
    return result;
}

std::string canonical_noise_result_with_n_gtt(
    const ParsedProfilingNoiseResult &result,
    std::uint64_t n_gtt_bytes) {
    auto document = nlohmann::json::parse(
        result.canonical_bytes().begin(), result.canonical_bytes().end());
    document.erase("checksum_sha256");
    document["n_gtt_bytes"] = n_gtt_bytes;

    constexpr char domain[] =
        "lemonade.residency.profiling-noise-result/v1\0";
    std::string checksummed_bytes(domain, sizeof(domain) - 1);
    checksummed_bytes += document.dump();
    document["checksum_sha256"] = raw_sha256(checksummed_bytes);
    return document.dump();
}

template <typename Result, typename = void>
struct HasReadSkewUncertainty : std::false_type {};

template <typename Result>
struct HasReadSkewUncertainty<
    Result,
    std::void_t<decltype(
        std::declval<const Result &>().read_skew_uncertainty_bytes())>>
    : std::true_type {};

template <typename Result>
std::optional<std::uint64_t> exposed_uncertainty(const Result &result) {
    if constexpr (HasReadSkewUncertainty<Result>::value) {
        return result.read_skew_uncertainty_bytes();
    }
    return std::nullopt;
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
    bindings.counter_continuity_epoch_sha256 = digest('8');
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

std::string canonical_selector_sha256(
    const ProfilingTransactionContext &context) {
    auto canonical =
        canonicalize_local_overlay_selector(context.selector);
    require(canonical.accepted(),
            "canonical selector fixture was rejected");
    return canonical.selector_sha256;
}

ProfilingDifferentialInputDraft input_draft(
    const ProfilingNoiseBindings &bindings,
    const ParsedProfilingNoiseResult &noise) {
    ProfilingDifferentialInputDraft draft;
    draft.identity.transaction = transaction_context(bindings);
    draft.identity.transaction.selector_sha256 =
        canonical_selector_sha256(draft.identity.transaction);
    draft.identity.target_client_identity_sha256 = digest('6');
    draft.identity.target_containment_identity_sha256 = digest('7');
    draft.identity.safety_contract_sha256 = digest('9');
    draft.identity.noise_trace_provenance_sha256 =
        std::string(noise.trace_provenance_sha256());
    draft.identity.counter_continuity_epoch_sha256 =
        bindings.counter_continuity_epoch_sha256;
    draft.method_binding.method_id = "differential_retained_gtt";
    draft.method_binding.method_revision_sha256 = digest('6');
    draft.method_binding.constraint_id = "amd.shared_gtt.retained_bytes";
    draft.method_binding.constraint_revision_sha256 = digest('7');
    draft.method_binding.covered_effect = "retained_gtt";
    draft.noise_validity.noise_result_checksum_sha256 =
        std::string(noise.checksum_sha256());
    draft.noise_validity.state = ProfilingNoiseValidityState::Valid;
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
    ProfilingDifferentialRevalidationDisposition expected_disposition,
    const std::string &message) {
    ProfilingDifferentialAttemptState attempt(input);
    auto rejected = revalidate_profiling_differential_input(
        input, attempt, ProfilingDifferentialRepetitionPhase::Calibration, 0,
        observation);
    require(!rejected.accepted() && rejected.status == expected &&
                rejected.disposition == expected_disposition &&
                rejected.receipt.has_value() &&
                attempt.revision_rejected() &&
                attempt.noise_result_invalidated() ==
                    (expected_disposition ==
                     ProfilingDifferentialRevalidationDisposition::
                         InvalidateNoiseResult),
            message);
}

void require_parser_rejects_bound_below_uncertainty() {
    auto trace = stable_noise_trace();
    auto produced = produce_no_target_gtt_noise(trace);
    require(produced.accepted(),
            "parser-to-freeze fixture did not produce canonical noise");

    const auto invalid_bytes = canonical_noise_result_with_n_gtt(
        *produced.result, trace.read_skew_uncertainty_bytes - 1);
    auto parsed = parse_profiling_noise_result(invalid_bytes);
    if (parsed.accepted()) {
        auto frozen = freeze_profiling_differential_input(
            *parsed.result, input_draft(trace.bindings, *parsed.result));
        require(!frozen.accepted(),
                "canonical N_gtt below read/skew uncertainty parsed and "
                "froze");
    }
    require(!parsed.accepted() &&
                parsed.status == ProfilingNoiseParseStatus::InvalidValue &&
                !parsed.result.has_value(),
            "canonical N_gtt below read/skew uncertainty was not rejected "
            "semantically");
}

void require_input_freezes_and_revalidates_before_repetitions() {
    auto trace = stable_noise_trace();
    auto produced = produce_no_target_gtt_noise(trace);
    require(produced.accepted(),
            "consumer fixture did not produce canonical no-target noise");

    auto parsed =
        parse_profiling_noise_result(produced.result->canonical_bytes());
    require(parsed.accepted(),
            "consumer fixture did not parse canonical no-target noise");
    const auto &noise = *parsed.result;
    const auto uncertainty = exposed_uncertainty(noise);
    require(uncertainty.has_value() &&
                *uncertainty == trace.read_skew_uncertainty_bytes,
            "parsed noise result did not expose its measured uncertainty");

    auto mismatched_draft =
        input_draft(trace.bindings, noise);
    auto mismatched_provenance =
        mismatched_draft.identity.noise_trace_provenance_sha256;
    mismatched_provenance.front() =
        mismatched_provenance.front() == '0' ? '1' : '0';
    mismatched_draft.identity.noise_trace_provenance_sha256 =
        std::move(mismatched_provenance);
    auto mismatched = freeze_profiling_differential_input(
        noise, std::move(mismatched_draft));
    require(!mismatched.accepted() &&
                mismatched.status ==
                    ProfilingDifferentialInputFreezeStatus::
                        TraceProvenanceMismatch,
            "a caller-supplied trace provenance digest replaced the "
            "producer result");

    auto draft = input_draft(trace.bindings, noise);
    const auto expected_noise_checksum =
        std::string(noise.checksum_sha256());
    const auto expected_trace_provenance =
        std::string(noise.trace_provenance_sha256());
    auto frozen =
        freeze_profiling_differential_input(noise, draft);
    require(frozen.accepted(),
            "a valid differential input freezes before target observation");

    auto input = std::move(*frozen.input);
    ProfilingDifferentialAttemptState attempt(input);
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
    auto baseline_check = revalidate_profiling_differential_input(
        input, attempt, ProfilingDifferentialRepetitionPhase::Calibration, 0,
        baseline);
    require(
        baseline_check.accepted() &&
            baseline_check.disposition ==
                ProfilingDifferentialRevalidationDisposition::Continue,
        "matching no-target bindings did not continue the revision");

    auto loaded =
        observation_for(input, trace.exact_end + std::chrono::hours(24 * 30));
    loaded.target_activity = ProfilingDifferentialTargetActivity{
        input.identity().target_client_identity_sha256,
        input.identity().target_containment_identity_sha256};
    auto loaded_check = revalidate_profiling_differential_input(
        input, attempt, ProfilingDifferentialRepetitionPhase::Calibration, 1,
        loaded);
    require(loaded_check.accepted() &&
                loaded_check.disposition ==
                    ProfilingDifferentialRevalidationDisposition::Continue,
            "elapsed time or the declared contained target invalidated the "
            "input");

    auto mismatched_target = loaded;
    mismatched_target.target_activity->client_identity_sha256 = digest('1');
    auto target_check = revalidate_profiling_differential_input(
        input, attempt, ProfilingDifferentialRepetitionPhase::Validation, 0,
        mismatched_target);
    require(!target_check.accepted() &&
                target_check.status ==
                    ProfilingDifferentialRevalidationStatus::TargetMismatch &&
                target_check.disposition ==
                    ProfilingDifferentialRevalidationDisposition::
                        RejectRevision &&
                attempt.revision_rejected() &&
                !attempt.noise_result_invalidated(),
            "mismatched declared target did not reject the revision");

    auto same_revision_retry = revalidate_profiling_differential_input(
        input, attempt, ProfilingDifferentialRepetitionPhase::Validation, 0,
        loaded);
    require(!same_revision_retry.accepted() &&
                same_revision_retry.status ==
                    ProfilingDifferentialRevalidationStatus::RevisionRejected &&
                same_revision_retry.disposition ==
                    ProfilingDifferentialRevalidationDisposition::
                        RejectRevision,
            "a rejected revision was allowed to refit");

    auto rejected_draft =
        input_draft(trace.bindings, noise);
    rejected_draft.revision.state =
        ProfilingDifferentialRevisionState::PreviouslyRejected;
    auto rejected_refit = freeze_profiling_differential_input(
        noise, std::move(rejected_draft));
    require(!rejected_refit.accepted() &&
                rejected_refit.status ==
                    ProfilingDifferentialInputFreezeStatus::
                        RevisionAlreadyRejected,
            "a journal-rejected calibration revision froze again");
}

void require_live_revalidation_issues_once_only_receipts() {
    auto trace = stable_noise_trace();
    auto produced = produce_no_target_gtt_noise(trace);
    require(produced.accepted(),
            "receipt fixture did not produce canonical noise");
    auto input = freeze_valid_input(*produced.result, trace.bindings);
    ProfilingDifferentialAttemptState attempt(input);

    auto first_observation = observation_for(input, trace.exact_end + 1h);
    auto first = revalidate_profiling_differential_input(
        input, attempt, ProfilingDifferentialRepetitionPhase::Calibration, 0,
        first_observation);
    require(first.accepted() && first.receipt.has_value() &&
                first.receipt->accepted() &&
                first.receipt->phase() ==
                    ProfilingDifferentialRepetitionPhase::Calibration &&
                first.receipt->ordinal() == 0 &&
                first.receipt->frozen_input_sha256() ==
                    input.frozen_input_sha256() &&
                first.receipt->noise_result_checksum_sha256() ==
                    input.noise().checksum_sha256() &&
                first.receipt->previous_receipt_sha256() ==
                    input.revision().attempt_receipt_sha256 &&
                first.receipt->observation_sha256().size() == 64 &&
                first.receipt->receipt_sha256().size() == 64,
            "first live revalidation did not issue a bound receipt");

    auto second_observation = observation_for(input, trace.exact_end + 2h);
    auto second = revalidate_profiling_differential_input(
        input, attempt, ProfilingDifferentialRepetitionPhase::Calibration, 1,
        second_observation);
    require(second.accepted() && second.receipt.has_value() &&
                second.receipt->previous_receipt_sha256() ==
                    first.receipt->receipt_sha256() &&
                second.receipt->receipt_sha256() !=
                    first.receipt->receipt_sha256() &&
                attempt.receipts_issued() == 2 &&
                !attempt.revision_rejected() &&
                !attempt.noise_result_invalidated(),
            "attempt state did not advance the receipt chain once");

    auto replay = revalidate_profiling_differential_input(
        input, attempt, ProfilingDifferentialRepetitionPhase::Calibration, 1,
        second_observation);
    require(!replay.accepted() && !replay.receipt.has_value() &&
                replay.status ==
                    ProfilingDifferentialRevalidationStatus::RevisionRejected &&
                attempt.revision_rejected(),
            "one repetition received more than one authority receipt");
}

void require_invalidation_precedence_and_terminal_authority_boundary() {
    auto trace = stable_noise_trace();
    auto produced = produce_no_target_gtt_noise(trace);
    require(produced.accepted(),
            "authority fixture did not produce canonical noise");

    auto compound_input =
        freeze_valid_input(*produced.result, trace.bindings);
    ProfilingDifferentialAttemptState compound_attempt(compound_input);
    auto compound = observation_for(compound_input, trace.exact_end + 3h);
    compound.counter_reset_detected = true;
    compound.target_activity = ProfilingDifferentialTargetActivity{
        digest('0'),
        compound_input.identity().target_containment_identity_sha256};
    auto compound_result = revalidate_profiling_differential_input(
        compound_input, compound_attempt,
        ProfilingDifferentialRepetitionPhase::Calibration, 0, compound);
    require(!compound_result.accepted() &&
                compound_result.receipt.has_value() &&
                compound_result.status ==
                    ProfilingDifferentialRevalidationStatus::CounterReset &&
                compound_result.disposition ==
                    ProfilingDifferentialRevalidationDisposition::
                        InvalidateNoiseResult &&
                compound_attempt.revision_rejected() &&
                compound_attempt.noise_result_invalidated(),
            "same-observation target mismatch masked counter invalidation");

    auto malformed_target_input =
        freeze_valid_input(*produced.result, trace.bindings);
    ProfilingDifferentialAttemptState malformed_target_attempt(
        malformed_target_input);
    auto malformed_target = observation_for(
        malformed_target_input, trace.exact_end + 3h + 1min);
    malformed_target.target_activity = ProfilingDifferentialTargetActivity{
        {},
        malformed_target_input.identity()
            .target_containment_identity_sha256};
    auto malformed_target_result = revalidate_profiling_differential_input(
        malformed_target_input, malformed_target_attempt,
        ProfilingDifferentialRepetitionPhase::Calibration, 0,
        malformed_target);
    require(!malformed_target_result.accepted() &&
                !malformed_target_result.receipt.has_value() &&
                malformed_target_result.status ==
                    ProfilingDifferentialRevalidationStatus::TargetMismatch &&
                malformed_target_result.disposition ==
                    ProfilingDifferentialRevalidationDisposition::
                        RejectRevision &&
                malformed_target_attempt.revision_rejected() &&
                !malformed_target_attempt.noise_result_invalidated(),
            "malformed target identity was reported as noise-binding drift");

    auto malformed_compound_input =
        freeze_valid_input(*produced.result, trace.bindings);
    ProfilingDifferentialAttemptState malformed_compound_attempt(
        malformed_compound_input);
    auto malformed_compound = observation_for(
        malformed_compound_input, trace.exact_end + 3h + 2min);
    malformed_compound.counter_reset_detected = true;
    malformed_compound.target_activity =
        ProfilingDifferentialTargetActivity{
            {},
            malformed_compound_input.identity()
                .target_containment_identity_sha256};
    auto malformed_compound_result =
        revalidate_profiling_differential_input(
            malformed_compound_input, malformed_compound_attempt,
            ProfilingDifferentialRepetitionPhase::Calibration, 0,
            malformed_compound);
    require(!malformed_compound_result.accepted() &&
                !malformed_compound_result.receipt.has_value() &&
                malformed_compound_result.status ==
                    ProfilingDifferentialRevalidationStatus::CounterReset &&
                malformed_compound_result.disposition ==
                    ProfilingDifferentialRevalidationDisposition::
                        InvalidateNoiseResult &&
                malformed_compound_attempt.revision_rejected() &&
                malformed_compound_attempt.noise_result_invalidated(),
            "malformed target identity masked same-observation invalidation");

    auto rejected_input =
        freeze_valid_input(*produced.result, trace.bindings);
    ProfilingDifferentialAttemptState rejected_attempt(rejected_input);
    auto mismatch = observation_for(rejected_input, trace.exact_end + 4h);
    mismatch.target_activity = ProfilingDifferentialTargetActivity{
        digest('0'),
        rejected_input.identity().target_containment_identity_sha256};
    auto rejected = revalidate_profiling_differential_input(
        rejected_input, rejected_attempt,
        ProfilingDifferentialRepetitionPhase::Calibration, 0, mismatch);
    require(!rejected.accepted() && rejected.receipt.has_value() &&
                rejected.disposition ==
                    ProfilingDifferentialRevalidationDisposition::
                        RejectRevision &&
                !rejected_attempt.noise_result_invalidated(),
            "target mismatch did not terminally reject its revision");

    auto later_reset = observation_for(rejected_input, trace.exact_end + 5h);
    later_reset.counter_reset_detected = true;
    auto after_terminal = revalidate_profiling_differential_input(
        rejected_input, rejected_attempt,
        ProfilingDifferentialRepetitionPhase::Calibration, 1, later_reset);
    require(!after_terminal.accepted() &&
                !after_terminal.receipt.has_value() &&
                after_terminal.status ==
                    ProfilingDifferentialRevalidationStatus::RevisionRejected &&
                after_terminal.disposition ==
                    ProfilingDifferentialRevalidationDisposition::
                        RejectRevision &&
                !rejected_attempt.noise_result_invalidated(),
            "bytes after terminal target rejection gained authority");
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

    auto missing_method_binding = input_draft(trace.bindings, noise);
    missing_method_binding.method_binding.constraint_revision_sha256.clear();
    require_freeze_rejected(
        noise, std::move(missing_method_binding),
        ProfilingDifferentialInputFreezeStatus::InvalidMethodBinding,
        "an incomplete differential method binding was accepted");

    auto mismatched_identity = input_draft(trace.bindings, noise);
    mismatched_identity.identity.transaction.selector
        .device_identity_sha256 = digest('a');
    mismatched_identity.identity.transaction.selector_sha256 =
        canonical_selector_sha256(mismatched_identity.identity.transaction);
    require_freeze_rejected(
        noise, std::move(mismatched_identity),
        ProfilingDifferentialInputFreezeStatus::InvalidIdentity,
        "a target transaction bound to another device was accepted");

    auto mismatched_counter_epoch = input_draft(trace.bindings, noise);
    mismatched_counter_epoch.identity.counter_continuity_epoch_sha256 =
        digest('a');
    require_freeze_rejected(
        noise, std::move(mismatched_counter_epoch),
        ProfilingDifferentialInputFreezeStatus::InvalidIdentity,
        "a differential input from another counter epoch was accepted");

    auto unrelated_constraint = input_draft(trace.bindings, noise);
    unrelated_constraint.identity.transaction.selector.catalog_selector
        .constraints = {ConstraintKind::ModelTypePool};
    unrelated_constraint.identity.transaction.selector_sha256 =
        canonical_selector_sha256(
            unrelated_constraint.identity.transaction);
    require_freeze_rejected(
        noise, std::move(unrelated_constraint),
        ProfilingDifferentialInputFreezeStatus::InvalidIdentity,
        "a selector without the shared-GTT constraint was accepted");

    auto invalid_closed_value = input_draft(trace.bindings, noise);
    invalid_closed_value.identity.transaction.selector.catalog_selector
        .operation_kind = static_cast<OperationKind>(255);
    require_freeze_rejected(
        noise, std::move(invalid_closed_value),
        ProfilingDifferentialInputFreezeStatus::InvalidIdentity,
        "a selector with an invalid closed operation was accepted");

    auto stale_selector_digest = input_draft(trace.bindings, noise);
    stale_selector_digest.identity.transaction.selector
        .canonical_model_id = "model-b";
    require_freeze_rejected(
        noise, std::move(stale_selector_digest),
        ProfilingDifferentialInputFreezeStatus::InvalidIdentity,
        "selector content did not match its canonical digest");

    auto reset_input = freeze_valid_input(noise, trace.bindings);
    auto reset = observation_for(reset_input, trace.exact_end + 1h);
    reset.counter_reset_detected = true;
    require_revalidation_rejected(
        reset_input, std::move(reset),
        ProfilingDifferentialRevalidationStatus::CounterReset,
        ProfilingDifferentialRevalidationDisposition::InvalidateNoiseResult,
        "a global GTT counter reset did not invalidate the input");

    auto discontinuity_input = freeze_valid_input(noise, trace.bindings);
    auto discontinuity =
        observation_for(discontinuity_input, trace.exact_end + 2h);
    discontinuity.counter_discontinuity_detected = true;
    require_revalidation_rejected(
        discontinuity_input, std::move(discontinuity),
        ProfilingDifferentialRevalidationStatus::CounterDiscontinuity,
        ProfilingDifferentialRevalidationDisposition::InvalidateNoiseResult,
        "a global GTT counter discontinuity did not invalidate the input");

    auto reboot_input = freeze_valid_input(noise, trace.bindings);
    auto reboot = observation_for(reboot_input, trace.exact_end + 3h);
    reboot.observed_bindings.boot_id_sha256 = digest('a');
    require_revalidation_rejected(
        reboot_input, std::move(reboot),
        ProfilingDifferentialRevalidationStatus::BindingMismatch,
        ProfilingDifferentialRevalidationDisposition::InvalidateNoiseResult,
        "a changed boot identity did not invalidate the input");

    auto background_input = freeze_valid_input(noise, trace.bindings);
    auto background =
        observation_for(background_input, trace.exact_end + 4h);
    background.observed_bindings.background_inventory_sha256 =
        digest('a');
    require_revalidation_rejected(
        background_input, std::move(background),
        ProfilingDifferentialRevalidationStatus::BackgroundDrift,
        ProfilingDifferentialRevalidationDisposition::InvalidateNoiseResult,
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
        ProfilingDifferentialRevalidationDisposition::InvalidateNoiseResult,
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
        ProfilingDifferentialRevalidationDisposition::InvalidateNoiseResult,
        "a changed immutable noise-result checksum did not invalidate the "
        "input");

    auto repeated_input = freeze_valid_input(noise, trace.bindings);
    ProfilingDifferentialAttemptState repeated_attempt(repeated_input);
    auto repeated_observation =
        observation_for(repeated_input, trace.exact_end + 7h);
    auto first_revalidation = revalidate_profiling_differential_input(
        repeated_input, repeated_attempt,
        ProfilingDifferentialRepetitionPhase::Calibration, 0,
        repeated_observation);
    require(first_revalidation.accepted(),
            "the first fresh revalidation observation was rejected");
    auto repeated_rejection = revalidate_profiling_differential_input(
        repeated_input, repeated_attempt,
        ProfilingDifferentialRepetitionPhase::Calibration, 1,
        repeated_observation);
    require(!repeated_rejection.accepted() &&
                repeated_rejection.status ==
                    ProfilingDifferentialRevalidationStatus::
                        NonIncreasingObservation &&
                repeated_rejection.disposition ==
                    ProfilingDifferentialRevalidationDisposition::
                        RejectRevision &&
                repeated_attempt.revision_rejected(),
            "a repeated revalidation observation was accepted");

    auto decreasing_input = freeze_valid_input(noise, trace.bindings);
    ProfilingDifferentialAttemptState decreasing_attempt(decreasing_input);
    auto newer_observation =
        observation_for(decreasing_input, trace.exact_end + 8h);
    auto newer_revalidation = revalidate_profiling_differential_input(
        decreasing_input, decreasing_attempt,
        ProfilingDifferentialRepetitionPhase::Calibration, 0,
        newer_observation);
    require(newer_revalidation.accepted(),
            "the first ordered revalidation observation was rejected");
    auto older_observation =
        observation_for(decreasing_input, trace.exact_end + 7h);
    auto older_rejection = revalidate_profiling_differential_input(
        decreasing_input, decreasing_attempt,
        ProfilingDifferentialRepetitionPhase::Calibration, 1,
        older_observation);
    require(!older_rejection.accepted() &&
                older_rejection.status ==
                    ProfilingDifferentialRevalidationStatus::
                        NonIncreasingObservation &&
                older_rejection.disposition ==
                    ProfilingDifferentialRevalidationDisposition::
                        RejectRevision &&
                decreasing_attempt.revision_rejected(),
            "an older revalidation observation was accepted");
}

void require_noise_validity_across_calibration_revisions() {
    auto trace = stable_noise_trace();
    auto produced = produce_no_target_gtt_noise(trace);
    require(produced.accepted(),
            "noise-validity fixture did not produce canonical noise");
    const auto &noise = *produced.result;

    auto reset_input = freeze_valid_input(noise, trace.bindings);
    ProfilingDifferentialAttemptState reset_attempt(reset_input);
    auto reset = observation_for(reset_input, trace.exact_end + 9h);
    reset.counter_reset_detected = true;
    auto reset_result = revalidate_profiling_differential_input(
        reset_input, reset_attempt,
        ProfilingDifferentialRepetitionPhase::Calibration, 0, reset);
    require(!reset_result.accepted() &&
                reset_result.disposition ==
                    ProfilingDifferentialRevalidationDisposition::
                        InvalidateNoiseResult,
            "counter reset did not require fresh noise");

    auto after_reset = input_draft(trace.bindings, noise);
    after_reset.revision.calibration_revision_sha256 = digest('a');
    after_reset.revision.attempt_receipt_sha256 = digest('0');
    after_reset.identity.counter_continuity_epoch_sha256 = digest('a');
    after_reset.noise_validity.state =
        ProfilingNoiseValidityState::Invalidated;
    require_freeze_rejected(
        noise, std::move(after_reset),
        ProfilingDifferentialInputFreezeStatus::NoiseResultInvalidated,
        "a fresh calibration revision revived reset-invalidated noise");

    auto variation_input = freeze_valid_input(noise, trace.bindings);
    ProfilingDifferentialAttemptState variation_attempt(variation_input);
    auto variation =
        observation_for(variation_input, trace.exact_end + 10h);
    variation.observed_non_target_gtt_range_bytes =
        noise.n_gtt_bytes() + 1;
    auto variation_result = revalidate_profiling_differential_input(
        variation_input, variation_attempt,
        ProfilingDifferentialRepetitionPhase::Calibration, 0, variation);
    require(!variation_result.accepted() &&
                variation_result.disposition ==
                    ProfilingDifferentialRevalidationDisposition::
                        InvalidateNoiseResult,
            "background range breach did not require fresh noise");

    auto after_variation = input_draft(trace.bindings, noise);
    after_variation.revision.calibration_revision_sha256 = digest('d');
    after_variation.revision.attempt_receipt_sha256 = digest('e');
    after_variation.noise_validity.state =
        ProfilingNoiseValidityState::Invalidated;
    require_freeze_rejected(
        noise, std::move(after_variation),
        ProfilingDifferentialInputFreezeStatus::NoiseResultInvalidated,
        "a fresh calibration revision revived range-invalidated noise");

    auto wrong_validity_key = input_draft(trace.bindings, noise);
    wrong_validity_key.noise_validity.noise_result_checksum_sha256 =
        digest('0');
    require_freeze_rejected(
        noise, std::move(wrong_validity_key),
        ProfilingDifferentialInputFreezeStatus::NoiseValidityMismatch,
        "noise validity for another result was accepted");

    auto replacement_trace = stable_noise_trace();
    replacement_trace.bindings.counter_continuity_epoch_sha256 = digest('a');
    for (auto &reading : replacement_trace.readings) {
        reading.observed_bindings = replacement_trace.bindings;
    }
    auto replacement = produce_no_target_gtt_noise(replacement_trace);
    require(replacement.accepted() &&
                replacement.result->checksum_sha256() !=
                    noise.checksum_sha256(),
            "fresh-trace replacement did not bind the new counter epoch");
    auto replacement_frozen = freeze_profiling_differential_input(
        *replacement.result,
        input_draft(replacement_trace.bindings, *replacement.result));
    require(replacement_frozen.accepted(),
            "fresh noise from the new counter epoch did not freeze");

    auto uninterrupted = freeze_valid_input(noise, trace.bindings);
    ProfilingDifferentialAttemptState uninterrupted_attempt(uninterrupted);
    auto matching = observation_for(uninterrupted, trace.exact_end + 11h);
    auto matching_result = revalidate_profiling_differential_input(
        uninterrupted, uninterrupted_attempt,
        ProfilingDifferentialRepetitionPhase::Calibration, 0, matching);
    require(matching_result.accepted(),
            "matching same-boot observation invalidated noise");
    auto later_revision = input_draft(trace.bindings, noise);
    later_revision.revision.calibration_revision_sha256 = digest('1');
    later_revision.revision.attempt_receipt_sha256 = digest('2');
    auto reused = freeze_profiling_differential_input(
        noise, std::move(later_revision));
    require(reused.accepted(),
            "uninterrupted same-boot noise did not serve a later revision");
}

} // namespace

int main() {
    try {
        require_parser_rejects_bound_below_uncertainty();
        require_input_freezes_and_revalidates_before_repetitions();
        require_live_revalidation_issues_once_only_receipts();
        require_invalidation_precedence_and_terminal_authority_boundary();
        require_consumer_failure_contract();
        require_noise_validity_across_calibration_revisions();
        std::cout << "PASS: residency profiling differential input tests\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << "\n";
        return 1;
    }
}
