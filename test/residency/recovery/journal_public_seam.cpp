#include "lemon/residency/journal.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace lemon::residency;

constexpr std::string_view genesis_checksum =
    "dae75ca123b7b872994ff29f8661049c9c28a1798b76fcd0015271739c5adf63";
constexpr std::string_view successor_checksum =
    "ec749a93f0e1a98911b9b91d1823f706d5ce16d1f477690b3c5e07b59fd98147";
constexpr std::string_view recovery_evidence =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

constexpr std::string_view genesis_wire =
    R"({"action_lease_claim_ids":["lease/a"],"checksum_sha256":"dae75ca123b7b872994ff29f8661049c9c28a1798b76fcd0015271739c5adf63","claim_closure":[{"completeness":"bounded","entries":[{"amount":4096,"constraint_id":"gpu/gtt","unit":"bytes"}],"family":"consumable_capacity"},{"completeness":"known_zero","entries":[],"family":"safety_floor"},{"completeness":"bounded","entries":[{"amount":2,"constraint_id":"model-type/llm","unit":"count"}],"family":"cardinality_pool"},{"completeness":"not_applicable","entries":[],"family":"compatibility_exclusivity"}],"daemon_epoch":"epoch/7","journal_id":"journal/main","operation":{"family":"resource_lifecycle","kind":"admission","operation_id":"op/1","plan_id":"plan/1"},"ownership_claim_ids":["owner/a"],"predecessor_checksum_sha256":null,"quarantine_origin":null,"recovery_claim_ids":["recovery/a"],"recovery_disposition":null,"resident_id":"resident/alpha","resident_state":"prepared","schema":{"major":1,"minor":0},"sequence":1})";

constexpr std::string_view successor_wire =
    R"({"action_lease_claim_ids":["lease/a"],"checksum_sha256":"ec749a93f0e1a98911b9b91d1823f706d5ce16d1f477690b3c5e07b59fd98147","claim_closure":[{"completeness":"bounded","entries":[{"amount":4096,"constraint_id":"gpu/gtt","unit":"bytes"}],"family":"consumable_capacity"},{"completeness":"known_zero","entries":[],"family":"safety_floor"},{"completeness":"bounded","entries":[{"amount":2,"constraint_id":"model-type/llm","unit":"count"}],"family":"cardinality_pool"},{"completeness":"not_applicable","entries":[],"family":"compatibility_exclusivity"}],"daemon_epoch":"epoch/7","journal_id":"journal/main","operation":{"family":"resource_lifecycle","kind":"admission","operation_id":"op/1","plan_id":"plan/1"},"ownership_claim_ids":["owner/a"],"predecessor_checksum_sha256":"dae75ca123b7b872994ff29f8661049c9c28a1798b76fcd0015271739c5adf63","quarantine_origin":null,"recovery_claim_ids":["recovery/a"],"recovery_disposition":null,"resident_id":"resident/alpha","resident_state":"provisional","schema":{"major":1,"minor":0},"sequence":2})";

constexpr std::string_view interleaved_beta_wire =
    R"({"action_lease_claim_ids":["lease/a"],"checksum_sha256":"f51528eb4d96e562a5a09c5ac314af4bd5a0821106053f0775e76407eba98ff6","claim_closure":[{"completeness":"bounded","entries":[{"amount":4096,"constraint_id":"gpu/gtt","unit":"bytes"}],"family":"consumable_capacity"},{"completeness":"known_zero","entries":[],"family":"safety_floor"},{"completeness":"bounded","entries":[{"amount":2,"constraint_id":"model-type/llm","unit":"count"}],"family":"cardinality_pool"},{"completeness":"not_applicable","entries":[],"family":"compatibility_exclusivity"}],"daemon_epoch":"epoch/7","journal_id":"journal/main","operation":{"family":"resource_lifecycle","kind":"admission","operation_id":"op/1","plan_id":"plan/1"},"ownership_claim_ids":["owner/a"],"predecessor_checksum_sha256":"dae75ca123b7b872994ff29f8661049c9c28a1798b76fcd0015271739c5adf63","quarantine_origin":null,"recovery_claim_ids":["recovery/a"],"recovery_disposition":null,"resident_id":"resident/beta","resident_state":"prepared","schema":{"major":1,"minor":0},"sequence":2})";

constexpr std::string_view interleaved_alpha_wire =
    R"({"action_lease_claim_ids":["lease/a"],"checksum_sha256":"f2f425a0216efbdc71a44778ac0d84ee21bc579377bd69df9fefcc7dd3890ad8","claim_closure":[{"completeness":"bounded","entries":[{"amount":4096,"constraint_id":"gpu/gtt","unit":"bytes"}],"family":"consumable_capacity"},{"completeness":"known_zero","entries":[],"family":"safety_floor"},{"completeness":"bounded","entries":[{"amount":2,"constraint_id":"model-type/llm","unit":"count"}],"family":"cardinality_pool"},{"completeness":"not_applicable","entries":[],"family":"compatibility_exclusivity"}],"daemon_epoch":"epoch/7","journal_id":"journal/main","operation":{"family":"resource_lifecycle","kind":"admission","operation_id":"op/1","plan_id":"plan/1"},"ownership_claim_ids":["owner/a"],"predecessor_checksum_sha256":"f51528eb4d96e562a5a09c5ac314af4bd5a0821106053f0775e76407eba98ff6","quarantine_origin":null,"recovery_claim_ids":["recovery/a"],"recovery_disposition":null,"resident_id":"resident/alpha","resident_state":"provisional","schema":{"major":1,"minor":0},"sequence":3})";

constexpr std::string_view illegal_transition_wire =
    R"({"action_lease_claim_ids":["lease/a"],"checksum_sha256":"3db5747617ca25795e6a961271308b9897e0ccaf825adfb92682181589b064dc","claim_closure":[{"completeness":"bounded","entries":[{"amount":4096,"constraint_id":"gpu/gtt","unit":"bytes"}],"family":"consumable_capacity"},{"completeness":"known_zero","entries":[],"family":"safety_floor"},{"completeness":"bounded","entries":[{"amount":2,"constraint_id":"model-type/llm","unit":"count"}],"family":"cardinality_pool"},{"completeness":"not_applicable","entries":[],"family":"compatibility_exclusivity"}],"daemon_epoch":"epoch/7","journal_id":"journal/main","operation":{"family":"resource_lifecycle","kind":"admission","operation_id":"op/1","plan_id":"plan/1"},"ownership_claim_ids":["owner/a"],"predecessor_checksum_sha256":"dae75ca123b7b872994ff29f8661049c9c28a1798b76fcd0015271739c5adf63","quarantine_origin":null,"recovery_claim_ids":["recovery/a"],"recovery_disposition":"verified_intact","resident_id":"resident/alpha","resident_state":"active","schema":{"major":1,"minor":0},"sequence":2})";

constexpr std::string_view barrier_illegal_beta_wire =
    R"({"action_lease_claim_ids":["lease/a"],"checksum_sha256":"58e1c4a6262c50469b598c7eb1adb065bd106a76bddfd9596a6cfb348b5bdeab","claim_closure":[{"completeness":"bounded","entries":[{"amount":4096,"constraint_id":"gpu/gtt","unit":"bytes"}],"family":"consumable_capacity"},{"completeness":"known_zero","entries":[],"family":"safety_floor"},{"completeness":"bounded","entries":[{"amount":2,"constraint_id":"model-type/llm","unit":"count"}],"family":"cardinality_pool"},{"completeness":"not_applicable","entries":[],"family":"compatibility_exclusivity"}],"daemon_epoch":"epoch/8","journal_id":"journal/main","operation":{"family":"resource_lifecycle","kind":"admission","operation_id":"op/1","plan_id":"plan/1"},"ownership_claim_ids":["owner/a"],"predecessor_checksum_sha256":"b7fd2b1c8ad796f3dc4667c60e5f1bc412f00e4d680b671412acf05bea03944b","quarantine_origin":null,"recovery_claim_ids":["recovery/a"],"recovery_disposition":"verified_released","resident_id":"resident/beta","resident_state":"released","schema":{"major":1,"minor":0},"sequence":8})";

constexpr std::string_view genesis_root_candidate_wire =
    R"({"checksum_sha256":"028c7ff45efd4f40029177121f4259cff7066015e1cada658d0e05a1fc3d2686","daemon_epoch":"epoch/7","generation":1,"journal_id":"journal/main","schema":{"major":1,"minor":0},"tip_checksum_sha256":"dae75ca123b7b872994ff29f8661049c9c28a1798b76fcd0015271739c5adf63","tip_sequence":1})";

