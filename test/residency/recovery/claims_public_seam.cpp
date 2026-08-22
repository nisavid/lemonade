#include "lemon/residency/claims.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
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

ClaimFamilyClosure family(ClaimFamily kind, std::vector<ClaimAmount> entries) {
    return ClaimFamilyClosure{
        kind,
        entries.empty() ? ClaimCompleteness::KnownZero : ClaimCompleteness::Bounded,
        std::move(entries),
    };
}

std::vector<ClaimFamilyClosure>
closure(std::vector<ClaimAmount> capacity = {}, std::vector<ClaimAmount> safety = {},
        std::vector<ClaimAmount> cardinality = {},
        std::vector<ClaimAmount> compatibility = {}) {
    return {
        family(ClaimFamily::CompatibilityExclusivity, std::move(compatibility)),
        family(ClaimFamily::CardinalityPool, std::move(cardinality)),
        family(ClaimFamily::SafetyFloor, std::move(safety)),
        family(ClaimFamily::ConsumableCapacity, std::move(capacity)),
    };
}

CheckedClaimSet checked(std::vector<ClaimFamilyClosure> value) {
    auto result = check_claim_closure(std::move(value));
    require(result.accepted(), "complete claim closure was rejected");
    require(result.claims.has_value(), "accepted closure omitted checked claims");
    return *std::move(result.claims);
}

std::uint64_t amount(const CheckedClaimSet &claims, ClaimFamily family,
                     std::string_view constraint_id) {
    const auto &entries = claims.entries(family);
    const auto found = std::find_if(entries.begin(), entries.end(), [&](const ClaimTotal &entry) {
        return entry.constraint_id == constraint_id;
    });
    return found == entries.end() ? 0 : found->amount;
}

void require_rejected(const CheckedClaimSetResult &result, ClaimStatus status,
                      std::string_view message) {
    require(!result.accepted(), message);
    require(result.status == status, "claim-set rejection used the wrong stable status");
    require(!result.claims.has_value(), "claim-set rejection returned a value");
}

void require_checked_arithmetic() {
    auto canonical = checked(closure(
        {ClaimAmount{"gpu/z", ClaimUnit::Bytes, 2},
         ClaimAmount{"gpu/a", ClaimUnit::Bytes, 3}},
        {}, {ClaimAmount{"model-type/llm", ClaimUnit::Count, 1}}));
    const auto &capacity = canonical.entries(ClaimFamily::ConsumableCapacity);
    require(capacity.size() == 2 && capacity[0].constraint_id == "gpu/a" &&
                capacity[1].constraint_id == "gpu/z",
            "claim keys were not canonicalized deterministically");
    require(canonical.entries(ClaimFamily::SafetyFloor).empty(),
            "empty safety-floor family was not represented");
    require(canonical.entries(ClaimFamily::CompatibilityExclusivity).empty(),
            "empty compatibility family was not represented");

    auto incomplete = closure();
    incomplete[2].completeness = ClaimCompleteness::Unknown;
    require_rejected(check_claim_closure(std::move(incomplete)),
                     ClaimStatus::IncompleteClaimClosure,
                     "unknown claim closure was accepted");

    require_rejected(
        check_claim_closure(closure({ClaimAmount{"gpu/gtt", ClaimUnit::Count, 1}})),
        ClaimStatus::UnitMismatch, "unit mismatch was accepted");
    require_rejected(check_claim_closure(closure(
                         {}, {}, {}, {ClaimAmount{"npu/exclusive", ClaimUnit::Count, 2}})),
                     ClaimStatus::InvalidClaimClosure,
                     "non-unit compatibility claim was accepted");

    auto near_limit = checked(closure({ClaimAmount{
        "gpu/gtt", ClaimUnit::Bytes, std::numeric_limits<std::uint64_t>::max() - 2}}));
    auto three = checked(closure({ClaimAmount{"gpu/gtt", ClaimUnit::Bytes, 3}}));
    require_rejected(checked_add(near_limit, three), ClaimStatus::Overflow,
                     "overflowing addition was accepted");

    auto ten = checked(closure({ClaimAmount{"gpu/gtt", ClaimUnit::Bytes, 10}}));
    auto eleven = checked(closure({ClaimAmount{"gpu/gtt", ClaimUnit::Bytes, 11}}));
    require_rejected(checked_subtract(ten, eleven), ClaimStatus::Underflow,
                     "underflowing subtraction was accepted");

    auto scenario_a = checked(closure(
        {ClaimAmount{"gpu/gtt", ClaimUnit::Bytes, 4096}}, {},
        {ClaimAmount{"model-type/llm", ClaimUnit::Count, 1}},
        {ClaimAmount{"npu/a", ClaimUnit::Count, 1}}));
    auto scenario_b = checked(closure(
        {ClaimAmount{"gpu/gtt", ClaimUnit::Bytes, 6144},
         ClaimAmount{"gpu/vram", ClaimUnit::Bytes, 2048}},
        {}, {ClaimAmount{"model-type/llm", ClaimUnit::Count, 1}},
        {ClaimAmount{"npu/b", ClaimUnit::Count, 1}}));
    auto maximum = checked_maximum({scenario_a, scenario_b});
    require(maximum.accepted() && maximum.claims.has_value(),
            "finite plausible alternatives were rejected");
    require(amount(*maximum.claims, ClaimFamily::ConsumableCapacity, "gpu/gtt") == 6144 &&
                amount(*maximum.claims, ClaimFamily::ConsumableCapacity, "gpu/vram") == 2048 &&
                amount(*maximum.claims, ClaimFamily::CardinalityPool, "model-type/llm") == 1 &&
                amount(*maximum.claims, ClaimFamily::CompatibilityExclusivity, "npu/a") == 1 &&
                amount(*maximum.claims, ClaimFamily::CompatibilityExclusivity, "npu/b") == 1,
            "maximum plausible closure did not preserve every alternative scope");
}

