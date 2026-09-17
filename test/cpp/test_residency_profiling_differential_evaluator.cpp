#include "lemon/residency/profiling_capture_authority.h"
#include "lemon/residency/profiling_differential_evaluator.h"

#include <mbedtls/md.h>
#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace lemon::residency;
using namespace std::chrono_literals;

constexpr std::string_view no_target_procedure_sha256 =
    "53154e34cf8387b0f7805accade0e431fec603470b48db5324d963b4b8b659ed";

using IntervalBeginSignature = ProfilingRawIntervalBeginResult (
    ProfilingIntervalObservationSource::*)(
    const ProfilingRawIntervalReadRequest &,
    const ProfilingCancellationCheck &);
using IntervalReadSignature = ProfilingRawIntervalBatch (
    ProfilingIntervalObservationSource::*)(
    ProfilingRawIntervalToken,
    ProfilingEventWatermark,
    const ProfilingCancellationCheck &);
using IntervalFinishSignature = ProfilingRawIntervalBatch (
    ProfilingIntervalObservationSource::*)(
    ProfilingRawIntervalToken,
    ProfilingEventWatermark) noexcept;

static_assert(std::is_same_v<
              decltype(&ProfilingIntervalObservationSource::begin),
              IntervalBeginSignature>);
static_assert(std::is_same_v<
              decltype(&ProfilingIntervalObservationSource::read_since),
              IntervalReadSignature>);
static_assert(std::is_same_v<
              decltype(&ProfilingIntervalObservationSource::finish),
              IntervalFinishSignature>);

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

std::string domain_sha256(std::string_view domain,
                          const nlohmann::json &payload) {
    std::string bytes(domain);
    bytes += payload.dump();
    return raw_sha256(bytes);
}

std::string checksummed_component(nlohmann::json document) {
    document.erase("checksum_sha256");
    constexpr char domain[] =
        "lemonade.residency.profiling-differential-evidence/v1\0";
    document["checksum_sha256"] = domain_sha256(
        std::string_view(domain, sizeof(domain) - 1), document);
    return document.dump();
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
    bindings.counter_continuity_epoch_sha256 = digest('7');
    bindings.campaign_contract_sha256 = digest('8');
    bindings.procedure_revision_sha256 = no_target_procedure_sha256;
    bindings.background_inventory_sha256 = digest('9');
    return bindings;
}

ParsedProfilingNoiseResult parsed_noise_result(
    ProfilingNoiseBindings bindings = noise_bindings()) {
    ProfilingNoTargetGttTrace trace;
    trace.started_at = std::chrono::steady_clock::time_point{1h};
    trace.exact_end = trace.started_at + profiling_noise_trace_duration;
    trace.bindings = std::move(bindings);
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

    auto produced = produce_no_target_gtt_noise(trace);
    require(produced.accepted(),
            "fixture did not produce no-target GTT evidence");
    auto parsed =
        parse_profiling_noise_result(produced.result->canonical_bytes());
    require(parsed.accepted(),
            "fixture did not parse no-target GTT evidence");
    return std::move(*parsed.result);
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
    auto canonical =
        canonicalize_local_overlay_selector(context.selector);
    require(canonical.accepted(), "fixture selector was not canonical");
    context.selector = std::move(*canonical.selector);
    context.selector_sha256 = canonical.selector_sha256;
    context.generations.model = 1;
    context.generations.backend = 2;
    context.generations.device = 3;
    context.generations.topology = 4;
    context.generations.driver = 5;
    context.generations.configuration = 6;
    context.generations.workload = 7;
    context.observation_contract_sha256 = digest('1');
    context.predictor_contract_sha256 = digest('2');
    context.ownership_recovery_evidence_sha256 = digest('3');
    context.action_lease_closure_sha256 = digest('4');
    return context;
}

ProfilingDifferentialInputDraft differential_input_draft(
    const ParsedProfilingNoiseResult &noise,
    std::uint32_t calibration_repetitions = 2,
    std::uint32_t validation_repetitions = 1,
    std::uint64_t x_gtt_bytes = 16,
    std::uint64_t m_gtt_bytes = 32) {
    ProfilingDifferentialInputDraft draft;
    draft.identity.transaction = transaction_context(noise.bindings());
    draft.identity.target_client_identity_sha256 = digest('5');
    draft.identity.target_containment_identity_sha256 = digest('6');
    draft.identity.counter_continuity_epoch_sha256 =
        noise.bindings().counter_continuity_epoch_sha256;
    draft.identity.safety_contract_sha256 = digest('7');
    draft.identity.noise_trace_provenance_sha256 =
        std::string(noise.trace_provenance_sha256());
    draft.noise_validity.noise_result_checksum_sha256 =
        std::string(noise.checksum_sha256());
    draft.noise_validity.state = ProfilingNoiseValidityState::Valid;
    draft.revision.calibration_revision_sha256 = digest('8');
    draft.revision.attempt_receipt_sha256 = digest('9');
    draft.revision.state = ProfilingDifferentialRevisionState::Fresh;
    draft.accounting.x_gtt_bytes = x_gtt_bytes;
    draft.accounting.m_gtt_bytes = m_gtt_bytes;
    draft.accounting.x_gtt_evidence_sha256 = digest('a');
    draft.accounting.m_gtt_policy_sha256 = digest('b');
    draft.accounting.partition_contract_sha256 = digest('c');
    draft.calibration_repetitions = calibration_repetitions;
    draft.validation_repetitions = validation_repetitions;
    return draft;
}

