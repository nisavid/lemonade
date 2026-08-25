#include "lemon/residency/profiling_provider.h"

#include <mbedtls/md.h>
#include <mbedtls/version.h>
#if MBEDTLS_VERSION_MAJOR >= 4
#include <psa/crypto.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <ctime>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <ratio>
#include <set>
#include <sstream>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace lemon::residency {
namespace {

using SteadyClock = std::chrono::steady_clock;
using SystemClock = std::chrono::system_clock;

struct SensorValues {
    std::optional<std::uint64_t> total;
    std::map<std::string, std::uint64_t> owners;
    std::uint64_t attributed_total = 0;
};

std::string bounded_diagnostic(std::string value) {
    if (value.size() <= max_local_overlay_diagnostic_bytes) return value;
    std::size_t boundary = max_local_overlay_diagnostic_bytes;
    while (boundary > 0 &&
           (static_cast<unsigned char>(value[boundary]) & 0xc0u) == 0x80u) {
        --boundary;
    }
    value.resize(boundary);
    return value;
}

ProfilingObservationCollectionResult
result_for(ProfilingCollectionStatus status, std::string diagnostic = {}) {
    return ProfilingObservationCollectionResult{
        status,
        bounded_diagnostic(std::move(diagnostic)),
        std::nullopt,
    };
}

bool cancelled(const ProfilingCancellationCheck &should_abort) noexcept {
    if (!should_abort) return false;
    try {
        return should_abort();
    } catch (...) {
        return true;
    }
}

std::optional<std::size_t> family_index(ClaimFamily family) noexcept {
    switch (family) {
    case ClaimFamily::ConsumableCapacity:
        return 0;
    case ClaimFamily::SafetyFloor:
        return 1;
    case ClaimFamily::CardinalityPool:
        return 2;
    case ClaimFamily::CompatibilityExclusivity:
        return 3;
    }
    return std::nullopt;
}

ClaimFamily family_at(std::size_t index) noexcept {
    static constexpr std::array<ClaimFamily, claim_family_count> families{
        ClaimFamily::ConsumableCapacity,
        ClaimFamily::SafetyFloor,
        ClaimFamily::CardinalityPool,
        ClaimFamily::CompatibilityExclusivity,
    };
    return families[index];
}

std::optional<ClaimUnit> expected_unit(ClaimFamily family) noexcept {
    switch (family) {
    case ClaimFamily::ConsumableCapacity:
    case ClaimFamily::SafetyFloor:
        return ClaimUnit::Bytes;
    case ClaimFamily::CardinalityPool:
    case ClaimFamily::CompatibilityExclusivity:
        return ClaimUnit::Count;
    }
    return std::nullopt;
}

std::string_view claim_family_wire(ClaimFamily family) noexcept {
    switch (family) {
    case ClaimFamily::ConsumableCapacity:
        return "consumable_capacity";
    case ClaimFamily::SafetyFloor:
        return "safety_floor";
    case ClaimFamily::CardinalityPool:
        return "cardinality_pool";
    case ClaimFamily::CompatibilityExclusivity:
        return "compatibility_exclusivity";
    }
    return {};
}

std::string_view claim_unit_wire(ClaimUnit unit) noexcept {
    switch (unit) {
    case ClaimUnit::Bytes:
        return "bytes";
    case ClaimUnit::Count:
        return "count";
    }
    return {};
}

bool identifier_is_valid(std::string_view value) noexcept {
    return !value.empty() &&
           value.size() <= max_local_overlay_identifier_bytes &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return character >= 0x21 && character <= 0x7e;
           });
}

