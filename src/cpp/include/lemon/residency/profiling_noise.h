#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lemon::residency {

inline constexpr std::chrono::milliseconds profiling_noise_nominal_cadence{50};
inline constexpr std::chrono::milliseconds profiling_noise_max_read_start_gap{
    100};
inline constexpr std::chrono::seconds profiling_noise_window{5};
inline constexpr std::chrono::minutes profiling_noise_trace_duration{15};
inline constexpr std::size_t profiling_noise_minimum_window_points = 50;

struct ProfilingNoiseBindings {
    std::string deployment_id;
    std::string deployment_epoch_sha256;
    std::string boot_id_sha256;
    std::string device_identity_sha256;
    std::string topology_sha256;
    std::string kernel_identity_sha256;
    std::string driver_identity_sha256;
    std::string counter_source_id;
    std::string counter_source_revision_sha256;
    std::string campaign_contract_sha256;
    std::string procedure_revision_sha256;
    std::string background_inventory_sha256;
};

struct ProfilingNoTargetGttReading {
    std::chrono::steady_clock::time_point scheduled_at;
    std::chrono::steady_clock::time_point read_started_at;
    std::chrono::steady_clock::time_point read_finished_at;
    std::optional<std::uint64_t> gtt_used_bytes;
    ProfilingNoiseBindings observed_bindings;
};

struct ProfilingNoTargetGttTrace {
    std::chrono::steady_clock::time_point started_at;
    std::chrono::steady_clock::time_point exact_end;
    ProfilingNoiseBindings bindings;
    std::uint64_t read_skew_uncertainty_bytes = 0;
    std::vector<ProfilingNoTargetGttReading> readings;
};

enum class ProfilingNoiseProductionStatus {
    Accepted,
    EvidenceUnavailable,
    InvalidTrace,
    ArithmeticOverflow,
    DigestUnavailable,
};

enum class ProfilingNoiseParseStatus {
    Accepted,
    InputTooLarge,
    MalformedJson,
    NonCanonical,
    UnsupportedSchema,
    InvalidIdentifier,
    UnknownField,
    InvalidValue,
    DigestMismatch,
    DigestUnavailable,
};

struct ProfilingNoiseProductionResult;
struct ProfilingNoiseParseResult;

class ParsedProfilingNoiseResult {
public:
    ParsedProfilingNoiseResult() = delete;
    ParsedProfilingNoiseResult(const ParsedProfilingNoiseResult &) = default;
    ParsedProfilingNoiseResult(ParsedProfilingNoiseResult &&) noexcept = default;
    ParsedProfilingNoiseResult &
    operator=(const ParsedProfilingNoiseResult &) = default;
    ParsedProfilingNoiseResult &
    operator=(ParsedProfilingNoiseResult &&) noexcept = default;

    const ProfilingNoiseBindings &bindings() const noexcept;
    std::string_view bindings_sha256() const noexcept;
    std::uint64_t n_gtt_bytes() const noexcept;
    std::string_view trace_provenance_sha256() const noexcept;
    std::string_view checksum_sha256() const noexcept;
    std::string_view canonical_bytes() const noexcept;

private:
    ParsedProfilingNoiseResult(ProfilingNoiseBindings bindings,
                               std::string bindings_sha256,
                               std::uint64_t n_gtt_bytes,
                               std::string trace_provenance_sha256,
                               std::string checksum_sha256,
                               std::string canonical_bytes);

    ProfilingNoiseBindings bindings_;
    std::string bindings_sha256_;
    std::uint64_t n_gtt_bytes_ = 0;
    std::string trace_provenance_sha256_;
    std::string checksum_sha256_;
    std::string canonical_bytes_;

    friend ProfilingNoiseProductionResult
    produce_no_target_gtt_noise(const ProfilingNoTargetGttTrace &trace);
    friend ProfilingNoiseParseResult
    parse_profiling_noise_result(std::string_view bytes);
};

struct ProfilingNoiseProductionResult {
    ProfilingNoiseProductionStatus status =
        ProfilingNoiseProductionStatus::EvidenceUnavailable;
    std::string diagnostic;
    std::optional<ParsedProfilingNoiseResult> result;

    bool accepted() const noexcept;
};

struct ProfilingNoiseParseResult {
    ProfilingNoiseParseStatus status = ProfilingNoiseParseStatus::InvalidValue;
    std::string diagnostic;
    std::optional<ParsedProfilingNoiseResult> result;

    bool accepted() const noexcept;
};

ProfilingNoiseProductionResult
produce_no_target_gtt_noise(const ProfilingNoTargetGttTrace &trace);
ProfilingNoiseParseResult
parse_profiling_noise_result(std::string_view bytes);

} // namespace lemon::residency