FrozenProfilingDifferentialInput frozen_input(
    const ParsedProfilingNoiseResult &noise,
    std::uint32_t calibration_repetitions = 2,
    std::uint32_t validation_repetitions = 1,
    std::uint64_t x_gtt_bytes = 16,
    std::uint64_t m_gtt_bytes = 32) {
    auto draft = differential_input_draft(
        noise, calibration_repetitions, validation_repetitions,
        x_gtt_bytes, m_gtt_bytes);
    auto binding = resolve_retained_gtt_differential_method_binding(
        draft.identity.transaction, "amd.shared_gtt.retained_bytes");
    require(binding.has_value(),
            "fixture method and constraint binding did not resolve");
    auto preflight =
        preflight_retained_gtt_differential(noise, draft, *binding);
    require(preflight.accepted(), "fixture evaluator preflight failed");

    auto frozen =
        freeze_profiling_differential_input(noise, std::move(draft));
    require(frozen.accepted(), "fixture input did not freeze");
    return std::move(*frozen.input);
}

ProfilingDifferentialMethodBinding method_binding() {
    auto binding = resolve_retained_gtt_differential_method_binding(
        transaction_context(noise_bindings()),
        "amd.shared_gtt.retained_bytes");
    require(binding.has_value(),
            "fixture method and constraint binding did not resolve");
    return std::move(*binding);
}

ProfilingDifferentialPhaseMarker phase_marker(
    ProfilingDifferentialMarkerKind kind,
    std::chrono::steady_clock::time_point marked_at,
    const FrozenProfilingDifferentialInput &input) {
    ProfilingDifferentialPhaseMarker marker;
    marker.kind = kind;
    marker.marked_at = marked_at;
    marker.ready = true;
    marker.frozen_input_sha256 = input.frozen_input_sha256();
    marker.selector_sha256 = input.identity().transaction.selector_sha256;
    marker.target_client_identity_sha256 =
        input.identity().target_client_identity_sha256;
    marker.target_containment_identity_sha256 =
        input.identity().target_containment_identity_sha256;
    marker.provenance_sha256 = digest('f');
    return marker;
}

std::vector<ProfilingDifferentialGttPoint> plateau_points(
    std::chrono::steady_clock::time_point started_at,
    std::uint64_t minimum,
    const FrozenProfilingDifferentialInput &input,
    std::size_t point_count = 100,
    std::chrono::milliseconds cadence = 50ms) {
    std::vector<ProfilingDifferentialGttPoint> points;
    for (std::uint64_t index = 0; index < point_count; ++index) {
        ProfilingDifferentialGttPoint point;
        point.scheduled_at = started_at + index * cadence;
        point.read_started_at = point.scheduled_at;
        point.read_finished_at = point.read_started_at + 1ms;
        point.global_gtt_used_bytes = minimum + (index % 2) * 64;
        point.observed_bindings = input.noise().bindings();
        point.frozen_input_sha256 = input.frozen_input_sha256();
        point.selector_sha256 = input.identity().transaction.selector_sha256;
        point.target_client_identity_sha256 =
            input.identity().target_client_identity_sha256;
        point.target_containment_identity_sha256 =
            input.identity().target_containment_identity_sha256;
        point.provenance_sha256 = digest('a');
        point.owner_projection_status =
            ProfilingDifferentialOwnerProjectionStatus::Absent;
        points.push_back(std::move(point));
    }
    return points;
}

ProfilingDifferentialRepetition repetition(
    ProfilingDifferentialRepetitionPhase phase,
    std::uint32_t ordinal,
    std::chrono::steady_clock::time_point started_at,
    std::uint64_t loaded_minimum,
    const FrozenProfilingDifferentialInput &input) {
    ProfilingDifferentialRepetition result;
    result.phase = phase;
    result.ordinal = ordinal;
    result.revalidation.checked_at = started_at - 1ms;
    result.revalidation.observed_bindings = input.noise().bindings();
    result.revalidation.observed_noise_result_checksum_sha256 =
        std::string(input.noise().checksum_sha256());
    result.revalidation.observed_counter_continuity_epoch_sha256 =
        input.identity().counter_continuity_epoch_sha256;
    result.revalidation.observed_non_target_gtt_range_bytes =
        input.noise().n_gtt_bytes();

    result.baseline.marker = phase_marker(
        ProfilingDifferentialMarkerKind::BaselineReady, started_at, input);
    result.baseline.points = plateau_points(started_at, 8192, input);
    result.loaded.marker = phase_marker(
        ProfilingDifferentialMarkerKind::LoadedReady, started_at + 6s, input);
    result.loaded.points =
        plateau_points(started_at + 6s, loaded_minimum, input);
    result.release.marker = phase_marker(
        ProfilingDifferentialMarkerKind::ReleaseReady, started_at + 12s,
        input);
    result.release.points = plateau_points(started_at + 12s, 8192, input);
    return result;
}