bool digest_is_valid(std::string_view value) noexcept {
    return value.size() == 64 &&
           std::all_of(value.begin(), value.end(), [](char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

bool contract_is_valid(const ProfilingDerivationContract &contract) {
    if (!identifier_is_valid(contract.provider_id) ||
        !digest_is_valid(contract.provider_revision_sha256) ||
        contract.sensors.empty() ||
        contract.sensors.size() > max_journal_array_entries ||
        contract.owner_scope_ids.empty() ||
        contract.owner_scope_ids.size() > max_journal_array_entries ||
        contract.freshness_window <= std::chrono::seconds::zero() ||
        contract.max_source_skew <= std::chrono::milliseconds::zero() ||
        contract.max_source_skew.count() / 1000 >=
            contract.freshness_window.count()) {
        return false;
    }

    std::set<std::string> sensor_ids;
    std::set<std::string> constraint_ids;
    for (const auto &sensor : contract.sensors) {
        if (!identifier_is_valid(sensor.sensor_id) ||
            !identifier_is_valid(sensor.constraint_id) ||
            sensor.claim_family != ClaimFamily::ConsumableCapacity ||
            sensor.safety_ceiling <= sensor.uncertainty_bound ||
            !sensor_ids.insert(sensor.sensor_id).second ||
            !constraint_ids.insert(sensor.constraint_id).second) {
            return false;
        }
    }

    std::set<std::string> owner_scope_ids;
    for (const auto &owner_scope_id : contract.owner_scope_ids) {
        if (!identifier_is_valid(owner_scope_id) ||
            !owner_scope_ids.insert(owner_scope_id).second) {
            return false;
        }
    }
    return true;
}

template <typename Rep>
std::optional<std::int64_t> count_as_i64(Rep value) noexcept {
    static_assert(std::is_integral_v<Rep>);
    if constexpr (std::is_signed_v<Rep>) {
        if constexpr (std::numeric_limits<Rep>::digits >
                      std::numeric_limits<std::int64_t>::digits) {
            if (value < static_cast<Rep>(
                            std::numeric_limits<std::int64_t>::min()) ||
                value > static_cast<Rep>(
                            std::numeric_limits<std::int64_t>::max())) {
                return std::nullopt;
            }
        }
    } else if constexpr (std::numeric_limits<Rep>::digits >=
                         std::numeric_limits<std::int64_t>::digits) {
        if (value >
            static_cast<Rep>(std::numeric_limits<std::int64_t>::max())) {
            return std::nullopt;
        }
    }
    return static_cast<std::int64_t>(value);
}

std::optional<std::int64_t>
system_clock_seconds(SystemClock::time_point value) noexcept {
    using Scale = std::ratio_divide<SystemClock::duration::period,
                                    std::chrono::seconds::period>;
    const auto ticks = count_as_i64(value.time_since_epoch().count());
    if (!ticks) return std::nullopt;

    static_assert(Scale::num > 0 && Scale::den > 0);
    const auto numerator = static_cast<std::int64_t>(Scale::num);
    const auto floor_divide = [](std::int64_t dividend) noexcept {
        constexpr auto denominator = static_cast<std::int64_t>(Scale::den);
        auto quotient = dividend / denominator;
        if (dividend % denominator < 0) --quotient;
        return quotient;
    };
    if (numerator == 1) return floor_divide(*ticks);
    if ((*ticks > 0 &&
         *ticks > std::numeric_limits<std::int64_t>::max() / numerator) ||
        (*ticks < 0 &&
         *ticks < std::numeric_limits<std::int64_t>::min() / numerator)) {
        return std::nullopt;
    }
    return floor_divide(*ticks * numerator);
}

bool system_clock_contains_seconds(std::int64_t value) noexcept {
    const auto minimum = system_clock_seconds(SystemClock::time_point::min());
    const auto maximum = system_clock_seconds(SystemClock::time_point::max());
    return minimum && maximum && value >= *minimum && value <= *maximum;
}

std::optional<std::int64_t> checked_add(std::int64_t left,
                                        std::int64_t right) noexcept {
    if ((right > 0 &&
         left > std::numeric_limits<std::int64_t>::max() - right) ||
        (right < 0 &&
         left < std::numeric_limits<std::int64_t>::min() - right)) {
        return std::nullopt;
    }
    return left + right;
}

std::string format_utc(std::int64_t seconds) {
    std::time_t time{};
    if constexpr (std::is_signed_v<std::time_t>) {
        if constexpr (std::numeric_limits<std::time_t>::digits <
                      std::numeric_limits<std::int64_t>::digits) {
            if (seconds < static_cast<std::int64_t>(
                              std::numeric_limits<std::time_t>::min()) ||
                seconds > static_cast<std::int64_t>(
                              std::numeric_limits<std::time_t>::max())) {
                return {};
            }
        }
        time = static_cast<std::time_t>(seconds);
    } else {
        if (seconds < 0 || static_cast<std::uint64_t>(seconds) >
                               std::numeric_limits<std::time_t>::max()) {
            return {};
        }
        time = static_cast<std::time_t>(seconds);
    }
    std::tm utc{};
#ifdef _WIN32
    if (gmtime_s(&utc, &time) != 0) return {};
#else
    if (gmtime_r(&time, &utc) == nullptr) return {};
#endif
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    auto result = output ? output.str() : std::string{};
    return result.size() == 20 ? result : std::string{};
}

void append_u64(std::string &bytes, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<char>((value >> shift) & 0xffu));
    }
}

void append_string(std::string &bytes, std::string_view value) {
    append_u64(bytes, static_cast<std::uint64_t>(value.size()));
    bytes.append(value.data(), value.size());
}

std::optional<std::string> sha256_hex(std::string_view bytes) {
#if MBEDTLS_VERSION_MAJOR >= 4
    static std::once_flag initialized;
    static psa_status_t initialization_status = PSA_ERROR_BAD_STATE;
    std::call_once(initialized,
                   [] { initialization_status = psa_crypto_init(); });
    if (initialization_status != PSA_SUCCESS) return std::nullopt;
#endif

    const auto *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == nullptr) return std::nullopt;

    mbedtls_md_context_t context;
    mbedtls_md_init(&context);
    std::array<unsigned char, 32> digest{};
    const bool failed =
        mbedtls_md_setup(&context, info, 0) != 0 ||
        mbedtls_md_starts(&context) != 0 ||
        mbedtls_md_update(&context,
                          reinterpret_cast<const unsigned char *>(bytes.data()),
                          bytes.size()) != 0 ||
        mbedtls_md_finish(&context, digest.data()) != 0;
    mbedtls_md_free(&context);
    if (failed) return std::nullopt;

    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const auto byte : digest) {
        result.push_back(hex[(byte >> 4) & 0x0f]);
        result.push_back(hex[byte & 0x0f]);
    }
    return result;
}

