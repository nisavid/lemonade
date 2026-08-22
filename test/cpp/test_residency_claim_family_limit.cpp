#include "lemon/residency/claims.h"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace lemon::residency;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

ClaimFamilyClosure empty_family(ClaimFamily family) {
    return ClaimFamilyClosure{family, ClaimCompleteness::KnownZero, {}};
}

CheckedClaimSet full_capacity_family(std::string_view prefix) {
    std::vector<ClaimAmount> entries;
    entries.reserve(max_journal_array_entries);
    for (std::size_t index = 0; index < max_journal_array_entries; ++index) {
        entries.push_back(
            ClaimAmount{std::string(prefix) + std::to_string(index), ClaimUnit::Bytes, 1});
    }

    auto result = check_claim_closure({
        ClaimFamilyClosure{ClaimFamily::ConsumableCapacity, ClaimCompleteness::Bounded,
                           std::move(entries)},
        empty_family(ClaimFamily::SafetyFloor),
        empty_family(ClaimFamily::CardinalityPool),
        empty_family(ClaimFamily::CompatibilityExclusivity),
    });
    require(result.accepted() && result.claims.has_value(),
            "individually bounded claim family was rejected");
    return *std::move(result.claims);
}

void require_rejected(const CheckedClaimSetResult &result, std::string_view operation) {
    require(!result.accepted(), operation);
    require(result.status == ClaimStatus::LimitExceeded,
            "oversized claim family used the wrong stable status");
    require(!result.claims.has_value(), "claim-set rejection returned a partial value");
}

void require_rejected(const ClaimProjectionResult &result, std::string_view operation) {
    require(!result.accepted(), operation);
    require(result.status == ClaimStatus::LimitExceeded,
            "oversized projection used the wrong stable status");
    require(!result.views.has_value(), "projection rejection returned a partial value");
}

void require_rejected(const ClaimSourcesResult &result, std::string_view operation) {
    require(!result.accepted(), operation);
    require(result.status == ClaimStatus::LimitExceeded,
            "oversized transfer used the wrong stable status");
    require(!result.sources.has_value(), "transfer rejection returned a partial value");
}

} // namespace

int main() {
    const auto first = full_capacity_family("gpu/first/");
    const auto second = full_capacity_family("gpu/second/");

    const auto added = checked_add(first, second);
    const auto maximum = checked_maximum({first, second});
    const auto projected = project_claims({
        ClaimSource{"resident/first", ClaimViewKind::Current, first},
        ClaimSource{"resident/second", ClaimViewKind::Current, second},
    });
    const auto quarantined = checked_quarantine_transfer(
        {ClaimSource{"resident/first", ClaimViewKind::Current, first}}, "resident/first",
        ClaimViewKind::Current, {second});

    require_rejected(added, "addition accepted an oversized claim family");
    require_rejected(maximum, "maximum accepted an oversized claim family");
    require_rejected(projected, "projection accepted an oversized claim family");
    require_rejected(quarantined, "quarantine transfer accepted an oversized claim family");
    return 0;
}
