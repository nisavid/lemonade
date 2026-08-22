#include "lemon/residency/claims.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lemon::residency {

namespace {

constexpr std::size_t claim_view_count = 4;

class ClaimFailure final : public std::runtime_error {
public:
    ClaimFailure(ClaimStatus status, std::string message)
        : std::runtime_error(std::move(message)), status_(status) {}

    ClaimStatus status() const noexcept { return status_; }

private:
    ClaimStatus status_;
};

[[noreturn]] void reject(ClaimStatus status, std::string message) {
    throw ClaimFailure(status, std::move(message));
}

std::string bounded_diagnostic(std::string message) {
    if (message.size() > max_journal_diagnostic_bytes) {
        std::size_t boundary = max_journal_diagnostic_bytes;
        while (boundary > 0 &&
               (static_cast<unsigned char>(message[boundary]) & 0xc0) == 0x80) {
            --boundary;
        }
        message.resize(boundary);
    }
    return message;
}

bool identifier_is_valid(std::string_view value) {
    return !value.empty() && value.size() <= max_journal_identifier_bytes &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return character >= 0x21 && character <= 0x7e;
           });
}

void require_identifier(std::string_view value) {
    if (!identifier_is_valid(value)) {
        reject(ClaimStatus::InvalidIdentifier, "claim source or constraint identifier is invalid");
    }
}

