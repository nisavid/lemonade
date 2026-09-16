#include "lemon/residency/profiling_noise.h"

#include "profiling_common.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <deque>
#include <initializer_list>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace lemon::residency {
namespace {

using json = nlohmann::json;
using profiling_internal::append_string;
using profiling_internal::append_u64;
using profiling_internal::bounded_diagnostic;
using profiling_internal::digest_is_valid;
using profiling_internal::elapsed_between;
using profiling_internal::sha256_hex;

constexpr char profiling_noise_bindings_domain[] =
    "lemonade.residency.profiling-noise-bindings/v1\0";
constexpr char profiling_noise_result_domain[] =
    "lemonade.residency.profiling-noise-result/v1\0";
constexpr char profiling_noise_trace_domain[] =
    "lemonade.residency.profiling-noise-trace/v1\0";

class NoiseParseFailure final : public std::runtime_error {
public:
    NoiseParseFailure(ProfilingNoiseParseStatus status, std::string diagnostic)
        : std::runtime_error(std::move(diagnostic)), status_(status) {}

    ProfilingNoiseParseStatus status() const noexcept {
        return status_;
    }

private:
    ProfilingNoiseParseStatus status_;
};

[[noreturn]] void reject_parse(ProfilingNoiseParseStatus status,
                               std::string diagnostic) {
    throw NoiseParseFailure(status, std::move(diagnostic));
}

bool identifier_is_valid(std::string_view value) noexcept {
    return !value.empty() && value.size() <= 128 &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return character >= 0x21 && character <= 0x7e;
           });
}

bool bindings_are_valid(const ProfilingNoiseBindings &bindings) noexcept {
    return digest_is_valid(bindings.deployment_id) &&
           digest_is_valid(bindings.deployment_epoch_sha256) &&
           digest_is_valid(bindings.boot_id_sha256) &&
           digest_is_valid(bindings.device_identity_sha256) &&
           digest_is_valid(bindings.topology_sha256) &&
           digest_is_valid(bindings.kernel_identity_sha256) &&
           digest_is_valid(bindings.driver_identity_sha256) &&
           identifier_is_valid(bindings.counter_source_id) &&
           digest_is_valid(bindings.counter_source_revision_sha256) &&
           digest_is_valid(bindings.campaign_contract_sha256) &&
           digest_is_valid(bindings.procedure_revision_sha256) &&
           digest_is_valid(bindings.background_inventory_sha256);
}

void require_identifier(std::string_view value, std::string_view label) {
    if (!identifier_is_valid(value)) {
        reject_parse(ProfilingNoiseParseStatus::InvalidIdentifier,
                     std::string(label) + " is invalid");
    }
}

void require_digest(std::string_view value, std::string_view label) {
    if (!digest_is_valid(value)) {
        reject_parse(ProfilingNoiseParseStatus::InvalidValue,
                     std::string(label) + " is invalid");
    }
}

void require_valid_bindings(const ProfilingNoiseBindings &bindings) {
    require_digest(bindings.deployment_id, "deployment ID");
    require_digest(bindings.deployment_epoch_sha256,
                   "deployment epoch digest");
    require_digest(bindings.boot_id_sha256, "boot ID digest");
    require_digest(bindings.device_identity_sha256,
                   "device identity digest");
    require_digest(bindings.topology_sha256, "topology digest");
    require_digest(bindings.kernel_identity_sha256,
                   "kernel identity digest");
    require_digest(bindings.driver_identity_sha256,
                   "driver identity digest");
    require_identifier(bindings.counter_source_id, "counter source ID");
    require_digest(bindings.counter_source_revision_sha256,
                   "counter source revision digest");
    require_digest(bindings.campaign_contract_sha256,
                   "campaign contract digest");
    require_digest(bindings.procedure_revision_sha256,
                   "procedure revision digest");
    require_digest(bindings.background_inventory_sha256,
                   "background inventory digest");
}

