#pragma once

#include "lemon/residency/journal.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lemon::residency {

inline constexpr std::size_t claim_family_count = 4;

enum class ClaimStatus {
    Accepted,
    InvalidIdentifier,
    LimitExceeded,
    InvalidClaimClosure,
    IncompleteClaimClosure,
    UnitMismatch,
    Overflow,
    Underflow,
    DuplicateSource,
    InvalidTransfer,
    InvalidRelease,
};

enum class ConstraintEvidenceKind {
    ClaimFamily,
    OwnerCoverage,
};

struct ConstraintEvidenceRequirement {
    ConstraintEvidenceKind kind;
    std::optional<ClaimFamily> family;
};

std::optional<ConstraintEvidenceRequirement>
constraint_evidence_requirement(ConstraintKind constraint) noexcept;

struct ClaimTotal {
    std::string constraint_id;
    ClaimUnit unit = ClaimUnit::Bytes;
    std::uint64_t amount = 0;

    bool operator==(const ClaimTotal &other) const noexcept;
    bool operator!=(const ClaimTotal &other) const noexcept;
};

class CheckedClaimSet;
struct CheckedClaimSetResult;

class CheckedClaimSet {
public:
    CheckedClaimSet() = delete;
    CheckedClaimSet(const CheckedClaimSet &) = default;
    CheckedClaimSet(CheckedClaimSet &&other) noexcept;
    CheckedClaimSet &operator=(const CheckedClaimSet &) = default;
    CheckedClaimSet &operator=(CheckedClaimSet &&other) noexcept;

    const std::vector<ClaimTotal> &entries(ClaimFamily family) const noexcept;
    ClaimCompleteness completeness(ClaimFamily family) const noexcept;

    bool operator==(const CheckedClaimSet &other) const;
    bool operator!=(const CheckedClaimSet &other) const;

private:
    CheckedClaimSet(std::array<std::vector<ClaimTotal>, claim_family_count> entries,
                    std::array<ClaimCompleteness, claim_family_count> completeness);

    static CheckedClaimSet known_zero();
    static CheckedClaimSet add_sets(const CheckedClaimSet &left,
                                    const CheckedClaimSet &right);
    static CheckedClaimSet subtract_sets(const CheckedClaimSet &left,
                                         const CheckedClaimSet &right);
    static CheckedClaimSet maximum_sets(const CheckedClaimSet &left,
                                        const CheckedClaimSet &right);

    std::array<std::vector<ClaimTotal>, claim_family_count> entries_;
    std::array<ClaimCompleteness, claim_family_count> completeness_;

    friend CheckedClaimSetResult check_claim_closure(std::vector<ClaimFamilyClosure> closure);
    friend CheckedClaimSetResult checked_add(const CheckedClaimSet &left,
                                             const CheckedClaimSet &right);
    friend CheckedClaimSetResult checked_subtract(const CheckedClaimSet &left,
                                                  const CheckedClaimSet &right);
    friend CheckedClaimSetResult checked_maximum(std::vector<CheckedClaimSet> alternatives);
};

struct CheckedClaimSetResult {
    ClaimStatus status = ClaimStatus::InvalidClaimClosure;
    std::string diagnostic;
    std::optional<CheckedClaimSet> claims;

    bool accepted() const noexcept;
};

CheckedClaimSetResult check_claim_closure(std::vector<ClaimFamilyClosure> closure);
bool claim_families_cover_ordinary_constraints(
    const CheckedClaimSet &claims,
    const std::vector<ConstraintKind> &constraints) noexcept;
CheckedClaimSetResult checked_add(const CheckedClaimSet &left, const CheckedClaimSet &right);
CheckedClaimSetResult checked_subtract(const CheckedClaimSet &left,
                                      const CheckedClaimSet &right);
CheckedClaimSetResult checked_maximum(std::vector<CheckedClaimSet> alternatives);

enum class ClaimViewKind {
    Current,
    Provisional,
    Retained,
    Quarantine,
};

struct ClaimSource {
    std::string source_id;
    ClaimViewKind view = ClaimViewKind::Current;
    CheckedClaimSet claims;
};

struct ClaimProjectionViews {
    CheckedClaimSet current;
    CheckedClaimSet provisional;
    CheckedClaimSet retained;
    CheckedClaimSet quarantine;
    CheckedClaimSet conservative_overlay;
};

struct ClaimProjectionResult {
    ClaimStatus status = ClaimStatus::InvalidClaimClosure;
    std::string diagnostic;
    std::optional<ClaimProjectionViews> views;

    bool accepted() const noexcept;
};

struct ClaimSourcesResult {
    ClaimStatus status = ClaimStatus::InvalidTransfer;
    std::string diagnostic;
    std::optional<std::vector<ClaimSource>> sources;

    bool accepted() const noexcept;
};

ClaimProjectionResult project_claims(std::vector<ClaimSource> sources);

ClaimSourcesResult checked_commit_transfer(std::vector<ClaimSource> sources,
                                           std::string_view source_id,
                                           ClaimViewKind expected_view,
                                           const CheckedClaimSet &verified_claims);

ClaimSourcesResult
checked_quarantine_transfer(std::vector<ClaimSource> sources, std::string_view source_id,
                            ClaimViewKind expected_view,
                            const std::vector<CheckedClaimSet> &plausible_claims);

ClaimSourcesResult checked_release(std::vector<ClaimSource> sources, std::string_view source_id,
                                   ClaimViewKind expected_view);

} // namespace lemon::residency