std::optional<std::string>
derivation_contract_digest(const ProfilingDerivationContract &contract) {
    if (!contract_is_valid(contract)) return std::nullopt;

    auto sensors = contract.sensors;
    std::sort(sensors.begin(), sensors.end(),
              [](const ProfilingSensorContract &left,
                 const ProfilingSensorContract &right) {
                  return std::tie(left.sensor_id, left.constraint_id,
                                  left.claim_family, left.uncertainty_bound,
                                  left.safety_ceiling) <
                         std::tie(right.sensor_id, right.constraint_id,
                                  right.claim_family, right.uncertainty_bound,
                                  right.safety_ceiling);
              });
    auto owner_scope_ids = contract.owner_scope_ids;
    std::sort(owner_scope_ids.begin(), owner_scope_ids.end());

    std::string bytes = "lemonade/profiling-derivation-contract/v1";
    append_string(bytes, contract.provider_id);
    append_string(bytes, contract.provider_revision_sha256);
    append_u64(bytes, static_cast<std::uint64_t>(sensors.size()));
    for (const auto &sensor : sensors) {
        const auto unit = expected_unit(sensor.claim_family);
        if (!unit) return std::nullopt;
        append_string(bytes, sensor.sensor_id);
        append_string(bytes, sensor.constraint_id);
        append_string(bytes, claim_family_wire(sensor.claim_family));
        append_string(bytes, claim_unit_wire(*unit));
        append_u64(bytes, sensor.uncertainty_bound);
        append_u64(bytes, sensor.safety_ceiling);
    }
    append_u64(bytes, static_cast<std::uint64_t>(owner_scope_ids.size()));
    for (const auto &owner_scope_id : owner_scope_ids) {
        append_string(bytes, owner_scope_id);
    }
    append_u64(bytes,
               static_cast<std::uint64_t>(contract.freshness_window.count()));
    append_u64(bytes,
               static_cast<std::uint64_t>(contract.max_source_skew.count()));
    return sha256_hex(bytes);
}

