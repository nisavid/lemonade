#include "lemon/residency/profiling_noise.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <utility>

namespace {

using namespace std::chrono_literals;
using lemon::residency::ParsedProfilingNoiseResult;
using lemon::residency::ProfilingNoTargetGttReading;
using lemon::residency::ProfilingNoTargetGttTrace;
using lemon::residency::ProfilingNoiseBindings;
using lemon::residency::ProfilingNoiseParseStatus;
using lemon::residency::ProfilingNoiseProductionStatus;
using lemon::residency::parse_profiling_noise_result;
using lemon::residency::produce_no_target_gtt_noise;
using lemon::residency::profiling_noise_nominal_cadence;
using lemon::residency::profiling_noise_trace_duration;

std::string digest(char digit) {
    return std::string(64, digit);
}

ProfilingNoiseBindings bindings() {
    ProfilingNoiseBindings result;
    result.deployment_id = "hatchery";
    result.deployment_epoch_sha256 = digest('0');
    result.boot_id_sha256 = digest('1');
    result.device_identity_sha256 = digest('2');
    result.topology_sha256 = digest('3');
    result.kernel_identity_sha256 = digest('4');
    result.driver_identity_sha256 = digest('5');
    result.counter_source_id = "linux-amd-mem-info-gtt-used";
    result.counter_source_revision_sha256 = digest('6');
    result.campaign_contract_sha256 = digest('7');
    result.procedure_revision_sha256 = digest('8');
    result.background_inventory_sha256 = digest('9');
    return result;
}

bool bindings_equal(const ProfilingNoiseBindings &left,
                    const ProfilingNoiseBindings &right) {
    return left.deployment_id == right.deployment_id &&
           left.deployment_epoch_sha256 == right.deployment_epoch_sha256 &&
           left.boot_id_sha256 == right.boot_id_sha256 &&
           left.device_identity_sha256 == right.device_identity_sha256 &&
           left.topology_sha256 == right.topology_sha256 &&
           left.kernel_identity_sha256 == right.kernel_identity_sha256 &&
           left.driver_identity_sha256 == right.driver_identity_sha256 &&
           left.counter_source_id == right.counter_source_id &&
           left.counter_source_revision_sha256 ==
               right.counter_source_revision_sha256 &&
           left.campaign_contract_sha256 == right.campaign_contract_sha256 &&
           left.procedure_revision_sha256 == right.procedure_revision_sha256 &&
           left.background_inventory_sha256 ==
               right.background_inventory_sha256;
}

ProfilingNoTargetGttTrace stable_trace() {
    ProfilingNoTargetGttTrace trace;
    trace.started_at = std::chrono::steady_clock::time_point{1h};
    trace.exact_end = trace.started_at + profiling_noise_trace_duration;
    trace.bindings = bindings();
    trace.read_skew_uncertainty_bytes = 8;

    std::uint64_t index = 0;
    for (auto scheduled = trace.started_at; scheduled < trace.exact_end;
         scheduled += profiling_noise_nominal_cadence, ++index) {
        ProfilingNoTargetGttReading reading;
        reading.scheduled_at = scheduled;
        reading.read_started_at = scheduled;
        reading.read_finished_at = scheduled + 1ms;
        reading.gtt_used_bytes = index % 2 == 0 ? 4096 : 4160;
        reading.observed_bindings = trace.bindings;
        trace.readings.push_back(std::move(reading));
    }
    return trace;
}

ProfilingNoTargetGttTrace jittered_boundary_trace() {
    ProfilingNoTargetGttTrace trace;
    trace.started_at = std::chrono::steady_clock::time_point{2h};
    trace.exact_end = trace.started_at + profiling_noise_trace_duration;
    trace.bindings = bindings();
    trace.read_skew_uncertainty_bytes = 8;

    auto scheduled = trace.started_at;
    constexpr std::size_t reading_count = 18000;
    for (std::size_t index = 0; index < reading_count; ++index) {
        ProfilingNoTargetGttReading reading;
        reading.scheduled_at = scheduled;
        reading.read_started_at =
            scheduled + std::chrono::milliseconds(index % 5);
        reading.read_finished_at = reading.read_started_at + 1ms;
        reading.gtt_used_bytes = 1000;
        if (index == 0) reading.gtt_used_bytes = 960;
        if (index == 100) reading.gtt_used_bytes = 1064;
        reading.observed_bindings = trace.bindings;
        trace.readings.push_back(std::move(reading));

        scheduled += index % 2 == 0 ? 49ms : 51ms;
    }
    return trace;
}

ProfilingNoTargetGttTrace scheduled_window_origin_trace() {
    auto trace = stable_trace();
    trace.read_skew_uncertainty_bytes = 0;
    const auto shifted_at = trace.exact_end - 5s;

    for (auto &reading : trace.readings) {
        reading.read_started_at = reading.scheduled_at + 10ms;
        reading.read_finished_at = reading.read_started_at + 1ms;
        reading.gtt_used_bytes =
            reading.scheduled_at < shifted_at ? 0 : 100;
    }
    return trace;
}

bool rejects(const ProfilingNoTargetGttTrace &trace,
             ProfilingNoiseProductionStatus status) {
    const auto produced = produce_no_target_gtt_noise(trace);
    return !produced.accepted() && produced.status == status &&
           !produced.result.has_value();
}

bool parse_rejects(std::string_view bytes,
                   ProfilingNoiseParseStatus status) {
    const auto parsed = parse_profiling_noise_result(bytes);
    return !parsed.accepted() && parsed.status == status &&
           !parsed.result.has_value();
}

bool invalid_trace_contract_is_enforced() {
    auto failed_reading = stable_trace();
    failed_reading.readings.back().gtt_used_bytes.reset();
    if (!rejects(failed_reading,
                 ProfilingNoiseProductionStatus::InvalidTrace)) {
        std::cerr << "FAIL: a failed reading did not reject the whole trace\n";
        return false;
    }

    auto excessive_gap = stable_trace();
    excessive_gap.readings[100].read_started_at += 51ms;
    excessive_gap.readings[100].read_finished_at += 51ms;
    if (!rejects(excessive_gap,
                 ProfilingNoiseProductionStatus::InvalidTrace)) {
        std::cerr << "FAIL: a read-start gap above 100 ms was accepted\n";
        return false;
    }

    auto identity_change = stable_trace();
    identity_change.readings[9000].observed_bindings.boot_id_sha256 =
        digest('a');
    if (!rejects(identity_change,
                 ProfilingNoiseProductionStatus::InvalidTrace)) {
        std::cerr << "FAIL: an identity change did not reject the trace\n";
        return false;
    }

    auto invalid_duration = stable_trace();
    invalid_duration.exact_end -= 1ms;
    if (!rejects(invalid_duration,
                 ProfilingNoiseProductionStatus::InvalidTrace)) {
        std::cerr << "FAIL: a trace without the exact duration was accepted\n";
        return false;
    }

    auto closed_end = stable_trace();
    auto end_reading = closed_end.readings.back();
    end_reading.scheduled_at = closed_end.exact_end;
    end_reading.read_started_at = closed_end.exact_end;
    end_reading.read_finished_at = closed_end.exact_end + 1ms;
    closed_end.readings.push_back(std::move(end_reading));
    if (!rejects(closed_end,
                 ProfilingNoiseProductionStatus::InvalidTrace)) {
        std::cerr << "FAIL: a reading begun at the exact trace end was "
                     "accepted\n";
        return false;
    }

    auto bound_overflow = stable_trace();
    bound_overflow.read_skew_uncertainty_bytes = 1;
    for (auto &reading : bound_overflow.readings) {
        reading.gtt_used_bytes = 0;
    }
    bound_overflow.readings[1].gtt_used_bytes =
        std::numeric_limits<std::uint64_t>::max();
    if (!rejects(bound_overflow,
                 ProfilingNoiseProductionStatus::ArithmeticOverflow)) {
        std::cerr << "FAIL: N_gtt range-plus-uncertainty overflow was "
                     "accepted\n";
        return false;
    }

    return true;
}

bool codec_rejection_contract_is_enforced(
    const ParsedProfilingNoiseResult &result) {
    const std::string canonical(result.canonical_bytes());

    auto malformed = canonical;
    malformed.pop_back();
    if (!parse_rejects(malformed,
                       ProfilingNoiseParseStatus::MalformedJson)) {
        std::cerr << "FAIL: malformed canonical-result JSON was accepted\n";
        return false;
    }

    auto ambiguous = canonical;
    ambiguous.insert(1, "\"n_gtt_bytes\":72,");
    if (!parse_rejects(ambiguous,
                       ProfilingNoiseParseStatus::InvalidValue)) {
        std::cerr << "FAIL: an ambiguous duplicate result field was "
                     "accepted\n";
        return false;
    }

    auto unknown_shape = canonical;
    unknown_shape.insert(1, "\"future_field\":0,");
    if (!parse_rejects(unknown_shape,
                       ProfilingNoiseParseStatus::UnknownField)) {
        std::cerr << "FAIL: an unknown canonical-result field was accepted\n";
        return false;
    }

    auto unsupported_schema = canonical;
    const auto schema_major = unsupported_schema.find("\"major\":1");
    if (schema_major == std::string::npos) {
        std::cerr << "FAIL: unsupported-schema fixture found no schema major\n";
        return false;
    }
    unsupported_schema[schema_major +
                       std::string_view("\"major\":").size()] = '2';
    if (!parse_rejects(unsupported_schema,
                       ProfilingNoiseParseStatus::UnsupportedSchema)) {
        std::cerr << "FAIL: an unsupported result schema was accepted\n";
        return false;
    }

    auto invalid_numeric = canonical;
    const auto noise_amount =
        invalid_numeric.find("\"n_gtt_bytes\":72");
    if (noise_amount == std::string::npos) {
        std::cerr << "FAIL: invalid-number fixture found no noise amount\n";
        return false;
    }
    invalid_numeric.replace(
        noise_amount, std::string_view("\"n_gtt_bytes\":72").size(),
        "\"n_gtt_bytes\":-1");
    if (!parse_rejects(invalid_numeric,
                       ProfilingNoiseParseStatus::InvalidValue)) {
        std::cerr << "FAIL: an invalid unsigned noise amount was accepted\n";
        return false;
    }

    auto corrupted_provenance = canonical;
    constexpr std::string_view provenance_field =
        "\"trace_provenance_sha256\":\"";
    const auto provenance =
        corrupted_provenance.find(provenance_field);
    if (provenance == std::string::npos) {
        std::cerr << "FAIL: provenance-corruption fixture found no digest\n";
        return false;
    }
    auto &provenance_digit =
        corrupted_provenance[provenance + provenance_field.size()];
    provenance_digit = provenance_digit == '0' ? '1' : '0';
    if (!parse_rejects(corrupted_provenance,
                       ProfilingNoiseParseStatus::DigestMismatch)) {
        std::cerr << "FAIL: checksum-bound trace provenance corruption was "
                     "accepted\n";
        return false;
    }

    auto noncanonical = canonical;
    noncanonical.push_back('\n');
    if (!parse_rejects(noncanonical,
                       ProfilingNoiseParseStatus::NonCanonical)) {
        std::cerr << "FAIL: noncanonical noise bytes are accepted\n";
        return false;
    }

    return true;
}

} // namespace