void require_claim_family_size(std::size_t size) {
    if (size > max_journal_array_entries) {
        reject(ClaimStatus::LimitExceeded, "claim family exceeds the entry limit");
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

ClaimFamily family_at(std::size_t index) {
    switch (index) {
    case 0:
        return ClaimFamily::ConsumableCapacity;
    case 1:
        return ClaimFamily::SafetyFloor;
    case 2:
        return ClaimFamily::CardinalityPool;
    case 3:
        return ClaimFamily::CompatibilityExclusivity;
    default:
        reject(ClaimStatus::InvalidClaimClosure, "claim family is invalid");
    }
}

ClaimUnit expected_unit(ClaimFamily family) {
    switch (family) {
    case ClaimFamily::ConsumableCapacity:
    case ClaimFamily::SafetyFloor:
        return ClaimUnit::Bytes;
    case ClaimFamily::CardinalityPool:
    case ClaimFamily::CompatibilityExclusivity:
        return ClaimUnit::Count;
    }
    reject(ClaimStatus::InvalidClaimClosure, "claim family is invalid");
}

std::optional<std::size_t> view_index(ClaimViewKind view) noexcept {
    switch (view) {
    case ClaimViewKind::Current:
        return 0;
    case ClaimViewKind::Provisional:
        return 1;
    case ClaimViewKind::Retained:
        return 2;
    case ClaimViewKind::Quarantine:
        return 3;
    }
    return std::nullopt;
}

using ClaimMaps = std::array<std::map<std::string, std::uint64_t>, claim_family_count>;

struct ClaimSetParts {
    std::array<std::vector<ClaimTotal>, claim_family_count> entries;
    std::array<ClaimCompleteness, claim_family_count> completeness;
};

ClaimMaps claim_maps(const CheckedClaimSet &claims) {
    ClaimMaps maps;
    for (std::size_t index = 0; index < claim_family_count; ++index) {
        for (const auto &entry : claims.entries(family_at(index))) {
            maps[index].emplace(entry.constraint_id, entry.amount);
        }
    }
    return maps;
}

ClaimSetParts claim_set_parts(
    const ClaimMaps &maps,
    std::array<ClaimCompleteness, claim_family_count> empty_states) {
    ClaimSetParts parts;
    for (std::size_t index = 0; index < claim_family_count; ++index) {
        require_claim_family_size(maps[index].size());
        parts.entries[index].reserve(maps[index].size());
        for (const auto &[constraint_id, amount] : maps[index]) {
            parts.entries[index].push_back(
                ClaimTotal{constraint_id, expected_unit(family_at(index)), amount});
        }
        parts.completeness[index] = parts.entries[index].empty() ? empty_states[index]
                                                                 : ClaimCompleteness::Bounded;
    }
    return parts;
}

std::vector<ClaimSource> checked_sources(std::vector<ClaimSource> sources) {
    if (sources.size() > max_journal_array_entries) {
        reject(ClaimStatus::LimitExceeded, "claim source list exceeds the limit");
    }
    for (const auto &source : sources) {
        require_identifier(source.source_id);
        if (!view_index(source.view).has_value()) {
            reject(ClaimStatus::InvalidTransfer, "claim source view is invalid");
        }
    }
    std::sort(sources.begin(), sources.end(),
              [](const ClaimSource &left, const ClaimSource &right) {
                  return left.source_id < right.source_id;
              });
    if (std::adjacent_find(sources.begin(), sources.end(),
                           [](const ClaimSource &left, const ClaimSource &right) {
                               return left.source_id == right.source_id;
                           }) != sources.end()) {
        reject(ClaimStatus::DuplicateSource, "claim source identifier is duplicated");
    }
    return sources;
}

std::vector<ClaimSource>::iterator find_source(std::vector<ClaimSource> &sources,
                                               std::string_view source_id) {
    return std::find_if(sources.begin(), sources.end(), [&](const ClaimSource &source) {
        return source.source_id == source_id;
    });
}

CheckedClaimSet require_claims(CheckedClaimSetResult result) {
    if (!result.accepted()) {
        reject(result.status, std::move(result.diagnostic));
    }
    return *std::move(result.claims);
}

CheckedClaimSet known_zero_claims() {
    return require_claims(checked_maximum({}));
}

CheckedClaimSet add_checked(const CheckedClaimSet &left, const CheckedClaimSet &right) {
    return require_claims(checked_add(left, right));
}

CheckedClaimSet maximum_checked(const CheckedClaimSet &left, const CheckedClaimSet &right) {
    return require_claims(checked_maximum({left, right}));
}

CheckedClaimSet value_or_zero(const std::optional<CheckedClaimSet> &value) {
    return value.has_value() ? *value : known_zero_claims();
}

CheckedClaimSetResult rejected_claims(const ClaimFailure &failure) {
    return CheckedClaimSetResult{failure.status(), bounded_diagnostic(failure.what()),
                                 std::nullopt};
}

ClaimProjectionResult rejected_projection(const ClaimFailure &failure) {
    return ClaimProjectionResult{failure.status(), bounded_diagnostic(failure.what()),
                                 std::nullopt};
}

ClaimSourcesResult rejected_sources(const ClaimFailure &failure) {
    return ClaimSourcesResult{failure.status(), bounded_diagnostic(failure.what()), std::nullopt};
}

} // namespace

bool ClaimTotal::operator==(const ClaimTotal &other) const noexcept {
    return constraint_id == other.constraint_id && unit == other.unit && amount == other.amount;
}

bool ClaimTotal::operator!=(const ClaimTotal &other) const noexcept {
    return !(*this == other);
}

CheckedClaimSet::CheckedClaimSet(
    std::array<std::vector<ClaimTotal>, claim_family_count> entries,
    std::array<ClaimCompleteness, claim_family_count> completeness)
    : entries_(std::move(entries)), completeness_(completeness) {
    for (const auto &family_entries : entries_) {
        require_claim_family_size(family_entries.size());
    }
}

CheckedClaimSet::CheckedClaimSet(CheckedClaimSet &&other) noexcept
    : entries_(std::move(other.entries_)), completeness_(other.completeness_) {
    other.entries_ = {};
    other.completeness_ = {ClaimCompleteness::KnownZero, ClaimCompleteness::KnownZero,
                           ClaimCompleteness::KnownZero, ClaimCompleteness::KnownZero};
}

CheckedClaimSet &CheckedClaimSet::operator=(CheckedClaimSet &&other) noexcept {
    if (this != &other) {
        entries_ = std::move(other.entries_);
        completeness_ = other.completeness_;
        other.entries_ = {};
        other.completeness_ = {ClaimCompleteness::KnownZero, ClaimCompleteness::KnownZero,
                               ClaimCompleteness::KnownZero, ClaimCompleteness::KnownZero};
    }
    return *this;
}

CheckedClaimSet CheckedClaimSet::known_zero() {
    return CheckedClaimSet(
        {},
        {ClaimCompleteness::KnownZero, ClaimCompleteness::KnownZero,
         ClaimCompleteness::KnownZero, ClaimCompleteness::KnownZero});
}

CheckedClaimSet CheckedClaimSet::add_sets(const CheckedClaimSet &left,
                                          const CheckedClaimSet &right) {
    auto totals = claim_maps(left);
    std::array<ClaimCompleteness, claim_family_count> empty_states;
    for (std::size_t index = 0; index < claim_family_count; ++index) {
        const auto family = family_at(index);
        for (const auto &entry : right.entries(family)) {
            auto &total = totals[index][entry.constraint_id];
            if (entry.amount > std::numeric_limits<std::uint64_t>::max() - total) {
                reject(ClaimStatus::Overflow, "claim addition overflowed");
            }
            total += entry.amount;
        }
        empty_states[index] = left.completeness(family) == ClaimCompleteness::NotApplicable &&
                                      right.completeness(family) ==
                                          ClaimCompleteness::NotApplicable
                                  ? ClaimCompleteness::NotApplicable
                                  : ClaimCompleteness::KnownZero;
    }
    auto parts = claim_set_parts(totals, empty_states);
    return CheckedClaimSet(std::move(parts.entries), parts.completeness);
}

CheckedClaimSet CheckedClaimSet::subtract_sets(const CheckedClaimSet &left,
                                               const CheckedClaimSet &right) {
    auto totals = claim_maps(left);
    std::array<ClaimCompleteness, claim_family_count> empty_states;
    for (std::size_t index = 0; index < claim_family_count; ++index) {
        const auto family = family_at(index);
        for (const auto &entry : right.entries(family)) {
            const auto found = totals[index].find(entry.constraint_id);
            if (found == totals[index].end() || found->second < entry.amount) {
                reject(ClaimStatus::Underflow, "claim subtraction underflowed");
            }
            found->second -= entry.amount;
            if (found->second == 0) {
                totals[index].erase(found);
            }
        }
        empty_states[index] = left.completeness(family) == ClaimCompleteness::NotApplicable
                                  ? ClaimCompleteness::NotApplicable
                                  : ClaimCompleteness::KnownZero;
    }
    auto parts = claim_set_parts(totals, empty_states);
    return CheckedClaimSet(std::move(parts.entries), parts.completeness);
}

CheckedClaimSet CheckedClaimSet::maximum_sets(const CheckedClaimSet &left,
                                              const CheckedClaimSet &right) {
    auto totals = claim_maps(left);
    std::array<ClaimCompleteness, claim_family_count> empty_states;
    for (std::size_t index = 0; index < claim_family_count; ++index) {
        const auto family = family_at(index);
        for (const auto &entry : right.entries(family)) {
            auto &total = totals[index][entry.constraint_id];
            total = std::max(total, entry.amount);
        }
        empty_states[index] = left.completeness(family) == ClaimCompleteness::NotApplicable &&
                                      right.completeness(family) ==
                                          ClaimCompleteness::NotApplicable
                                  ? ClaimCompleteness::NotApplicable
                                  : ClaimCompleteness::KnownZero;
    }
    auto parts = claim_set_parts(totals, empty_states);
    return CheckedClaimSet(std::move(parts.entries), parts.completeness);
}

const std::vector<ClaimTotal> &CheckedClaimSet::entries(ClaimFamily family) const noexcept {
    static const std::vector<ClaimTotal> empty;
    const auto index = family_index(family);
    return index.has_value() ? entries_[*index] : empty;
}

ClaimCompleteness CheckedClaimSet::completeness(ClaimFamily family) const noexcept {
    const auto index = family_index(family);
    return index.has_value() ? completeness_[*index] : ClaimCompleteness::Unknown;
}

bool CheckedClaimSet::operator==(const CheckedClaimSet &other) const {
    return entries_ == other.entries_ && completeness_ == other.completeness_;
}

bool CheckedClaimSet::operator!=(const CheckedClaimSet &other) const {
    return !(*this == other);
}

bool CheckedClaimSetResult::accepted() const noexcept {
    return status == ClaimStatus::Accepted && claims.has_value();
}

CheckedClaimSetResult check_claim_closure(std::vector<ClaimFamilyClosure> closure) {
    try {
        if (closure.size() != claim_family_count) {
            reject(ClaimStatus::InvalidClaimClosure,
                   "claim closure must contain exactly four families");
        }

        std::array<bool, claim_family_count> seen{};
        std::array<std::vector<ClaimTotal>, claim_family_count> entries;
        std::array<ClaimCompleteness, claim_family_count> completeness;
        for (auto &family : closure) {
            const auto index = family_index(family.family);
            if (!index.has_value() || seen[*index]) {
                reject(ClaimStatus::InvalidClaimClosure,
                       "claim closure has a duplicate or invalid family");
            }
            seen[*index] = true;
            require_claim_family_size(family.entries.size());
            switch (family.completeness) {
            case ClaimCompleteness::Unknown:
                reject(ClaimStatus::IncompleteClaimClosure,
                       "unknown claim family cannot be projected numerically");
            case ClaimCompleteness::NotApplicable:
            case ClaimCompleteness::KnownZero:
                if (!family.entries.empty()) {
                    reject(ClaimStatus::InvalidClaimClosure,
                           "empty claim completeness contains entries");
                }
                break;
            case ClaimCompleteness::Bounded:
                if (family.entries.empty()) {
                    reject(ClaimStatus::InvalidClaimClosure,
                           "bounded claim family contains no entries");
                }
                break;
            default:
                reject(ClaimStatus::InvalidClaimClosure, "claim completeness is invalid");
            }

            const auto unit = expected_unit(family.family);
            for (const auto &entry : family.entries) {
                require_identifier(entry.constraint_id);
                if (entry.unit != unit) {
                    reject(ClaimStatus::UnitMismatch, "claim amount uses the wrong unit");
                }
                if (entry.amount == 0 ||
                    (family.family == ClaimFamily::CompatibilityExclusivity &&
                     entry.amount != 1)) {
                    reject(ClaimStatus::InvalidClaimClosure, "claim amount is invalid");
                }
            }
            std::sort(family.entries.begin(), family.entries.end(),
                      [](const ClaimAmount &left, const ClaimAmount &right) {
                          return left.constraint_id < right.constraint_id;
                      });
            if (std::adjacent_find(family.entries.begin(), family.entries.end(),
                                   [](const ClaimAmount &left, const ClaimAmount &right) {
                                       return left.constraint_id == right.constraint_id;
                                   }) != family.entries.end()) {
                reject(ClaimStatus::InvalidClaimClosure,
                       "claim closure contains a duplicate constraint");
            }
            completeness[*index] = family.completeness;
            entries[*index].reserve(family.entries.size());
            for (auto &entry : family.entries) {
                entries[*index].push_back(ClaimTotal{std::move(entry.constraint_id), entry.unit,
                                                     entry.amount});
            }
        }
        return CheckedClaimSetResult{
            ClaimStatus::Accepted,
            {},
            CheckedClaimSet(std::move(entries), completeness),
        };
    } catch (const ClaimFailure &failure) {
        return rejected_claims(failure);
    }
}

CheckedClaimSetResult checked_add(const CheckedClaimSet &left, const CheckedClaimSet &right) {
    try {
        return CheckedClaimSetResult{ClaimStatus::Accepted, {},
                                     CheckedClaimSet::add_sets(left, right)};
    } catch (const ClaimFailure &failure) {
        return rejected_claims(failure);
    }
}

CheckedClaimSetResult checked_subtract(const CheckedClaimSet &left,
                                      const CheckedClaimSet &right) {
    try {
        return CheckedClaimSetResult{ClaimStatus::Accepted, {},
                                     CheckedClaimSet::subtract_sets(left, right)};
    } catch (const ClaimFailure &failure) {
        return rejected_claims(failure);
    }
}

CheckedClaimSetResult checked_maximum(std::vector<CheckedClaimSet> alternatives) {
    try {
        if (alternatives.size() > max_journal_array_entries) {
            reject(ClaimStatus::LimitExceeded, "claim alternatives exceed the limit");
        }
        if (alternatives.empty()) {
            return CheckedClaimSetResult{ClaimStatus::Accepted, {}, CheckedClaimSet::known_zero()};
        }
        auto maximum = std::move(alternatives.front());
        for (std::size_t index = 1; index < alternatives.size(); ++index) {
            maximum = CheckedClaimSet::maximum_sets(maximum, alternatives[index]);
        }
        return CheckedClaimSetResult{ClaimStatus::Accepted, {}, std::move(maximum)};
    } catch (const ClaimFailure &failure) {
        return rejected_claims(failure);
    }
}

bool ClaimProjectionResult::accepted() const noexcept {
    return status == ClaimStatus::Accepted && views.has_value();
}

bool ClaimSourcesResult::accepted() const noexcept {
    return status == ClaimStatus::Accepted && sources.has_value();
}

ClaimProjectionResult project_claims(std::vector<ClaimSource> sources) {
    try {
        sources = checked_sources(std::move(sources));
        std::array<std::optional<CheckedClaimSet>, claim_view_count> projections;
        std::optional<CheckedClaimSet> overlay;
        for (const auto &source : sources) {
            const auto index = *view_index(source.view);
            projections[index] = projections[index].has_value()
                                     ? add_checked(*projections[index], source.claims)
                                     : source.claims;
            overlay = overlay.has_value() ? add_checked(*overlay, source.claims) : source.claims;
        }
        return ClaimProjectionResult{
            ClaimStatus::Accepted,
            {},
            ClaimProjectionViews{
                value_or_zero(projections[0]),
                value_or_zero(projections[1]),
                value_or_zero(projections[2]),
                value_or_zero(projections[3]),
                value_or_zero(overlay),
            },
        };
    } catch (const ClaimFailure &failure) {
        return rejected_projection(failure);
    }
}

ClaimSourcesResult checked_commit_transfer(std::vector<ClaimSource> sources,
                                           std::string_view source_id,
                                           ClaimViewKind expected_view,
                                           const CheckedClaimSet &verified_claims) {
    try {
        require_identifier(source_id);
        const std::string stable_source_id(source_id);
        const CheckedClaimSet stable_verified_claims(verified_claims);
        sources = checked_sources(std::move(sources));
        const auto source = find_source(sources, stable_source_id);
        if (source == sources.end() || source->view != expected_view ||
            (expected_view != ClaimViewKind::Provisional &&
             expected_view != ClaimViewKind::Retained) ||
            source->claims != stable_verified_claims) {
            reject(ClaimStatus::InvalidTransfer, "verified commit transfer is invalid");
        }
        source->view = ClaimViewKind::Current;
        return ClaimSourcesResult{ClaimStatus::Accepted, {}, std::move(sources)};
    } catch (const ClaimFailure &failure) {
        return rejected_sources(failure);
    }
}

ClaimSourcesResult
checked_quarantine_transfer(std::vector<ClaimSource> sources, std::string_view source_id,
                            ClaimViewKind expected_view,
                            const std::vector<CheckedClaimSet> &plausible_claims) {
    try {
        require_identifier(source_id);
        const std::string stable_source_id(source_id);
        if (plausible_claims.size() > max_journal_array_entries) {
            reject(ClaimStatus::LimitExceeded, "plausible claim alternatives exceed the limit");
        }
        sources = checked_sources(std::move(sources));
        const auto source = find_source(sources, stable_source_id);
        if (source == sources.end() || source->view != expected_view ||
            !view_index(expected_view).has_value()) {
            reject(ClaimStatus::InvalidTransfer, "quarantine transfer is invalid");
        }
        auto maximum = source->claims;
        for (const auto &plausible : plausible_claims) {
            maximum = maximum_checked(maximum, plausible);
        }
        source->claims = std::move(maximum);
        source->view = ClaimViewKind::Quarantine;
        return ClaimSourcesResult{ClaimStatus::Accepted, {}, std::move(sources)};
    } catch (const ClaimFailure &failure) {
        return rejected_sources(failure);
    }
}

ClaimSourcesResult checked_release(std::vector<ClaimSource> sources, std::string_view source_id,
                                   ClaimViewKind expected_view) {
    try {
        require_identifier(source_id);
        const std::string stable_source_id(source_id);
        if (!view_index(expected_view).has_value()) {
            reject(ClaimStatus::InvalidRelease, "release view is invalid");
        }
        sources = checked_sources(std::move(sources));
        const auto source = find_source(sources, stable_source_id);
        if (source == sources.end() || source->view != expected_view) {
            reject(ClaimStatus::InvalidRelease, "whole-source release is invalid");
        }
        sources.erase(source);
        return ClaimSourcesResult{ClaimStatus::Accepted, {}, std::move(sources)};
    } catch (const ClaimFailure &failure) {
        return rejected_sources(failure);
    }
}

} // namespace lemon::residency