std::optional<std::string>
raw_provenance_digest(const ProfilingTransactionContext &context,
                      const std::vector<ProfilingRawSample> &input_samples,
                      std::string_view observed_at) {
    auto samples = input_samples;
    std::sort(
        samples.begin(), samples.end(),
        [](const ProfilingRawSample &left, const ProfilingRawSample &right) {
            return std::tie(left.sensor_id, left.owner_scope_id, left.value,
                            left.source_generation) <
                   std::tie(right.sensor_id, right.owner_scope_id, right.value,
                            right.source_generation);
        });

    std::string bytes = "lemonade/profiling-raw-observation/v1";
    append_string(bytes, context.deployment_id);
    append_string(bytes, context.profiling_transaction_id);
    append_string(bytes, context.selector_sha256);
    append_string(bytes, context.observation_contract_sha256);
    append_string(bytes, observed_at);
    append_u64(bytes, static_cast<std::uint64_t>(samples.size()));
    for (const auto &sample : samples) {
        append_string(bytes, sample.sensor_id);
        bytes.push_back(sample.owner_scope_id.has_value() ? '\1' : '\0');
        if (sample.owner_scope_id) append_string(bytes, *sample.owner_scope_id);
        append_u64(bytes, sample.value);
        append_u64(bytes, sample.source_generation);
    }
    return sha256_hex(bytes);
}

using ClaimEntries = std::array<std::vector<ClaimAmount>, claim_family_count>;

std::vector<ClaimFamilyClosure>
claim_closure(const ProfilingDerivationContract &contract,
              const std::vector<std::uint64_t> &amounts) {
    ClaimEntries entries;
    std::array<bool, claim_family_count> represented{};
    for (std::size_t index = 0; index < contract.sensors.size(); ++index) {
        const auto &sensor = contract.sensors[index];
        const auto family = family_index(sensor.claim_family);
        if (!family) return {};
        represented[*family] = true;
        if (amounts[index] > 0) {
            const auto unit = expected_unit(sensor.claim_family);
            if (!unit) return {};
            entries[*family].push_back(
                ClaimAmount{sensor.constraint_id, *unit, amounts[index]});
        }
    }

    std::vector<ClaimFamilyClosure> result;
    result.reserve(claim_family_count);
    for (std::size_t index = 0; index < claim_family_count; ++index) {
        auto completeness = ClaimCompleteness::NotApplicable;
        if (represented[index]) {
            completeness = entries[index].empty() ? ClaimCompleteness::KnownZero
                                                  : ClaimCompleteness::Bounded;
        }
        result.push_back(ClaimFamilyClosure{family_at(index), completeness,
                                            std::move(entries[index])});
    }
    return result;
}

std::optional<std::uint64_t>
source_generation(const std::vector<ProfilingRawSample> &samples) noexcept {
    if (samples.empty() || samples.front().source_generation == 0) {
        return std::nullopt;
    }
    const auto generation = samples.front().source_generation;
    for (const auto &sample : samples) {
        if (sample.source_generation != generation) return std::nullopt;
    }
    return generation;
}