std::vector<ClaimSource> worked_sources() {
    return {
        ClaimSource{"resident/current", ClaimViewKind::Current,
                    checked(closure({ClaimAmount{"gpu/gtt", ClaimUnit::Bytes, 4096}}, {},
                                    {ClaimAmount{"model-type/llm", ClaimUnit::Count, 1}}))},
        ClaimSource{"load/provisional", ClaimViewKind::Provisional,
                    checked(closure({ClaimAmount{"gpu/gtt", ClaimUnit::Bytes, 2048}},
                                    {ClaimAmount{"host/floor", ClaimUnit::Bytes, 1024}},
                                    {ClaimAmount{"model-type/llm", ClaimUnit::Count, 1}},
                                    {ClaimAmount{"npu/exclusive", ClaimUnit::Count, 1}}))},
        ClaimSource{"request/retained", ClaimViewKind::Retained,
                    checked(closure({ClaimAmount{"gpu/gtt", ClaimUnit::Bytes, 512}},
                                    {ClaimAmount{"host/floor", ClaimUnit::Bytes, 512}}))},
        ClaimSource{"resident/quarantine", ClaimViewKind::Quarantine,
                    checked(closure({ClaimAmount{"gpu/gtt", ClaimUnit::Bytes, 3072}},
                                    {ClaimAmount{"host/floor", ClaimUnit::Bytes, 1536}},
                                    {ClaimAmount{"model-type/llm", ClaimUnit::Count, 1}},
                                    {ClaimAmount{"npu/exclusive", ClaimUnit::Count, 1}}))},
    };
}

void require_worked_projection() {
    auto projected = project_claims(worked_sources());
    require(projected.accepted() && projected.views.has_value(),
            "worked claim projection was rejected");
    const auto &views = *projected.views;
    require(amount(views.current, ClaimFamily::ConsumableCapacity, "gpu/gtt") == 4096,
            "current projection drifted");
    require(amount(views.provisional, ClaimFamily::ConsumableCapacity, "gpu/gtt") == 2048,
            "provisional projection drifted");
    require(amount(views.retained, ClaimFamily::ConsumableCapacity, "gpu/gtt") == 512,
            "retained projection drifted");
    require(amount(views.quarantine, ClaimFamily::ConsumableCapacity, "gpu/gtt") == 3072,
            "quarantine projection drifted");
    require(amount(views.conservative_overlay, ClaimFamily::ConsumableCapacity, "gpu/gtt") ==
                9728 &&
                amount(views.conservative_overlay, ClaimFamily::SafetyFloor, "host/floor") ==
                    3072 &&
                amount(views.conservative_overlay, ClaimFamily::CardinalityPool,
                       "model-type/llm") == 3 &&
                amount(views.conservative_overlay, ClaimFamily::CompatibilityExclusivity,
                       "npu/exclusive") == 2,
            "conservative overlay did not sum the four distinct views");

    auto shuffled = worked_sources();
    std::reverse(shuffled.begin(), shuffled.end());
    auto reordered = project_claims(std::move(shuffled));
    require(reordered.accepted() && reordered.views.has_value() &&
                reordered.views->conservative_overlay == views.conservative_overlay,
            "projection depended on source order");

    auto duplicate = worked_sources();
    duplicate.push_back(duplicate.front());
    auto rejected = project_claims(std::move(duplicate));
    require(!rejected.accepted() && rejected.status == ClaimStatus::DuplicateSource &&
                !rejected.views.has_value(),
            "duplicate claim source was not rejected atomically");
}