void require_stable_nonzero_background_succeeds() {
    auto noise = parsed_noise_result();
    auto input = frozen_input(noise);
    const auto first = std::chrono::steady_clock::time_point{20h};
    std::vector<ProfilingDifferentialRepetition> repetitions;
    repetitions.push_back(repetition(
        ProfilingDifferentialRepetitionPhase::Calibration, 0, first, 9200,
        input));
    repetitions.push_back(repetition(
        ProfilingDifferentialRepetitionPhase::Calibration, 1, first + 20s,
        9300, input));
    repetitions.push_back(repetition(
        ProfilingDifferentialRepetitionPhase::Validation, 0, first + 40s,
        9420, input));

    const auto frozen_digest = std::string(input.frozen_input_sha256());
    auto evaluated = evaluate_retained_gtt_differential(
        std::move(input), method_binding(), std::move(repetitions));

    require(evaluated.accepted() && evaluated.evidence.has_value(),
            "stable nonzero background was rejected");
    const auto &evidence = *evaluated.evidence;
    require(evidence.frozen_input_sha256() == frozen_digest &&
                evidence.retained_gtt_bound_bytes() == 1292 &&
                evidence.calibration_repetitions().size() == 2 &&
                evidence.validation_repetitions().size() == 1 &&
                evidence.calibration_repetitions().at(0).delta_bytes == 1072 &&
                evidence.calibration_repetitions().at(1).delta_bytes == 1172 &&
                evidence.validation_repetitions().at(0).delta_bytes == 1292 &&
                evidence.owner_projection_coverage() ==
                    ProfilingDifferentialOwnerProjectionCoverage::Absent &&
                !evidence.canonical_bytes().empty() &&
                evidence.checksum_sha256().size() == 64,
            "accepted component evidence changed the worked retained bound");
}

void require_fixed_window_boundaries_and_cadence() {
    auto noise = parsed_noise_result();
    const auto first = std::chrono::steady_clock::time_point{30h};

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first,
            9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0, first + 20s,
            9200, input));
        for (auto &record : repetitions) {
            auto before_marker = record.baseline.points.front();
            before_marker.scheduled_at -= 50ms;
            before_marker.read_started_at -= 50ms;
            before_marker.read_finished_at -= 50ms;
            before_marker.global_gtt_used_bytes = 1;
            record.baseline.points.insert(record.baseline.points.begin(),
                                          std::move(before_marker));
            auto at_end = record.baseline.points.back();
            at_end.scheduled_at = record.baseline.points.at(1).scheduled_at + 5s;
            at_end.read_started_at = at_end.scheduled_at;
            at_end.read_finished_at = at_end.read_started_at + 1ms;
            at_end.global_gtt_used_bytes = 1;
            record.baseline.points.push_back(std::move(at_end));
        }
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(result.accepted() &&
                    result.evidence->calibration_repetitions().at(0)
                            .delta_bytes == 1072,
                "marker boundary or exact-end exclusion changed the window");
    }

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first + 1h,
            9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0,
            first + 1h + 20s, 9200, input));
        repetitions.front().baseline.points.resize(49);
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.status ==
                        ProfilingDifferentialEvaluationStatus::InvalidWindow,
                "a fixed window with only 49 points was accepted");
    }

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first + 2h,
            9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0,
            first + 2h + 20s, 9200, input));
        for (auto &record : repetitions) {
            record.baseline.points = plateau_points(
                record.baseline.marker->marked_at, 8192, input, 50, 100ms);
            record.loaded.points = plateau_points(
                record.loaded.marker->marked_at, 9200, input, 50, 100ms);
            record.release.points = plateau_points(
                record.release.marker->marked_at, 8192, input, 50, 100ms);
        }
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(result.accepted(),
                "the inclusive 100 ms read-start gap boundary was rejected");
    }

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first + 3h,
            9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0,
            first + 3h + 20s, 9200, input));
        repetitions.front().baseline.points = plateau_points(
            repetitions.front().baseline.marker->marked_at, 8192, input, 50,
            101ms);
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.status ==
                        ProfilingDifferentialEvaluationStatus::InvalidWindow,
                "a 101 ms read-start gap was accepted");
    }
}

void require_first_window_and_disjoint_repetition_order() {
    auto noise = parsed_noise_result();
    const auto first = std::chrono::steady_clock::time_point{40h};

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first,
            9200, input));
        auto later_quiet = plateau_points(first + 5s, 8192, input);
        repetitions.front().baseline.points.at(10).global_gtt_used_bytes =
            8300;
        repetitions.front().baseline.points.insert(
            repetitions.front().baseline.points.end(),
            later_quiet.begin(), later_quiet.end());
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0, first + 20s,
            9200, input));
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.status ==
                        ProfilingDifferentialEvaluationStatus::UnstablePlateau,
                "a quieter later window replaced the first fixed window");
    }

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first + 1h,
            9200, input));
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.status == ProfilingDifferentialEvaluationStatus::
                                         InvalidRepetitionCount,
                "fewer than the frozen repetition count was accepted");
    }

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0, first + 2h,
            9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0,
            first + 2h + 20s, 9200, input));
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.status == ProfilingDifferentialEvaluationStatus::
                                         InvalidRepetitionOrder,
                "validation was accepted before calibration");
    }

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first + 3h,
            9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0,
            first + 3h + 1ms, 9200, input));
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.status == ProfilingDifferentialEvaluationStatus::
                                         InvalidRepetitionOrder,
                "overlapping calibration and validation repetitions were accepted");
    }

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first + 4h,
            9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0,
            first + 4h + 20s, 9200, input));
        auto trailing_release = repetitions.front().release.points.back();
        trailing_release.scheduled_at =
            repetitions.back().baseline.marker->marked_at;
        trailing_release.read_started_at = trailing_release.scheduled_at;
        trailing_release.read_finished_at =
            trailing_release.read_started_at + 1ms;
        repetitions.front().release.points.push_back(
            std::move(trailing_release));
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.status == ProfilingDifferentialEvaluationStatus::
                                         InvalidRepetitionOrder,
                "a trailing release read overlapped the next repetition");
    }
}