int main() {
    const auto produced = produce_no_target_gtt_noise(stable_trace());
    if (!produced.accepted()) {
        std::cerr << "FAIL: a complete stable no-target trace produces N_gtt\n";
        return 1;
    }
    if (produced.result->n_gtt_bytes() != 72) {
        std::cerr << "FAIL: N_gtt includes the largest window range and "
                     "read/skew uncertainty exactly once\n";
        return 1;
    }

    const auto jittered =
        produce_no_target_gtt_noise(jittered_boundary_trace());
    if (!jittered.accepted()) {
        std::cerr << "FAIL: nominal scheduling and bounded read-start jitter "
                     "produce a valid trace\n";
        return 1;
    }
    if (jittered.result->n_gtt_bytes() != 72) {
        std::cerr << "FAIL: a reading begun exactly at a five-second window "
                     "end is excluded from that window\n";
        return 1;
    }

    const auto scheduled_origin =
        produce_no_target_gtt_noise(scheduled_window_origin_trace());
    if (scheduled_origin.accepted() ||
        scheduled_origin.status !=
            ProfilingNoiseProductionStatus::InvalidTrace ||
        scheduled_origin.result.has_value()) {
        std::cerr << "FAIL: scheduled window origin was replaced by delayed "
                     "read initiation\n";
        return 1;
    }

    if (!invalid_trace_contract_is_enforced()) return 1;

    const auto parsed =
        parse_profiling_noise_result(produced.result->canonical_bytes());
    if (!parsed.accepted()) {
        std::cerr << "FAIL: a canonical noise result round-trips\n";
        return 1;
    }
    if (parsed.result->canonical_bytes() !=
            produced.result->canonical_bytes() ||
        parsed.result->checksum_sha256() !=
            produced.result->checksum_sha256() ||
        parsed.result->bindings_sha256() !=
            produced.result->bindings_sha256() ||
        parsed.result->trace_provenance_sha256() !=
            produced.result->trace_provenance_sha256() ||
        parsed.result->n_gtt_bytes() != produced.result->n_gtt_bytes() ||
        !bindings_equal(parsed.result->bindings(),
                        produced.result->bindings())) {
        std::cerr << "FAIL: round-trip preserves the immutable noise result\n";
        return 1;
    }

    if (!codec_rejection_contract_is_enforced(*produced.result)) return 1;
    return 0;
}