bool bindings_equal(const ProfilingNoiseBindings &left,
                    const ProfilingNoiseBindings &right) noexcept {
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

json bindings_document(const ProfilingNoiseBindings &bindings) {
    return json{
        {"background_inventory_sha256",
         bindings.background_inventory_sha256},
        {"boot_id_sha256", bindings.boot_id_sha256},
        {"campaign_contract_sha256", bindings.campaign_contract_sha256},
        {"counter_source_id", bindings.counter_source_id},
        {"counter_source_revision_sha256",
         bindings.counter_source_revision_sha256},
        {"deployment_epoch_sha256", bindings.deployment_epoch_sha256},
        {"deployment_id", bindings.deployment_id},
        {"device_identity_sha256", bindings.device_identity_sha256},
        {"driver_identity_sha256", bindings.driver_identity_sha256},
        {"kernel_identity_sha256", bindings.kernel_identity_sha256},
        {"procedure_revision_sha256", bindings.procedure_revision_sha256},
        {"topology_sha256", bindings.topology_sha256},
    };
}

template <std::size_t Size>
std::optional<std::string> domain_digest(const char (&domain)[Size],
                                         std::string_view payload) {
    std::string bytes(domain, Size - 1);
    bytes.append(payload.data(), payload.size());
    return sha256_hex(bytes);
}

std::optional<std::string> trace_provenance_digest(
    const ProfilingNoTargetGttTrace &trace) {
    std::string bytes(profiling_noise_trace_domain,
                      sizeof(profiling_noise_trace_domain) - 1);
    append_u64(bytes, static_cast<std::uint64_t>(
                          std::chrono::steady_clock::period::num));
    append_u64(bytes, static_cast<std::uint64_t>(
                          std::chrono::steady_clock::period::den));
    append_string(bytes, bindings_document(trace.bindings).dump());
    append_u64(bytes, static_cast<std::uint64_t>(
                          trace.started_at.time_since_epoch().count()));
    append_u64(bytes, static_cast<std::uint64_t>(
                          trace.exact_end.time_since_epoch().count()));
    append_u64(bytes, trace.read_skew_uncertainty_bytes);
    append_u64(bytes,
               static_cast<std::uint64_t>(trace.readings.size()));
    for (const auto &reading : trace.readings) {
        append_u64(bytes, static_cast<std::uint64_t>(
                              reading.scheduled_at.time_since_epoch().count()));
        append_u64(bytes, static_cast<std::uint64_t>(
                              reading.read_started_at.time_since_epoch().count()));
        append_u64(bytes, static_cast<std::uint64_t>(
                              reading.read_finished_at.time_since_epoch().count()));
        append_u64(bytes, *reading.gtt_used_bytes);
    }
    return sha256_hex(bytes);
}

void require_exact_keys(const json &object,
                        std::initializer_list<std::string_view> expected,
                        std::string_view label) {
    if (!object.is_object()) {
        reject_parse(ProfilingNoiseParseStatus::InvalidValue,
                     std::string(label) + " must be an object");
    }

    std::set<std::string> expected_keys;
    for (const auto key : expected) {
        expected_keys.emplace(key);
        if (!object.contains(std::string(key))) {
            reject_parse(ProfilingNoiseParseStatus::InvalidValue,
                         std::string(label) + " is incomplete");
        }
    }
    for (const auto &[key, value] : object.items()) {
        static_cast<void>(value);
        if (expected_keys.find(key) == expected_keys.end()) {
            reject_parse(ProfilingNoiseParseStatus::UnknownField,
                         std::string(label) + " has an unknown field");
        }
    }
}

const json &required(const json &object, std::string_view key) {
    const auto found = object.find(std::string(key));
    if (found == object.end()) {
        reject_parse(ProfilingNoiseParseStatus::InvalidValue,
                     "required field is missing");
    }
    return *found;
}

std::string require_string(const json &value, std::string_view label) {
    if (!value.is_string()) {
        reject_parse(ProfilingNoiseParseStatus::InvalidValue,
                     std::string(label) + " must be a string");
    }
    return value.get<std::string>();
}

std::uint64_t require_u64(const json &value, std::string_view label) {
    if (value.is_number_unsigned()) {
        return value.get<std::uint64_t>();
    }
    if (value.is_number_integer() && value.get<std::int64_t>() == 0) {
        return 0;
    }
    reject_parse(ProfilingNoiseParseStatus::InvalidValue,
                 std::string(label) + " must be an unsigned integer");
}

json parse_json(std::string_view bytes) {
    if (bytes.size() > max_local_overlay_input_bytes) {
        reject_parse(ProfilingNoiseParseStatus::InputTooLarge,
                     "noise result exceeds the input limit");
    }
    if (bytes.find('\0') != std::string_view::npos) {
        reject_parse(ProfilingNoiseParseStatus::MalformedJson,
                     "noise result contains a NUL byte");
    }

    std::map<int, std::set<std::string>> object_keys;
    const auto callback = [&object_keys](int depth, json::parse_event_t event,
                                         json &parsed) {
        if (event == json::parse_event_t::object_start) {
            object_keys[depth + 1].clear();
        } else if (event == json::parse_event_t::key) {
            auto &keys = object_keys[depth];
            const auto key = parsed.get<std::string>();
            if (!keys.insert(key).second) {
                reject_parse(ProfilingNoiseParseStatus::InvalidValue,
                             "noise result has a duplicate key");
            }
        } else if (event == json::parse_event_t::object_end) {
            object_keys.erase(depth + 1);
        }
        return true;
    };

    try {
        return json::parse(bytes.begin(), bytes.end(), callback, true, false);
    } catch (const NoiseParseFailure &) {
        throw;
    } catch (const json::exception &) {
        reject_parse(ProfilingNoiseParseStatus::MalformedJson,
                     "noise result is malformed JSON");
    }
}

void parse_schema(const json &value) {
    require_exact_keys(value, {"major", "minor"}, "noise result schema");
    const auto major = require_u64(required(value, "major"), "schema major");
    const auto minor = require_u64(required(value, "minor"), "schema minor");
    if (major != 1 || minor != 0) {
        reject_parse(ProfilingNoiseParseStatus::UnsupportedSchema,
                     "noise result schema is unsupported");
    }
}

ProfilingNoiseBindings parse_bindings(const json &value) {
    require_exact_keys(
        value,
        {"background_inventory_sha256", "boot_id_sha256",
         "campaign_contract_sha256", "counter_source_id",
         "counter_source_revision_sha256", "deployment_epoch_sha256",
         "deployment_id", "device_identity_sha256",
         "driver_identity_sha256", "kernel_identity_sha256",
         "procedure_revision_sha256", "topology_sha256"},
        "noise bindings");

    ProfilingNoiseBindings bindings;
    bindings.background_inventory_sha256 = require_string(
        required(value, "background_inventory_sha256"),
        "background inventory digest");
    bindings.boot_id_sha256 =
        require_string(required(value, "boot_id_sha256"), "boot ID digest");
    bindings.campaign_contract_sha256 = require_string(
        required(value, "campaign_contract_sha256"),
        "campaign contract digest");
    bindings.counter_source_id = require_string(
        required(value, "counter_source_id"), "counter source ID");
    bindings.counter_source_revision_sha256 = require_string(
        required(value, "counter_source_revision_sha256"),
        "counter source revision digest");
    bindings.deployment_epoch_sha256 = require_string(
        required(value, "deployment_epoch_sha256"),
        "deployment epoch digest");
    bindings.deployment_id =
        require_string(required(value, "deployment_id"), "deployment ID");
    bindings.device_identity_sha256 = require_string(
        required(value, "device_identity_sha256"),
        "device identity digest");
    bindings.driver_identity_sha256 = require_string(
        required(value, "driver_identity_sha256"),
        "driver identity digest");
    bindings.kernel_identity_sha256 = require_string(
        required(value, "kernel_identity_sha256"),
        "kernel identity digest");
    bindings.procedure_revision_sha256 = require_string(
        required(value, "procedure_revision_sha256"),
        "procedure revision digest");
    bindings.topology_sha256 = require_string(
        required(value, "topology_sha256"), "topology digest");
    require_valid_bindings(bindings);
    return bindings;
}

ProfilingNoiseProductionResult reject(ProfilingNoiseProductionStatus status,
                                      std::string diagnostic) {
    return {status, std::move(diagnostic), std::nullopt};
}

} // namespace