void require_canonical_component_evidence_round_trips() {
    auto noise = parsed_noise_result();
    auto input = frozen_input(noise);
    const auto first = std::chrono::steady_clock::time_point{50h};
    std::vector<ProfilingDifferentialRepetition> repetitions;
    repetitions.push_back(repetition(
        ProfilingDifferentialRepetitionPhase::Calibration, 0, first, 9200,
        input));
    repetitions.push_back(repetition(
        ProfilingDifferentialRepetitionPhase::Calibration, 1, first + 20s,
        9300, input));
    repetitions.push_back(repetition(
        ProfilingDifferentialRepetitionPhase::Validation, 0, first + 40s,
        9420, input));
    auto evaluated = evaluate_retained_gtt_differential(
        std::move(input), method_binding(), std::move(repetitions));
    require(evaluated.accepted(),
            "canonical fixture did not produce component evidence");

    const auto canonical =
        std::string(evaluated.evidence->canonical_bytes());
    const auto document = nlohmann::json::parse(canonical);
    require(document.at("method_binding").at("method_id") ==
                    "differential_retained_gtt" &&
                document.at("covered_effect") == "retained_gtt" &&
                document.at("accounting_terms").at("n_gtt_bytes") == 72 &&
                document.at("accounting_terms").at("x_gtt_bytes") == 16 &&
                document.at("accounting_terms").at("m_gtt_bytes") == 32 &&
                document.at("retained_gtt_bound_bytes") == 1292 &&
                document.contains("retained_gtt_claim") &&
                document.at("retained_gtt_claim").at("amount") == 1292 &&
                document.at("retained_gtt_claim").at("constraint_id") ==
                    "amd.shared_gtt.retained_bytes" &&
                document.at("retained_gtt_claim").at("unit") == "bytes" &&
                document.at("calibration_repetitions").size() == 2 &&
                document.at("validation_repetitions").size() == 1 &&
                document.contains("exact_fingerprint") &&
                document.contains("frozen_identities") &&
                document.at("frozen_identities").at("deployment_id") ==
                    digest('f') &&
                document.at("frozen_identities").at("profiling_sequence") ==
                    41 &&
                document.at("frozen_identities")
                        .at("source_generations")
                        .at("workload") == 7 &&
                document.at("frozen_identities")
                        .at("counter_continuity_epoch_sha256") ==
                    digest('7') &&
                document.at("frozen_identities")
                        .at("observation_contract_sha256") ==
                    digest('1') &&
                document.at("frozen_identities")
                        .at("predictor_contract_sha256") == digest('2') &&
                document.at("frozen_identities")
                        .at("ownership_recovery_evidence_sha256") ==
                    digest('3') &&
                document.at("frozen_identities")
                        .at("action_lease_closure_sha256") == digest('4') &&
                canonical.find("transient_envelope") == std::string::npos &&
                canonical.find("external_demand_absent") ==
                    std::string::npos &&
                canonical.find("confidence") == std::string::npos &&
                canonical.find("manifest") == std::string::npos &&
                canonical.find("activation") == std::string::npos &&
                canonical.find("publication") == std::string::npos &&
                canonical.find("history") == std::string::npos &&
                canonical.find("points") == std::string::npos,
            "component serialization overstated its retained-GTT claim");

    auto parsed = parse_profiling_differential_evidence(canonical);
    require(parsed.accepted() &&
                parsed.evidence->canonical_bytes() == canonical &&
                parsed.evidence->checksum_sha256() ==
                    evaluated.evidence->checksum_sha256() &&
                parsed.evidence->retained_gtt_bound_bytes() == 1292 &&
                parsed.evidence->owner_projection_coverage() ==
                    ProfilingDifferentialOwnerProjectionCoverage::Absent,
            "canonical component evidence did not round-trip");

    auto tampered = canonical;
    const auto checksum_value = tampered.find(
        "\"checksum_sha256\":\"");
    require(checksum_value != std::string::npos,
            "canonical fixture has no checksum field");
    const auto first_checksum_character =
        checksum_value + std::string("\"checksum_sha256\":\"").size();
    tampered.at(first_checksum_character) =
        tampered.at(first_checksum_character) == '0' ? '1' : '0';
    auto rejected = parse_profiling_differential_evidence(tampered);
    require(!rejected.accepted() &&
                rejected.status ==
                    ProfilingDifferentialEvidenceParseStatus::DigestMismatch &&
                !rejected.evidence.has_value(),
            "tampered component checksum was accepted");

    auto noncanonical = parse_profiling_differential_evidence(canonical + "\n");
    require(!noncanonical.accepted() &&
                noncanonical.status ==
                    ProfilingDifferentialEvidenceParseStatus::NonCanonical,
            "noncanonical component bytes were accepted");
}