std::optional<std::vector<SensorValues>>
reconcile_samples(const ProfilingDerivationContract &contract,
                  const std::vector<ProfilingRawSample> &samples) {
    if (contract.sensors.size() > std::numeric_limits<std::size_t>::max() /
                                      (contract.owner_scope_ids.size() + 1)) {
        return std::nullopt;
    }
    const auto expected_count =
        contract.sensors.size() * (contract.owner_scope_ids.size() + 1);
    if (samples.size() != expected_count) return std::nullopt;

    std::map<std::string, std::size_t> sensor_indices;
    for (std::size_t index = 0; index < contract.sensors.size(); ++index) {
        sensor_indices.emplace(contract.sensors[index].sensor_id, index);
    }
    const std::set<std::string> owner_scope_ids(
        contract.owner_scope_ids.begin(), contract.owner_scope_ids.end());

    std::vector<SensorValues> values(contract.sensors.size());
    for (const auto &sample : samples) {
        const auto sensor = sensor_indices.find(sample.sensor_id);
        if (sensor == sensor_indices.end()) return std::nullopt;
        auto &sensor_values = values[sensor->second];
        if (!sample.owner_scope_id) {
            if (sensor_values.total.has_value()) return std::nullopt;
            sensor_values.total = sample.value;
            continue;
        }
        if (owner_scope_ids.count(*sample.owner_scope_id) == 0 ||
            !sensor_values.owners.emplace(*sample.owner_scope_id, sample.value)
                 .second) {
            return std::nullopt;
        }
    }

    for (auto &sensor_values : values) {
        if (!sensor_values.total ||
            sensor_values.owners.size() != contract.owner_scope_ids.size()) {
            return std::nullopt;
        }
        for (const auto &owner_scope_id : contract.owner_scope_ids) {
            const auto owner = sensor_values.owners.find(owner_scope_id);
            if (owner == sensor_values.owners.end() ||
                owner->second > std::numeric_limits<std::uint64_t>::max() -
                                    sensor_values.attributed_total) {
                return std::nullopt;
            }
            sensor_values.attributed_total += owner->second;
        }
        if (sensor_values.attributed_total != *sensor_values.total) {
            return std::nullopt;
        }
    }
    return values;
}

std::optional<SteadyClock::duration>
elapsed_between(SteadyClock::time_point started,
                SteadyClock::time_point finished) noexcept {
    using Rep = SteadyClock::duration::rep;
    static_assert(std::is_integral_v<Rep>);

    const auto start = count_as_i64(started.time_since_epoch().count());
    const auto finish = count_as_i64(finished.time_since_epoch().count());
    if (!start || !finish || *finish < *start) return std::nullopt;

    std::uint64_t ticks = 0;
    if (*start < 0 && *finish >= 0) {
        const auto magnitude =
            static_cast<std::uint64_t>(-(*start + 1)) + 1;
        ticks = magnitude + static_cast<std::uint64_t>(*finish);
    } else {
        ticks = static_cast<std::uint64_t>(*finish - *start);
    }
    if constexpr (std::is_signed_v<Rep>) {
        if (ticks > static_cast<std::uint64_t>(
                        std::numeric_limits<Rep>::max())) {
            return std::nullopt;
        }
    } else if constexpr (std::numeric_limits<Rep>::digits <
                         std::numeric_limits<std::uint64_t>::digits) {
        if (ticks > static_cast<std::uint64_t>(
                        std::numeric_limits<Rep>::max())) {
            return std::nullopt;
        }
    }
    return SteadyClock::duration(static_cast<Rep>(ticks));
}

std::optional<std::uint64_t>
rounded_up_milliseconds(SteadyClock::duration elapsed) noexcept {
    using Scale = std::ratio_divide<SteadyClock::duration::period,
                                    std::chrono::milliseconds::period>;
    const auto ticks = count_as_i64(elapsed.count());
    if (!ticks || *ticks < 0) return std::nullopt;

    static_assert(Scale::num > 0 && Scale::den > 0);
    const auto numerator = static_cast<std::uint64_t>(Scale::num);
    const auto denominator = static_cast<std::uint64_t>(Scale::den);
    const auto unsigned_ticks = static_cast<std::uint64_t>(*ticks);
    if (unsigned_ticks >
        std::numeric_limits<std::uint64_t>::max() / numerator) {
        return std::nullopt;
    }
    const auto scaled = unsigned_ticks * numerator;
    auto milliseconds = scaled / denominator;
    if (scaled % denominator != 0) {
        if (milliseconds == std::numeric_limits<std::uint64_t>::max()) {
            return std::nullopt;
        }
        ++milliseconds;
    }
    return milliseconds;
}

} // namespace