constexpr std::string_view successor_root_candidate_wire =
    R"({"checksum_sha256":"08fd05eabaef071c3dc2b87e4b4e5bacf89d974f08158c5e5a731b34b0c7c6c6","daemon_epoch":"epoch/7","generation":2,"journal_id":"journal/main","schema":{"major":1,"minor":0},"tip_checksum_sha256":"ec749a93f0e1a98911b9b91d1823f706d5ce16d1f477690b3c5e07b59fd98147","tip_sequence":2})";

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool is_valid_utf8(std::string_view value) {
    std::size_t index = 0;
    while (index < value.size()) {
        const auto lead = static_cast<unsigned char>(value[index]);
        std::size_t width = 0;
        std::uint32_t code_point = 0;
        std::uint32_t minimum = 0;
        if (lead <= 0x7f) {
            width = 1;
            code_point = lead;
        } else if (lead >= 0xc2 && lead <= 0xdf) {
            width = 2;
            code_point = lead & 0x1f;
            minimum = 0x80;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            width = 3;
            code_point = lead & 0x0f;
            minimum = 0x800;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            width = 4;
            code_point = lead & 0x07;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (index + width > value.size()) {
            return false;
        }
        for (std::size_t offset = 1; offset < width; ++offset) {
            const auto continuation = static_cast<unsigned char>(value[index + offset]);
            if ((continuation & 0xc0) != 0x80) {
                return false;
            }
            code_point = (code_point << 6) | (continuation & 0x3f);
        }
        if (code_point < minimum || code_point > 0x10ffff ||
            (code_point >= 0xd800 && code_point <= 0xdfff)) {
            return false;
        }
        index += width;
    }
    return true;
}

void require_rejected(const ParsedJournalRecordResult &result, JournalStatus status,
                      std::string_view message) {
    require(!result.accepted(), message);
    require(!result.candidate.has_value(), "rejection returned a journal-record candidate");
    require(result.status == status, "rejection returned the wrong stable error class");
    require(result.diagnostic.size() <= max_journal_diagnostic_bytes, "diagnostic is unbounded");
}

void require_rejected(const AuthorityRootCandidateResult &result, JournalStatus status,
                      std::string_view message) {
    require(!result.accepted(), message);
    require(!result.candidate.has_value(), "rejection returned an authority-root candidate");
    require(result.status == status,
            "root-candidate rejection returned the wrong stable error class");
    require(result.diagnostic.size() <= max_journal_diagnostic_bytes,
            "root-candidate diagnostic is unbounded");
}

void require_rejected(const JournalHistoryResult &result, JournalStatus status,
                      std::string_view message) {
    require(!result.accepted(), message);
    require(!result.history.has_value(), "rejection returned validated journal history");
    require(result.status == status, "history rejection returned the wrong stable error class");
    require(result.diagnostic.size() <= max_journal_diagnostic_bytes,
            "history diagnostic is unbounded");
}

class FixtureOriginVerifier final : public RecoveryOriginVerifier {
public:
    FixtureOriginVerifier(std::string resident_id = "resident/alpha",
                          std::string daemon_epoch = "epoch/7",
                          RecoveryOriginKind kind = RecoveryOriginKind::RuntimeRealization)
        : resident_id_(std::move(resident_id)), daemon_epoch_(std::move(daemon_epoch)),
          kind_(kind) {}

    bool verify(const RecoveryOriginVerification &request) const noexcept override {
        ++calls;
        return request.journal_id == "journal/main" && request.resident_id == resident_id_ &&
               request.daemon_epoch == daemon_epoch_ && request.origin.kind == kind_ &&
               request.origin.evidence_sha256 == recovery_evidence;
    }

    mutable std::size_t calls = 0;

private:
    std::string resident_id_;
    std::string daemon_epoch_;
    RecoveryOriginKind kind_;
};

std::optional<RecoveryDisposition> disposition_for(ResidentState state) {
    switch (state) {
    case ResidentState::Prepared:
    case ResidentState::Provisional:
        return std::nullopt;
    case ResidentState::Active:
    case ResidentState::Suspended:
        return RecoveryDisposition::VerifiedIntact;
    case ResidentState::Quarantined:
        return RecoveryDisposition::Quarantined;
    case ResidentState::Released:
        return RecoveryDisposition::VerifiedReleased;
    }
    return std::nullopt;
}

std::vector<ClaimFamilyClosure> complete_claims() {
    return {
        ClaimFamilyClosure{
            ClaimFamily::ConsumableCapacity,
            ClaimCompleteness::Bounded,
            {ClaimAmount{"gpu/gtt", ClaimUnit::Bytes, 4096}},
        },
        ClaimFamilyClosure{
            ClaimFamily::SafetyFloor,
            ClaimCompleteness::KnownZero,
            {},
        },
        ClaimFamilyClosure{
            ClaimFamily::CardinalityPool,
            ClaimCompleteness::Bounded,
            {ClaimAmount{"model-type/llm", ClaimUnit::Count, 2}},
        },
        ClaimFamilyClosure{
            ClaimFamily::CompatibilityExclusivity,
            ClaimCompleteness::NotApplicable,
            {},
        },
    };
}

JournalRecordDraft draft_for(ResidentState state, std::string daemon_epoch = "epoch/7",
                             std::string resident_id = "resident/alpha") {
    JournalRecordDraft draft;
    draft.schema = supported_journal_schema;
    draft.journal_id = "journal/main";
    draft.resident_id = std::move(resident_id);
    draft.daemon_epoch = std::move(daemon_epoch);
    draft.operation = OperationIdentity{
        "op/1",
        "plan/1",
        OperationFamily::ResourceLifecycle,
        OperationKind::Admission,
    };
    draft.claim_closure = complete_claims();
    draft.resident_state = state;
    draft.recovery_disposition = disposition_for(state);
    if (state == ResidentState::Quarantined) {
        draft.quarantine_origin = RecoveryOrigin{
            RecoveryOriginKind::RuntimeRealization,
            std::string(recovery_evidence),
        };
    }
    draft.action_lease_claim_ids = {"lease/a"};
    draft.ownership_claim_ids = {"owner/a"};
    draft.recovery_claim_ids = {"recovery/a"};
    return draft;
}

std::string replace_once(std::string input, std::string_view needle, std::string_view replacement);

struct JournalFixture {
    ParsedJournalRecord tip;
    JournalHistory history;
};

JournalFixture require_genesis(ResidentState state = ResidentState::Prepared,
                               const RecoveryOriginVerifier *verifier = nullptr) {
    auto candidate = seal_genesis(draft_for(state), verifier);
    require(candidate.accepted(), "fixture genesis was rejected");
    auto history = begin_history(*candidate.candidate, verifier);
    require(history.accepted(), "fixture genesis history was rejected");
    return JournalFixture{
        *std::move(candidate.candidate),
        *std::move(history.history),
    };
}

JournalFixture require_successor(JournalFixture previous, JournalRecordDraft draft,
                                 const RecoveryOriginVerifier *verifier = nullptr) {
    auto candidate = seal_successor(previous.history, std::move(draft), verifier);
    require(candidate.accepted(), "fixture successor was rejected");
    auto history = advance_history(std::move(previous.history), *candidate.candidate, verifier);
    require(history.accepted(), "fixture successor history was rejected");
    return JournalFixture{
        *std::move(candidate.candidate),
        *std::move(history.history),
    };
}

JournalFixture history_in_state(ResidentState state, const RecoveryOriginVerifier &verifier) {
    auto current = require_genesis();
    if (state == ResidentState::Prepared) {
        return current;
    }
    if (state == ResidentState::Quarantined) {
        return require_genesis(ResidentState::Quarantined, &verifier);
    }
    if (state == ResidentState::Released) {
        return require_successor(std::move(current), draft_for(ResidentState::Released));
    }
    current = require_successor(std::move(current), draft_for(ResidentState::Provisional));
    if (state == ResidentState::Provisional) {
        return current;
    }
    current = require_successor(std::move(current), draft_for(ResidentState::Active));
    if (state == ResidentState::Active) {
        return current;
    }
    return require_successor(std::move(current), draft_for(ResidentState::Suspended));
}

void require_canonical_round_trip() {
    const auto genesis = seal_genesis(draft_for(ResidentState::Prepared));
    require(genesis.accepted() && genesis.candidate.has_value(), "valid genesis was rejected");
    require(genesis.candidate->sequence() == 1, "genesis sequence was not derived");
    require(!genesis.candidate->predecessor_checksum_sha256().has_value(),
            "genesis predecessor was invented");
    require(genesis.candidate->checksum_sha256() == genesis_checksum, "genesis checksum drifted");
    require(genesis.candidate->canonical_bytes() == genesis_wire,
            "genesis canonical bytes drifted");

    const auto loaded_genesis = parse_record_candidate(genesis_wire);
    require(loaded_genesis.accepted(), "independent genesis bytes were rejected");
    require(loaded_genesis.candidate->journal_id() == "journal/main", "journal identity was lost");
    require(loaded_genesis.candidate->resident_id() == "resident/alpha",
            "resident identity was lost");
    require(loaded_genesis.candidate->operation().operation_id == "op/1",
            "operation identity was lost");
    require(loaded_genesis.candidate->schema().major == 1 &&
                loaded_genesis.candidate->schema().minor == 0 &&
                loaded_genesis.candidate->daemon_epoch() == "epoch/7" &&
                loaded_genesis.candidate->sequence() == 1 &&
                !loaded_genesis.candidate->predecessor_checksum_sha256().has_value(),
            "record stream identity was lost");
    require(loaded_genesis.candidate->operation().plan_id == std::optional<std::string>{"plan/1"} &&
                loaded_genesis.candidate->operation().family ==
                    OperationFamily::ResourceLifecycle &&
                loaded_genesis.candidate->operation().kind == OperationKind::Admission,
            "typed operation identity was lost");
    const auto &loaded_claims = loaded_genesis.candidate->claim_closure();
    require(loaded_claims.size() == 4 &&
                loaded_claims[0].family == ClaimFamily::ConsumableCapacity &&
                loaded_claims[0].completeness == ClaimCompleteness::Bounded &&
                loaded_claims[0].entries.size() == 1 &&
                loaded_claims[0].entries[0].constraint_id == "gpu/gtt" &&
                loaded_claims[0].entries[0].unit == ClaimUnit::Bytes &&
                loaded_claims[0].entries[0].amount == 4096 &&
                loaded_claims[1].family == ClaimFamily::SafetyFloor &&
                loaded_claims[1].completeness == ClaimCompleteness::KnownZero &&
                loaded_claims[1].entries.empty() &&
                loaded_claims[2].family == ClaimFamily::CardinalityPool &&
                loaded_claims[2].completeness == ClaimCompleteness::Bounded &&
                loaded_claims[2].entries.size() == 1 &&
                loaded_claims[2].entries[0].constraint_id == "model-type/llm" &&
                loaded_claims[2].entries[0].unit == ClaimUnit::Count &&
                loaded_claims[2].entries[0].amount == 2 &&
                loaded_claims[3].family == ClaimFamily::CompatibilityExclusivity &&
                loaded_claims[3].completeness == ClaimCompleteness::NotApplicable &&
                loaded_claims[3].entries.empty(),
            "typed claim closure was lost");
    require(loaded_genesis.candidate->resident_state() == ResidentState::Prepared &&
                !loaded_genesis.candidate->recovery_disposition().has_value() &&
                !loaded_genesis.candidate->quarantine_origin().has_value(),
            "typed resident state was lost");
    require(loaded_genesis.candidate->action_lease_claim_ids() ==
                    std::vector<std::string>{"lease/a"} &&
                loaded_genesis.candidate->ownership_claim_ids() ==
                    std::vector<std::string>{"owner/a"} &&
                loaded_genesis.candidate->recovery_claim_ids() ==
                    std::vector<std::string>{"recovery/a"},
            "auxiliary claim identities were lost");
    require(loaded_genesis.candidate->checksum_sha256() == genesis_checksum &&
                loaded_genesis.candidate->canonical_bytes() == genesis_wire,
            "record checksum or canonical bytes were lost");

    auto genesis_history = begin_history(*genesis.candidate);
    require(genesis_history.accepted(), "valid genesis did not create validated history");
    const auto successor =
        seal_successor(*genesis_history.history, draft_for(ResidentState::Provisional));
    require(successor.accepted(), "valid successor was rejected");
    require(successor.candidate->sequence() == 2, "successor sequence was not derived");
    require(successor.candidate->predecessor_checksum_sha256() ==
                std::optional<std::string>{std::string(genesis_checksum)},
            "successor predecessor was not derived");
    require(successor.candidate->checksum_sha256() == successor_checksum,
            "successor checksum drifted");
    require(successor.candidate->canonical_bytes() == successor_wire, "successor bytes drifted");

    const auto loaded_successor = parse_record_candidate(successor_wire);
    require(loaded_successor.accepted(), "independent successor bytes were rejected");
    require_rejected(begin_history(*loaded_successor.candidate), JournalStatus::InvalidHistory,
                     "non-genesis candidate created validated history");
    auto replay_history = begin_history(*loaded_genesis.candidate);
    require(replay_history.accepted(), "loaded genesis failed history validation");
    const auto advanced =
        advance_history(std::move(*replay_history.history), *loaded_successor.candidate);
    require(advanced.accepted(), "loaded successor failed replay history validation");
}

void require_interleaved_literal_round_trip() {
    const auto genesis = parse_record_candidate(genesis_wire);
    const auto beta = parse_record_candidate(interleaved_beta_wire);
    const auto alpha = parse_record_candidate(interleaved_alpha_wire);
    require(genesis.accepted() && beta.accepted() && alpha.accepted(),
            "independent interleaved vectors did not parse");

    auto history = begin_history(*genesis.candidate);
    require(history.accepted(), "interleaved genesis did not create history");
    const auto sealed_beta = seal_successor(
        *history.history, draft_for(ResidentState::Prepared, "epoch/7", "resident/beta"));
    require(sealed_beta.accepted() &&
                sealed_beta.candidate->canonical_bytes() == interleaved_beta_wire,
            "beta canonical vector drifted");
    auto after_beta = advance_history(std::move(*history.history), *beta.candidate);
    require(after_beta.accepted(), "literal beta resident genesis was rejected");
    const auto sealed_alpha =
        seal_successor(*after_beta.history, draft_for(ResidentState::Provisional));
    require(sealed_alpha.accepted() &&
                sealed_alpha.candidate->canonical_bytes() == interleaved_alpha_wire,
            "interleaved alpha canonical vector drifted");
    auto after_alpha = advance_history(std::move(*after_beta.history), *alpha.candidate);
    require(after_alpha.accepted(), "literal alpha successor was rejected");
}

void require_global_stream_and_lifecycle() {
    FixtureOriginVerifier verifier;
    const std::array<ResidentState, 6> states{
        ResidentState::Prepared,  ResidentState::Provisional, ResidentState::Active,
        ResidentState::Suspended, ResidentState::Quarantined, ResidentState::Released,
    };
    const bool valid[6][6] = {
        {true, true, false, false, true, true},   {false, true, true, false, true, true},
        {false, false, true, true, true, true},   {false, false, true, true, true, true},
        {false, false, false, false, true, true}, {false, false, false, false, false, false},
    };

    for (std::size_t from = 0; from < states.size(); ++from) {
        const auto previous = history_in_state(states[from], verifier);
        for (std::size_t to = 0; to < states.size(); ++to) {
            const auto candidate =
                seal_successor(previous.history, draft_for(states[to]),
                               states[to] == ResidentState::Quarantined ? &verifier : nullptr);
            require(candidate.accepted() == valid[from][to],
                    "lifecycle transition table was not enforced");
        }
    }

    auto alpha = require_genesis();
    require(seal_successor(alpha.history,
                           draft_for(ResidentState::Prepared, "epoch/7", "resident/beta"))
                .accepted(),
            "prepared later-resident genesis was rejected");
    FixtureOriginVerifier beta_verifier("resident/beta");
    require(seal_successor(alpha.history,
                           draft_for(ResidentState::Quarantined, "epoch/7", "resident/beta"),
                           &beta_verifier)
                .accepted(),
            "quarantined later-resident genesis was rejected");
    for (const auto state : {ResidentState::Provisional, ResidentState::Active,
                             ResidentState::Suspended, ResidentState::Released}) {
        require_rejected(
            seal_successor(alpha.history, draft_for(state, "epoch/7", "resident/beta")),
            JournalStatus::InvalidHistory, "invalid later-resident genesis state was accepted");
    }
    const auto stale_alpha = seal_successor(alpha.history, draft_for(ResidentState::Provisional));
    require(stale_alpha.accepted(), "stale-candidate fixture was rejected");
    auto beta = require_successor(std::move(alpha),
                                  draft_for(ResidentState::Prepared, "epoch/7", "resident/beta"));
    const auto alpha_next = seal_successor(beta.history, draft_for(ResidentState::Provisional));
    require(alpha_next.accepted(), "interleaved resident successor was rejected");
    require(alpha_next.candidate->sequence() == 3, "global stream sequence was not derived");
    require(alpha_next.candidate->predecessor_checksum_sha256() ==
                std::optional<std::string>{std::string(beta.tip.checksum_sha256())},
            "global predecessor did not follow the stream tip");

    require_rejected(advance_history(std::move(beta.history), *stale_alpha.candidate),
                     JournalStatus::InvalidHistory,
                     "stale resident candidate bypassed the current stream tip");

    auto released_alpha = require_genesis();
    released_alpha =
        require_successor(std::move(released_alpha), draft_for(ResidentState::Released));
    auto beta_after_release = require_successor(
        std::move(released_alpha), draft_for(ResidentState::Prepared, "epoch/7", "resident/beta"));
    require_rejected(seal_successor(beta_after_release.history, draft_for(ResidentState::Prepared)),
                     JournalStatus::InvalidHistory,
                     "interleaving omitted a terminal resident history");

    auto active_before_interleave = history_in_state(ResidentState::Active, verifier);
    auto beta_between_states =
        require_successor(std::move(active_before_interleave),
                          draft_for(ResidentState::Prepared, "epoch/7", "resident/beta"));
    require_rejected(
        seal_successor(beta_between_states.history, draft_for(ResidentState::Provisional)),
        JournalStatus::InvalidHistory, "interleaving rewound an active resident");
    const auto suspended_after_beta =
        seal_successor(beta_between_states.history, draft_for(ResidentState::Suspended));
    require(suspended_after_beta.accepted(),
            "interleaving lost the latest nonterminal resident state");
    require(suspended_after_beta.candidate->sequence() == beta_between_states.tip.sequence() + 1,
            "interleaved successor sequence was not exact");
    require(suspended_after_beta.candidate->predecessor_checksum_sha256() ==
                std::optional<std::string>{std::string(beta_between_states.tip.checksum_sha256())},
            "interleaved successor predecessor was not exact");

    auto other_journal_draft = draft_for(ResidentState::Prepared);
    other_journal_draft.journal_id = "journal/other";
    auto other_candidate = seal_genesis(other_journal_draft);
    require(other_candidate.accepted(), "other-journal fixture was rejected");
    auto other_history = begin_history(*other_candidate.candidate);
    require(other_history.accepted(), "other-journal history fixture was rejected");
    require_rejected(seal_successor(*other_history.history, draft_for(ResidentState::Prepared)),
                     JournalStatus::InvalidHistory,
                     "successor accepted a history from another journal");
    auto other_successor_draft = draft_for(ResidentState::Provisional);
    other_successor_draft.journal_id = "journal/other";
    const auto other_successor = seal_successor(*other_history.history, other_successor_draft);
    require(other_successor.accepted(), "other-journal successor fixture was rejected");
    auto main_transplant_target = require_genesis();
    require_rejected(
        advance_history(std::move(main_transplant_target.history), *other_successor.candidate),
        JournalStatus::InvalidHistory, "foreign stream candidate advanced journal history");

    auto active = history_in_state(ResidentState::Active, verifier);
    auto suspended_new_epoch = draft_for(ResidentState::Suspended, "epoch/8");
    require_rejected(seal_successor(active.history, suspended_new_epoch),
                     JournalStatus::InvalidHistory,
                     "new epoch preserved a nonquarantined resident");

    auto wrong_origin = draft_for(ResidentState::Quarantined, "epoch/8");
    require_rejected(seal_successor(active.history, wrong_origin, &verifier),
                     JournalStatus::InvalidHistory,
                     "new epoch accepted a non-recovery quarantine origin");

    auto prior_epoch = draft_for(ResidentState::Quarantined, "epoch/8");
    prior_epoch.quarantine_origin->kind = RecoveryOriginKind::PriorEpochRecovery;
    FixtureOriginVerifier prior_epoch_verifier("resident/alpha", "epoch/8",
                                               RecoveryOriginKind::PriorEpochRecovery);
    const auto prior_epoch_result =
        seal_successor(active.history, prior_epoch, &prior_epoch_verifier);
    require(prior_epoch_result.accepted(), "new epoch prior-recovery quarantine was rejected");
    require_rejected(seal_successor(active.history, draft_for(ResidentState::Released, "epoch/8")),
                     JournalStatus::InvalidHistory,
                     "epoch change skipped prior-recovery quarantine");
    auto prior_epoch_history = advance_history(
        std::move(active.history), *prior_epoch_result.candidate, &prior_epoch_verifier);
    require(prior_epoch_history.accepted(), "prior-recovery quarantine did not advance history");
    require(
        seal_successor(*prior_epoch_history.history, draft_for(ResidentState::Released, "epoch/8"))
            .accepted(),
        "same-epoch verified release after recovery quarantine was rejected");

    for (const auto state : {
             ResidentState::Prepared,
             ResidentState::Provisional,
             ResidentState::Active,
             ResidentState::Suspended,
             ResidentState::Quarantined,
         }) {
        const auto prior_state = history_in_state(state, verifier);
        auto recovery = draft_for(ResidentState::Quarantined, "epoch/8");
        recovery.quarantine_origin->kind = RecoveryOriginKind::PriorEpochRecovery;
        require(seal_successor(prior_state.history, recovery, &prior_epoch_verifier).accepted(),
                "nonreleased prior-epoch state could not enter recovery quarantine");
    }

    auto fork = require_genesis();
    const auto fork_left = seal_successor(fork.history, draft_for(ResidentState::Provisional));
    const auto fork_right = seal_successor(fork.history, draft_for(ResidentState::Released));
    require(fork_left.accepted() && fork_right.accepted(), "fork fixtures were rejected");
    require(fork_left.candidate->checksum_sha256() != fork_right.candidate->checksum_sha256(),
            "fork fixtures did not diverge");
    auto chosen = advance_history(std::move(fork.history), *fork_left.candidate);
    require(chosen.accepted(), "chosen fork did not advance history");
    require_rejected(advance_history(std::move(*chosen.history), *fork_right.candidate),
                     JournalStatus::InvalidHistory, "competing fork advanced the chosen history");
}

JournalFixture two_active_residents() {
    FixtureOriginVerifier verifier;
    auto history = history_in_state(ResidentState::Active, verifier);
    history = require_successor(std::move(history),
                                draft_for(ResidentState::Prepared, "epoch/7", "resident/beta"));
    history = require_successor(std::move(history),
                                draft_for(ResidentState::Provisional, "epoch/7", "resident/beta"));
    return require_successor(std::move(history),
                             draft_for(ResidentState::Active, "epoch/7", "resident/beta"));
}

void require_global_epoch_barrier() {
    FixtureOriginVerifier alpha_verifier("resident/alpha", "epoch/8",
                                         RecoveryOriginKind::PriorEpochRecovery);
    FixtureOriginVerifier beta_verifier("resident/beta", "epoch/8",
                                        RecoveryOriginKind::PriorEpochRecovery);
    auto history = two_active_residents();

    auto alpha_recovery = draft_for(ResidentState::Quarantined, "epoch/8");
    alpha_recovery.quarantine_origin->kind = RecoveryOriginKind::PriorEpochRecovery;
    const auto alpha_barrier_record =
        seal_successor(history.history, alpha_recovery, &alpha_verifier);
    require(alpha_barrier_record.accepted(), "first epoch-barrier record was rejected");
    require(alpha_barrier_record.candidate->checksum_sha256() ==
                "b7fd2b1c8ad796f3dc4667c60e5f1bc412f00e4d680b671412acf05bea03944b",
            "epoch-barrier literal predecessor drifted");
    auto barrier = advance_history(std::move(history.history), *alpha_barrier_record.candidate,
                                   &alpha_verifier);
    require(barrier.accepted(), "epoch barrier did not enter history");

    const auto illegal_beta = parse_record_candidate(barrier_illegal_beta_wire);
    require(illegal_beta.accepted(), "checksum-valid epoch-barrier violation did not parse");
    require_rejected(advance_history(std::move(*barrier.history), *illegal_beta.candidate),
                     JournalStatus::InvalidHistory,
                     "parsed candidate skipped an outstanding resident recovery quarantine");
    require(barrier.history->available(),
            "rejected consuming advancement destroyed the prior validated history");

    require_rejected(
        seal_successor(*barrier.history, draft_for(ResidentState::Released, "epoch/8")),
        JournalStatus::InvalidHistory,
        "already-migrated resident advanced during an epoch barrier");
    require_rejected(seal_successor(*barrier.history, draft_for(ResidentState::Prepared, "epoch/8",
                                                                "resident/gamma")),
                     JournalStatus::InvalidHistory, "new resident entered during an epoch barrier");
    auto second_epoch = draft_for(ResidentState::Quarantined, "epoch/9", "resident/beta");
    second_epoch.quarantine_origin->kind = RecoveryOriginKind::PriorEpochRecovery;
    require_rejected(seal_successor(*barrier.history, second_epoch, &beta_verifier),
                     JournalStatus::InvalidHistory,
                     "second epoch began before the barrier completed");
    require_rejected(seal_successor(*barrier.history,
                                    draft_for(ResidentState::Released, "epoch/8", "resident/beta")),
                     JournalStatus::InvalidHistory,
                     "outstanding resident skipped its epoch-barrier quarantine");

    auto beta_recovery = draft_for(ResidentState::Quarantined, "epoch/8", "resident/beta");
    beta_recovery.quarantine_origin->kind = RecoveryOriginKind::PriorEpochRecovery;
    const auto beta_barrier_record =
        seal_successor(*barrier.history, beta_recovery, &beta_verifier);
    require(beta_barrier_record.accepted(),
            "next outstanding resident was rejected by the epoch barrier");
    auto completed = advance_history(std::move(*barrier.history), *beta_barrier_record.candidate,
                                     &beta_verifier);
    require(completed.accepted(), "epoch barrier did not complete");
    require(seal_successor(*completed.history, draft_for(ResidentState::Released, "epoch/8"))
                .accepted(),
            "ordinary transition stayed blocked after epoch-barrier completion");
    require(seal_successor(*completed.history,
                           draft_for(ResidentState::Prepared, "epoch/8", "resident/gamma"))
                .accepted(),
            "new resident stayed blocked after epoch-barrier completion");

    auto beta_first = two_active_residents();
    auto beta_first_recovery = draft_for(ResidentState::Quarantined, "epoch/8", "resident/beta");
    beta_first_recovery.quarantine_origin->kind = RecoveryOriginKind::PriorEpochRecovery;
    const auto beta_first_record =
        seal_successor(beta_first.history, beta_first_recovery, &beta_verifier);
    require(beta_first_record.accepted(), "epoch barrier imposed an unrequested resident order");
    auto beta_first_barrier = advance_history(std::move(beta_first.history),
                                              *beta_first_record.candidate, &beta_verifier);
    require(beta_first_barrier.accepted(), "beta-first epoch barrier did not enter history");
    require(seal_successor(*beta_first_barrier.history, alpha_recovery, &alpha_verifier).accepted(),
            "beta-first epoch barrier rejected the remaining resident");

    FixtureOriginVerifier fixture_verifier;
    auto released_resident = history_in_state(ResidentState::Active, fixture_verifier);
    released_resident =
        require_successor(std::move(released_resident),
                          draft_for(ResidentState::Prepared, "epoch/7", "resident/beta"));
    released_resident =
        require_successor(std::move(released_resident),
                          draft_for(ResidentState::Released, "epoch/7", "resident/beta"));
    const auto only_live_record =
        seal_successor(released_resident.history, alpha_recovery, &alpha_verifier);
    require(only_live_record.accepted(),
            "released resident blocked the live resident epoch migration");
    auto released_excluded = advance_history(std::move(released_resident.history),
                                             *only_live_record.candidate, &alpha_verifier);
    require(released_excluded.accepted(), "released-resident epoch barrier did not enter history");
    require(
        seal_successor(*released_excluded.history, draft_for(ResidentState::Released, "epoch/8"))
            .accepted(),
        "released resident remained outstanding in the epoch barrier");
}

void require_large_history_advancement() {
#ifdef __SANITIZE_ADDRESS__
    constexpr std::size_t phase_residents = 2048;
#else
    constexpr std::size_t phase_residents = 8192;
#endif
    constexpr std::size_t additional_residents = phase_residents * 2;
    constexpr auto generous_bound = std::chrono::seconds(90);
    auto current = require_genesis();
    const auto append_residents = [&current](std::size_t begin, std::size_t end) {
        for (std::size_t index = begin; index < end; ++index) {
            const auto candidate = seal_successor(
                current.history, draft_for(ResidentState::Prepared, "epoch/7",
                                           "resident/stress/" + std::to_string(index)));
            require(candidate.accepted(), "large-history resident genesis was rejected");
            auto advanced = advance_history(std::move(current.history), *candidate.candidate);
            require(advanced.accepted(), "large-history candidate did not advance");
            current = JournalFixture{
                *candidate.candidate,
                *std::move(advanced.history),
            };
        }
    };
    const auto started = std::chrono::steady_clock::now();
    append_residents(0, phase_residents);
    const auto midpoint = std::chrono::steady_clock::now();
    append_residents(phase_residents, additional_residents);
    const auto finished = std::chrono::steady_clock::now();
    const auto first_phase = midpoint - started;
    const auto second_phase = finished - midpoint;
    require(finished - started < generous_bound,
            "large consuming-history advancement exceeded its generous bound");
    require(second_phase < first_phase * 9 / 4,
            "history advancement cost grew superlinearly with resident count");
    require(current.history.tip_sequence() == additional_residents + 1,
            "large history lost its global sequence");

    auto alpha_recovery = draft_for(ResidentState::Quarantined, "epoch/8");
    alpha_recovery.quarantine_origin->kind = RecoveryOriginKind::PriorEpochRecovery;
    FixtureOriginVerifier alpha_verifier("resident/alpha", "epoch/8",
                                         RecoveryOriginKind::PriorEpochRecovery);
    const auto alpha_record = seal_successor(current.history, alpha_recovery, &alpha_verifier);
    require(alpha_record.accepted(), "large-history epoch barrier did not begin");
    auto barrier =
        advance_history(std::move(current.history), *alpha_record.candidate, &alpha_verifier);
    require(barrier.accepted(), "large-history epoch barrier did not enter history");
    require_rejected(seal_successor(*barrier.history,
                                    draft_for(ResidentState::Prepared, "epoch/8", "resident/new")),
                     JournalStatus::InvalidHistory,
                     "large-history epoch barrier lost its outstanding residents");

    auto survivor_recovery = draft_for(ResidentState::Quarantined, "epoch/8", "resident/stress/0");
    survivor_recovery.quarantine_origin->kind = RecoveryOriginKind::PriorEpochRecovery;
    FixtureOriginVerifier survivor_verifier("resident/stress/0", "epoch/8",
                                            RecoveryOriginKind::PriorEpochRecovery);
    const auto survivor_record =
        seal_successor(*barrier.history, survivor_recovery, &survivor_verifier);
    require(survivor_record.accepted(), "large-history survivor migration was rejected");
    auto remaining = advance_history(std::move(*barrier.history), *survivor_record.candidate,
                                     &survivor_verifier);
    require(remaining.accepted(), "large-history survivor did not advance");
    require_rejected(seal_successor(*remaining.history,
                                    draft_for(ResidentState::Prepared, "epoch/8", "resident/new")),
                     JournalStatus::InvalidHistory,
                     "large-history barrier completed before all survivors migrated");
}

void require_internal_state_and_origin_rules() {
    FixtureOriginVerifier verifier;
    require(seal_genesis(draft_for(ResidentState::Prepared)).accepted(),
            "prepared genesis was rejected");
    require(seal_genesis(draft_for(ResidentState::Quarantined), &verifier).accepted(),
            "quarantined genesis was rejected");
    for (const auto state : {ResidentState::Provisional, ResidentState::Active,
                             ResidentState::Suspended, ResidentState::Released}) {
        require_rejected(seal_genesis(draft_for(state)), JournalStatus::InvalidLifecycle,
                         "non-genesis lifecycle state was accepted");
    }

    auto invalid_disposition = draft_for(ResidentState::Prepared);
    invalid_disposition.recovery_disposition = RecoveryDisposition::VerifiedIntact;
    require_rejected(seal_genesis(invalid_disposition), JournalStatus::InvalidLifecycle,
                     "prepared record accepted a disposition");

    auto active_without_disposition = draft_for(ResidentState::Active);
    active_without_disposition.recovery_disposition.reset();
    require_rejected(seal_genesis(active_without_disposition), JournalStatus::InvalidLifecycle,
                     "active record omitted its disposition");
    auto active_wrong_disposition = draft_for(ResidentState::Active);
    active_wrong_disposition.recovery_disposition = RecoveryDisposition::VerifiedReleased;
    require_rejected(seal_genesis(active_wrong_disposition), JournalStatus::InvalidLifecycle,
                     "active record accepted the wrong disposition");
    auto released_wrong_disposition = draft_for(ResidentState::Released);
    released_wrong_disposition.recovery_disposition = RecoveryDisposition::VerifiedIntact;
    require_rejected(seal_genesis(released_wrong_disposition), JournalStatus::InvalidLifecycle,
                     "released record accepted the wrong disposition");

    auto quarantine_without_disposition = draft_for(ResidentState::Quarantined);
    quarantine_without_disposition.recovery_disposition.reset();
    require_rejected(seal_genesis(quarantine_without_disposition, &verifier),
                     JournalStatus::InvalidLifecycle, "quarantine record omitted its disposition");
    auto quarantine_wrong_disposition = draft_for(ResidentState::Quarantined);
    quarantine_wrong_disposition.recovery_disposition = RecoveryDisposition::VerifiedIntact;
    require_rejected(seal_genesis(quarantine_wrong_disposition, &verifier),
                     JournalStatus::InvalidLifecycle,
                     "quarantine record accepted the wrong disposition");
    auto quarantine_without_origin = draft_for(ResidentState::Quarantined);
    quarantine_without_origin.quarantine_origin.reset();
    require_rejected(seal_genesis(quarantine_without_origin, &verifier),
                     JournalStatus::InvalidLifecycle, "quarantine record omitted its origin");

    auto origin_without_quarantine = draft_for(ResidentState::Prepared);
    origin_without_quarantine.quarantine_origin = RecoveryOrigin{
        RecoveryOriginKind::PreparedLaunch,
        std::string(recovery_evidence),
    };
    require_rejected(seal_genesis(origin_without_quarantine, &verifier),
                     JournalStatus::InvalidLifecycle, "nonquarantined record accepted an origin");

    auto quarantined = draft_for(ResidentState::Quarantined);
    require_rejected(seal_genesis(quarantined), JournalStatus::UnverifiedRecoveryOrigin,
                     "raw recovery digest produced a quarantine candidate");

    class RejectingVerifier final : public RecoveryOriginVerifier {
    public:
        bool verify(const RecoveryOriginVerification &) const noexcept override { return false; }
    } rejecting;
    require_rejected(seal_genesis(quarantined, &rejecting), JournalStatus::UnverifiedRecoveryOrigin,
                     "rejected recovery evidence produced a quarantine candidate");

    auto successor_history = require_genesis();
    const auto sealed_successor_quarantine =
        seal_successor(successor_history.history, quarantined, &verifier);
    require(sealed_successor_quarantine.accepted(), "verified quarantine successor was rejected");
    const auto parsed_successor_quarantine =
        parse_record_candidate(sealed_successor_quarantine.candidate->canonical_bytes());
    require(parsed_successor_quarantine.accepted(),
            "quarantine successor did not parse as a candidate");
    require_rejected(advance_history(std::move(successor_history.history),
                                     *parsed_successor_quarantine.candidate),
                     JournalStatus::UnverifiedRecoveryOrigin,
                     "history advancement trusted a raw recovery digest");
    require_rejected(advance_history(std::move(successor_history.history),
                                     *parsed_successor_quarantine.candidate, &rejecting),
                     JournalStatus::UnverifiedRecoveryOrigin,
                     "history advancement trusted rejected recovery evidence");
    require(advance_history(std::move(successor_history.history),
                            *parsed_successor_quarantine.candidate, &verifier)
                .accepted(),
            "history advancement rejected verified recovery evidence");

    const auto sealed_quarantine = seal_genesis(quarantined, &verifier);
    require(sealed_quarantine.accepted(), "verified quarantine was rejected");
    for (const auto kind : {
             RecoveryOriginKind::PreparedLaunch,
             RecoveryOriginKind::RuntimeRealization,
             RecoveryOriginKind::LifecycleEffect,
             RecoveryOriginKind::JournalReplay,
             RecoveryOriginKind::PriorEpochRecovery,
             RecoveryOriginKind::CutoverReconciliation,
             RecoveryOriginKind::ArtifactIdentityEffect,
         }) {
        auto origin_variant = quarantined;
        origin_variant.quarantine_origin->kind = kind;
        FixtureOriginVerifier origin_verifier("resident/alpha", "epoch/7", kind);
        const auto sealed_origin = seal_genesis(origin_variant, &origin_verifier);
        require(sealed_origin.accepted(), "accepted lifecycle recovery origin was rejected");
        const auto parsed_origin =
            parse_record_candidate(sealed_origin.candidate->canonical_bytes());
        require(parsed_origin.accepted() &&
                    parsed_origin.candidate->resident_state() == ResidentState::Quarantined &&
                    parsed_origin.candidate->recovery_disposition() ==
                        std::optional<RecoveryDisposition>{RecoveryDisposition::Quarantined} &&
                    parsed_origin.candidate->quarantine_origin().has_value() &&
                    parsed_origin.candidate->quarantine_origin()->kind == kind &&
                    parsed_origin.candidate->quarantine_origin()->evidence_sha256 ==
                        recovery_evidence,
                "lifecycle recovery-origin identity did not round-trip");
        require(begin_history(*parsed_origin.candidate, &origin_verifier).accepted(),
                "accepted lifecycle recovery origin did not create history");
        require(origin_verifier.calls == 2,
                "recovery-origin verifier did not bind both seal and history load");
    }
    const std::string quarantine_wire(sealed_quarantine.candidate->canonical_bytes());
    const auto loaded_quarantine = parse_record_candidate(quarantine_wire);
    require(loaded_quarantine.accepted(),
            "persisted quarantine did not produce a parsed candidate");
    require_rejected(begin_history(*loaded_quarantine.candidate),
                     JournalStatus::UnverifiedRecoveryOrigin,
                     "persisted quarantine produced validated history without origin verification");
    require(begin_history(*loaded_quarantine.candidate, &verifier).accepted(),
            "persisted quarantine rejected verified origin evidence");

    std::string artifact_origin(quarantine_wire);
    const auto state_position = artifact_origin.find("\"kind\":\"runtime_realization\"");
    require(state_position != std::string::npos, "fixture origin marker is absent");
    artifact_origin.replace(state_position,
                            std::string_view("\"kind\":\"runtime_realization\"").size(),
                            "\"kind\":\"load_acquisition\"");
    require_rejected(parse_record_candidate(artifact_origin), JournalStatus::UnknownValue,
                     "artifact quarantine vocabulary entered lifecycle origins");

    auto wrong_major = draft_for(ResidentState::Prepared);
    wrong_major.schema.major = 2;
    require_rejected(seal_genesis(wrong_major), JournalStatus::UnsupportedSchema,
                     "unknown journal schema major was accepted");
    auto wrong_minor = draft_for(ResidentState::Prepared);
    wrong_minor.schema.minor = 1;
    require_rejected(seal_genesis(wrong_minor), JournalStatus::UnsupportedSchema,
                     "unknown journal schema minor was accepted");

    auto active_null_wire =
        replace_once(std::string(genesis_wire), R"("resident_state":"prepared")",
                     R"("resident_state":"active")");
    active_null_wire =
        replace_once(std::move(active_null_wire), genesis_checksum,
                     "6d74879bcd0de09cd768c75b14b9a3bc1631e00ad40bca75524315b8d390d0f3");
    require_rejected(parse_record_candidate(active_null_wire), JournalStatus::InvalidLifecycle,
                     "checksum-valid active record omitted its disposition");

    auto active_wrong_wire = replace_once(active_null_wire, R"("recovery_disposition":null)",
                                          R"("recovery_disposition":"verified_released")");
    active_wrong_wire =
        replace_once(std::move(active_wrong_wire),
                     "6d74879bcd0de09cd768c75b14b9a3bc1631e00ad40bca75524315b8d390d0f3",
                     "bf7619dec8aedb90cbf144927a5e64b2589f57f032d6557cab6ed836a2a91200");
    require_rejected(parse_record_candidate(active_wrong_wire), JournalStatus::InvalidLifecycle,
                     "checksum-valid active record carried the wrong disposition");

    auto released_wrong_wire =
        replace_once(std::string(genesis_wire), R"("resident_state":"prepared")",
                     R"("resident_state":"released")");
    released_wrong_wire =
        replace_once(std::move(released_wrong_wire), R"("recovery_disposition":null)",
                     R"("recovery_disposition":"verified_intact")");
    released_wrong_wire =
        replace_once(std::move(released_wrong_wire), genesis_checksum,
                     "0e0a1d52d5b02d1d97d3101968b238a2c15ea09514cdd9b1c31df94a288a77ff");
    require_rejected(parse_record_candidate(released_wrong_wire), JournalStatus::InvalidLifecycle,
                     "checksum-valid released record carried the wrong disposition");

    auto quarantine_missing_origin_wire =
        replace_once(std::string(genesis_wire), R"("resident_state":"prepared")",
                     R"("resident_state":"quarantined")");
    quarantine_missing_origin_wire =
        replace_once(std::move(quarantine_missing_origin_wire), R"("recovery_disposition":null)",
                     R"("recovery_disposition":"quarantined")");
    quarantine_missing_origin_wire =
        replace_once(std::move(quarantine_missing_origin_wire), genesis_checksum,
                     "0591e43163c1e9b58c913f575f25af7b3baade52d3120c15602bf7cee22bb221");
    require_rejected(parse_record_candidate(quarantine_missing_origin_wire),
                     JournalStatus::InvalidLifecycle,
                     "checksum-valid quarantine omitted its origin");

    auto quarantine_wrong_wire = replace_once(
        std::string(genesis_wire), R"("quarantine_origin":null)",
        R"("quarantine_origin":{"evidence_sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","kind":"runtime_realization"})");
    quarantine_wrong_wire =
        replace_once(std::move(quarantine_wrong_wire), R"("resident_state":"prepared")",
                     R"("resident_state":"quarantined")");
    quarantine_wrong_wire =
        replace_once(std::move(quarantine_wrong_wire), R"("recovery_disposition":null)",
                     R"("recovery_disposition":"verified_intact")");
    quarantine_wrong_wire =
        replace_once(std::move(quarantine_wrong_wire), genesis_checksum,
                     "458f556b7262fc5e4453a3b1152e0a505c6b6027aba1034d5ad4746d972eec7b");
    require_rejected(parse_record_candidate(quarantine_wrong_wire), JournalStatus::InvalidLifecycle,
                     "checksum-valid quarantine carried the wrong disposition");
}

void require_claim_closure_rules() {
    const auto require_positive_round_trip = [](JournalRecordDraft draft,
                                                std::string_view message) {
        const auto sealed = seal_genesis(std::move(draft));
        require(sealed.accepted(), message);
        auto parsed = parse_record_candidate(sealed.candidate->canonical_bytes());
        require(parsed.accepted(), "accepted claim and operation vocabulary did not parse");
        return parsed;
    };

    auto no_plan = draft_for(ResidentState::Prepared);
    no_plan.operation.plan_id.reset();
    const auto parsed_no_plan = require_positive_round_trip(
        std::move(no_plan), "absent operation plan identity was rejected");
    require(!parsed_no_plan.candidate->operation().plan_id.has_value(),
            "absent operation plan identity did not round-trip");

    const std::array<OperationKind, 14> operation_kinds{{
        OperationKind::Admission,
        OperationKind::ExplicitUnload,
        OperationKind::ForceUnload,
        OperationKind::PressureReclamation,
        OperationKind::StartupLoad,
        OperationKind::ServiceTermination,
        OperationKind::DeadBackendPruning,
        OperationKind::SameEpochRecoveryCleanup,
        OperationKind::PriorEpochOwnerCleanup,
        OperationKind::ArtifactScopeRecoveryCleanup,
        OperationKind::SavedPinMutation,
        OperationKind::RuntimePinMutation,
        OperationKind::LegacyPinBatch,
        OperationKind::ResidentStateRecoveryCleanup,
    }};
    for (const auto kind : operation_kinds) {
        auto operation_kind = draft_for(ResidentState::Prepared);
        operation_kind.operation.kind = kind;
        const auto parsed_kind = require_positive_round_trip(
            std::move(operation_kind), "accepted operation kind was rejected");
        require(parsed_kind.candidate->operation().kind == kind,
                "operation-kind identity did not round-trip");
    }

    auto alternate_operation = draft_for(ResidentState::Prepared);
    alternate_operation.operation.family = OperationFamily::ResidentState;
    alternate_operation.operation.kind = OperationKind::ExplicitUnload;
    const auto alternate_operation_candidate = seal_genesis(alternate_operation);
    require(alternate_operation_candidate.accepted(),
            "accepted alternate operation identity was rejected");
    const auto parsed_alternate_operation =
        parse_record_candidate(alternate_operation_candidate.candidate->canonical_bytes());
    require(parsed_alternate_operation.accepted() &&
                parsed_alternate_operation.candidate->operation().family ==
                    OperationFamily::ResidentState &&
                parsed_alternate_operation.candidate->operation().kind ==
                    OperationKind::ExplicitUnload,
            "alternate operation identity did not round-trip");

    auto unknown_capacity = draft_for(ResidentState::Prepared);
    unknown_capacity.claim_closure.front().completeness = ClaimCompleteness::Unknown;
    unknown_capacity.claim_closure.front().entries.clear();
    const auto parsed_unknown = require_positive_round_trip(
        std::move(unknown_capacity), "unknown claim completeness was rejected");
    require(parsed_unknown.candidate->claim_closure()[0].family ==
                    ClaimFamily::ConsumableCapacity &&
                parsed_unknown.candidate->claim_closure()[0].completeness ==
                    ClaimCompleteness::Unknown &&
                parsed_unknown.candidate->claim_closure()[0].entries.empty(),
            "unknown claim completeness did not round-trip");

    auto bounded_safety = draft_for(ResidentState::Prepared);
    bounded_safety.claim_closure[1].completeness = ClaimCompleteness::Bounded;
    bounded_safety.claim_closure[1].entries = {
        ClaimAmount{"host/floor", ClaimUnit::Bytes, 1024},
    };
    const auto parsed_safety = require_positive_round_trip(
        std::move(bounded_safety), "bounded safety-floor bytes were rejected");
    const auto &safety = parsed_safety.candidate->claim_closure()[1];
    require(safety.family == ClaimFamily::SafetyFloor &&
                safety.completeness == ClaimCompleteness::Bounded && safety.entries.size() == 1 &&
                safety.entries[0].constraint_id == "host/floor" &&
                safety.entries[0].unit == ClaimUnit::Bytes && safety.entries[0].amount == 1024,
            "bounded safety-floor identity did not round-trip");

    auto bounded_compatibility = draft_for(ResidentState::Prepared);
    bounded_compatibility.claim_closure.back().completeness = ClaimCompleteness::Bounded;
    bounded_compatibility.claim_closure.back().entries = {
        ClaimAmount{"npu/exclusive", ClaimUnit::Count, 1},
    };
    const auto parsed_compatibility = require_positive_round_trip(
        std::move(bounded_compatibility), "unit compatibility-exclusivity claim was rejected");
    const auto &compatibility = parsed_compatibility.candidate->claim_closure()[3];
    require(compatibility.family == ClaimFamily::CompatibilityExclusivity &&
                compatibility.completeness == ClaimCompleteness::Bounded &&
                compatibility.entries.size() == 1 &&
                compatibility.entries[0].constraint_id == "npu/exclusive" &&
                compatibility.entries[0].unit == ClaimUnit::Count &&
                compatibility.entries[0].amount == 1,
            "compatibility-exclusivity identity did not round-trip");

    auto permuted_families = draft_for(ResidentState::Prepared);
    std::reverse(permuted_families.claim_closure.begin(), permuted_families.claim_closure.end());
    const auto canonicalized_families = seal_genesis(std::move(permuted_families));
    require(canonicalized_families.accepted() &&
                canonicalized_families.candidate->canonical_bytes() == genesis_wire &&
                canonicalized_families.candidate->checksum_sha256() == genesis_checksum,
            "claim-family section order changed canonical identity");

    auto missing = draft_for(ResidentState::Prepared);
    missing.claim_closure.pop_back();
    require_rejected(seal_genesis(missing), JournalStatus::InvalidClaimClosure,
                     "incomplete claim closure was accepted");

    auto duplicate = draft_for(ResidentState::Prepared);
    duplicate.claim_closure.back() = duplicate.claim_closure.front();
    require_rejected(seal_genesis(duplicate), JournalStatus::Duplicate,
                     "duplicate claim family was accepted");

    auto empty_bound = draft_for(ResidentState::Prepared);
    empty_bound.claim_closure.front().entries.clear();
    require_rejected(seal_genesis(empty_bound), JournalStatus::InvalidClaimClosure,
                     "empty bounded family was accepted");

    auto nonbounded_entry = draft_for(ResidentState::Prepared);
    nonbounded_entry.claim_closure[1].entries = {
        ClaimAmount{"host/floor", ClaimUnit::Bytes, 1},
    };
    require_rejected(seal_genesis(nonbounded_entry), JournalStatus::InvalidClaimClosure,
                     "nonbounded family carried amounts");

    auto wrong_unit = draft_for(ResidentState::Prepared);
    wrong_unit.claim_closure.front().entries.front().unit = ClaimUnit::Count;
    require_rejected(seal_genesis(wrong_unit), JournalStatus::InvalidClaimClosure,
                     "capacity family accepted count units");

    auto zero_amount = draft_for(ResidentState::Prepared);
    zero_amount.claim_closure.front().entries.front().amount = 0;
    require_rejected(seal_genesis(zero_amount), JournalStatus::InvalidClaimClosure,
                     "bounded family accepted a zero amount");

    auto compatibility_amount = draft_for(ResidentState::Prepared);
    compatibility_amount.claim_closure.back().completeness = ClaimCompleteness::Bounded;
    compatibility_amount.claim_closure.back().entries = {
        ClaimAmount{"npu/exclusive", ClaimUnit::Count, 2},
    };
    require_rejected(seal_genesis(compatibility_amount), JournalStatus::InvalidClaimClosure,
                     "compatibility claim accepted an amount other than one");

    auto duplicate_identity = draft_for(ResidentState::Prepared);
    duplicate_identity.claim_closure.front().entries.push_back(
        duplicate_identity.claim_closure.front().entries.front());
    require_rejected(seal_genesis(duplicate_identity), JournalStatus::Duplicate,
                     "duplicate family-constraint identity was accepted");

    for (std::size_t array_index = 0; array_index < 3; ++array_index) {
        const std::string prefix = array_index == 0   ? "lease"
                                   : array_index == 1 ? "owner"
                                                      : "recovery";
        auto sortable = draft_for(ResidentState::Prepared);
        auto &sortable_ids = array_index == 0   ? sortable.action_lease_claim_ids
                             : array_index == 1 ? sortable.ownership_claim_ids
                                                : sortable.recovery_claim_ids;
        sortable_ids = {prefix + "/z", prefix + "/a"};
        const auto sorted = seal_genesis(sortable);
        require(sorted.accepted(), "sealer rejected sortable auxiliary claim identities");
        const auto &parsed_ids = array_index == 0   ? sorted.candidate->action_lease_claim_ids()
                                 : array_index == 1 ? sorted.candidate->ownership_claim_ids()
                                                    : sorted.candidate->recovery_claim_ids();
        require(parsed_ids == std::vector<std::string>({prefix + "/a", prefix + "/z"}),
                "sealer did not canonicalize an auxiliary claim-identity array");

        auto duplicate_auxiliary = draft_for(ResidentState::Prepared);
        auto &duplicate_ids = array_index == 0   ? duplicate_auxiliary.action_lease_claim_ids
                              : array_index == 1 ? duplicate_auxiliary.ownership_claim_ids
                                                 : duplicate_auxiliary.recovery_claim_ids;
        duplicate_ids = {prefix + "/a", prefix + "/a"};
        require_rejected(seal_genesis(duplicate_auxiliary), JournalStatus::Duplicate,
                         "duplicate auxiliary claim identity was accepted");
    }

    auto maximum_identifier = draft_for(ResidentState::Prepared);
    maximum_identifier.operation.operation_id = std::string(max_journal_identifier_bytes, 'a');
    const auto parsed_maximum_identifier = require_positive_round_trip(
        std::move(maximum_identifier), "maximum-length identifier was rejected");
    require(parsed_maximum_identifier.candidate->operation().operation_id.size() ==
                max_journal_identifier_bytes,
            "maximum-length identifier did not round-trip");

    auto maximum_auxiliary_arrays = draft_for(ResidentState::Prepared);
    maximum_auxiliary_arrays.action_lease_claim_ids.clear();
    maximum_auxiliary_arrays.ownership_claim_ids.clear();
    maximum_auxiliary_arrays.recovery_claim_ids.clear();
    for (std::size_t index = 0; index < max_journal_array_entries; ++index) {
        maximum_auxiliary_arrays.action_lease_claim_ids.push_back("lease/" + std::to_string(index));
        maximum_auxiliary_arrays.ownership_claim_ids.push_back("owner/" + std::to_string(index));
        maximum_auxiliary_arrays.recovery_claim_ids.push_back("recovery/" + std::to_string(index));
    }
    const auto parsed_maximum_auxiliary = require_positive_round_trip(
        std::move(maximum_auxiliary_arrays), "maximum-size auxiliary arrays were rejected");
    require(parsed_maximum_auxiliary.candidate->action_lease_claim_ids().size() ==
                    max_journal_array_entries &&
                parsed_maximum_auxiliary.candidate->ownership_claim_ids().size() ==
                    max_journal_array_entries &&
                parsed_maximum_auxiliary.candidate->recovery_claim_ids().size() ==
                    max_journal_array_entries,
            "maximum-size auxiliary arrays did not round-trip");

    auto maximum_claim_entries = draft_for(ResidentState::Prepared);
    maximum_claim_entries.claim_closure.front().entries.clear();
    for (std::size_t index = 0; index < max_journal_array_entries; ++index) {
        maximum_claim_entries.claim_closure.front().entries.push_back(
            ClaimAmount{"gpu/" + std::to_string(index), ClaimUnit::Bytes, index + 1});
    }
    const auto parsed_maximum_claim_entries = require_positive_round_trip(
        std::move(maximum_claim_entries), "maximum-size claim-entry array was rejected");
    require(parsed_maximum_claim_entries.candidate->claim_closure().front().entries.size() ==
                max_journal_array_entries,
            "maximum-size claim-entry array did not round-trip");
}

std::string replace_once(std::string input, std::string_view needle, std::string_view replacement) {
    const auto position = input.find(needle);
    require(position != std::string::npos, "fixture mutation marker is absent");
    input.replace(position, needle.size(), replacement);
    return input;
}

void require_untrusted_input_rules() {
    const auto duplicate =
        replace_once(std::string(genesis_wire), "{", R"({"journal_id":"journal/main",)");
    require_rejected(parse_record_candidate(duplicate), JournalStatus::Duplicate,
                     "duplicate JSON key was accepted");

    const auto nested_duplicate = replace_once(std::string(genesis_wire), R"("operation":{)",
                                               R"("operation":{"operation_id":"op/1",)");
    require_rejected(parse_record_candidate(nested_duplicate), JournalStatus::Duplicate,
                     "nested duplicate JSON key was accepted");

    const auto unknown_field = replace_once(std::string(genesis_wire), "{", R"({"future":true,)");
    require_rejected(parse_record_candidate(unknown_field), JournalStatus::UnknownField,
                     "unknown field was accepted");

    const auto nested_unknown_field = replace_once(std::string(genesis_wire), R"("operation":{)",
                                                   R"("operation":{"future":true,)");
    require_rejected(parse_record_candidate(nested_unknown_field), JournalStatus::UnknownField,
                     "nested unknown field was accepted");

    const auto escaped_string =
        replace_once(std::string(genesis_wire), R"("journal_id":"journal/main")",
                     R"("journal_id":"journal\/main")");
    require_rejected(parse_record_candidate(escaped_string), JournalStatus::NonCanonical,
                     "equivalent escaped string was accepted");

    const auto reordered_schema =
        replace_once(std::string(genesis_wire), R"("schema":{"major":1,"minor":0})",
                     R"("schema":{"minor":0,"major":1})");
    require_rejected(parse_record_candidate(reordered_schema), JournalStatus::NonCanonical,
                     "equivalent object-key order was accepted");

    require_rejected(parse_record_candidate(" " + std::string(genesis_wire)),
                     JournalStatus::NonCanonical, "noncanonical whitespace was accepted");
    const std::array<std::pair<std::string_view, std::string_view>, 3> unsorted_id_arrays{{
        {R"("action_lease_claim_ids":["lease/a"])",
         R"("action_lease_claim_ids":["lease/z","lease/a"])"},
        {R"("ownership_claim_ids":["owner/a"])", R"("ownership_claim_ids":["owner/z","owner/a"])"},
        {R"("recovery_claim_ids":["recovery/a"])",
         R"("recovery_claim_ids":["recovery/z","recovery/a"])"},
    }};
    for (const auto &[canonical, unsorted] : unsorted_id_arrays) {
        require_rejected(
            parse_record_candidate(replace_once(std::string(genesis_wire), canonical, unsorted)),
            JournalStatus::NonCanonical, "persisted unsorted auxiliary claim IDs were accepted");
    }
    const std::array<std::pair<std::string_view, std::string_view>, 3> duplicate_id_arrays{{
        {R"("action_lease_claim_ids":["lease/a"])",
         R"("action_lease_claim_ids":["lease/a","lease/a"])"},
        {R"("ownership_claim_ids":["owner/a"])", R"("ownership_claim_ids":["owner/a","owner/a"])"},
        {R"("recovery_claim_ids":["recovery/a"])",
         R"("recovery_claim_ids":["recovery/a","recovery/a"])"},
    }};
    for (const auto &[canonical, duplicate_ids] : duplicate_id_arrays) {
        require_rejected(parse_record_candidate(
                             replace_once(std::string(genesis_wire), canonical, duplicate_ids)),
                         JournalStatus::Duplicate,
                         "persisted duplicate auxiliary claim IDs were accepted");
    }

    auto two_amounts = draft_for(ResidentState::Prepared);
    two_amounts.claim_closure.front().entries = {
        ClaimAmount{"gpu/z", ClaimUnit::Bytes, 2},
        ClaimAmount{"gpu/a", ClaimUnit::Bytes, 1},
    };
    const auto sorted_amounts = seal_genesis(two_amounts);
    require(sorted_amounts.accepted(), "sortable claim-entry fixture was rejected");
    const auto unsorted_entries = replace_once(
        std::string(sorted_amounts.candidate->canonical_bytes()),
        R"({"amount":1,"constraint_id":"gpu/a","unit":"bytes"},{"amount":2,"constraint_id":"gpu/z","unit":"bytes"})",
        R"({"amount":2,"constraint_id":"gpu/z","unit":"bytes"},{"amount":1,"constraint_id":"gpu/a","unit":"bytes"})");
    require_rejected(parse_record_candidate(unsorted_entries), JournalStatus::NonCanonical,
                     "persisted unsorted claim entries were accepted");
    require_rejected(parse_record_candidate(std::string(genesis_wire) + " trailing"),
                     JournalStatus::MalformedJson, "trailing input was accepted");
    require_rejected(parse_record_candidate(genesis_wire.substr(0, genesis_wire.size() - 1)),
                     JournalStatus::MalformedJson, "truncated input was accepted");
    require_rejected(parse_record_candidate(std::string(genesis_wire) + std::string(1, '\0')),
                     JournalStatus::MalformedJson, "NUL-tailed input was accepted");

    std::string invalid_utf8(genesis_wire);
    invalid_utf8[invalid_utf8.find("journal/main")] = static_cast<char>(0xff);
    require_rejected(parse_record_candidate(invalid_utf8), JournalStatus::MalformedJson,
                     "invalid UTF-8 was accepted");

    const auto unknown_state =
        replace_once(std::string(genesis_wire), R"("resident_state":"prepared")",
                     R"("resident_state":"launching")");
    require_rejected(parse_record_candidate(unknown_state), JournalStatus::UnknownValue,
                     "unknown resident state was accepted");

    const std::array<std::pair<std::string_view, std::string_view>, 6> unknown_closed_values{{
        {R"("operation":{"family":"resource_lifecycle")",
         R"("operation":{"family":"future_family")"},
        {R"("kind":"admission")", R"("kind":"future_kind")"},
        {R"("family":"consumable_capacity")", R"("family":"future_claim_family")"},
        {R"("completeness":"bounded")", R"("completeness":"future_completeness")"},
        {R"("unit":"bytes")", R"("unit":"future_unit")"},
        {R"("recovery_disposition":null)", R"("recovery_disposition":"future_disposition")"},
    }};
    for (const auto &[known, unknown] : unknown_closed_values) {
        require_rejected(
            parse_record_candidate(replace_once(std::string(genesis_wire), known, unknown)),
            JournalStatus::UnknownValue, "unknown closed journal value was accepted");
    }

    std::string long_unknown_key = "/private/journal/path/";
    for (std::size_t index = 0; index < max_journal_diagnostic_bytes; ++index) {
        long_unknown_key += "\xf0\x9f\x92\xa5";
    }
    const auto long_unknown_field =
        replace_once(std::string(genesis_wire), "{", "{\"" + long_unknown_key + "\":true,");
    const auto long_diagnostic = parse_record_candidate(long_unknown_field);
    require_rejected(long_diagnostic, JournalStatus::UnknownField,
                     "long unknown field was accepted");
    require(is_valid_utf8(long_diagnostic.diagnostic),
            "bounded diagnostic ended inside a UTF-8 code point");
    require(long_diagnostic.diagnostic.find("/private/journal/path/") == std::string::npos,
            "bounded diagnostic echoed an untrusted path-shaped key");

    for (const auto invalid_sequence : {"1.0", "1e0", "-1", "18446744073709551616"}) {
        require_rejected(
            parse_record_candidate(replace_once(std::string(genesis_wire), R"("sequence":1)",
                                                std::string("\"sequence\":") + invalid_sequence)),
            JournalStatus::InvalidValue, "invalid sequence number was accepted");
    }

    const auto bad_checksum = replace_once(std::string(genesis_wire), std::string(genesis_checksum),
                                           std::string(64, '0'));
    require_rejected(parse_record_candidate(bad_checksum), JournalStatus::ChecksumMismatch,
                     "invalid checksum produced a record candidate");

    require_rejected(parse_record_candidate(std::string(max_journal_input_bytes + 1, 'x')),
                     JournalStatus::InputTooLarge, "oversized journal input was accepted");

    auto long_identifier = draft_for(ResidentState::Prepared);
    long_identifier.operation.operation_id = std::string(max_journal_identifier_bytes + 1, 'a');
    require_rejected(seal_genesis(long_identifier), JournalStatus::InvalidIdentifier,
                     "oversized identifier was accepted");

    auto non_ascii = draft_for(ResidentState::Prepared);
    non_ascii.operation.operation_id =
        std::string("op/") + static_cast<char>(0xc3) + static_cast<char>(0xa9);
    require_rejected(seal_genesis(non_ascii), JournalStatus::InvalidIdentifier,
                     "non-ASCII identifier was accepted");

    auto oversized_array = draft_for(ResidentState::Prepared);
    oversized_array.action_lease_claim_ids.clear();
    for (std::size_t index = 0; index <= max_journal_array_entries; ++index) {
        oversized_array.action_lease_claim_ids.push_back("lease/" + std::to_string(index));
    }
    require_rejected(seal_genesis(oversized_array), JournalStatus::LimitExceeded,
                     "oversized array was accepted");
}

void require_authority_root_candidates() {
    auto genesis = require_genesis();
    const auto root_candidate = seal_authority_root_candidate(genesis.history, nullptr);
    require(root_candidate.accepted(), "genesis root candidate was rejected");
    require(root_candidate.candidate->generation() == 1,
            "root-candidate generation was not derived");
    require(root_candidate.candidate->tip_sequence() == 1,
            "root-candidate tip sequence was not derived");
    require(root_candidate.candidate->canonical_bytes() == genesis_root_candidate_wire,
            "genesis root-candidate bytes drifted");
    const auto parsed_genesis_root =
        parse_authority_root_candidate(genesis_root_candidate_wire, genesis.history, nullptr);
    require(parsed_genesis_root.accepted() && parsed_genesis_root.candidate->schema().major == 1 &&
                parsed_genesis_root.candidate->schema().minor == 0 &&
                parsed_genesis_root.candidate->journal_id() == "journal/main" &&
                parsed_genesis_root.candidate->daemon_epoch() == "epoch/7" &&
                parsed_genesis_root.candidate->generation() == 1 &&
                parsed_genesis_root.candidate->tip_sequence() == 1 &&
                parsed_genesis_root.candidate->tip_checksum_sha256() == genesis_checksum &&
                parsed_genesis_root.candidate->checksum_sha256() ==
                    "028c7ff45efd4f40029177121f4259cff7066015e1cada658d0e05a1fc3d2686" &&
                parsed_genesis_root.candidate->canonical_bytes() == genesis_root_candidate_wire,
            "independent genesis root-candidate identity did not round-trip");
    const auto duplicate_root_key = replace_once(std::string(genesis_root_candidate_wire), "{",
                                                 R"({"journal_id":"journal/main",)");
    require_rejected(parse_authority_root_candidate(duplicate_root_key, genesis.history),
                     JournalStatus::Duplicate, "duplicate root-candidate key was accepted");
    const auto unknown_root_field =
        replace_once(std::string(genesis_root_candidate_wire), "{", R"({"future":true,)");
    require_rejected(parse_authority_root_candidate(unknown_root_field, genesis.history),
                     JournalStatus::UnknownField, "unknown root-candidate field was accepted");
    require_rejected(parse_authority_root_candidate(
                         std::string(genesis_root_candidate_wire) + " trailing", genesis.history),
                     JournalStatus::MalformedJson, "trailing root-candidate input was accepted");
    require_rejected(parse_authority_root_candidate(genesis_root_candidate_wire.substr(
                                                        0, genesis_root_candidate_wire.size() - 1),
                                                    genesis.history),
                     JournalStatus::MalformedJson, "truncated root-candidate input was accepted");
    require_rejected(parse_authority_root_candidate(std::string(genesis_root_candidate_wire) +
                                                        std::string(1, '\0'),
                                                    genesis.history),
                     JournalStatus::MalformedJson, "NUL-tailed root-candidate input was accepted");
    std::string invalid_root_utf8(genesis_root_candidate_wire);
    invalid_root_utf8[invalid_root_utf8.find("journal/main")] = static_cast<char>(0xff);
    require_rejected(parse_authority_root_candidate(invalid_root_utf8, genesis.history),
                     JournalStatus::MalformedJson,
                     "invalid UTF-8 root-candidate input was accepted");
    const auto unsupported_root_schema =
        replace_once(std::string(genesis_root_candidate_wire), R"("major":1)", R"("major":2)");
    require_rejected(parse_authority_root_candidate(unsupported_root_schema, genesis.history),
                     JournalStatus::UnsupportedSchema,
                     "unsupported root-candidate schema was accepted");
    const auto invalid_root_generation = replace_once(std::string(genesis_root_candidate_wire),
                                                      R"("generation":1)", R"("generation":1.0)");
    require_rejected(parse_authority_root_candidate(invalid_root_generation, genesis.history),
                     JournalStatus::InvalidValue,
                     "fractional root-candidate generation was accepted");
    const auto escaped_root_journal =
        replace_once(std::string(genesis_root_candidate_wire), R"("journal_id":"journal/main")",
                     R"("journal_id":"journal\/main")");
    require_rejected(parse_authority_root_candidate(escaped_root_journal, genesis.history),
                     JournalStatus::NonCanonical,
                     "equivalent escaped root-candidate string was accepted");
    const auto reordered_root_schema =
        replace_once(std::string(genesis_root_candidate_wire), R"("schema":{"major":1,"minor":0})",
                     R"("schema":{"minor":0,"major":1})");
    require_rejected(parse_authority_root_candidate(reordered_root_schema, genesis.history),
                     JournalStatus::NonCanonical,
                     "equivalent root-candidate object-key order was accepted");

    auto successor = require_successor(std::move(genesis), draft_for(ResidentState::Provisional));
    const auto next_root_candidate =
        seal_authority_root_candidate(successor.history, &*parsed_genesis_root.candidate);
    require(next_root_candidate.accepted(), "successor root candidate was rejected");
    require(next_root_candidate.candidate->generation() == 2,
            "root-candidate generation did not advance");
    require(next_root_candidate.candidate->canonical_bytes() == successor_root_candidate_wire,
            "successor root-candidate bytes drifted");
    const auto parsed_successor_root = parse_authority_root_candidate(
        successor_root_candidate_wire, successor.history, &*parsed_genesis_root.candidate);
    require(parsed_successor_root.accepted() &&
                parsed_successor_root.candidate->generation() == 2 &&
                parsed_successor_root.candidate->tip_sequence() == 2 &&
                parsed_successor_root.candidate->tip_checksum_sha256() == successor_checksum &&
                parsed_successor_root.candidate->checksum_sha256() ==
                    "08fd05eabaef071c3dc2b87e4b4e5bacf89d974f08158c5e5a731b34b0c7c6c6",
            "independent successor root-candidate identity did not round-trip");
    require_rejected(seal_authority_root_candidate(successor.history, nullptr),
                     JournalStatus::InvalidHistory,
                     "successor history formed a candidate without its prior candidate");
    require_rejected(
        parse_authority_root_candidate(successor_root_candidate_wire, successor.history, nullptr),
        JournalStatus::InvalidHistory,
        "persisted successor root candidate parsed without its prior candidate");

    const auto third = require_successor(std::move(successor), draft_for(ResidentState::Active));
    require_rejected(seal_authority_root_candidate(third.history, &*root_candidate.candidate),
                     JournalStatus::InvalidHistory,
                     "root candidate skipped a direct stream record");

    auto fork_genesis = require_genesis();
    const auto fork_root = seal_authority_root_candidate(fork_genesis.history);
    const auto fork_left_record =
        seal_successor(fork_genesis.history, draft_for(ResidentState::Provisional));
    const auto fork_right_record =
        seal_successor(fork_genesis.history, draft_for(ResidentState::Released));
    require(fork_root.accepted() && fork_left_record.accepted() && fork_right_record.accepted() &&
                fork_left_record.candidate->checksum_sha256() !=
                    fork_right_record.candidate->checksum_sha256(),
            "root fork fixtures did not diverge");
    auto fork_left_base = begin_history(fork_genesis.tip);
    auto fork_right_base = begin_history(fork_genesis.tip);
    require(fork_left_base.accepted() && fork_right_base.accepted(),
            "independent fork bases were rejected");
    const auto fork_left_history =
        advance_history(std::move(*fork_left_base.history), *fork_left_record.candidate);
    const auto fork_right_history =
        advance_history(std::move(*fork_right_base.history), *fork_right_record.candidate);
    require(fork_left_history.accepted() && fork_right_history.accepted(),
            "root fork histories were rejected");
    const auto fork_left_root =
        seal_authority_root_candidate(*fork_left_history.history, &*fork_root.candidate);
    const auto fork_right_root =
        seal_authority_root_candidate(*fork_right_history.history, &*fork_root.candidate);
    require(fork_left_root.accepted() && fork_right_root.accepted() &&
                fork_left_root.candidate->generation() == 2 &&
                fork_right_root.candidate->generation() == 2 &&
                fork_left_root.candidate->tip_checksum_sha256() ==
                    fork_left_record.candidate->checksum_sha256() &&
                fork_right_root.candidate->tip_checksum_sha256() ==
                    fork_right_record.candidate->checksum_sha256() &&
                fork_left_root.candidate->tip_checksum_sha256() !=
                    fork_right_root.candidate->tip_checksum_sha256() &&
                fork_left_root.candidate->checksum_sha256() !=
                    fork_right_root.candidate->checksum_sha256(),
            "same-generation root candidates collapsed distinct stream forks");
    require(parse_authority_root_candidate(fork_left_root.candidate->canonical_bytes(),
                                           *fork_left_history.history, &*fork_root.candidate)
                    .accepted() &&
                parse_authority_root_candidate(fork_right_root.candidate->canonical_bytes(),
                                               *fork_right_history.history, &*fork_root.candidate)
                    .accepted(),
            "matching fork root candidate was rejected");
    require_rejected(
        parse_authority_root_candidate(fork_left_root.candidate->canonical_bytes(),
                                       *fork_right_history.history, &*fork_root.candidate),
        JournalStatus::InvalidHistory, "left fork root candidate parsed against the right history");
    require_rejected(
        parse_authority_root_candidate(fork_right_root.candidate->canonical_bytes(),
                                       *fork_left_history.history, &*fork_root.candidate),
        JournalStatus::InvalidHistory, "right fork root candidate parsed against the left history");

    auto genesis_again = require_genesis();
    require_rejected(
        parse_authority_root_candidate(successor_root_candidate_wire, genesis_again.history,
                                       &*root_candidate.candidate),
        JournalStatus::InvalidHistory, "root candidate was transplanted to another tip");
    auto other_journal_draft = draft_for(ResidentState::Prepared);
    other_journal_draft.journal_id = "journal/other";
    auto other_candidate = seal_genesis(other_journal_draft);
    require(other_candidate.accepted(), "other-journal root-candidate fixture was rejected");
    auto other_history = begin_history(*other_candidate.candidate);
    require(other_history.accepted(), "other-journal root-candidate history was rejected");
    const auto other_root_candidate = seal_authority_root_candidate(*other_history.history);
    require(other_root_candidate.accepted(), "other-journal root candidate was rejected");
    require_rejected(parse_authority_root_candidate(
                         other_root_candidate.candidate->canonical_bytes(), genesis_again.history),
                     JournalStatus::InvalidHistory,
                     "foreign root candidate parsed against main history");
    require_rejected(
        seal_authority_root_candidate(*other_history.history, &*root_candidate.candidate),
        JournalStatus::InvalidHistory, "root candidate was transplanted across journals");
    const auto bad_root_checksum = replace_once(
        std::string(genesis_root_candidate_wire),
        "028c7ff45efd4f40029177121f4259cff7066015e1cada658d0e05a1fc3d2686", std::string(64, '0'));
    require_rejected(
        parse_authority_root_candidate(bad_root_checksum, genesis_again.history, nullptr),
        JournalStatus::ChecksumMismatch, "invalid root-candidate checksum produced a candidate");
    require_rejected(parse_authority_root_candidate(" " + std::string(genesis_root_candidate_wire),
                                                    genesis_again.history, nullptr),
                     JournalStatus::NonCanonical, "noncanonical root candidate was accepted");
}

void require_parsed_candidates_are_nonauthorizing() {
    auto history = require_genesis();
    const auto illegal = parse_record_candidate(illegal_transition_wire);
    require(illegal.accepted(), "checksum-valid illegal transition did not parse as a candidate");
    require_rejected(advance_history(std::move(history.history), *illegal.candidate),
                     JournalStatus::InvalidHistory,
                     "checksum-valid illegal transition produced validated history");
    require(seal_authority_root_candidate(history.history).accepted(),
            "failed history advancement corrupted the prior validated history");
}

void require_value_transport_is_safe() {
    auto parsed = parse_record_candidate(genesis_wire);
    require(parsed.accepted(), "record transport fixture was rejected");
    ParsedJournalRecord transported(std::move(*parsed.candidate));
    require(parsed.accepted() && parsed.candidate->canonical_bytes() == genesis_wire &&
                parsed.candidate->journal_id() == "journal/main" &&
                parsed.candidate->resident_id() == "resident/alpha" &&
                transported.canonical_bytes() == parsed.candidate->canonical_bytes() &&
                transported.checksum_sha256() == parsed.candidate->checksum_sha256() &&
                transported.sequence() == parsed.candidate->sequence(),
            "rvalue record transport destroyed candidate identity");
    auto source_history = begin_history(*parsed.candidate);
    auto transported_history = begin_history(transported);
    require(source_history.accepted() && transported_history.accepted(),
            "record transport changed its validated-history path");
    const auto source_root = seal_authority_root_candidate(*source_history.history);
    const auto transported_root = seal_authority_root_candidate(*transported_history.history);
    require(source_root.accepted() && transported_root.accepted() &&
                source_root.candidate->canonical_bytes() ==
                    transported_root.candidate->canonical_bytes(),
            "record transport changed its root-candidate path");

    auto parsed_root =
        parse_authority_root_candidate(genesis_root_candidate_wire, *source_history.history);
    require(parsed_root.accepted(), "root-candidate transport fixture was rejected");
    AuthorityRootCandidate transported_root_candidate(std::move(*parsed_root.candidate));
    require(parsed_root.accepted() &&
                parsed_root.candidate->canonical_bytes() == genesis_root_candidate_wire &&
                transported_root_candidate.canonical_bytes() ==
                    parsed_root.candidate->canonical_bytes() &&
                transported_root_candidate.tip_checksum_sha256() ==
                    parsed_root.candidate->tip_checksum_sha256() &&
                transported_root_candidate.generation() == parsed_root.candidate->generation(),
            "rvalue root-candidate transport destroyed typed identity");
    const auto successor =
        seal_successor(*source_history.history, draft_for(ResidentState::Provisional));
    require(successor.accepted(), "root-candidate transport successor was rejected");
    auto successor_history =
        advance_history(std::move(*source_history.history), *successor.candidate);
    require(successor_history.accepted(),
            "root-candidate transport successor history was rejected");
    require(!source_history.history->available() && source_history.history->journal_id().empty() &&
                source_history.history->daemon_epoch().empty() &&
                source_history.history->tip_sequence() == 0 &&
                source_history.history->tip_checksum_sha256().empty(),
            "successful advancement retained the consumed history authority");
    const auto source_next =
        seal_authority_root_candidate(*successor_history.history, &*parsed_root.candidate);
    const auto transported_next =
        seal_authority_root_candidate(*successor_history.history, &transported_root_candidate);
    require(source_next.accepted() && transported_next.accepted() &&
                source_next.candidate->canonical_bytes() ==
                    transported_next.candidate->canonical_bytes(),
            "root-candidate transport changed its successor path");

    auto history_move_source = begin_history(*parsed.candidate);
    JournalHistoryResult history_move_destination(std::move(history_move_source));
    require(!history_move_source.accepted() && history_move_source.history.has_value() &&
                !history_move_source.history->available() &&
                history_move_source.history->journal_id().empty() &&
                history_move_source.history->daemon_epoch().empty() &&
                history_move_source.history->tip_sequence() == 0 &&
                history_move_source.history->tip_checksum_sha256().empty(),
            "moved-from journal history advertised usable authority");
    require(history_move_destination.accepted(),
            "journal-history transport lost the destination authority");
    const auto moved_history_successor =
        seal_successor(*history_move_destination.history, draft_for(ResidentState::Provisional));
    require(moved_history_successor.accepted(),
            "transported journal history rejected a valid successor");
    require_rejected(
        seal_successor(*history_move_source.history, draft_for(ResidentState::Provisional)),
        JournalStatus::InvalidHistory, "moved-from history sealed a successor");
    require_rejected(advance_history(std::move(*history_move_source.history),
                                     *moved_history_successor.candidate),
                     JournalStatus::InvalidHistory, "moved-from history advanced a candidate");
    require_rejected(seal_authority_root_candidate(*history_move_source.history),
                     JournalStatus::InvalidHistory, "moved-from history formed a root candidate");
    require_rejected(
        parse_authority_root_candidate(genesis_root_candidate_wire, *history_move_source.history),
        JournalStatus::InvalidHistory, "moved-from history parsed a root candidate");
}

} // namespace