ParsedProfilingNoiseResult::ParsedProfilingNoiseResult(
    ProfilingNoiseBindings bindings, std::string bindings_sha256,
    std::uint64_t n_gtt_bytes,
    std::uint64_t read_skew_uncertainty_bytes,
    std::string trace_provenance_sha256,
    std::string checksum_sha256, std::string canonical_bytes)
    : bindings_(std::move(bindings)),
      bindings_sha256_(std::move(bindings_sha256)),
      n_gtt_bytes_(n_gtt_bytes),
      read_skew_uncertainty_bytes_(read_skew_uncertainty_bytes),
      trace_provenance_sha256_(std::move(trace_provenance_sha256)),
      checksum_sha256_(std::move(checksum_sha256)),
      canonical_bytes_(std::move(canonical_bytes)) {}

const ProfilingNoiseBindings &
ParsedProfilingNoiseResult::bindings() const noexcept {
    return bindings_;
}

std::string_view
ParsedProfilingNoiseResult::bindings_sha256() const noexcept {
    return bindings_sha256_;
}

std::uint64_t ParsedProfilingNoiseResult::n_gtt_bytes() const noexcept {
    return n_gtt_bytes_;
}

std::uint64_t
ParsedProfilingNoiseResult::read_skew_uncertainty_bytes() const noexcept {
    return read_skew_uncertainty_bytes_;
}

