#include "lemon/residency/profiling_noise.h"

#include <chrono>
#include <cstdint>
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

ProfilingNoTargetGttTrace shifted_trace(
    std::uint64_t lower_level,
    std::uint64_t upper_level,
    std::uint64_t uncertainty_bytes) {
    ProfilingNoTargetGttTrace trace;
    trace.started_at = std::chrono::steady_clock::time_point{1h};
    trace.exact_end =
        trace.started_at + profiling_noise_trace_duration;
    trace.bindings = bindings();
    trace.read_skew_uncertainty_bytes = uncertainty_bytes;

    const auto shift_at =
        trace.started_at + profiling_noise_trace_duration / 2;
    for (auto scheduled = trace.started_at; scheduled < trace.exact_end;
         scheduled += profiling_noise_nominal_cadence) {
        ProfilingNoTargetGttReading reading;
        reading.scheduled_at = scheduled;
        reading.read_started_at = scheduled;
        reading.read_finished_at = scheduled + 1ms;
        reading.gtt_used_bytes =
            scheduled < shift_at ? lower_level : upper_level;
        reading.observed_bindings = trace.bindings;
        trace.readings.push_back(std::move(reading));
    }
    return trace;
}

void require_common_level_equality_is_accepted() {
    auto trace = shifted_trace(100, 200, 100);
    auto result = produce_no_target_gtt_noise(trace);
    require(result.accepted() &&
                result.result->n_gtt_bytes() == 200,
            "the exact common-level uncertainty boundary was rejected");
}

void require_shift_beyond_uncertainty_is_rejected() {
    auto trace = shifted_trace(100, 200, 99);
    auto result = produce_no_target_gtt_noise(trace);
    require(!result.accepted() &&
                result.status ==
                    ProfilingNoiseProductionStatus::InvalidTrace,
            "a persistent shift beyond uncertainty did not reject the trace");
}

void require_trend_addition_overflow_is_rejected() {
    const auto level =
        std::numeric_limits<std::uint64_t>::max() - 4;
    auto trace = shifted_trace(level, level, 8);
    auto result = produce_no_target_gtt_noise(trace);
    require(!result.accepted() &&
                result.status ==
                    ProfilingNoiseProductionStatus::ArithmeticOverflow,
            "common-level checked addition overflow did not reject the trace");
}

} // namespace

int main() {
    try {
        require_common_level_equality_is_accepted();
        require_shift_beyond_uncertainty_is_rejected();
        require_trend_addition_overflow_is_rejected();
        std::cout << "PASS: residency profiling noise trend tests\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << "\n";
        return 1;
    }
}