std::optional<std::string> profiling_derivation_contract_sha256(
    const ProfilingDerivationContract &contract) {
    return derivation_contract_digest(contract);
}

ProfilingRawReadResult UnavailableProfilingObservationSource::read(
    const ProfilingRawReadRequest &, const ProfilingCancellationCheck &) {
    return ProfilingRawReadResult{
        ProfilingSourceError::Unavailable,
        {},
        "profiling observation source is unavailable",
    };
}

bool ProfilingObservationCollectionResult::accepted() const noexcept {
    return status == ProfilingCollectionStatus::Accepted &&
           observation.has_value();
}

ProfilingObservationCollector::ProfilingObservationCollector(
    ProfilingDerivationContract contract, ProfilingCollectionClock clock)
    : contract_(std::move(contract)), source_(&unavailable_source_),
      clock_(std::move(clock)) {
    if (!clock_.monotonic_now) {
        clock_.monotonic_now = [] { return SteadyClock::now(); };
    }
    if (!clock_.utc_now) clock_.utc_now = [] { return SystemClock::now(); };
}

ProfilingObservationCollector::ProfilingObservationCollector(
    ProfilingDerivationContract contract, ProfilingObservationSource &source,
    ProfilingCollectionClock clock)
    : contract_(std::move(contract)), source_(&source),
      clock_(std::move(clock)) {
    if (!clock_.monotonic_now) {
        clock_.monotonic_now = [] { return SteadyClock::now(); };
    }
    if (!clock_.utc_now) clock_.utc_now = [] { return SystemClock::now(); };
}