std::string_view
ParsedProfilingNoiseResult::trace_provenance_sha256() const noexcept {
    return trace_provenance_sha256_;
}

std::string_view
ParsedProfilingNoiseResult::checksum_sha256() const noexcept {
    return checksum_sha256_;
}

std::string_view
ParsedProfilingNoiseResult::canonical_bytes() const noexcept {
    return canonical_bytes_;
}

bool ProfilingNoiseProductionResult::accepted() const noexcept {
    return status == ProfilingNoiseProductionStatus::Accepted &&
           result.has_value();
}

bool ProfilingNoiseParseResult::accepted() const noexcept {
    return status == ProfilingNoiseParseStatus::Accepted &&
           result.has_value();
}

ProfilingNoiseParseResult
parse_profiling_noise_result(std::string_view bytes) {
    try {
        const auto document = parse_json(bytes);
        require_exact_keys(
            document,
            {"bindings", "bindings_sha256", "checksum_sha256", "n_gtt_bytes",
             "read_skew_uncertainty_bytes", "schema",
             "trace_provenance_sha256"},
            "noise result");

        parse_schema(required(document, "schema"));
        auto bindings = parse_bindings(required(document, "bindings"));
        const auto bindings_sha256 = require_string(
            required(document, "bindings_sha256"), "bindings digest");
        const auto checksum = require_string(
            required(document, "checksum_sha256"), "noise result checksum");
        require_digest(bindings_sha256, "bindings digest");
        require_digest(checksum, "noise result checksum");
        const auto n_gtt_bytes =
            require_u64(required(document, "n_gtt_bytes"), "N_gtt");
        const auto trace_provenance_sha256 = require_string(
            required(document, "trace_provenance_sha256"),
            "trace provenance digest");
        const auto read_skew_uncertainty_bytes = require_u64(
            required(document, "read_skew_uncertainty_bytes"),
            "read/skew uncertainty");
        require_digest(trace_provenance_sha256, "trace provenance digest");

        const auto binding_payload = bindings_document(bindings);
        const auto expected_bindings_sha256 =
            domain_digest(profiling_noise_bindings_domain,
                          binding_payload.dump());
        if (!expected_bindings_sha256) {
            return {ProfilingNoiseParseStatus::DigestUnavailable,
                    "noise bindings SHA-256 is unavailable", std::nullopt};
        }
        if (bindings_sha256 != *expected_bindings_sha256) {
            reject_parse(ProfilingNoiseParseStatus::DigestMismatch,
                         "noise bindings digest does not match");
        }

        json payload{
            {"bindings", binding_payload},
            {"bindings_sha256", bindings_sha256},
            {"n_gtt_bytes", n_gtt_bytes},
            {"read_skew_uncertainty_bytes",
             read_skew_uncertainty_bytes},
            {"schema", json{{"major", 1}, {"minor", 0}}},
            {"trace_provenance_sha256", trace_provenance_sha256},
        };
        const auto expected_checksum =
            domain_digest(profiling_noise_result_domain, payload.dump());
        if (!expected_checksum) {
            return {ProfilingNoiseParseStatus::DigestUnavailable,
                    "noise result SHA-256 is unavailable", std::nullopt};
        }
        if (checksum != *expected_checksum) {
            reject_parse(ProfilingNoiseParseStatus::DigestMismatch,
                         "noise result checksum does not match");
        }

        payload["checksum_sha256"] = checksum;
        auto canonical_bytes = payload.dump();
        if (bytes != canonical_bytes) {
            reject_parse(ProfilingNoiseParseStatus::NonCanonical,
                         "noise result is not canonical JSON");
        }

        return {
            ProfilingNoiseParseStatus::Accepted,
            {},
            ParsedProfilingNoiseResult(
                std::move(bindings), bindings_sha256, n_gtt_bytes,
                read_skew_uncertainty_bytes, trace_provenance_sha256,
                checksum, std::move(canonical_bytes)),
        };
    } catch (const NoiseParseFailure &failure) {
        return {failure.status(), bounded_diagnostic(failure.what()),
                std::nullopt};
    } catch (...) {
        return {ProfilingNoiseParseStatus::InvalidValue,
                "noise result validation failed closed", std::nullopt};
    }
}