void require_parser_rejects_unreviewed_bindings() {
    auto noise = parsed_noise_result();
    auto input = frozen_input(noise, 1, 1);
    const auto first = std::chrono::steady_clock::time_point{55h};
    std::vector<ProfilingDifferentialRepetition> repetitions;
    repetitions.push_back(repetition(
        ProfilingDifferentialRepetitionPhase::Calibration, 0, first, 9200,
        input));
    repetitions.push_back(repetition(
        ProfilingDifferentialRepetitionPhase::Validation, 0, first + 20s,
        9200, input));
    auto evaluated = evaluate_retained_gtt_differential(
        std::move(input), method_binding(), std::move(repetitions));
    require(evaluated.accepted(),
            "parser binding fixture did not produce component evidence");
    const auto canonical = nlohmann::json::parse(
        evaluated.evidence->canonical_bytes().begin(),
        evaluated.evidence->canonical_bytes().end());

    for (std::size_t mismatch = 0; mismatch < 4; ++mismatch) {
        auto document = canonical;
        if (mismatch == 0) {
            document["method_binding"]["method_revision_sha256"] =
                digest('0');
        } else if (mismatch == 1) {
            document["method_binding"]["constraint_id"] =
                "amd.shared_gtt.other_bytes";
            document["retained_gtt_claim"]["constraint_id"] =
                "amd.shared_gtt.other_bytes";
        } else if (mismatch == 2) {
            document["method_binding"]["constraint_revision_sha256"] =
                digest('0');
        } else {
            auto &identities = document["frozen_identities"];
            identities["noise_bindings"]["procedure_revision_sha256"] =
                digest('0');
            constexpr char domain[] =
                "lemonade.residency.profiling-noise-bindings/v1\0";
            identities["noise_bindings_sha256"] = domain_sha256(
                std::string_view(domain, sizeof(domain) - 1),
                identities["noise_bindings"]);
        }
        auto parsed = parse_profiling_differential_evidence(
            checksummed_component(std::move(document)));
        require(!parsed.accepted() &&
                    parsed.status ==
                        ProfilingDifferentialEvidenceParseStatus::InvalidValue &&
                    !parsed.evidence.has_value(),
                "canonical evidence with an unreviewed binding was parsed");
    }
}

void require_checked_bound_validation_and_release_failures() {
    auto noise = parsed_noise_result();
    const auto first = std::chrono::steady_clock::time_point{60h};

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first,
            9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0, first + 20s,
            9200, input));
        auto after_window = repetitions.front().release.points.back();
        after_window.scheduled_at =
            repetitions.front().release.marker->marked_at + 5s;
        after_window.read_started_at = after_window.scheduled_at;
        after_window.read_finished_at = after_window.read_started_at + 1ms;
        after_window.global_gtt_used_bytes = 9000;
        repetitions.front().release.points.push_back(std::move(after_window));
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.status == ProfilingDifferentialEvaluationStatus::
                                         ReleaseEnvelopeBreach,
                "a post-release point outside the envelope was ignored");
    }

    {
        auto input = frozen_input(noise);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first + 1h,
            9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 1,
            first + 1h + 20s, 9300, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0,
            first + 1h + 40s, 9421, input));
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.status == ProfilingDifferentialEvaluationStatus::
                                         ValidationExceeded &&
                    result.disposition ==
                        ProfilingDifferentialRevalidationDisposition::
                            RejectRevision &&
                    !result.evidence.has_value(),
                "a one-byte validation exceedance refit the retained bound");
    }

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first + 2h,
            8000, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0,
            first + 2h + 20s, 9200, input));
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.status ==
                        ProfilingDifferentialEvaluationStatus::NegativeDelta,
                "a negative loaded-minus-baseline delta was accepted");
    }

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first + 3h,
            9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0,
            first + 3h + 20s, 9200, input));
        repetitions.front().release.marker.reset();
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.status ==
                        ProfilingDifferentialEvaluationStatus::MissingMarker,
                "a repetition without release completion was accepted");
    }

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first + 4h,
            9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0,
            first + 4h + 20s, 9200, input));
        repetitions.front().release.points.at(12).global_gtt_used_bytes =
            8400;
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    (result.status == ProfilingDifferentialEvaluationStatus::
                                          ReleaseEnvelopeBreach ||
                     result.status == ProfilingDifferentialEvaluationStatus::
                                          UnstablePlateau),
                "a release-envelope breach was accepted");
    }

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first + 5h,
            9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0,
            first + 5h + 20s, 9200, input));
        const auto near_maximum =
            std::numeric_limits<std::uint64_t>::max() - 64;
        for (auto &record : repetitions) {
            for (auto *plateau : {&record.baseline, &record.loaded,
                                  &record.release}) {
                for (std::size_t index = 0; index < plateau->points.size();
                     ++index) {
                    plateau->points[index].global_gtt_used_bytes =
                        near_maximum + (index % 2) * 64;
                }
            }
        }
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.status == ProfilingDifferentialEvaluationStatus::
                                         ArithmeticOverflow,
                "an overflowing release-envelope upper bound was accepted");
    }
}