int main() {
    static_assert(!std::is_same_v<ClaimFamily, ConstraintKind>);
    static_assert(!std::is_default_constructible_v<ParsedJournalRecord>);
    static_assert(!std::is_default_constructible_v<JournalHistory>);
    static_assert(!std::is_default_constructible_v<AuthorityRootCandidate>);
    static_assert(std::is_copy_constructible_v<ParsedJournalRecord>);
    static_assert(std::is_copy_assignable_v<ParsedJournalRecord>);
    static_assert(std::is_move_constructible_v<ParsedJournalRecord>);
    static_assert(std::is_copy_constructible_v<AuthorityRootCandidate>);
    static_assert(std::is_copy_assignable_v<AuthorityRootCandidate>);
    static_assert(std::is_move_constructible_v<AuthorityRootCandidate>);
    static_assert(!std::is_constructible_v<ParsedJournalRecord, JournalRecordDraft, std::uint64_t,
                                           std::optional<std::string>, std::string, std::string>);
    static_assert(!std::is_constructible_v<AuthorityRootCandidate, SchemaVersion, std::string,
                                           std::string, std::uint64_t, std::uint64_t, std::string,
                                           std::string, std::string>);
    static_assert(!std::is_copy_constructible_v<JournalHistory>);
    static_assert(!std::is_copy_assignable_v<JournalHistory>);
    static_assert(std::is_move_constructible_v<JournalHistory>);
    static_assert(!std::is_constructible_v<JournalHistory, ParsedJournalRecord>);
    using RootSealer =
        AuthorityRootCandidateResult (*)(const JournalHistory &, const AuthorityRootCandidate *);
    using RootParser = AuthorityRootCandidateResult (*)(std::string_view, const JournalHistory &,
                                                        const AuthorityRootCandidate *);
    using GenesisSealer =
        ParsedJournalRecordResult (*)(JournalRecordDraft, const RecoveryOriginVerifier *);
    using SuccessorSealer = ParsedJournalRecordResult (*)(
        const JournalHistory &, JournalRecordDraft, const RecoveryOriginVerifier *);
    using HistoryBeginner =
        JournalHistoryResult (*)(const ParsedJournalRecord &, const RecoveryOriginVerifier *);
    using HistoryAdvancer = JournalHistoryResult (*)(JournalHistory &&, const ParsedJournalRecord &,
                                                     const RecoveryOriginVerifier *);
    using CandidateParser = ParsedJournalRecordResult (*)(std::string_view);
    static_assert(std::is_same_v<decltype(&seal_authority_root_candidate), RootSealer>);
    static_assert(std::is_same_v<decltype(&parse_authority_root_candidate), RootParser>);
    static_assert(std::is_same_v<decltype(&seal_genesis), GenesisSealer>);
    static_assert(std::is_same_v<decltype(&seal_successor), SuccessorSealer>);
    static_assert(std::is_same_v<decltype(&begin_history), HistoryBeginner>);
    static_assert(std::is_same_v<decltype(&advance_history), HistoryAdvancer>);
    static_assert(
        !std::is_invocable_v<HistoryAdvancer, JournalHistory &, const ParsedJournalRecord &,
                             const RecoveryOriginVerifier *>);
    static_assert(std::is_same_v<decltype(&parse_record_candidate), CandidateParser>);
    require_canonical_round_trip();
    require_interleaved_literal_round_trip();
    require_global_stream_and_lifecycle();
    require_global_epoch_barrier();
    require_large_history_advancement();
    require_internal_state_and_origin_rules();
    require_claim_closure_rules();
    require_untrusted_input_rules();
    require_authority_root_candidates();
    require_parsed_candidates_are_nonauthorizing();
    require_value_transport_is_safe();
    return 0;
}