ProfilingObservationCollectionResult ProfilingObservationCollector::collect(
    const ProfilingTransactionContext &context,
    const ProfilingCancellationCheck &should_abort) {
    try {
        if (!contract_is_valid(contract_)) {
            return result_for(ProfilingCollectionStatus::InvalidContract,
                              "profiling derivation contract is invalid");
        }
        const auto contract_sha256 = derivation_contract_digest(contract_);
        if (!contract_sha256) {
            return result_for(
                ProfilingCollectionStatus::DigestUnavailable,
                "profiling derivation contract digest is unavailable");
        }
        if (context.observation_contract_sha256 != *contract_sha256) {
            return result_for(ProfilingCollectionStatus::InvalidContract,
                              "profiling derivation contract identity does not "
                              "match context");
        }
        if (cancelled(should_abort)) {
            return result_for(ProfilingCollectionStatus::Cancelled,
                              "profiling observation collection cancelled");
        }

        const auto started = clock_.monotonic_now();
        const auto observed_time = clock_.utc_now();
        const auto observed_seconds =
            observed_time >= SystemClock::time_point{}
                ? system_clock_seconds(observed_time)
                : std::nullopt;
        const auto freshness_seconds =
            count_as_i64(contract_.freshness_window.count());
        const auto fresh_seconds =
            observed_seconds && freshness_seconds
                ? checked_add(*observed_seconds, *freshness_seconds)
                : std::nullopt;
        const auto observed_at =
            observed_seconds ? format_utc(*observed_seconds) : std::string{};
        const auto fresh_until =
            fresh_seconds && system_clock_contains_seconds(*fresh_seconds)
                ? format_utc(*fresh_seconds)
                : std::string{};
        if (observed_at.empty() || fresh_until.empty()) {
            return result_for(ProfilingCollectionStatus::InvalidObservation,
                              "profiling collection clock is unavailable");
        }

        ProfilingRawReadRequest request;
        request.sensor_ids.reserve(contract_.sensors.size());
        for (const auto &sensor : contract_.sensors) {
            request.sensor_ids.push_back(sensor.sensor_id);
        }
        request.owner_scope_ids = contract_.owner_scope_ids;

        ProfilingRawReadResult raw;
        try {
            raw = source_->read(request, should_abort);
        } catch (...) {
            return result_for(ProfilingCollectionStatus::EvidenceUnavailable,
                              "profiling observation source failed");
        }
        const auto finished = clock_.monotonic_now();

        if (cancelled(should_abort) ||
            raw.error == ProfilingSourceError::Cancelled) {
            return result_for(ProfilingCollectionStatus::Cancelled,
                              raw.diagnostic.empty()
                                  ? "profiling observation collection cancelled"
                                  : std::move(raw.diagnostic));
        }
        if (raw.error != ProfilingSourceError::None) {
            return result_for(
                ProfilingCollectionStatus::EvidenceUnavailable,
                raw.diagnostic.empty()
                    ? "profiling observation source is unavailable"
                    : std::move(raw.diagnostic));
        }
        const auto elapsed = elapsed_between(started, finished);
        const auto skew = elapsed ? rounded_up_milliseconds(*elapsed)
                                  : std::nullopt;
        if (!raw.diagnostic.empty() || !skew) {
            return result_for(ProfilingCollectionStatus::InvalidObservation,
                              "profiling raw observation is incoherent");
        }

        if (*skew >
            static_cast<std::uint64_t>(contract_.max_source_skew.count())) {
            return result_for(ProfilingCollectionStatus::InvalidObservation,
                              "profiling source skew exceeds the contract");
        }
        const auto generation = source_generation(raw.samples);
        const auto values = reconcile_samples(contract_, raw.samples);
        if (!generation || !values) {
            return result_for(ProfilingCollectionStatus::InvalidObservation,
                              "profiling raw observation does not close");
        }

        std::vector<std::uint64_t> observed;
        std::vector<std::uint64_t> attributed;
        std::vector<std::uint64_t> uncertainty;
        std::vector<std::uint64_t> safety_margin;
        observed.reserve(values->size());
        attributed.reserve(values->size());
        uncertainty.reserve(values->size());
        safety_margin.reserve(values->size());
        for (std::size_t index = 0; index < values->size(); ++index) {
            const auto amount = *(*values)[index].total;
            const auto &sensor = contract_.sensors[index];
            if (amount >= sensor.safety_ceiling) {
                return result_for(ProfilingCollectionStatus::InvalidObservation,
                                  "profiling observation meets or exceeds its "
                                  "safety ceiling");
            }
            const auto remaining = sensor.safety_ceiling - amount;
            if (sensor.uncertainty_bound >= remaining) {
                return result_for(ProfilingCollectionStatus::InvalidObservation,
                                  "profiling safety margin is not positive");
            }
            observed.push_back(amount);
            attributed.push_back((*values)[index].attributed_total);
            uncertainty.push_back(sensor.uncertainty_bound);
            safety_margin.push_back(remaining - sensor.uncertainty_bound);
        }

        const auto provenance =
            raw_provenance_digest(context, raw.samples, observed_at);
        if (!provenance) {
            return result_for(ProfilingCollectionStatus::DigestUnavailable,
                              "profiling provenance digest is unavailable");
        }

        ProfilingDerivedObservation observation;
        observation.provider_id = contract_.provider_id;
        observation.provider_revision_sha256 =
            contract_.provider_revision_sha256;
        observation.raw_provenance_sha256 = *provenance;
        observation.observation_contract_sha256 = *contract_sha256;
        observation.observation_generation = *generation;
        observation.observed_at = observed_at;
        observation.fresh_until = fresh_until;
        observation.source_skew_milliseconds = *skew;
        observation.max_source_skew_milliseconds =
            static_cast<std::uint64_t>(contract_.max_source_skew.count());
        observation.health = ProfilingObservationHealth::Valid;
        observation.owner_coverage = ProfilingOwnerCoverage::Complete;
        observation.observed_claims = claim_closure(contract_, observed);
        observation.attributed_claims = claim_closure(contract_, attributed);
        observation.uncertainty_claims = claim_closure(contract_, uncertainty);
        observation.safety_margin_claims =
            claim_closure(contract_, safety_margin);

        return ProfilingObservationCollectionResult{
            ProfilingCollectionStatus::Accepted,
            {},
            std::move(observation),
        };
    } catch (...) {
        return result_for(ProfilingCollectionStatus::InvalidObservation,
                          "profiling observation collection failed");
    }
}

} // namespace lemon::residency