void require_frozen_identity_actor_and_source_bindings() {
    auto noise = parsed_noise_result();
    const auto first = std::chrono::steady_clock::time_point{70h};

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first,
            9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0, first + 20s,
            9200, input));
        auto after_window = repetitions.front().release.points.back();
        after_window.scheduled_at =
            repetitions.front().release.marker->marked_at + 5s;
        after_window.read_started_at = after_window.scheduled_at;
        after_window.read_finished_at = after_window.read_started_at + 1ms;
        after_window.target_client_identity_sha256 = digest('0');
        repetitions.front().release.points.push_back(std::move(after_window));
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.status ==
                        ProfilingDifferentialEvaluationStatus::ActorDrift,
                "actor drift in a post-release point was ignored");
    }

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first + 1h,
            9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0,
            first + 1h + 20s, 9200, input));
        repetitions.front().baseline.points.at(5).frozen_input_sha256 =
            digest('0');
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.status ==
                        ProfilingDifferentialEvaluationStatus::IdentityDrift,
                "frozen-input drift was accepted");
    }

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first + 2h,
            9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0,
            first + 2h + 20s, 9200, input));
        repetitions.front().loaded.points.at(6)
            .target_containment_identity_sha256 = digest('0');
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.status ==
                        ProfilingDifferentialEvaluationStatus::ActorDrift,
                "target containment drift was accepted");
    }

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first + 3h,
            9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0,
            first + 3h + 20s, 9200, input));
        repetitions.front().release.points.at(7)
            .observed_bindings.counter_source_revision_sha256 = digest('0');
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.status ==
                        ProfilingDifferentialEvaluationStatus::SourceDrift,
                "global GTT source drift was accepted");
    }

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first + 4h,
            9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0,
            first + 4h + 20s, 9200, input));
        repetitions.front().loaded.marker->selector_sha256 = digest('0');
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.status ==
                        ProfilingDifferentialEvaluationStatus::InvalidMarker,
                "phase marker identity drift was accepted");
    }
}

void require_noise_invalidating_drift_preserves_disposition() {
    auto noise = parsed_noise_result();
    const auto first = std::chrono::steady_clock::time_point{75h};

    for (std::size_t phase = 0; phase < 4; ++phase) {
        auto input = frozen_input(noise, 1, 1);
        const auto noise_checksum =
            std::string(input.noise().checksum_sha256());
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0,
            first + phase * 1h, 9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0,
            first + phase * 1h + 20s, 9200, input));

        if (phase == 0) {
            repetitions.front().baseline.points.at(4)
                .observed_bindings.boot_id_sha256 = digest('0');
        } else if (phase == 1) {
            repetitions.front().loaded.points.at(4)
                .observed_bindings.background_inventory_sha256 = digest('0');
        } else if (phase == 2) {
            repetitions.front().release.points.at(4)
                .observed_bindings.counter_source_revision_sha256 =
                digest('0');
        } else {
            auto trailing = repetitions.front().release.points.back();
            trailing.scheduled_at =
                repetitions.front().release.marker->marked_at + 5s;
            trailing.read_started_at = trailing.scheduled_at;
            trailing.read_finished_at = trailing.read_started_at + 1ms;
            trailing.observed_bindings.procedure_revision_sha256 =
                digest('0');
            repetitions.front().release.points.push_back(std::move(trailing));
        }

        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.status ==
                        ProfilingDifferentialEvaluationStatus::SourceDrift &&
                    result.disposition ==
                        ProfilingDifferentialRevalidationDisposition::
                            InvalidateNoiseResult &&
                    result.noise_result_checksum_sha256 == noise_checksum &&
                    !result.evidence.has_value(),
                "noise-invalidating point drift was reduced to revision "
                "rejection");
    }
}

void require_reviewed_noise_procedure_revision() {
    auto bindings = noise_bindings();
    bindings.procedure_revision_sha256 = digest('a');
    auto noise = parsed_noise_result(std::move(bindings));
    auto draft = differential_input_draft(noise, 1, 1);
    auto frozen =
        freeze_profiling_differential_input(noise, std::move(draft));
    require(frozen.accepted(),
            "unsupported-procedure fixture did not freeze");
    auto input = std::move(*frozen.input);
    const auto first = std::chrono::steady_clock::time_point{79h};
    std::vector<ProfilingDifferentialRepetition> repetitions;
    repetitions.push_back(repetition(
        ProfilingDifferentialRepetitionPhase::Calibration, 0, first, 9200,
        input));
    repetitions.push_back(repetition(
        ProfilingDifferentialRepetitionPhase::Validation, 0, first + 20s,
        9200, input));

    auto result = evaluate_retained_gtt_differential(
        std::move(input), method_binding(), std::move(repetitions));
    require(!result.accepted() &&
                result.status == ProfilingDifferentialEvaluationStatus::
                                     InvalidMethodBinding &&
                result.disposition ==
                    ProfilingDifferentialRevalidationDisposition::
                        RejectRevision &&
                !result.noise_result_checksum_sha256.has_value() &&
                !result.evidence.has_value(),
            "a noise result from an unsupported producer procedure was "
            "accepted");
}

