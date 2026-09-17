#include "lemon/residency/profiling_noise.h"

#include <chrono>
#include <cstdint>
#include <iostream>
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

bool is_digest(std::string_view value) {
    if (value.size() != 64) return false;
    for (const auto character : value) {
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return true;
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

ProfilingNoTargetGttTrace stable_trace(std::uint64_t base_value) {
    ProfilingNoTargetGttTrace trace;
    trace.started_at = std::chrono::steady_clock::time_point{1h};
    trace.exact_end =
        trace.started_at + profiling_noise_trace_duration;
    trace.bindings = bindings();
    trace.read_skew_uncertainty_bytes = 8;

    std::uint64_t index = 0;
    for (auto scheduled = trace.started_at; scheduled < trace.exact_end;
         scheduled += profiling_noise_nominal_cadence, ++index) {
        ProfilingNoTargetGttReading reading;
        reading.scheduled_at = scheduled;
        reading.read_started_at = scheduled;
        reading.read_finished_at = scheduled + 1ms;
        reading.gtt_used_bytes =
            base_value + (index % 2) * 64;
        reading.observed_bindings = trace.bindings;
        trace.readings.push_back(std::move(reading));
    }
    return trace;
}

void require_producer_binds_the_accepted_trace() {
    auto first = produce_no_target_gtt_noise(stable_trace(8192));
    auto second = produce_no_target_gtt_noise(stable_trace(8193));
    require(first.accepted() && second.accepted() &&
                first.result->n_gtt_bytes() == 72 &&
                second.result->n_gtt_bytes() == 72,
            "trace provenance fixtures did not produce equal noise bounds");

    const auto first_provenance =
        first.result->trace_provenance_sha256();
    const auto second_provenance =
        second.result->trace_provenance_sha256();
    require(is_digest(first_provenance) &&
                is_digest(second_provenance),
            "accepted noise result lacks producer-derived trace provenance");
    require(first_provenance != second_provenance,
            "distinct accepted traces share one provenance digest");
}

} // namespace

int main() {
    try {
        require_producer_binds_the_accepted_trace();
        std::cout << "PASS: residency profiling noise provenance tests\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << "\n";
        return 1;
    }
}
