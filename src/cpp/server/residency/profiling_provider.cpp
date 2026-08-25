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

std::string_view
claim_completeness_wire(ClaimCompleteness completeness) noexcept {
    switch (completeness) {
    case ClaimCompleteness::NotApplicable:
        return "not_applicable";
    case ClaimCompleteness::KnownZero:
        return "known_zero";
    case ClaimCompleteness::Bounded:
        return "bounded";
    case ClaimCompleteness::Unknown:
        return "unknown";
    }
    return {};
}

std::string_view
profiling_health_wire(ProfilingObservationHealth health) noexcept {
    switch (health) {
    case ProfilingObservationHealth::Valid:
        return "valid";
    case ProfilingObservationHealth::Missing:
        return "missing";
    case ProfilingObservationHealth::Stale:
        return "stale";
    case ProfilingObservationHealth::Unhealthy:
        return "unhealthy";
    case ProfilingObservationHealth::Incoherent:
        return "incoherent";
    case ProfilingObservationHealth::Superseded:
        return "superseded";
    }
    return {};
}

std::string_view profiling_owner_coverage_wire(
    ProfilingOwnerCoverage coverage) noexcept {
    switch (coverage) {
    case ProfilingOwnerCoverage::Complete:
        return "complete";
    case ProfilingOwnerCoverage::Incomplete:
        return "incomplete";
    case ProfilingOwnerCoverage::Unknown:
        return "unknown";
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

bool owner_scopes_are_valid(
    const std::vector<ProfilingOwnerScopeBinding> &owner_scopes) {
    if (owner_scopes.empty() ||
        owner_scopes.size() > max_journal_array_entries) {
        return false;
    }

    std::set<std::string> owner_scope_ids;
    std::set<std::string> containment_identities;
    for (const auto &owner_scope : owner_scopes) {
        if (!identifier_is_valid(owner_scope.owner_scope_id) ||
            !digest_is_valid(owner_scope.containment_identity_sha256) ||
            !owner_scope_ids.insert(owner_scope.owner_scope_id).second ||
            !containment_identities
                 .insert(owner_scope.containment_identity_sha256)
                 .second) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> owner_scope_ids(
    const std::vector<ProfilingOwnerScopeBinding> &owner_scopes) {
    std::vector<std::string> result;
    result.reserve(owner_scopes.size());
    for (const auto &owner_scope : owner_scopes) {
        result.push_back(owner_scope.owner_scope_id);
    }
    return result;
}

bool contract_is_valid(const ProfilingDerivationContract &contract) {
    if (!identifier_is_valid(contract.provider_id) ||
        !digest_is_valid(contract.provider_revision_sha256) ||
        contract.sensors.empty() ||
        contract.sensors.size() > max_journal_array_entries ||
        !owner_scopes_are_valid(contract.owner_scopes) ||
        contract.freshness_window <= std::chrono::seconds::zero() ||
        contract.max_source_skew <= std::chrono::milliseconds::zero() ||
        contract.max_source_skew.count() / 1000 >=
            contract.freshness_window.count() ||
        !digest_is_valid(
            contract.interval.event_semantics_revision_sha256) ||
        contract.interval.max_observation_gap <=
            std::chrono::milliseconds::zero() ||
        contract.interval.baseline_stability_window <=
            std::chrono::milliseconds::zero() ||
        contract.interval.release_stability_window <=
            std::chrono::milliseconds::zero() ||
        contract.interval.max_interval_frames == 0 ||
        contract.interval.max_interval_frames > max_profiling_interval_frames) {
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

void append_claim_closure(std::string &bytes,
                          std::vector<ClaimFamilyClosure> closure) {
    std::sort(closure.begin(), closure.end(),
              [](const ClaimFamilyClosure &left,
                 const ClaimFamilyClosure &right) {
                  return claim_family_wire(left.family) <
                         claim_family_wire(right.family);
              });
    append_u64(bytes, static_cast<std::uint64_t>(closure.size()));
    for (auto &family : closure) {
        std::sort(family.entries.begin(), family.entries.end(),
                  [](const ClaimAmount &left, const ClaimAmount &right) {
                      return std::tie(left.constraint_id, left.unit,
                                      left.amount) <
                             std::tie(right.constraint_id, right.unit,
                                      right.amount);
                  });
        append_string(bytes, claim_family_wire(family.family));
        append_string(bytes,
                      claim_completeness_wire(family.completeness));
        append_u64(bytes,
                   static_cast<std::uint64_t>(family.entries.size()));
        for (const auto &entry : family.entries) {
            append_string(bytes, entry.constraint_id);
            append_string(bytes, claim_unit_wire(entry.unit));
            append_u64(bytes, entry.amount);
        }
    }
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

std::optional<std::string> owner_scope_set_digest(
    const std::vector<ProfilingOwnerScopeBinding> &input_owner_scopes) {
    if (!owner_scopes_are_valid(input_owner_scopes)) return std::nullopt;

    auto owner_scopes = input_owner_scopes;
    std::sort(owner_scopes.begin(), owner_scopes.end(),
              [](const ProfilingOwnerScopeBinding &left,
                 const ProfilingOwnerScopeBinding &right) {
                  return std::tie(left.owner_scope_id,
                                  left.containment_identity_sha256) <
                         std::tie(right.owner_scope_id,
                                  right.containment_identity_sha256);
              });

    std::string bytes = "lemonade/profiling-owner-scope-set/v1";
    append_u64(bytes, static_cast<std::uint64_t>(owner_scopes.size()));
    for (const auto &owner_scope : owner_scopes) {
        append_string(bytes, owner_scope.owner_scope_id);
        append_string(bytes, owner_scope.containment_identity_sha256);
    }
    return sha256_hex(bytes);
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
    const auto owner_scope_set_sha256 =
        owner_scope_set_digest(contract.owner_scopes);
    if (!owner_scope_set_sha256) return std::nullopt;

    std::string bytes = "lemonade/profiling-derivation-contract/v2";
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
    append_string(bytes, *owner_scope_set_sha256);
    append_u64(bytes,
               static_cast<std::uint64_t>(contract.freshness_window.count()));
    append_u64(bytes,
               static_cast<std::uint64_t>(contract.max_source_skew.count()));
    append_string(bytes, contract.interval.event_semantics_revision_sha256);
    append_u64(bytes, static_cast<std::uint64_t>(
                          contract.interval.max_observation_gap.count()));
    append_u64(bytes, static_cast<std::uint64_t>(
                          contract.interval.baseline_stability_window.count()));
    append_u64(bytes, static_cast<std::uint64_t>(
                          contract.interval.release_stability_window.count()));
    append_u64(bytes, contract.interval.max_interval_frames);
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
source_generation(const std::vector<ProfilingRawSample> &samples,
                  bool require_nonzero) noexcept {
    if (samples.empty() ||
        (require_nonzero && samples.front().source_generation == 0)) {
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
    const auto expected_owner_scope_ids = owner_scope_ids(contract.owner_scopes);
    if (contract.sensors.size() > std::numeric_limits<std::size_t>::max() /
                                      (expected_owner_scope_ids.size() + 1)) {
        return std::nullopt;
    }
    const auto expected_count =
        contract.sensors.size() * (expected_owner_scope_ids.size() + 1);
    if (samples.size() != expected_count) return std::nullopt;

    std::map<std::string, std::size_t> sensor_indices;
    for (std::size_t index = 0; index < contract.sensors.size(); ++index) {
        sensor_indices.emplace(contract.sensors[index].sensor_id, index);
    }
    const std::set<std::string> expected_owner_scope_id_set(
        expected_owner_scope_ids.begin(), expected_owner_scope_ids.end());

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
        if (expected_owner_scope_id_set.count(*sample.owner_scope_id) == 0 ||
            !sensor_values.owners.emplace(*sample.owner_scope_id, sample.value)
                 .second) {
            return std::nullopt;
        }
    }

    for (auto &sensor_values : values) {
        if (!sensor_values.total ||
            sensor_values.owners.size() != expected_owner_scope_ids.size()) {
            return std::nullopt;
        }
        for (const auto &owner_scope_id : expected_owner_scope_ids) {
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

struct DerivedClaimAmounts {
    std::uint64_t source_generation = 0;
    std::vector<std::uint64_t> observed;
    std::vector<std::uint64_t> attributed;
    std::vector<std::uint64_t> uncertainty;
    std::vector<std::uint64_t> safety_margin;
};

std::optional<DerivedClaimAmounts>
derive_claim_amounts(const ProfilingDerivationContract &contract,
                     const std::vector<ProfilingRawSample> &samples,
                     bool require_nonzero_generation = true) {
    const auto generation =
        source_generation(samples, require_nonzero_generation);
    const auto values = reconcile_samples(contract, samples);
    if (!generation || !values) return std::nullopt;

    DerivedClaimAmounts result;
    result.source_generation = *generation;
    result.observed.reserve(values->size());
    result.attributed.reserve(values->size());
    result.uncertainty.reserve(values->size());
    result.safety_margin.reserve(values->size());
    for (std::size_t index = 0; index < values->size(); ++index) {
        const auto amount = *(*values)[index].total;
        const auto &sensor = contract.sensors[index];
        if (amount >= sensor.safety_ceiling) return std::nullopt;
        const auto remaining = sensor.safety_ceiling - amount;
        if (sensor.uncertainty_bound >= remaining) return std::nullopt;
        result.observed.push_back(amount);
        result.attributed.push_back((*values)[index].attributed_total);
        result.uncertainty.push_back(sensor.uncertainty_bound);
        result.safety_margin.push_back(remaining - sensor.uncertainty_bound);
    }
    return result;
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

struct ProfilingAcquisitionTiming {
    std::string observed_at;
    std::string fresh_until;
    std::uint64_t source_skew_milliseconds = 0;
};

std::optional<ProfilingAcquisitionTiming> acquisition_timing(
    const ProfilingDerivationContract &contract, SteadyClock::time_point started,
    SystemClock::time_point observed_time, SteadyClock::time_point finished) {
    const auto observed_seconds =
        observed_time >= SystemClock::time_point{}
            ? system_clock_seconds(observed_time)
            : std::nullopt;
    const auto freshness_seconds =
        count_as_i64(contract.freshness_window.count());
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
    const auto elapsed = elapsed_between(started, finished);
    const auto skew = elapsed ? rounded_up_milliseconds(*elapsed)
                              : std::nullopt;
    if (observed_at.empty() || fresh_until.empty() || !skew ||
        *skew >
            static_cast<std::uint64_t>(contract.max_source_skew.count())) {
        return std::nullopt;
    }
    return ProfilingAcquisitionTiming{observed_at, fresh_until, *skew};
}

std::optional<ProfilingDerivedObservation> derived_observation(
    const ProfilingDerivationContract &contract,
    const ProfilingTransactionContext &context,
    const std::vector<ProfilingRawSample> &samples,
    const DerivedClaimAmounts &claims,
    const ProfilingAcquisitionTiming &timing,
    std::string_view contract_sha256) {
    const auto provenance =
        raw_provenance_digest(context, samples, timing.observed_at);
    if (!provenance) return std::nullopt;

    ProfilingDerivedObservation observation;
    observation.provider_id = contract.provider_id;
    observation.provider_revision_sha256 = contract.provider_revision_sha256;
    observation.raw_provenance_sha256 = *provenance;
    observation.observation_contract_sha256 = std::string(contract_sha256);
    observation.source_generation = claims.source_generation;
    observation.observed_at = timing.observed_at;
    observation.fresh_until = timing.fresh_until;
    observation.source_skew_milliseconds = timing.source_skew_milliseconds;
    observation.max_source_skew_milliseconds =
        static_cast<std::uint64_t>(contract.max_source_skew.count());
    observation.health = ProfilingObservationHealth::Valid;
    observation.owner_coverage = ProfilingOwnerCoverage::Complete;
    observation.observed_claims = claim_closure(contract, claims.observed);
    observation.attributed_claims =
        claim_closure(contract, claims.attributed);
    observation.uncertainty_claims =
        claim_closure(contract, claims.uncertainty);
    observation.safety_margin_claims =
        claim_closure(contract, claims.safety_margin);
    return observation;
}

std::optional<std::string> interval_frame_provenance_digest(
    const ProfilingTransactionContext &context,
    const ProfilingRawIntervalFrame &frame) {
    auto samples = frame.samples;
    std::sort(
        samples.begin(), samples.end(),
        [](const ProfilingRawSample &left, const ProfilingRawSample &right) {
            return std::tie(left.sensor_id, left.owner_scope_id, left.value,
                            left.source_generation) <
                   std::tie(right.sensor_id, right.owner_scope_id, right.value,
                            right.source_generation);
        });

    std::string bytes = "lemonade/profiling-interval-frame/v1";
    append_string(bytes, context.deployment_id);
    append_string(bytes, context.profiling_transaction_id);
    append_string(bytes, context.selector_sha256);
    append_string(bytes, context.observation_contract_sha256);
    append_string(bytes, frame.source_epoch_sha256);
    append_string(bytes, frame.owner_scope_set_sha256);
    append_string(bytes, frame.event_semantics_revision_sha256);
    append_u64(bytes, frame.event_watermark.value);
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

std::optional<std::string> segment_provenance_seed(
    const ProfilingTransactionContext &context, std::string_view source_epoch,
    std::string_view owner_scope_set, std::string_view event_semantics,
    ProfilingEventWatermark after_event_watermark) {
    std::string bytes = "lemonade/profiling-interval-segment/v1";
    append_string(bytes, context.deployment_id);
    append_string(bytes, context.profiling_transaction_id);
    append_string(bytes, context.observation_contract_sha256);
    append_string(bytes, source_epoch);
    append_string(bytes, owner_scope_set);
    append_string(bytes, event_semantics);
    append_u64(bytes, after_event_watermark.value);
    return sha256_hex(bytes);
}

std::optional<std::string> extend_segment_provenance(
    std::string_view prior, std::uint64_t capture_generation,
    ProfilingEventWatermark event_watermark,
    std::string_view frame_provenance_sha256) {
    std::string bytes = "lemonade/profiling-interval-segment-frame/v1";
    append_string(bytes, prior);
    append_u64(bytes, capture_generation);
    append_u64(bytes, event_watermark.value);
    append_string(bytes, frame_provenance_sha256);
    return sha256_hex(bytes);
}

std::optional<std::string> checkpoint_provenance_digest(
    const ProfilingRecordedCheckpoint &checkpoint) {
    const auto &observation = checkpoint.observation;
    std::string bytes = "lemonade/profiling-interval-checkpoint/v1";
    append_u64(bytes, checkpoint.capture_generation);
    append_u64(bytes, checkpoint.event_watermark.value);
    append_string(bytes, observation.provider_id);
    append_string(bytes, observation.provider_revision_sha256);
    append_string(bytes, observation.raw_provenance_sha256);
    append_string(bytes, observation.observation_contract_sha256);
    append_u64(bytes, observation.source_generation);
    append_string(bytes, observation.observed_at);
    append_string(bytes, observation.fresh_until);
    append_u64(bytes, observation.source_skew_milliseconds);
    append_u64(bytes, observation.max_source_skew_milliseconds);
    append_string(bytes, profiling_health_wire(observation.health));
    append_string(bytes,
                  profiling_owner_coverage_wire(observation.owner_coverage));
    append_claim_closure(bytes, observation.observed_claims);
    append_claim_closure(bytes, observation.attributed_claims);
    append_claim_closure(bytes, observation.uncertainty_claims);
    append_claim_closure(bytes, observation.safety_margin_claims);
    return sha256_hex(bytes);
}

std::optional<std::string> extend_checkpoint_provenance(
    std::string_view prior, std::string_view checkpoint_sha256) {
    std::string bytes = "lemonade/profiling-interval-segment-checkpoint/v1";
    append_string(bytes, prior);
    append_string(bytes, checkpoint_sha256);
    return sha256_hex(bytes);
}

bool componentwise_maximum(std::vector<std::uint64_t> &target,
                           const std::vector<std::uint64_t> &candidate) {
    if (target.empty()) {
        target = candidate;
        return true;
    }
    if (target.size() != candidate.size()) return false;
    for (std::size_t index = 0; index < target.size(); ++index) {
        target[index] = std::max(target[index], candidate[index]);
    }
    return true;
}

bool componentwise_minimum(std::vector<std::uint64_t> &target,
                           const std::vector<std::uint64_t> &candidate) {
    if (target.empty()) {
        target = candidate;
        return true;
    }
    if (target.size() != candidate.size()) return false;
    for (std::size_t index = 0; index < target.size(); ++index) {
        target[index] = std::min(target[index], candidate[index]);
    }
    return true;
}

} // namespace

std::optional<std::string> profiling_derivation_contract_sha256(
    const ProfilingDerivationContract &contract) {
    return derivation_contract_digest(contract);
}

std::optional<std::string> profiling_owner_scope_set_sha256(
    const std::vector<ProfilingOwnerScopeBinding> &owner_scopes) {
    return owner_scope_set_digest(owner_scopes);
}

ProfilingRawReadResult UnavailableProfilingObservationSource::read(
    const ProfilingRawReadRequest &, const ProfilingCancellationCheck &) {
    return ProfilingRawReadResult{
        ProfilingSourceError::Unavailable,
        {},
        {},
        "profiling observation source is unavailable",
    };
}

ProfilingRawIntervalBeginResult
UnavailableProfilingIntervalObservationSource::begin(
    const ProfilingRawIntervalReadRequest &,
    const ProfilingCancellationCheck &) {
    return ProfilingRawIntervalBeginResult{
        ProfilingIntervalSourceError::Unavailable,
        {},
        {},
        "profiling interval observation source is unavailable",
    };
}

ProfilingRawIntervalBatch
UnavailableProfilingIntervalObservationSource::read_since(
    ProfilingRawIntervalToken, ProfilingEventWatermark after_event_watermark,
    const ProfilingCancellationCheck &) {
    return ProfilingRawIntervalBatch{
        ProfilingIntervalSourceError::Unavailable,
        after_event_watermark,
        after_event_watermark,
        {},
        {},
        "profiling interval observation source is unavailable",
    };
}

ProfilingRawIntervalBatch UnavailableProfilingIntervalObservationSource::finish(
    ProfilingRawIntervalToken,
    ProfilingEventWatermark after_event_watermark) noexcept {
    return ProfilingRawIntervalBatch{
        ProfilingIntervalSourceError::Unavailable,
        after_event_watermark,
        after_event_watermark,
        {},
        {},
        "profiling interval observation source is unavailable",
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
        const auto owner_scope_set_sha256 =
            owner_scope_set_digest(contract_.owner_scopes);
        if (!owner_scope_set_sha256) {
            return result_for(
                ProfilingCollectionStatus::DigestUnavailable,
                "profiling owner-scope identity is unavailable");
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
        request.owner_scope_ids = owner_scope_ids(contract_.owner_scopes);
        request.owner_scope_set_sha256 = *owner_scope_set_sha256;

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
        if (raw.owner_scope_set_sha256 != *owner_scope_set_sha256) {
            return result_for(
                ProfilingCollectionStatus::InvalidObservation,
                "profiling owner-scope identity does not match the contract");
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
        const auto claims = derive_claim_amounts(contract_, raw.samples);
        if (!claims) {
            return result_for(ProfilingCollectionStatus::InvalidObservation,
                              "profiling raw observation does not close");
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
        observation.source_generation = claims->source_generation;
        observation.observed_at = observed_at;
        observation.fresh_until = fresh_until;
        observation.source_skew_milliseconds = *skew;
        observation.max_source_skew_milliseconds =
            static_cast<std::uint64_t>(contract_.max_source_skew.count());
        observation.health = ProfilingObservationHealth::Valid;
        observation.owner_coverage = ProfilingOwnerCoverage::Complete;
        observation.observed_claims =
            claim_closure(contract_, claims->observed);
        observation.attributed_claims =
            claim_closure(contract_, claims->attributed);
        observation.uncertainty_claims =
            claim_closure(contract_, claims->uncertainty);
        observation.safety_margin_claims =
            claim_closure(contract_, claims->safety_margin);

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

namespace {

ProfilingIntervalRecorderResult
recorder_result(ProfilingIntervalRecorderStatus status,
                std::string diagnostic = {}) {
    return ProfilingIntervalRecorderResult{
        status,
        bounded_diagnostic(std::move(diagnostic)),
        std::nullopt,
    };
}

struct SegmentAccumulator {
    ProfilingEventWatermark after_event_watermark;
    ProfilingEventWatermark through_event_watermark;
    std::uint64_t first_capture_generation = 0;
    std::uint64_t last_capture_generation = 0;
    std::uint64_t frame_count = 0;
    std::string provenance_sha256;
    std::vector<std::uint64_t> peak_observed;
    std::vector<std::uint64_t> peak_attributed;
    std::vector<std::uint64_t> maximum_uncertainty;
    std::vector<std::uint64_t> minimum_safety_margin;
    ProfilingRecordedCheckpoint checkpoint;
    bool has_checkpoint = false;
};

} // namespace

struct ProfilingIntervalRecorder::State {
    ProfilingDerivationContract contract;
    UnavailableProfilingIntervalObservationSource unavailable_source;
    ProfilingIntervalObservationSource *source = nullptr;
    ProfilingCollectionClock clock;
    ProfilingTransactionContext context;
    std::string contract_sha256;
    std::string owner_scope_set_sha256;
    std::string source_epoch_sha256;
    ProfilingRawIntervalToken token;
    ProfilingEventWatermark current_event_watermark;
    std::optional<SteadyClock::time_point> last_read_started;
    std::uint64_t next_capture_generation = 1;
    std::uint64_t total_frame_count = 0;
    SegmentAccumulator segment;
    DerivedClaimAmounts last_claims;
    std::string last_raw_frame_provenance_sha256;
    std::string last_checkpoint_provenance_sha256;
    ProfilingRecordedCheckpoint last_checkpoint;
    bool is_active = false;

    State(ProfilingDerivationContract input_contract,
          ProfilingCollectionClock input_clock)
        : contract(std::move(input_contract)), source(&unavailable_source),
          clock(std::move(input_clock)) {
        initialize_clock();
    }

    State(ProfilingDerivationContract input_contract,
          ProfilingIntervalObservationSource &input_source,
          ProfilingCollectionClock input_clock)
        : contract(std::move(input_contract)), source(&input_source),
          clock(std::move(input_clock)) {
        initialize_clock();
    }

    void initialize_clock() {
        if (!clock.monotonic_now) {
            clock.monotonic_now = [] { return SteadyClock::now(); };
        }
        if (!clock.utc_now) clock.utc_now = [] { return SystemClock::now(); };
    }

    void cleanup() noexcept {
        if (!is_active) return;
        const auto owned_token = token;
        is_active = false;
        token = {};
        source->finish(owned_token, current_event_watermark);
    }

    ProfilingIntervalRecorderResult fail_active(
        ProfilingIntervalRecorderStatus status, std::string diagnostic) {
        cleanup();
        return recorder_result(status, std::move(diagnostic));
    }

    bool frame_matches_stream(const ProfilingRawIntervalFrame &frame) const {
        return digest_is_valid(frame.source_epoch_sha256) &&
               frame.source_epoch_sha256 == source_epoch_sha256 &&
               frame.owner_scope_set_sha256 == owner_scope_set_sha256 &&
               frame.event_semantics_revision_sha256 ==
                   contract.interval.event_semantics_revision_sha256;
    }

    bool initialize_segment(ProfilingEventWatermark after) {
        segment = {};
        segment.after_event_watermark = after;
        segment.through_event_watermark = after;
        const auto seed = segment_provenance_seed(
            context, source_epoch_sha256, owner_scope_set_sha256,
            contract.interval.event_semantics_revision_sha256, after);
        if (!seed) return false;
        segment.provenance_sha256 = *seed;
        return true;
    }

    bool fold_claims(const DerivedClaimAmounts &claims) {
        return componentwise_maximum(segment.peak_observed,
                                     claims.observed) &&
               componentwise_maximum(segment.peak_attributed,
                                     claims.attributed) &&
               componentwise_maximum(segment.maximum_uncertainty,
                                     claims.uncertainty) &&
               componentwise_minimum(segment.minimum_safety_margin,
                                     claims.safety_margin);
    }

    bool record_frame(const ProfilingRawIntervalFrame &frame,
                      const std::optional<ProfilingAcquisitionTiming> &timing,
                      bool counts_toward_limit) {
        if (!frame_matches_stream(frame)) return false;
        const auto claims = derive_claim_amounts(contract, frame.samples, false);
        if (!claims ||
            claims->source_generation != frame.event_watermark.value) {
            return false;
        }
        if (counts_toward_limit) {
            if (total_frame_count >= contract.interval.max_interval_frames ||
                next_capture_generation == 0) {
                return false;
            }
            ++total_frame_count;
        }
        const auto capture_generation = next_capture_generation;
        if (counts_toward_limit) {
            if (next_capture_generation ==
                std::numeric_limits<std::uint64_t>::max()) {
                next_capture_generation = 0;
            } else {
                ++next_capture_generation;
            }
        }

        const auto frame_provenance =
            interval_frame_provenance_digest(context, frame);
        if (!frame_provenance ||
            (frame.event_watermark == segment.through_event_watermark &&
             !last_raw_frame_provenance_sha256.empty() &&
             *frame_provenance != last_raw_frame_provenance_sha256)) {
            return false;
        }
        auto provenance = extend_segment_provenance(
            segment.provenance_sha256, capture_generation,
            frame.event_watermark, *frame_provenance);
        if (!provenance || !fold_claims(*claims)) return false;

        if (segment.frame_count == 0) {
            segment.first_capture_generation = capture_generation;
        }
        segment.last_capture_generation = capture_generation;
        ++segment.frame_count;
        segment.through_event_watermark = frame.event_watermark;
        segment.provenance_sha256 = *provenance;

        if (timing) {
            const auto observation = derived_observation(
                contract, context, frame.samples, *claims, *timing,
                contract_sha256);
            if (!observation) return false;
            segment.checkpoint = ProfilingRecordedCheckpoint{
                capture_generation,
                frame.event_watermark,
                *observation,
            };
            const auto checkpoint_provenance =
                checkpoint_provenance_digest(segment.checkpoint);
            provenance = checkpoint_provenance
                             ? extend_checkpoint_provenance(
                                   *provenance, *checkpoint_provenance)
                             : std::nullopt;
            if (!provenance) return false;
            segment.provenance_sha256 = *provenance;
            segment.has_checkpoint = true;
            last_checkpoint = segment.checkpoint;
            last_claims = *claims;
            last_checkpoint_provenance_sha256 = *checkpoint_provenance;
        }
        last_raw_frame_provenance_sha256 = *frame_provenance;
        return true;
    }

    bool seed_next_segment() {
        if (!initialize_segment(current_event_watermark) ||
            !fold_claims(last_claims)) {
            return false;
        }
        const auto provenance = extend_checkpoint_provenance(
            segment.provenance_sha256,
            last_checkpoint_provenance_sha256);
        if (!provenance) return false;
        segment.first_capture_generation =
            last_checkpoint.capture_generation;
        segment.last_capture_generation = last_checkpoint.capture_generation;
        segment.frame_count = 1;
        segment.through_event_watermark = current_event_watermark;
        segment.provenance_sha256 = *provenance;
        segment.checkpoint = last_checkpoint;
        segment.has_checkpoint = true;
        return true;
    }

    std::optional<ProfilingIntervalSegment> close_segment() const {
        if (!segment.has_checkpoint || segment.frame_count == 0 ||
            segment.provenance_sha256.size() != 64) {
            return std::nullopt;
        }
        std::vector<std::uint64_t> zero(contract.sensors.size(), 0);
        ProfilingIntervalSegment result;
        result.source_epoch_sha256 = source_epoch_sha256;
        result.owner_scope_set_sha256 = owner_scope_set_sha256;
        result.event_semantics_revision_sha256 =
            contract.interval.event_semantics_revision_sha256;
        result.after_event_watermark = segment.after_event_watermark;
        result.through_event_watermark = segment.through_event_watermark;
        result.first_capture_generation =
            segment.first_capture_generation;
        result.last_capture_generation = segment.last_capture_generation;
        result.frame_count = segment.frame_count;
        result.provenance_sha256 = segment.provenance_sha256;
        result.checkpoint = segment.checkpoint;
        result.peak_observed_claims =
            claim_closure(contract, segment.peak_observed);
        result.peak_attributed_claims =
            claim_closure(contract, segment.peak_attributed);
        result.external_change_claims = claim_closure(contract, zero);
        result.unattributed_claims = claim_closure(contract, zero);
        result.uncertainty_claims =
            claim_closure(contract, segment.maximum_uncertainty);
        result.safety_margin_claims =
            claim_closure(contract, segment.minimum_safety_margin);
        return result;
    }

    ProfilingIntervalRecorderResult handle_batch(
        ProfilingRawIntervalBatch raw,
        const ProfilingAcquisitionTiming &timing, bool close,
        bool source_finished) {
        auto fail = [&](ProfilingIntervalRecorderStatus status,
                        std::string diagnostic) {
            if (!source_finished) cleanup();
            return recorder_result(status, std::move(diagnostic));
        };
        if (!raw.diagnostic.empty() ||
            raw.after_event_watermark != current_event_watermark ||
            raw.through_event_watermark.value <
                raw.after_event_watermark.value) {
            return fail(ProfilingIntervalRecorderStatus::InvalidObservation,
                        "profiling interval batch is incoherent");
        }
        const auto event_count = raw.through_event_watermark.value -
                                 raw.after_event_watermark.value;
        const auto remaining =
            contract.interval.max_interval_frames - total_frame_count;
        if (event_count !=
                static_cast<std::uint64_t>(raw.event_frames.size()) ||
            remaining == 0 || event_count >= remaining) {
            return fail(ProfilingIntervalRecorderStatus::InvalidObservation,
                        "profiling interval history is incomplete");
        }

        auto expected = raw.after_event_watermark.value;
        for (const auto &frame : raw.event_frames) {
            if (expected == std::numeric_limits<std::uint64_t>::max()) {
                return fail(
                    ProfilingIntervalRecorderStatus::InvalidObservation,
                    "profiling event watermark wrapped");
            }
            ++expected;
            if (frame.event_watermark.value != expected ||
                !record_frame(frame, std::nullopt, true)) {
                return fail(
                    ProfilingIntervalRecorderStatus::InvalidObservation,
                    "profiling interval event history does not close");
            }
        }
        if (expected != raw.through_event_watermark.value ||
            raw.checkpoint.event_watermark !=
                raw.through_event_watermark ||
            !record_frame(raw.checkpoint, timing, true)) {
            return fail(ProfilingIntervalRecorderStatus::InvalidObservation,
                        "profiling interval checkpoint does not close");
        }
        current_event_watermark = raw.through_event_watermark;
        if (!close) return recorder_result(ProfilingIntervalRecorderStatus::Ok);

        auto closed = close_segment();
        if (!closed) {
            return fail(ProfilingIntervalRecorderStatus::DigestUnavailable,
                        "profiling interval segment could not be sealed");
        }
        if (!source_finished && !seed_next_segment()) {
            cleanup();
            return recorder_result(
                ProfilingIntervalRecorderStatus::DigestUnavailable,
                "profiling interval boundary could not be retained");
        }
        return ProfilingIntervalRecorderResult{
            ProfilingIntervalRecorderStatus::Ok,
            {},
            std::move(closed),
        };
    }

    ProfilingIntervalRecorderResult read(bool close, bool finish_source,
                                         const ProfilingCancellationCheck &abort) {
        if (!is_active) {
            return recorder_result(ProfilingIntervalRecorderStatus::InvalidState,
                                   "profiling interval is not active");
        }
        if (!finish_source && cancelled(abort)) {
            return fail_active(ProfilingIntervalRecorderStatus::Cancelled,
                               "profiling interval recording cancelled");
        }

        SteadyClock::time_point started;
        SystemClock::time_point observed_time;
        try {
            started = clock.monotonic_now();
            observed_time = clock.utc_now();
        } catch (...) {
            return fail_active(
                ProfilingIntervalRecorderStatus::InvalidObservation,
                "profiling interval clock is unavailable");
        }
        if (last_read_started) {
            const auto elapsed = elapsed_between(*last_read_started, started);
            const auto gap = elapsed ? rounded_up_milliseconds(*elapsed)
                                     : std::nullopt;
            if (!gap ||
                *gap > static_cast<std::uint64_t>(
                           contract.interval.max_observation_gap.count())) {
                return fail_active(
                    ProfilingIntervalRecorderStatus::InvalidObservation,
                    "profiling observation cadence exceeded its contract");
            }
        }

        ProfilingRawIntervalBatch raw;
        if (finish_source) {
            const auto owned_token = token;
            is_active = false;
            token = {};
            raw = source->finish(owned_token, current_event_watermark);
        } else {
            try {
                raw = source->read_since(token, current_event_watermark,
                                         abort);
            } catch (...) {
                return fail_active(
                    ProfilingIntervalRecorderStatus::EvidenceUnavailable,
                    "profiling interval observation source failed");
            }
        }
        SteadyClock::time_point finished;
        try {
            finished = clock.monotonic_now();
        } catch (...) {
            return fail_active(
                ProfilingIntervalRecorderStatus::InvalidObservation,
                "profiling interval clock is unavailable");
        }
        last_read_started = started;

        if (!finish_source &&
            (cancelled(abort) ||
             raw.error == ProfilingIntervalSourceError::Cancelled)) {
            return fail_active(
                ProfilingIntervalRecorderStatus::Cancelled,
                raw.diagnostic.empty()
                    ? "profiling interval recording cancelled"
                    : std::move(raw.diagnostic));
        }
        if (raw.error != ProfilingIntervalSourceError::None) {
            const auto status =
                raw.error == ProfilingIntervalSourceError::Cancelled
                    ? ProfilingIntervalRecorderStatus::Cancelled
                    : ProfilingIntervalRecorderStatus::EvidenceUnavailable;
            if (!finish_source) cleanup();
            return recorder_result(
                status,
                raw.diagnostic.empty()
                    ? "profiling interval observation source is unavailable"
                    : std::move(raw.diagnostic));
        }
        try {
            const auto timing =
                acquisition_timing(contract, started, observed_time, finished);
            if (!timing) {
                if (!finish_source) cleanup();
                return recorder_result(
                    ProfilingIntervalRecorderStatus::InvalidObservation,
                    "profiling interval acquisition exceeded its contract");
            }
            return handle_batch(std::move(raw), *timing, close, finish_source);
        } catch (...) {
            if (!finish_source) cleanup();
            return recorder_result(
                ProfilingIntervalRecorderStatus::EvidenceUnavailable,
                "profiling interval evidence processing failed");
        }
    }
};

bool ProfilingIntervalRecorderResult::ok() const noexcept {
    return status == ProfilingIntervalRecorderStatus::Ok;
}

ProfilingIntervalRecorder::ProfilingIntervalRecorder(
    ProfilingDerivationContract contract, ProfilingCollectionClock clock)
    : state_(std::make_unique<State>(std::move(contract), std::move(clock))) {}

ProfilingIntervalRecorder::ProfilingIntervalRecorder(
    ProfilingDerivationContract contract,
    ProfilingIntervalObservationSource &source, ProfilingCollectionClock clock)
    : state_(std::make_unique<State>(std::move(contract), source,
                                     std::move(clock))) {}

ProfilingIntervalRecorder::~ProfilingIntervalRecorder() { state_->cleanup(); }

ProfilingIntervalRecorderResult ProfilingIntervalRecorder::begin(
    const ProfilingTransactionContext &context,
    const ProfilingCancellationCheck &should_abort) {
    if (state_->is_active) {
        return recorder_result(ProfilingIntervalRecorderStatus::InvalidState,
                               "profiling interval is already active");
    }
    if (!contract_is_valid(state_->contract)) {
        return recorder_result(ProfilingIntervalRecorderStatus::InvalidContract,
                               "profiling derivation contract is invalid");
    }
    const auto contract_sha256 =
        derivation_contract_digest(state_->contract);
    const auto owner_scope_set_sha256 =
        owner_scope_set_digest(state_->contract.owner_scopes);
    if (!contract_sha256 || !owner_scope_set_sha256) {
        return recorder_result(
            ProfilingIntervalRecorderStatus::DigestUnavailable,
            "profiling interval contract identity is unavailable");
    }
    if (context.observation_contract_sha256 != *contract_sha256) {
        return recorder_result(
            ProfilingIntervalRecorderStatus::InvalidContract,
            "profiling derivation contract identity does not match context");
    }
    if (cancelled(should_abort)) {
        return recorder_result(ProfilingIntervalRecorderStatus::Cancelled,
                               "profiling interval recording cancelled");
    }

    ProfilingRawIntervalReadRequest request;
    request.read.sensor_ids.reserve(state_->contract.sensors.size());
    for (const auto &sensor : state_->contract.sensors) {
        request.read.sensor_ids.push_back(sensor.sensor_id);
    }
    request.read.owner_scope_ids =
        owner_scope_ids(state_->contract.owner_scopes);
    request.read.owner_scope_set_sha256 = *owner_scope_set_sha256;
    request.event_semantics_revision_sha256 =
        state_->contract.interval.event_semantics_revision_sha256;

    SteadyClock::time_point started;
    SystemClock::time_point observed_time;
    try {
        started = state_->clock.monotonic_now();
        observed_time = state_->clock.utc_now();
    } catch (...) {
        return recorder_result(
            ProfilingIntervalRecorderStatus::InvalidObservation,
            "profiling interval clock is unavailable");
    }

    ProfilingRawIntervalBeginResult raw;
    try {
        raw = state_->source->begin(request, should_abort);
    } catch (...) {
        return recorder_result(
            ProfilingIntervalRecorderStatus::EvidenceUnavailable,
            "profiling interval observation source failed");
    }
    if (raw.error == ProfilingIntervalSourceError::None) {
        state_->token = raw.token;
        state_->current_event_watermark = raw.checkpoint.event_watermark;
        state_->is_active = true;
        try {
            state_->context = context;
            state_->contract_sha256 = *contract_sha256;
            state_->owner_scope_set_sha256 = *owner_scope_set_sha256;
            state_->source_epoch_sha256 = raw.checkpoint.source_epoch_sha256;
            state_->last_read_started = started;
            state_->next_capture_generation = 1;
            state_->total_frame_count = 0;
            state_->last_raw_frame_provenance_sha256.clear();
            state_->last_checkpoint_provenance_sha256.clear();
            state_->last_checkpoint = {};
            state_->last_claims = {};
        } catch (...) {
            return state_->fail_active(
                ProfilingIntervalRecorderStatus::InvalidObservation,
                "profiling interval state could not be retained");
        }
    }
    if (cancelled(should_abort) ||
        raw.error == ProfilingIntervalSourceError::Cancelled) {
        state_->cleanup();
        return recorder_result(
            ProfilingIntervalRecorderStatus::Cancelled,
            raw.diagnostic.empty() ? "profiling interval recording cancelled"
                                   : std::move(raw.diagnostic));
    }
    if (raw.error != ProfilingIntervalSourceError::None) {
        return recorder_result(
            ProfilingIntervalRecorderStatus::EvidenceUnavailable,
            raw.diagnostic.empty()
                ? "profiling interval observation source is unavailable"
                : std::move(raw.diagnostic));
    }

    SteadyClock::time_point finished;
    try {
        finished = state_->clock.monotonic_now();
    } catch (...) {
        return state_->fail_active(
            ProfilingIntervalRecorderStatus::InvalidObservation,
            "profiling interval clock is unavailable");
    }

    if (raw.token.opaque_id == 0 || !raw.diagnostic.empty() ||
        !digest_is_valid(state_->source_epoch_sha256) ||
        raw.checkpoint.owner_scope_set_sha256 !=
            state_->owner_scope_set_sha256 ||
        raw.checkpoint.event_semantics_revision_sha256 !=
            state_->contract.interval.event_semantics_revision_sha256) {
        return state_->fail_active(
            ProfilingIntervalRecorderStatus::InvalidObservation,
            "profiling interval begin checkpoint is incoherent");
    }
    try {
        const auto timing = acquisition_timing(state_->contract, started,
                                               observed_time, finished);
        if (timing &&
            state_->initialize_segment(state_->current_event_watermark) &&
            state_->record_frame(raw.checkpoint, *timing, true)) {
            return recorder_result(ProfilingIntervalRecorderStatus::Ok);
        }
    } catch (...) {
        return state_->fail_active(
            ProfilingIntervalRecorderStatus::EvidenceUnavailable,
            "profiling interval evidence processing failed");
    }
    return state_->fail_active(
        ProfilingIntervalRecorderStatus::InvalidObservation,
        "profiling interval begin checkpoint does not close");
}

ProfilingIntervalRecorderResult ProfilingIntervalRecorder::poll(
    const ProfilingCancellationCheck &should_abort) {
    return state_->read(false, false, should_abort);
}

ProfilingIntervalRecorderResult ProfilingIntervalRecorder::checkpoint(
    const ProfilingCancellationCheck &should_abort) {
    return state_->read(true, false, should_abort);
}

ProfilingIntervalRecorderResult ProfilingIntervalRecorder::finish() {
    return state_->read(true, true, {});
}

bool ProfilingIntervalRecorder::active() const noexcept {
    return state_->is_active;
}

} // namespace lemon::residency