void require_exact_method_and_constraint_binding() {
    auto noise = parsed_noise_result();
    const auto first = std::chrono::steady_clock::time_point{79h + 30min};

    for (std::size_t mismatch = 0; mismatch < 3; ++mismatch) {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0,
            first + mismatch * 1h, 9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0,
            first + mismatch * 1h + 20s, 9200, input));
        auto binding = method_binding();
        if (mismatch == 0) {
            binding.method_revision_sha256 = digest('0');
        } else if (mismatch == 1) {
            binding.constraint_id = "amd.shared_gtt.other_bytes";
        } else {
            binding.constraint_revision_sha256 = digest('0');
        }

        auto result = evaluate_retained_gtt_differential(
            std::move(input), std::move(binding), std::move(repetitions));
        require(!result.accepted() &&
                    result.status == ProfilingDifferentialEvaluationStatus::
                                         InvalidMethodBinding &&
                    result.disposition ==
                        ProfilingDifferentialRevalidationDisposition::
                            RejectRevision &&
                    !result.evidence.has_value(),
                "an unreviewed method or constraint binding was accepted");
    }
}

void require_explicit_owner_projection_coverage() {
    auto noise = parsed_noise_result();
    const auto first = std::chrono::steady_clock::time_point{80h};

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first,
            9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0, first + 20s,
            9200, input));
        auto &point = repetitions.front().baseline.points.at(3);
        point.owner_projection_status =
            static_cast<ProfilingDifferentialOwnerProjectionStatus>(255);
        point.owner_gtt_used_bytes = 0;
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.status == ProfilingDifferentialEvaluationStatus::
                                         ContradictoryOwnerProjection,
                "an unknown owner-projection status was accepted");
    }

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first + 1h,
            9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0,
            first + 1h + 20s, 9200, input));
        for (auto &record : repetitions) {
            for (auto *plateau : {&record.baseline, &record.loaded,
                                  &record.release}) {
                for (auto &point : plateau->points) {
                    point.owner_projection_status =
                        ProfilingDifferentialOwnerProjectionStatus::Complete;
                    point.owner_gtt_used_bytes =
                        *point.global_gtt_used_bytes;
                }
            }
        }
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(result.accepted() &&
                    result.evidence->owner_projection_coverage() ==
                        ProfilingDifferentialOwnerProjectionCoverage::Complete,
                "complete owner projection was not retained as complete");
    }

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first + 2h,
            9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0,
            first + 2h + 20s, 9200, input));
        repetitions.front().loaded.points.at(4).owner_projection_status =
            ProfilingDifferentialOwnerProjectionStatus::Contradictory;
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.status == ProfilingDifferentialEvaluationStatus::
                                         ContradictoryOwnerProjection,
                "contradictory owner projection was accepted");
    }

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first + 3h,
            9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0,
            first + 3h + 20s, 9200, input));
        repetitions.front().baseline.points.at(5).owner_projection_status =
            ProfilingDifferentialOwnerProjectionStatus::SharedBuffer;
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.status == ProfilingDifferentialEvaluationStatus::
                                         SharedBufferEvidence,
                "shared-buffer owner evidence was accepted");
    }
}

void require_complete_ordered_point_schedule() {
    auto noise = parsed_noise_result();
    auto input = frozen_input(noise, 1, 1);
    const auto first = std::chrono::steady_clock::time_point{90h};
    std::vector<ProfilingDifferentialRepetition> repetitions;
    repetitions.push_back(repetition(
        ProfilingDifferentialRepetitionPhase::Calibration, 0, first, 9200,
        input));
    repetitions.push_back(repetition(
        ProfilingDifferentialRepetitionPhase::Validation, 0, first + 20s,
        9200, input));

    auto boundary = repetitions.front().baseline.points.back();
    boundary.scheduled_at =
        repetitions.front().baseline.marker->marked_at + 5s;
    boundary.read_started_at = boundary.scheduled_at;
    boundary.read_finished_at = boundary.read_started_at + 1ms;
    repetitions.front().baseline.points.push_back(std::move(boundary));
    auto hidden = repetitions.front().baseline.points.at(50);
    hidden.global_gtt_used_bytes = 9000;
    repetitions.front().baseline.points.push_back(std::move(hidden));

    auto result = evaluate_retained_gtt_differential(
        std::move(input), method_binding(), std::move(repetitions));
    require(!result.accepted() &&
                result.status ==
                    ProfilingDifferentialEvaluationStatus::InvalidWindow,
            "an out-of-order point hid behind the fixed-end boundary");
}

