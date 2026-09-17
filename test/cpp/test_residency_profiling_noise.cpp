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
using lemon::residency::profiling_noise_maximum_trace_points;
using lemon::residency::profiling_noise_nominal_cadence;
using lemon::residency::profiling_noise_trace_duration;

std::string digest(char digit) {
    return std::string(64, digit);
}

ProfilingNoiseBindings bindings() {
    ProfilingNoiseBindings result;
    result.deployment_id = digest('f');
    result.deployment_epoch_sha256 = digest('0');
    result.boot_id_sha256 = digest('1');
    result.device_identity_sha256 = digest('2');
    result.topology_sha256 = digest('3');
    result.kernel_identity_sha256 = digest('4');
    result.driver_identity_sha256 = digest('5');
    result.counter_source_id = "linux-amd-mem-info-gtt-used";
    result.counter_source_revision_sha256 = digest('6');
    result.counter_continuity_epoch_sha256 = digest('a');
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
           left.counter_continuity_epoch_sha256 ==
               right.counter_continuity_epoch_sha256 &&
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

ProfilingNoTargetGttTrace dense_trace(
    std::chrono::milliseconds cadence) {
    auto trace = stable_trace();
    trace.readings.clear();
    trace.read_skew_uncertainty_bytes = 0;

    for (auto scheduled = trace.started_at; scheduled < trace.exact_end;
         scheduled += cadence) {
        ProfilingNoTargetGttReading reading;
        reading.scheduled_at = scheduled;
        reading.read_started_at = scheduled;
        reading.read_finished_at = scheduled + 1ms;
        reading.gtt_used_bytes = 4096;
        reading.observed_bindings = trace.bindings;
        trace.readings.push_back(std::move(reading));
    }
    return trace;
}

ProfilingNoTargetGttReading constant_reading(
    const ProfilingNoTargetGttTrace &trace,
    std::chrono::steady_clock::time_point scheduled_at) {
    ProfilingNoTargetGttReading reading;
    reading.scheduled_at = scheduled_at;
    reading.read_started_at = scheduled_at;
    reading.read_finished_at = scheduled_at + 1ns;
    reading.gtt_used_bytes = 4096;
    reading.observed_bindings = trace.bindings;
    return reading;
}

ProfilingNoTargetGttTrace front_loaded_cluster_trace() {
    auto trace = stable_trace();
    trace.readings.clear();
    trace.read_skew_uncertainty_bytes = 0;

    constexpr std::size_t cluster_points = 27000;
    constexpr std::size_t tail_points = 9000;
    const auto first_span_nanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(1s).count();
    const auto tail_span_nanoseconds =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            profiling_noise_trace_duration - 1s)
            .count();

    trace.readings.reserve(profiling_noise_maximum_trace_points);
    for (std::size_t index = 0; index < cluster_points; ++index) {
        const auto offset = std::chrono::nanoseconds{
            first_span_nanoseconds * static_cast<std::int64_t>(index) /
            static_cast<std::int64_t>(cluster_points)};
        trace.readings.push_back(
            constant_reading(trace, trace.started_at + offset));
    }
    for (std::size_t index = 0; index < tail_points; ++index) {
        const auto offset = std::chrono::nanoseconds{
            tail_span_nanoseconds * static_cast<std::int64_t>(index) /
            static_cast<std::int64_t>(tail_points)};
        trace.readings.push_back(
            constant_reading(trace, trace.started_at + 1s + offset));
    }
    return trace;
}

ProfilingNoTargetGttTrace midpoint_cluster_trace() {
    auto trace = stable_trace();
    trace.read_skew_uncertainty_bytes = 0;
    for (auto &reading : trace.readings) {
        reading.gtt_used_bytes = 4096;
    }

    constexpr std::size_t inserted_points = 18000;
    const auto midpoint = trace.readings.size() / 2;
    std::vector<ProfilingNoTargetGttReading> readings;
    readings.reserve(profiling_noise_maximum_trace_points);
    for (std::size_t index = 0; index < trace.readings.size(); ++index) {
        readings.push_back(trace.readings[index]);
        if (index != midpoint) continue;

        const auto cluster_start =
            trace.readings[index].read_finished_at;
        for (std::size_t inserted = 0; inserted < inserted_points;
             ++inserted) {
            readings.push_back(constant_reading(
                trace,
                cluster_start +
                    std::chrono::nanoseconds{static_cast<std::int64_t>(
                        inserted + 1)}));
        }
    }
    trace.readings = std::move(readings);
    return trace;
}