void require_checked_transfers_and_release() {
    auto sources = worked_sources();
    const auto before = project_claims(sources);
    auto retained_claims = sources[2].claims;
    auto committed = checked_commit_transfer(std::move(sources), "request/retained",
                                             ClaimViewKind::Retained, retained_claims);
    require(committed.accepted() && committed.sources.has_value(),
            "verified retained-to-current transfer was rejected");
    auto after = project_claims(*committed.sources);
    require(after.accepted() && after.views.has_value() && before.views.has_value() &&
                amount(after.views->retained, ClaimFamily::ConsumableCapacity, "gpu/gtt") == 0 &&
                amount(after.views->current, ClaimFamily::ConsumableCapacity, "gpu/gtt") == 4608 &&
                after.views->conservative_overlay == before.views->conservative_overlay,
            "verified transfer changed the conservative overlay");

    auto invalid_transfer = checked_commit_transfer(
        *committed.sources, "resident/quarantine", ClaimViewKind::Quarantine,
        checked(closure({ClaimAmount{"gpu/gtt", ClaimUnit::Bytes, 3072}})));
    require(!invalid_transfer.accepted() &&
                invalid_transfer.status == ClaimStatus::InvalidTransfer &&
                !invalid_transfer.sources.has_value(),
            "quarantine was reactivated through a commit transfer");

    std::vector<ClaimSource> ambiguity_sources{
        ClaimSource{"resident/current", ClaimViewKind::Current,
                    checked(closure({ClaimAmount{"gpu/gtt", ClaimUnit::Bytes, 4096}}, {},
                                    {ClaimAmount{"model-type/llm", ClaimUnit::Count, 1}}))},
    };
    auto ambiguous = checked_quarantine_transfer(
        std::move(ambiguity_sources), "resident/current", ClaimViewKind::Current,
        {checked(closure({ClaimAmount{"gpu/gtt", ClaimUnit::Bytes, 8192}}, {},
                         {ClaimAmount{"model-type/llm", ClaimUnit::Count, 1}})),
         checked(closure({ClaimAmount{"gpu/vram", ClaimUnit::Bytes, 2048}}, {},
                         {ClaimAmount{"model-type/llm", ClaimUnit::Count, 1}}))});
    require(ambiguous.accepted() && ambiguous.sources.has_value(),
            "maximum-plausible quarantine transfer was rejected");
    auto quarantined = project_claims(*ambiguous.sources);
    require(quarantined.accepted() && quarantined.views.has_value() &&
                amount(quarantined.views->quarantine, ClaimFamily::ConsumableCapacity,
                       "gpu/gtt") == 8192 &&
                amount(quarantined.views->quarantine, ClaimFamily::ConsumableCapacity,
                       "gpu/vram") == 2048,
            "quarantine transfer dropped an existing or plausible claim");

    auto released = checked_release(*ambiguous.sources, "resident/current",
                                    ClaimViewKind::Quarantine);
    require(released.accepted() && released.sources.has_value(),
            "whole-source verified quarantine release was rejected");
    auto duplicate_release = checked_release(*released.sources, "resident/current",
                                              ClaimViewKind::Quarantine);
    require(!duplicate_release.accepted() &&
                duplicate_release.status == ClaimStatus::InvalidRelease &&
                !duplicate_release.sources.has_value(),
            "duplicate source-identity release was accepted");
}

} // namespace

int main() {
    require_checked_arithmetic();
    require_worked_projection();
    require_checked_transfers_and_release();
    return 0;
}