ProfilingNoiseProductionResult
produce_no_target_gtt_noise(const ProfilingNoTargetGttTrace &trace) {
    try {
        if (!bindings_are_valid(trace.bindings)) {
            return reject(ProfilingNoiseProductionStatus::InvalidTrace,
                          "noise trace bindings are invalid");
        }

        const auto trace_duration =
            elapsed_between(trace.started_at, trace.exact_end);
        if (!trace_duration ||
            *trace_duration != std::chrono::duration_cast<
                                   std::chrono::steady_clock::duration>(
                                   profiling_noise_trace_duration)) {
            return reject(ProfilingNoiseProductionStatus::InvalidTrace,
                          "noise trace duration is invalid");
        }
        if (trace.readings.empty()) {
            return reject(ProfilingNoiseProductionStatus::InvalidTrace,
                          "noise trace has no scheduled readings");
        }
        if (trace.readings.size() >
            profiling_noise_maximum_trace_points) {
            return reject(ProfilingNoiseProductionStatus::InvalidTrace,
                          "noise trace exceeds the supported point limit");
        }

        const auto max_gap =
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                profiling_noise_max_read_start_gap);
        for (std::size_t index = 0; index < trace.readings.size(); ++index) {
            const auto &reading = trace.readings[index];
            if (!reading.gtt_used_bytes ||
                !bindings_equal(trace.bindings, reading.observed_bindings)) {
                return reject(ProfilingNoiseProductionStatus::InvalidTrace,
                              "noise trace contains an invalid reading");
            }
            const auto schedule_delay = elapsed_between(
                reading.scheduled_at, reading.read_started_at);
            const auto read_duration = elapsed_between(
                reading.read_started_at, reading.read_finished_at);
            if (!schedule_delay || !read_duration ||
                reading.scheduled_at < trace.started_at ||
                reading.scheduled_at >= trace.exact_end ||
                reading.read_started_at >= trace.exact_end ||
                reading.read_finished_at > trace.exact_end) {
                return reject(ProfilingNoiseProductionStatus::InvalidTrace,
                              "noise trace reading times are invalid");
            }

            if (index == 0) {
                if (reading.scheduled_at != trace.started_at ||
                    *schedule_delay > max_gap) {
                    return reject(
                        ProfilingNoiseProductionStatus::InvalidTrace,
                        "noise trace does not begin at its first schedule");
                }
            } else {
                const auto scheduled_gap = elapsed_between(
                    trace.readings[index - 1].scheduled_at,
                    reading.scheduled_at);
                const auto read_gap = elapsed_between(
                    trace.readings[index - 1].read_started_at,
                    reading.read_started_at);
                if (!scheduled_gap ||
                    *scheduled_gap <=
                        std::chrono::steady_clock::duration::zero() ||
                    *scheduled_gap > max_gap || !read_gap ||
                    *read_gap <= std::chrono::steady_clock::duration::zero() ||
                    *read_gap > max_gap) {
                    return reject(
                        ProfilingNoiseProductionStatus::InvalidTrace,
                        "noise trace cadence or read gap is invalid");
                }
            }
        }

        const auto final_schedule_gap = elapsed_between(
            trace.readings.back().scheduled_at, trace.exact_end);
        const auto final_read_gap = elapsed_between(
            trace.readings.back().read_started_at, trace.exact_end);
        if (!final_schedule_gap ||
            *final_schedule_gap <=
                std::chrono::steady_clock::duration::zero() ||
            *final_schedule_gap > max_gap || !final_read_gap ||
            *final_read_gap <= std::chrono::steady_clock::duration::zero() ||
            *final_read_gap > max_gap) {
            return reject(ProfilingNoiseProductionStatus::InvalidTrace,
                          "noise trace does not reach its exact boundary");
        }

        const auto window_duration =
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                profiling_noise_window);
        bool found_eligible_window = false;
        std::uint64_t largest_range = 0;
        std::uint64_t greatest_window_minimum = 0;
        std::uint64_t least_window_maximum =
            std::numeric_limits<std::uint64_t>::max();
        std::size_t first_point = 0;
        std::size_t past_last_point = 0;
        std::deque<std::size_t> minimum_points;
        std::deque<std::size_t> maximum_points;
        for (std::size_t first = 0; first < trace.readings.size(); ++first) {
            const auto remaining = elapsed_between(
                trace.readings[first].scheduled_at, trace.exact_end);
            if (!remaining || *remaining < window_duration) continue;

            found_eligible_window = true;
            const auto window_start = trace.readings[first].scheduled_at;
            while (first_point < trace.readings.size() &&
                   trace.readings[first_point].read_started_at <
                       window_start) {
                if (!minimum_points.empty() &&
                    minimum_points.front() == first_point) {
                    minimum_points.pop_front();
                }
                if (!maximum_points.empty() &&
                    maximum_points.front() == first_point) {
                    maximum_points.pop_front();
                }
                ++first_point;
            }
            if (past_last_point < first_point) {
                past_last_point = first_point;
            }

            while (past_last_point < trace.readings.size()) {
                const auto offset = elapsed_between(
                    window_start,
                    trace.readings[past_last_point].read_started_at);
                if (!offset) {
                    return reject(
                        ProfilingNoiseProductionStatus::InvalidTrace,
                        "noise trace read order is invalid");
                }
                if (*offset >= window_duration) break;

                const auto value =
                    *trace.readings[past_last_point].gtt_used_bytes;
                while (!minimum_points.empty() &&
                       *trace.readings[minimum_points.back()]
                            .gtt_used_bytes >= value) {
                    minimum_points.pop_back();
                }
                minimum_points.push_back(past_last_point);
                while (!maximum_points.empty() &&
                       *trace.readings[maximum_points.back()]
                            .gtt_used_bytes <= value) {
                    maximum_points.pop_back();
                }
                maximum_points.push_back(past_last_point);
                ++past_last_point;
            }

            const auto point_count = past_last_point - first_point;
            if (point_count < profiling_noise_minimum_window_points) {
                return reject(
                    ProfilingNoiseProductionStatus::InvalidTrace,
                    "noise trace has an invalid eligible window");
            }
            const auto minimum =
                *trace.readings[minimum_points.front()].gtt_used_bytes;
            const auto maximum =
                *trace.readings[maximum_points.front()].gtt_used_bytes;
            largest_range = std::max(largest_range, maximum - minimum);
            greatest_window_minimum =
                std::max(greatest_window_minimum, minimum);
            least_window_maximum =
                std::min(least_window_maximum, maximum);
        }
        if (!found_eligible_window) {
            return reject(ProfilingNoiseProductionStatus::InvalidTrace,
                          "noise trace has no eligible complete window");
        }

        if (least_window_maximum >
            std::numeric_limits<std::uint64_t>::max() -
                trace.read_skew_uncertainty_bytes) {
            return reject(ProfilingNoiseProductionStatus::ArithmeticOverflow,
                          "noise trend arithmetic overflowed");
        }
        if (greatest_window_minimum >
            least_window_maximum +
                trace.read_skew_uncertainty_bytes) {
            return reject(ProfilingNoiseProductionStatus::InvalidTrace,
                          "noise trace has an unexplained trend");
        }
        if (largest_range >
            std::numeric_limits<std::uint64_t>::max() -
                trace.read_skew_uncertainty_bytes) {
            return reject(ProfilingNoiseProductionStatus::ArithmeticOverflow,
                          "noise bound arithmetic overflowed");
        }
        const auto n_gtt_bytes =
            largest_range + trace.read_skew_uncertainty_bytes;

        const auto trace_provenance_sha256 =
            trace_provenance_digest(trace);
        if (!trace_provenance_sha256) {
            return reject(ProfilingNoiseProductionStatus::DigestUnavailable,
                          "noise trace SHA-256 is unavailable");
        }
        const auto binding_payload = bindings_document(trace.bindings);
        const auto binding_payload_bytes = binding_payload.dump();
        const auto bindings_sha256 =
            domain_digest(profiling_noise_bindings_domain,
                          binding_payload_bytes);
        if (!bindings_sha256) {
            return reject(ProfilingNoiseProductionStatus::DigestUnavailable,
                          "noise bindings SHA-256 is unavailable");
        }

        json payload{
            {"bindings", binding_payload},
            {"bindings_sha256", *bindings_sha256},
            {"n_gtt_bytes", n_gtt_bytes},
            {"read_skew_uncertainty_bytes",
             trace.read_skew_uncertainty_bytes},
            {"schema", json{{"major", 1}, {"minor", 0}}},
            {"trace_provenance_sha256", *trace_provenance_sha256},
        };
        const auto payload_bytes = payload.dump();
        const auto checksum =
            domain_digest(profiling_noise_result_domain, payload_bytes);
        if (!checksum) {
            return reject(ProfilingNoiseProductionStatus::DigestUnavailable,
                          "noise result SHA-256 is unavailable");
        }
        payload["checksum_sha256"] = *checksum;
        auto canonical_bytes = payload.dump();

        return {
            ProfilingNoiseProductionStatus::Accepted,
            {},
            ParsedProfilingNoiseResult(
                trace.bindings, *bindings_sha256, n_gtt_bytes,
                trace.read_skew_uncertainty_bytes,
                *trace_provenance_sha256, *checksum,
                std::move(canonical_bytes)),
        };
    } catch (...) {
        return reject(ProfilingNoiseProductionStatus::EvidenceUnavailable,
                      "noise production failed closed");
    }
}

} // namespace lemon::residency