ProfilingNoTargetGttTrace cap_plus_one_trace() {
    auto trace = dense_trace(25ms);
    auto inserted = trace.readings.front();
    inserted.scheduled_at += 12ms;
    inserted.read_started_at = inserted.scheduled_at;
    inserted.read_finished_at = inserted.read_started_at + 1ms;
    trace.readings.insert(trace.readings.begin() + 1, std::move(inserted));
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

bool counter_continuity_epoch_contract_is_enforced(
    const ParsedProfilingNoiseResult &first) {
    const auto first_epoch = digest('a');
    const auto second_epoch = digest('b');
    const auto parsed = parse_profiling_noise_result(first.canonical_bytes());
    if (!parsed.accepted() ||
        parsed.result->bindings().counter_continuity_epoch_sha256 !=
            first_epoch) {
        std::cerr << "FAIL: canonical parsing lost the trace-era counter "
                     "continuity epoch\n";
        return false;
    }

    auto second_trace = stable_trace();
    second_trace.bindings.counter_continuity_epoch_sha256 = second_epoch;
    for (auto &reading : second_trace.readings) {
        reading.observed_bindings = second_trace.bindings;
    }
    const auto second = produce_no_target_gtt_noise(second_trace);
    if (!second.accepted() ||
        first.bindings_sha256() == second.result->bindings_sha256() ||
        first.checksum_sha256() == second.result->checksum_sha256() ||
        first.trace_provenance_sha256() ==
            second.result->trace_provenance_sha256()) {
        std::cerr << "FAIL: the trace-era counter continuity epoch is not "
                     "integrity-bound\n";
        return false;
    }
    return true;
}

bool invalid_trace_contract_is_enforced() {
    auto invalid_deployment = stable_trace();
    invalid_deployment.bindings.deployment_id = "hatchery";
    for (auto &reading : invalid_deployment.readings) {
        reading.observed_bindings = invalid_deployment.bindings;
    }
    if (!rejects(invalid_deployment,
                 ProfilingNoiseProductionStatus::InvalidTrace)) {
        std::cerr << "FAIL: a printable non-digest deployment ID was "
                     "accepted\n";
        return false;
    }

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

    auto boundary_finish = stable_trace();
    boundary_finish.readings.back().read_finished_at =
        boundary_finish.exact_end;
    if (!produce_no_target_gtt_noise(boundary_finish).accepted()) {
        std::cerr << "FAIL: a read finishing exactly at the trace boundary "
                     "was rejected\n";
        return false;
    }

    auto escaped_finish = stable_trace();
    escaped_finish.readings.back().read_finished_at =
        escaped_finish.exact_end + 1ns;
    if (!rejects(escaped_finish,
                 ProfilingNoiseProductionStatus::InvalidTrace)) {
        std::cerr << "FAIL: a read finishing after the trace boundary was "
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

    const auto maximum_density =
        produce_no_target_gtt_noise(dense_trace(25ms));
    if (!maximum_density.accepted()) {
        std::cerr << "FAIL: the documented maximum trace density was "
                     "rejected\n";
        return false;
    }

    auto cap_plus_one = cap_plus_one_trace();
    if (cap_plus_one.readings.size() !=
            profiling_noise_maximum_trace_points + 1 ||
        !rejects(cap_plus_one,
                 ProfilingNoiseProductionStatus::InvalidTrace)) {
        std::cerr << "FAIL: the first reading above the documented trace "
                     "limit was accepted\n";
        return false;
    }

    if (!rejects(dense_trace(24ms),
                 ProfilingNoiseProductionStatus::InvalidTrace)) {
        std::cerr << "FAIL: an over-dense trace exceeded the resource "
                     "bound\n";
        return false;
    }

    const auto midpoint_cluster =
        produce_no_target_gtt_noise(midpoint_cluster_trace());
    if (!midpoint_cluster.accepted() ||
        midpoint_cluster.result->n_gtt_bytes() != 0) {
        std::cerr << "FAIL: an exact-cap midpoint cluster was rejected or "
                     "changed the noise bound\n";
        return false;
    }

    const auto front_loaded_cluster =
        produce_no_target_gtt_noise(front_loaded_cluster_trace());
    if (!front_loaded_cluster.accepted() ||
        front_loaded_cluster.result->n_gtt_bytes() != 0) {
        std::cerr << "FAIL: an exact-cap front-loaded cluster was rejected "
                     "or changed the noise bound\n";
        return false;
    }

    return true;
}

bool codec_rejection_contract_is_enforced(
    const ParsedProfilingNoiseResult &result) {
    const std::string canonical(result.canonical_bytes());

    auto invalid_deployment = canonical;
    const auto deployment_field =
        std::string("\"deployment_id\":\"") + digest('f') + "\"";
    const auto deployment = invalid_deployment.find(deployment_field);
    if (deployment == std::string::npos) {
        std::cerr << "FAIL: deployment-ID fixture found no binding\n";
        return false;
    }
    invalid_deployment.replace(
        deployment, deployment_field.size(),
        "\"deployment_id\":\"hatchery\"");
    if (!parse_rejects(invalid_deployment,
                       ProfilingNoiseParseStatus::InvalidValue)) {
        std::cerr << "FAIL: a parsed non-digest deployment ID was accepted\n";
        return false;
    }

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
    if (!counter_continuity_epoch_contract_is_enforced(*produced.result)) {
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