void require_fresh_revalidation_and_preserved_dispositions() {
    auto noise = parsed_noise_result();
    const auto first = std::chrono::steady_clock::time_point{100h};

    {
        auto input = frozen_input(noise, 1, 1);
        const auto noise_checksum =
            std::string(input.noise().checksum_sha256());
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first,
            9200, input));
        repetitions.front().revalidation.counter_reset_detected = true;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first + 20s,
            9200, input));
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.status == ProfilingDifferentialEvaluationStatus::
                                         RevalidationRejected &&
                    result.revalidation_status ==
                        ProfilingDifferentialRevalidationStatus::CounterReset &&
                    result.disposition ==
                        ProfilingDifferentialRevalidationDisposition::
                            InvalidateNoiseResult &&
                    result.noise_result_checksum_sha256 == noise_checksum,
                "counter reset invalidation was reduced to revision rejection");
    }

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first + 1h,
            9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0,
            first + 1h + 20s, 9200, input));
        repetitions.back().revalidation.checked_at =
            repetitions.front().revalidation.checked_at;
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.revalidation_status ==
                        ProfilingDifferentialRevalidationStatus::
                            NonIncreasingObservation &&
                    result.disposition ==
                        ProfilingDifferentialRevalidationDisposition::
                            RejectRevision,
                "one revalidation observation was reused for two repetitions");
    }

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first + 2h,
            9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0,
            first + 2h + 20s, 9200, input));
        repetitions.back().revalidation.target_activity =
            ProfilingDifferentialTargetActivity{
                digest('0'),
                repetitions.back().loaded.marker
                    ->target_containment_identity_sha256};
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.revalidation_status ==
                        ProfilingDifferentialRevalidationStatus::TargetMismatch &&
                    result.disposition ==
                        ProfilingDifferentialRevalidationDisposition::
                            RejectRevision,
                "target mismatch invalidated otherwise continuous noise");
    }

    {
        auto input = frozen_input(noise, 1, 1);
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first + 3h,
            9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0,
            first + 3h + 20s, 9200, input));
        repetitions.back().revalidation.observed_bindings.boot_id_sha256 =
            digest('0');
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.revalidation_status ==
                        ProfilingDifferentialRevalidationStatus::BindingMismatch &&
                    result.disposition ==
                        ProfilingDifferentialRevalidationDisposition::
                            InvalidateNoiseResult,
                "boot drift did not invalidate the noise result");
    }
}

void require_phase_markers_follow_completed_reads() {
    auto noise = parsed_noise_result();
    auto input = frozen_input(noise, 1, 1);
    const auto first = std::chrono::steady_clock::time_point{110h};
    std::vector<ProfilingDifferentialRepetition> repetitions;
    repetitions.push_back(repetition(
        ProfilingDifferentialRepetitionPhase::Calibration, 0, first, 9200,
        input));
    repetitions.push_back(repetition(
        ProfilingDifferentialRepetitionPhase::Validation, 0, first + 20s,
        9200, input));
    repetitions.front().baseline.points.back().read_finished_at = first + 7s;

    auto result = evaluate_retained_gtt_differential(
        std::move(input), method_binding(), std::move(repetitions));
    require(!result.accepted() &&
                result.status ==
                    ProfilingDifferentialEvaluationStatus::InvalidWindow,
            "loaded phase began before its baseline read completed");
}

void require_bounded_repetitions_and_retained_bound_arithmetic() {
    auto noise = parsed_noise_result();

    {
        auto draft = differential_input_draft(noise, 65, 64);
        auto frozen =
            freeze_profiling_differential_input(noise, std::move(draft));
        require(frozen.accepted(),
                "over-limit defense fixture did not freeze");
        auto input = std::move(*frozen.input);
        std::vector<ProfilingDifferentialRepetition> repetitions(129);
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.status == ProfilingDifferentialEvaluationStatus::
                                         InvalidRepetitionCount,
                "the total bounded repetition limit was not enforced");
    }

    {
        const auto x_gtt_bytes =
            std::numeric_limits<std::uint64_t>::max() -
            noise.n_gtt_bytes() - 50;
        auto input = frozen_input(noise, 1, 1, x_gtt_bytes, 0);
        const auto first = std::chrono::steady_clock::time_point{120h};
        std::vector<ProfilingDifferentialRepetition> repetitions;
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Calibration, 0, first,
            9200, input));
        repetitions.push_back(repetition(
            ProfilingDifferentialRepetitionPhase::Validation, 0, first + 20s,
            9200, input));
        auto result = evaluate_retained_gtt_differential(
            std::move(input), method_binding(), std::move(repetitions));
        require(!result.accepted() &&
                    result.status == ProfilingDifferentialEvaluationStatus::
                                         ArithmeticOverflow &&
                    !result.evidence.has_value(),
                "an overflowing retained-GTT bound was accepted");
    }
}

void require_preflight_rejects_uncollectable_repetition_count() {
    auto noise = parsed_noise_result();
    auto draft = differential_input_draft(noise, 65, 64);
    auto binding = resolve_retained_gtt_differential_method_binding(
        draft.identity.transaction, "amd.shared_gtt.retained_bytes");
    require(binding.has_value(),
            "preflight fixture method binding did not resolve");

    auto preflight = preflight_retained_gtt_differential(
        noise, draft, *binding);
    require(!preflight.accepted() &&
                preflight.status == ProfilingDifferentialPreflightStatus::
                                        InvalidRepetitionCount,
            "preflight allowed collection for an input the evaluator must "
            "reject");
}

} // namespace

int main() {
    try {
        require_stable_nonzero_background_succeeds();
        require_fixed_window_boundaries_and_cadence();
        require_first_window_and_disjoint_repetition_order();
        require_canonical_component_evidence_round_trips();
        require_parser_rejects_unreviewed_bindings();
        require_checked_bound_validation_and_release_failures();
        require_frozen_identity_actor_and_source_bindings();
        require_noise_invalidating_drift_preserves_disposition();
        require_reviewed_noise_procedure_revision();
        require_exact_method_and_constraint_binding();
        require_explicit_owner_projection_coverage();
        require_complete_ordered_point_schedule();
        require_fresh_revalidation_and_preserved_dispositions();
        require_phase_markers_follow_completed_reads();
        require_bounded_repetitions_and_retained_bound_arithmetic();
        require_preflight_rejects_uncollectable_repetition_count();
        std::cout
            << "PASS: residency profiling differential evaluator tests\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << "\n";
        return 1;
    }
}
