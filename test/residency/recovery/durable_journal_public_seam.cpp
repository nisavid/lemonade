#include "journal_persistence_test_support.h"

#include "platform/durable_file_adapter.h"

#include "lemon/residency/durable_journal.h"
#include "lemon/residency/journal.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

using namespace lemon::residency;
using namespace lemon::residency::testing;

constexpr std::string_view genesis_wire =
    R"({"action_lease_claim_ids":["lease/a"],"checksum_sha256":"dae75ca123b7b872994ff29f8661049c9c28a1798b76fcd0015271739c5adf63","claim_closure":[{"completeness":"bounded","entries":[{"amount":4096,"constraint_id":"gpu/gtt","unit":"bytes"}],"family":"consumable_capacity"},{"completeness":"known_zero","entries":[],"family":"safety_floor"},{"completeness":"bounded","entries":[{"amount":2,"constraint_id":"model-type/llm","unit":"count"}],"family":"cardinality_pool"},{"completeness":"not_applicable","entries":[],"family":"compatibility_exclusivity"}],"daemon_epoch":"epoch/7","journal_id":"journal/main","operation":{"family":"resource_lifecycle","kind":"admission","operation_id":"op/1","plan_id":"plan/1"},"ownership_claim_ids":["owner/a"],"predecessor_checksum_sha256":null,"quarantine_origin":null,"recovery_claim_ids":["recovery/a"],"recovery_disposition":null,"resident_id":"resident/alpha","resident_state":"prepared","schema":{"major":1,"minor":0},"sequence":1})";

constexpr std::string_view successor_wire =
    R"({"action_lease_claim_ids":["lease/a"],"checksum_sha256":"ec749a93f0e1a98911b9b91d1823f706d5ce16d1f477690b3c5e07b59fd98147","claim_closure":[{"completeness":"bounded","entries":[{"amount":4096,"constraint_id":"gpu/gtt","unit":"bytes"}],"family":"consumable_capacity"},{"completeness":"known_zero","entries":[],"family":"safety_floor"},{"completeness":"bounded","entries":[{"amount":2,"constraint_id":"model-type/llm","unit":"count"}],"family":"cardinality_pool"},{"completeness":"not_applicable","entries":[],"family":"compatibility_exclusivity"}],"daemon_epoch":"epoch/7","journal_id":"journal/main","operation":{"family":"resource_lifecycle","kind":"admission","operation_id":"op/1","plan_id":"plan/1"},"ownership_claim_ids":["owner/a"],"predecessor_checksum_sha256":"dae75ca123b7b872994ff29f8661049c9c28a1798b76fcd0015271739c5adf63","quarantine_origin":null,"recovery_claim_ids":["recovery/a"],"recovery_disposition":null,"resident_id":"resident/alpha","resident_state":"provisional","schema":{"major":1,"minor":0},"sequence":2})";

constexpr std::string_view wrong_predecessor_wire =
    R"({"action_lease_claim_ids":["lease/a"],"checksum_sha256":"dacce12645761d37967d12704a1eef45ea8634c12b6d77d01f4d1e69cd7606b0","claim_closure":[{"completeness":"bounded","entries":[{"amount":4096,"constraint_id":"gpu/gtt","unit":"bytes"}],"family":"consumable_capacity"},{"completeness":"known_zero","entries":[],"family":"safety_floor"},{"completeness":"bounded","entries":[{"amount":2,"constraint_id":"model-type/llm","unit":"count"}],"family":"cardinality_pool"},{"completeness":"not_applicable","entries":[],"family":"compatibility_exclusivity"}],"daemon_epoch":"epoch/7","journal_id":"journal/main","operation":{"family":"resource_lifecycle","kind":"admission","operation_id":"op/1","plan_id":"plan/1"},"ownership_claim_ids":["owner/a"],"predecessor_checksum_sha256":"f51528eb4d96e562a5a09c5ac314af4bd5a0821106053f0775e76407eba98ff6","quarantine_origin":null,"recovery_claim_ids":["recovery/a"],"recovery_disposition":null,"resident_id":"resident/alpha","resident_state":"provisional","schema":{"major":1,"minor":0},"sequence":2})";

constexpr std::string_view illegal_transition_wire =
    R"({"action_lease_claim_ids":["lease/a"],"checksum_sha256":"3db5747617ca25795e6a961271308b9897e0ccaf825adfb92682181589b064dc","claim_closure":[{"completeness":"bounded","entries":[{"amount":4096,"constraint_id":"gpu/gtt","unit":"bytes"}],"family":"consumable_capacity"},{"completeness":"known_zero","entries":[],"family":"safety_floor"},{"completeness":"bounded","entries":[{"amount":2,"constraint_id":"model-type/llm","unit":"count"}],"family":"cardinality_pool"},{"completeness":"not_applicable","entries":[],"family":"compatibility_exclusivity"}],"daemon_epoch":"epoch/7","journal_id":"journal/main","operation":{"family":"resource_lifecycle","kind":"admission","operation_id":"op/1","plan_id":"plan/1"},"ownership_claim_ids":["owner/a"],"predecessor_checksum_sha256":"dae75ca123b7b872994ff29f8661049c9c28a1798b76fcd0015271739c5adf63","quarantine_origin":null,"recovery_claim_ids":["recovery/a"],"recovery_disposition":"verified_intact","resident_id":"resident/alpha","resident_state":"active","schema":{"major":1,"minor":0},"sequence":2})";

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::string frame(const ParsedJournalRecord &record) {
    return std::string(record.canonical_bytes()) + '\n';
}

JournalLimits generous_limits() {
    return JournalLimits{1024 * 1024, 128, 32, 1024 * 1024};
}

JournalRecordDraft draft_for(std::string journal_id, std::string resident_id,
                             ResidentState state) {
    JournalRecordDraft draft;
    draft.journal_id = std::move(journal_id);
    draft.resident_id = std::move(resident_id);
    draft.daemon_epoch = "epoch/7";
    draft.operation = OperationIdentity{"op/1", std::string("plan/1"),
                                        OperationFamily::ResourceLifecycle,
                                        OperationKind::Admission};
    draft.claim_closure = {
        ClaimFamilyClosure{ClaimFamily::ConsumableCapacity,
                           ClaimCompleteness::Bounded,
                           {ClaimAmount{"gpu/gtt", ClaimUnit::Bytes, 4096}}},
        ClaimFamilyClosure{
            ClaimFamily::SafetyFloor, ClaimCompleteness::KnownZero, {}},
        ClaimFamilyClosure{
            ClaimFamily::CardinalityPool,
            ClaimCompleteness::Bounded,
            {ClaimAmount{"model-type/llm", ClaimUnit::Count, 2}}},
        ClaimFamilyClosure{ClaimFamily::CompatibilityExclusivity,
                           ClaimCompleteness::NotApplicable,
                           {}},
    };
    draft.resident_state = state;
    switch (state) {
    case ResidentState::Prepared:
    case ResidentState::Provisional:
        break;
    case ResidentState::Active:
    case ResidentState::Suspended:
        draft.recovery_disposition = RecoveryDisposition::VerifiedIntact;
        break;
    case ResidentState::Quarantined:
        draft.recovery_disposition = RecoveryDisposition::Quarantined;
        draft.quarantine_origin = RecoveryOrigin{
            RecoveryOriginKind::RuntimeRealization,
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"};
        break;
    case ResidentState::Released:
        draft.recovery_disposition = RecoveryDisposition::VerifiedReleased;
        break;
    }
    draft.action_lease_claim_ids = {"lease/a"};
    draft.ownership_claim_ids = {"owner/a"};
    draft.recovery_claim_ids = {"recovery/a"};
    return draft;
}

class AllowingOriginVerifier final : public RecoveryOriginVerifier {
public:
    bool verify(const RecoveryOriginVerification &) const noexcept override {
        return true;
    }
};

class RejectIfCalledVerifier final : public RecoveryOriginVerifier {
public:
    bool verify(const RecoveryOriginVerification &) const noexcept override {
        called.store(true);
        return false;
    }

    mutable std::atomic<bool> called{false};
};

struct VerifierLifetimeState {
    std::atomic<std::size_t> calls{0};
    std::atomic<bool> alive{false};
};

class LifetimeAllowingVerifier final : public RecoveryOriginVerifier {
public:
    explicit LifetimeAllowingVerifier(
        std::shared_ptr<VerifierLifetimeState> state)
        : state_(std::move(state)) {
        state_->alive.store(true);
    }

    ~LifetimeAllowingVerifier() override { state_->alive.store(false); }

    bool verify(const RecoveryOriginVerification &) const noexcept override {
        state_->calls.fetch_add(1);
        return true;
    }

private:
    std::shared_ptr<VerifierLifetimeState> state_;
};

struct Chain {
    ParsedJournalRecord genesis;
    ParsedJournalRecord successor;
    ParsedJournalRecord fork;
    ParsedJournalRecord third;
    AuthorityRootCandidate genesis_root;
    AuthorityRootCandidate successor_root;
    AuthorityRootCandidate fork_root;
    AuthorityRootCandidate third_root;
};

ParsedJournalRecord make_quarantine_tail(const Chain &chain);

ParsedJournalRecord take_record(ParsedJournalRecordResult result,
                                std::string_view label) {
    require(result.accepted() && result.candidate.has_value(), label);
    return std::move(*result.candidate);
}

JournalHistory take_history(JournalHistoryResult result,
                            std::string_view label) {
    require(result.accepted() && result.history.has_value(), label);
    return std::move(*result.history);
}

AuthorityRootCandidate take_root(AuthorityRootCandidateResult result,
                                 std::string_view label) {
    require(result.accepted() && result.candidate.has_value(), label);
    return std::move(*result.candidate);
}

Chain make_chain(std::string journal_id = "journal/main") {
    auto genesis =
        take_record(seal_genesis(draft_for(journal_id, "resident/alpha",
                                           ResidentState::Prepared)),
                    "genesis fixture was rejected");
    auto genesis_history =
        take_history(begin_history(genesis), "genesis history was rejected");
    auto genesis_root =
        take_root(seal_authority_root_candidate(genesis_history),
                  "genesis root fixture was rejected");

    auto successor = take_record(
        seal_successor(genesis_history, draft_for(journal_id, "resident/alpha",
                                                  ResidentState::Provisional)),
        "successor fixture was rejected");
    if (journal_id == "journal/main") {
        require(genesis.canonical_bytes() == genesis_wire &&
                    successor.canonical_bytes() == successor_wire,
                "TASK-019 canonical fixtures drifted");
    }
    auto fork = take_record(
        seal_successor(genesis_history, draft_for(journal_id, "resident/alpha",
                                                  ResidentState::Released)),
        "fork fixture was rejected");

    auto successor_history =
        take_history(advance_history(std::move(genesis_history), successor),
                     "successor history fixture was rejected");
    auto successor_root = take_root(
        seal_authority_root_candidate(successor_history, &genesis_root),
        "successor root fixture was rejected");

    auto fork_history =
        take_history(begin_history(genesis), "fork genesis was rejected");
    fork_history = take_history(advance_history(std::move(fork_history), fork),
                                "fork history fixture was rejected");
    auto fork_root =
        take_root(seal_authority_root_candidate(fork_history, &genesis_root),
                  "fork root fixture was rejected");

    auto third =
        take_record(seal_successor(successor_history,
                                   draft_for(journal_id, "resident/alpha",
                                             ResidentState::Active)),
                    "third fixture was rejected");
    auto third_history =
        take_history(advance_history(std::move(successor_history), third),
                     "third history fixture was rejected");
    auto third_root =
        take_root(seal_authority_root_candidate(third_history, &successor_root),
                  "third root fixture was rejected");

    return Chain{std::move(genesis),      std::move(successor),
                 std::move(fork),         std::move(third),
                 std::move(genesis_root), std::move(successor_root),
                 std::move(fork_root),    std::move(third_root)};
}

std::string replace_once(std::string value, std::string_view needle,
                         std::string_view replacement) {
    const auto offset = value.find(needle);
    require(offset != std::string::npos,
            "root-adversary fixture token was absent");
    value.replace(offset, needle.size(), replacement);
    return value;
}

template <typename Map>
bool same_optional_value(const Map &left, const Map &right,
                         std::string_view key) {
    const auto left_value = left.find(std::string(key));
    const auto right_value = right.find(std::string(key));
    if ((left_value == left.end()) != (right_value == right.end())) {
        return false;
    }
    return left_value == left.end() ||
           left_value->second == right_value->second;
}

bool fixed_file_unchanged(const NamespaceSnapshot &before,
                          const NamespaceSnapshot &after,
                          std::string_view name) {
    return same_optional_value(before.live_file_bytes, after.live_file_bytes,
                               name) &&
           same_optional_value(before.durable_file_bytes,
                               after.durable_file_bytes, name) &&
           same_optional_value(before.entry_kinds, after.entry_kinds, name) &&
           before.relative_paths.count(std::string(name)) ==
               after.relative_paths.count(std::string(name)) &&
           before.durable_paths.count(std::string(name)) ==
               after.durable_paths.count(std::string(name)) &&
           before.durable_content_available.count(std::string(name)) ==
               after.durable_content_available.count(std::string(name));
}

bool authority_files_unchanged(const NamespaceSnapshot &before,
                               const NamespaceSnapshot &after) {
    for (const auto name :
         {"journal.jsonl", "authority-root.json", ".journal.jsonl.stage",
          ".authority-root.json.stage"}) {
        if (!fixed_file_unchanged(before, after, name)) {
            return false;
        }
    }
    return before.journal_bytes == after.journal_bytes &&
           before.authority_root_bytes == after.authority_root_bytes;
}

bool stage_files_unchanged(const NamespaceSnapshot &before,
                           const NamespaceSnapshot &after) {
    for (const auto name :
         {".journal.jsonl.stage", ".authority-root.json.stage"}) {
        if (!fixed_file_unchanged(before, after, name)) {
            return false;
        }
    }
    return true;
}

bool is_stage_operation(FaultOperation operation) {
    switch (operation) {
    case FaultOperation::RootStageCreate:
    case FaultOperation::RootStageWrite:
    case FaultOperation::RootStageFlush:
    case FaultOperation::RootStageClose:
    case FaultOperation::RootReplace:
    case FaultOperation::NamespaceDurability:
    case FaultOperation::CompactionStageCreate:
    case FaultOperation::CompactionWrite:
    case FaultOperation::CompactionFlush:
    case FaultOperation::CompactionClose:
    case FaultOperation::CompactionReplace:
    case FaultOperation::CompactionNamespaceDurability:
        return true;
    default:
        return false;
    }
}

bool has_stage_operation(const NamespaceSnapshot &snapshot) {
    return std::any_of(snapshot.observations.begin(),
                       snapshot.observations.end(),
                       [](const OperationObservation &observation) {
                           return is_stage_operation(observation.operation);
                       });
}

ParsedJournalRecord make_second_resident(const Chain &chain,
                                         std::string journal_id) {
    auto history = take_history(begin_history(chain.genesis),
                                "second-resident genesis failed");
    return take_record(
        seal_successor(history,
                       draft_for(std::move(journal_id), "resident/beta",
                                 ResidentState::Prepared)),
        "second-resident fixture was rejected");
}

AuthorityRootCandidate root_after(const Chain &chain,
                                  const ParsedJournalRecord &record) {
    auto history =
        take_history(begin_history(chain.genesis), "root-after genesis failed");
    history = take_history(advance_history(std::move(history), record),
                           "root-after successor failed");
    return take_root(
        seal_authority_root_candidate(history, &chain.genesis_root),
        "root-after seal failed");
}

void require_result(DurableJournalResult &result, DurableJournalStatus expected,
                    std::string_view label) {
    require(result.status == expected, label);
    require(result.published() == (expected == DurableJournalStatus::Published),
            "result success predicate disagreed with status");
    require(result.journal.has_value() ==
                (expected == DurableJournalStatus::Published),
            "authority handle availability disagreed with status");
    if (result.journal.has_value()) {
        require(result.journal->available(),
                "returned authority handle was unavailable");
    }
}

void require_storage_released_after_return(JournalTestStorage &storage,
                                           std::string_view label) {
    const auto snapshot = storage.snapshot();
    const auto acquired = static_cast<std::size_t>(std::count_if(
        snapshot.observations.begin(), snapshot.observations.end(),
        [](const OperationObservation &observation) {
            return observation.operation ==
                       FaultOperation::AuthorityLockAcquire &&
                   observation.lock_held;
        }));
    require(snapshot.open_handles.empty(), label);
    if (acquired != 0) {
        require(storage.attempt_count(FaultOperation::AuthorityLockClose) ==
                    acquired,
                "acquired authority lock was not closed exactly once");
    }
}

void require_public_shape() {
    static_assert(!std::is_default_constructible_v<TrustedReplayFloor>);
    static_assert(!std::is_default_constructible_v<DurableJournal>);
    static_assert(!std::is_copy_constructible_v<DurableJournal>);
    static_assert(!std::is_copy_assignable_v<DurableJournal>);
    static_assert(std::is_nothrow_move_constructible_v<DurableJournal>);
    static_assert(std::is_nothrow_move_assignable_v<DurableJournal>);
    static_assert(!std::is_default_constructible_v<PublishedJournal>);
    static_assert(!std::is_copy_constructible_v<PublishedJournal>);
    static_assert(!std::is_copy_assignable_v<PublishedJournal>);
    static_assert(std::is_move_constructible_v<PublishedJournal>);
    static_assert(std::is_move_assignable_v<PublishedJournal>);
    static_assert(
        !std::is_constructible_v<PublishedJournal, ParsedJournalRecord>);
    static_assert(!std::is_constructible_v<PublishedJournal, JournalHistory>);
    static_assert(
        !std::is_constructible_v<PublishedJournal, AuthorityRootCandidate>);
    static_assert(
        !std::is_constructible_v<PublishedJournal, ExactSchemaExportCandidate>);
    static_assert(
        !std::is_convertible_v<ParsedJournalRecord, PublishedJournal>);
    static_assert(!std::is_convertible_v<JournalHistory, PublishedJournal>);
    static_assert(
        !std::is_convertible_v<AuthorityRootCandidate, PublishedJournal>);
    static_assert(
        !std::is_convertible_v<ExactSchemaExportCandidate, PublishedJournal>);

    const auto limits = generous_limits();
    require(
        limits.max_committed_bytes != 0 && limits.max_committed_records != 0 &&
            limits.max_resident_heads != 0 && limits.max_crash_tail_bytes != 0,
        "test limits were not explicit");

    const std::set<DurableJournalStatus> statuses{
        DurableJournalStatus::Published,
        DurableJournalStatus::ConflictBeforeWrite,
        DurableJournalStatus::UnsupportedStorage,
        DurableJournalStatus::CorruptOrRollback,
        DurableJournalStatus::LimitExceeded,
        DurableJournalStatus::RecoveryRequired,
    };
    require(statuses.size() == 6, "stable durable-journal outcomes collapsed");
    require(durable_journal_data_filename == "journal.jsonl" &&
                durable_journal_root_filename == "authority-root.json" &&
                durable_journal_lock_filename == "authority.lock",
            "fixed public authority filenames drifted");
    const std::set<ExactSchemaExportStatus> export_statuses{
        ExactSchemaExportStatus::ExportedCandidate,
        ExactSchemaExportStatus::QuiescenceRequired,
        ExactSchemaExportStatus::ConflictBeforeRead,
        ExactSchemaExportStatus::RecoveryRequired,
        ExactSchemaExportStatus::UnsupportedStorage,
        ExactSchemaExportStatus::CorruptOrRollback,
        ExactSchemaExportStatus::SchemaMismatch,
        ExactSchemaExportStatus::LimitExceeded,
    };
    require(export_statuses.size() == 8,
            "stable exact-schema export outcomes collapsed");
}

void require_shared_io_helpers() {
    const std::string bytes = "partial-write-contract";
    const auto partial = probe_write_all(bytes, {FaultAction::ShortWrite});
    require(partial.result == HelperResultKind::Succeeded &&
                partial.bytes == bytes && partial.attempts > 1,
            "write_all did not complete an exact partial write");

    const auto interrupted = probe_write_all(
        bytes, {FaultAction::InterruptedOnce, FaultAction::InterruptedOnce,
                FaultAction::ShortWrite});
    require(interrupted.result == HelperResultKind::Succeeded &&
                interrupted.bytes == bytes && interrupted.attempts > 3,
            "write_all did not retry interrupted partial progress");
    const auto interrupted_after_progress = probe_write_all(
        bytes, {FaultAction::ShortWrite, FaultAction::InterruptedOnce,
                FaultAction::ShortWrite});
    require(interrupted_after_progress.result == HelperResultKind::Succeeded &&
                interrupted_after_progress.bytes == bytes &&
                interrupted_after_progress.attempts > 3,
            "write_all reset or duplicated progress after interruption");

    for (const auto action :
         {FaultAction::ZeroProgress, FaultAction::OverreportedWrite,
          FaultAction::Error}) {
        const auto rejected = probe_write_all(bytes, {action});
        require(rejected.result == HelperResultKind::FailedBeforeEffect &&
                    rejected.attempts == 1,
                "write_all did not fail closed on invalid progress");
    }
    const auto partial_prefix = bytes.substr(0, bytes.size() / 2);
    for (const auto action : {FaultAction::Error, FaultAction::ZeroProgress,
                              FaultAction::OverreportedWrite}) {
        const auto rejected_after_progress =
            probe_write_all(bytes, {FaultAction::ShortWrite, action});
        require(rejected_after_progress.result ==
                        HelperResultKind::EffectMayHaveOccurred &&
                    rejected_after_progress.bytes == partial_prefix &&
                    rejected_after_progress.attempts == 2,
                "write_all forgot partial progress before terminal failure");
    }
    const auto effect_may =
        probe_write_all(bytes, {FaultAction::EffectThenError});
    require(effect_may.result == HelperResultKind::EffectMayHaveOccurred &&
                effect_may.bytes == bytes && effect_may.attempts == 1,
            "write_all lost a possibly-effectful failure");

    const auto flushed = probe_flush_retry(
        {FaultAction::InterruptedOnce, FaultAction::InterruptedOnce});
    require(flushed.result == HelperResultKind::Succeeded &&
                flushed.attempts == 3,
            "flush helper did not retry all interruptions");

    const auto truncated = probe_truncate_retry(
        bytes, 7, {FaultAction::InterruptedOnce, FaultAction::InterruptedOnce});
    require(truncated.result == HelperResultKind::Succeeded &&
                truncated.bytes == bytes.substr(0, 7) &&
                truncated.attempts == 3,
            "truncate helper did not retry to the exact boundary");

    for (const auto &[action, expected] :
         {std::pair{FaultAction::Error, HelperResultKind::FailedBeforeEffect},
          std::pair{FaultAction::EffectThenError,
                    HelperResultKind::EffectMayHaveOccurred}}) {
        const auto terminal_flush = probe_flush_retry({action});
        require(terminal_flush.result == expected &&
                    terminal_flush.attempts == 1,
                "flush helper retried a non-interrupted failure");
        const auto terminal_truncate = probe_truncate_retry(bytes, 7, {action});
        const auto expected_bytes =
            action == FaultAction::EffectThenError ? bytes.substr(0, 7) : bytes;
        require(terminal_truncate.result == expected &&
                    terminal_truncate.bytes == expected_bytes &&
                    terminal_truncate.attempts == 1,
                "truncate helper retried or lost a terminal outcome");
    }

    for (const auto &[action, expected] :
         {std::pair{FaultAction::InterruptedOnce,
                    HelperResultKind::Interrupted},
          std::pair{FaultAction::EffectThenError,
                    HelperResultKind::EffectMayHaveOccurred},
          std::pair{FaultAction::Error,
                    HelperResultKind::FailedBeforeEffect}}) {
        const auto closed = probe_close_once({action});
        require(closed.result == expected && closed.attempts == 1,
                "close_once retried or collapsed a failed close");
    }

    const auto write_success = probe_write_flush_close(bytes, {}, {}, {});
    require(write_success.result == HelperResultKind::Succeeded &&
                write_success.bytes == bytes &&
                write_success.write_attempts == 1 &&
                write_success.flush_attempts == 1 &&
                write_success.truncate_attempts == 0 &&
                write_success.close_attempts == 1,
            "write-flush-close changed the all-success path");

    const auto write_retried = probe_write_flush_close(
        bytes,
        {FaultAction::InterruptedOnce, FaultAction::ShortWrite,
         FaultAction::InterruptedOnce},
        {FaultAction::InterruptedOnce, FaultAction::InterruptedOnce}, {});
    require(write_retried.result == HelperResultKind::Succeeded &&
                write_retried.bytes == bytes &&
                write_retried.write_attempts > 3 &&
                write_retried.flush_attempts == 3 &&
                write_retried.close_attempts == 1,
            "write-flush-close did not reuse retrying primitives exactly");

    const auto possibly_written =
        probe_write_flush_close(bytes, {FaultAction::EffectThenError}, {}, {});
    require(possibly_written.result ==
                    HelperResultKind::EffectMayHaveOccurred &&
                possibly_written.bytes == bytes &&
                possibly_written.write_attempts == 1 &&
                possibly_written.close_attempts == 1,
            "write-flush-close erased a possibly-effectful write");

    for (const auto flush_action :
         {FaultAction::Error, FaultAction::EffectThenError}) {
        const auto failed_flush =
            probe_write_flush_close(bytes, {}, {flush_action}, {});
        require(failed_flush.result ==
                        HelperResultKind::EffectMayHaveOccurred &&
                    failed_flush.bytes == bytes &&
                    failed_flush.write_attempts == 1 &&
                    failed_flush.flush_attempts == 1 &&
                    failed_flush.close_attempts == 1,
                "write-flush-close forgot a confirmed write before flush "
                "failure");
    }
    for (const auto close_action :
         {FaultAction::Error, FaultAction::InterruptedOnce,
          FaultAction::EffectThenError}) {
        const auto failed_close =
            probe_write_flush_close(bytes, {}, {}, {close_action});
        require(failed_close.result ==
                        HelperResultKind::EffectMayHaveOccurred &&
                    failed_close.bytes == bytes &&
                    failed_close.write_attempts == 1 &&
                    failed_close.flush_attempts == 1 &&
                    failed_close.close_attempts == 1,
                "write-flush-close retried close or forgot prior progress");
    }
    for (const auto close_action : {FaultAction::Error}) {
        const auto failed_before = probe_write_flush_close(
            bytes, {FaultAction::Error}, {}, {close_action});
        require(failed_before.result == HelperResultKind::FailedBeforeEffect &&
                    failed_before.bytes.empty() &&
                    failed_before.write_attempts == 1 &&
                    failed_before.close_attempts == 1,
                "write-flush-close collapsed two pre-effect failures");
    }
    const auto close_effect_after_write_failure = probe_write_flush_close(
        bytes, {FaultAction::Error}, {}, {FaultAction::EffectThenError});
    require(close_effect_after_write_failure.result ==
                    HelperResultKind::FailedBeforeEffect &&
                close_effect_after_write_failure.bytes.empty() &&
                close_effect_after_write_failure.close_attempts == 1,
            "write-flush-close changed a pre-effect write failure during "
            "checked close");

    const auto truncate_success =
        probe_truncate_flush_close(bytes, 7, {}, {}, {});
    require(truncate_success.result == HelperResultKind::Succeeded &&
                truncate_success.bytes == bytes.substr(0, 7) &&
                truncate_success.write_attempts == 0 &&
                truncate_success.truncate_attempts == 1 &&
                truncate_success.flush_attempts == 1 &&
                truncate_success.close_attempts == 1,
            "truncate-flush-close changed the all-success path");

    const auto truncate_retried = probe_truncate_flush_close(
        bytes, 7, {FaultAction::InterruptedOnce, FaultAction::InterruptedOnce},
        {FaultAction::InterruptedOnce, FaultAction::InterruptedOnce}, {});
    require(truncate_retried.result == HelperResultKind::Succeeded &&
                truncate_retried.bytes == bytes.substr(0, 7) &&
                truncate_retried.truncate_attempts == 3 &&
                truncate_retried.flush_attempts == 3 &&
                truncate_retried.close_attempts == 1,
            "truncate-flush-close did not reuse retrying primitives exactly");

    const auto possibly_truncated = probe_truncate_flush_close(
        bytes, 7, {FaultAction::EffectThenError}, {}, {});
    require(possibly_truncated.result ==
                    HelperResultKind::EffectMayHaveOccurred &&
                possibly_truncated.bytes == bytes.substr(0, 7) &&
                possibly_truncated.truncate_attempts == 1 &&
                possibly_truncated.close_attempts == 1,
            "truncate-flush-close erased a possibly-effectful truncate");

    for (const auto flush_action :
         {FaultAction::Error, FaultAction::EffectThenError}) {
        const auto failed_flush =
            probe_truncate_flush_close(bytes, 7, {}, {flush_action}, {});
        require(failed_flush.result ==
                        HelperResultKind::EffectMayHaveOccurred &&
                    failed_flush.bytes == bytes.substr(0, 7) &&
                    failed_flush.truncate_attempts == 1 &&
                    failed_flush.flush_attempts == 1 &&
                    failed_flush.close_attempts == 1,
                "truncate-flush-close forgot a confirmed truncate before "
                "flush failure");
    }
    for (const auto close_action :
         {FaultAction::Error, FaultAction::InterruptedOnce,
          FaultAction::EffectThenError}) {
        const auto failed_close =
            probe_truncate_flush_close(bytes, 7, {}, {}, {close_action});
        require(failed_close.result ==
                        HelperResultKind::EffectMayHaveOccurred &&
                    failed_close.bytes == bytes.substr(0, 7) &&
                    failed_close.truncate_attempts == 1 &&
                    failed_close.flush_attempts == 1 &&
                    failed_close.close_attempts == 1,
                "truncate-flush-close retried close or forgot prior effect");
    }
    for (const auto close_action : {FaultAction::Error}) {
        const auto failed_before = probe_truncate_flush_close(
            bytes, 7, {FaultAction::Error}, {}, {close_action});
        require(failed_before.result == HelperResultKind::FailedBeforeEffect &&
                    failed_before.bytes == bytes &&
                    failed_before.truncate_attempts == 1 &&
                    failed_before.close_attempts == 1,
                "truncate-flush-close collapsed two pre-effect failures");
    }
    const auto close_effect_after_truncate_failure = probe_truncate_flush_close(
        bytes, 7, {FaultAction::Error}, {}, {FaultAction::EffectThenError});
    require(close_effect_after_truncate_failure.result ==
                    HelperResultKind::FailedBeforeEffect &&
                close_effect_after_truncate_failure.bytes == bytes &&
                close_effect_after_truncate_failure.close_attempts == 1,
            "truncate-flush-close changed a pre-effect truncate failure "
            "during checked close");

    const auto requests_stay_within_remaining_sentinel =
        [](const ReadHelperProbeResult &probe, std::size_t max_bytes) {
            if (probe.requested_bytes.size() != probe.returned_bytes.size()) {
                return false;
            }
            auto retained = std::size_t{0};
            for (std::size_t index = 0; index < probe.requested_bytes.size();
                 ++index) {
                if (retained > max_bytes + 1 ||
                    probe.requested_bytes[index] > max_bytes + 1 - retained ||
                    probe.returned_bytes[index] >
                        probe.requested_bytes[index]) {
                    return false;
                }
                retained += probe.returned_bytes[index];
            }
            return retained <= max_bytes + 1;
        };

    const auto empty_read = probe_read_bounded_close({}, 7, {}, {});
    require(empty_read.result == HelperResultKind::Succeeded &&
                empty_read.bytes.empty() && !empty_read.truncated &&
                empty_read.read_attempts == 1 &&
                empty_read.close_attempts == 1 &&
                requests_stay_within_remaining_sentinel(empty_read, 7),
            "bounded read did not preserve explicit empty EOF");

    const std::string exact_cap_bytes = "1234567";
    const auto exact_cap = probe_read_bounded_close(exact_cap_bytes, 7, {}, {});
    require(exact_cap.result == HelperResultKind::Succeeded &&
                exact_cap.bytes == exact_cap_bytes && !exact_cap.truncated &&
                exact_cap.read_attempts >= 2 &&
                !exact_cap.returned_bytes.empty() &&
                exact_cap.returned_bytes.back() == 0 &&
                exact_cap.close_attempts == 1 &&
                requests_stay_within_remaining_sentinel(exact_cap, 7),
            "bounded read did not confirm exact-cap EOF with a zero read");

    constexpr auto bounded_read_chunk_bytes = std::size_t{64 * 1024};
    const std::string large_exact_cap_bytes(
        bounded_read_chunk_bytes * 2 + 17, 'q');
    const auto large_exact_cap = probe_read_bounded_close(
        large_exact_cap_bytes, large_exact_cap_bytes.size(), {}, {});
    require(large_exact_cap.result == HelperResultKind::Succeeded &&
                large_exact_cap.bytes == large_exact_cap_bytes &&
                !large_exact_cap.truncated &&
                large_exact_cap.read_attempts >= 4 &&
                std::all_of(large_exact_cap.requested_bytes.begin(),
                            large_exact_cap.requested_bytes.end(),
                            [&](std::size_t requested) {
                                return requested <= bounded_read_chunk_bytes;
                            }) &&
                large_exact_cap.close_attempts == 1 &&
                requests_stay_within_remaining_sentinel(
                    large_exact_cap, large_exact_cap_bytes.size()),
            "bounded read requested an eager max-sized native allocation");

    for (const auto &over_cap_bytes :
         {std::string("12345678"), std::string(64, 'z')}) {
        const auto over_cap =
            probe_read_bounded_close(over_cap_bytes, 7, {}, {});
        require(over_cap.result == HelperResultKind::Succeeded &&
                    over_cap.bytes == over_cap_bytes.substr(0, 7) &&
                    over_cap.truncated && over_cap.close_attempts == 1 &&
                    requests_stay_within_remaining_sentinel(over_cap, 7),
                "bounded read did not cap and classify extra input");
    }

    const std::string chunked_bytes = "abcdef";
    const auto chunked = probe_read_bounded_close(chunked_bytes, 10,
                                                  {FaultAction::ShortWrite,
                                                   FaultAction::InterruptedOnce,
                                                   FaultAction::ShortWrite},
                                                  {});
    require(chunked.result == HelperResultKind::Succeeded &&
                chunked.bytes == chunked_bytes && !chunked.truncated &&
                chunked.read_attempts >= 4 && chunked.close_attempts == 1 &&
                !chunked.returned_bytes.empty() &&
                chunked.returned_bytes.back() == 0 &&
                requests_stay_within_remaining_sentinel(chunked, 10),
            "bounded read duplicated, skipped, or reset bytes across an "
            "interruption or skipped zero-count EOF confirmation");

    for (const auto action :
         {FaultAction::Error, FaultAction::EffectThenError}) {
        const auto failed_after_progress = probe_read_bounded_close(
            chunked_bytes, 10, {FaultAction::ShortWrite, action}, {});
        const auto expected_partial =
            action == FaultAction::EffectThenError ? chunked_bytes : "abc";
        require(failed_after_progress.result ==
                        HelperResultKind::EffectMayHaveOccurred &&
                    failed_after_progress.bytes == expected_partial &&
                    !failed_after_progress.truncated &&
                    failed_after_progress.read_attempts == 2 &&
                    failed_after_progress.close_attempts == 1 &&
                    requests_stay_within_remaining_sentinel(
                        failed_after_progress, 10),
                "bounded read forgot partial progress before terminal read "
                "failure");
    }

    const auto zero_progress = probe_read_bounded_close(
        chunked_bytes, 10, {FaultAction::ZeroProgress}, {});
    require(zero_progress.result == HelperResultKind::FailedBeforeEffect &&
                zero_progress.bytes.empty() &&
                zero_progress.read_attempts == 1 &&
                zero_progress.close_attempts == 1,
            "bounded read accepted successful non-EOF zero progress");

    const auto overreported = probe_read_bounded_close(
        chunked_bytes, 10, {FaultAction::OverreportedWrite}, {});
    require(overreported.result == HelperResultKind::FailedBeforeEffect &&
                overreported.bytes.empty() &&
                overreported.returned_bytes.size() == 1 &&
                overreported.requested_bytes.size() == 1 &&
                overreported.returned_bytes.front() >
                    overreported.requested_bytes.front() &&
                overreported.close_attempts == 1,
            "bounded read accepted a chunk larger than its request");

    for (const auto close_action :
         {FaultAction::Error, FaultAction::InterruptedOnce,
          FaultAction::EffectThenError}) {
        const auto close_after_success =
            probe_read_bounded_close(chunked_bytes, 10, {}, {close_action});
        require(close_after_success.result ==
                        HelperResultKind::EffectMayHaveOccurred &&
                    close_after_success.bytes == chunked_bytes &&
                    close_after_success.close_attempts == 1,
                "bounded read retried close or forgot observed bytes");
    }

    const auto read_and_close_failed = probe_read_bounded_close(
        chunked_bytes, 10, {FaultAction::Error}, {FaultAction::Error});
    require(read_and_close_failed.result ==
                    HelperResultKind::FailedBeforeEffect &&
                read_and_close_failed.bytes.empty() &&
                read_and_close_failed.read_attempts == 1 &&
                read_and_close_failed.close_attempts == 1,
            "bounded read collapsed two pre-effect failures");
    const auto effectful_close_after_read_failure =
        probe_read_bounded_close(chunked_bytes, 10, {FaultAction::Error},
                                 {FaultAction::EffectThenError});
    require(effectful_close_after_read_failure.result ==
                    HelperResultKind::EffectMayHaveOccurred &&
                effectful_close_after_read_failure.bytes.empty() &&
                effectful_close_after_read_failure.close_attempts == 1,
            "possibly-effectful read close did not dominate a read failure");
}

PublishedJournal create(JournalTestStorage &storage, const Chain &chain,
                        JournalLimits limits = generous_limits()) {
    auto journal = storage.make_journal(limits);
    auto result =
        journal.create_new(TrustedReplayFloor::uninitialized(), chain.genesis);
    require_result(result, DurableJournalStatus::Published,
                   "fresh create was not published");
    require(result.journal->root_bytes() ==
                chain.genesis_root.canonical_bytes(),
            "fresh create returned the wrong root");
    require(result.journal->journal_id() == chain.genesis.journal_id(),
            "fresh create returned the wrong journal identity");
    require(result.journal->tip_sequence() == 1,
            "fresh create returned the wrong sequence");
    require_storage_released_after_return(
        storage, "fresh create returned with a live authority handle");
    return std::move(*result.journal);
}

PublishedJournal recover(JournalTestStorage &storage, std::string_view floor,
                         JournalLimits limits = generous_limits(),
                         const RecoveryOriginVerifier *verifier = nullptr) {
    auto journal = storage.make_journal(limits);
    auto result = journal.recover_existing(
        TrustedReplayFloor::exact_root(std::string(floor)), verifier);
    require_result(result, DurableJournalStatus::Published,
                   "valid recovery was rejected");
    require_storage_released_after_return(
        storage, "recovery returned with a live authority handle");
    return std::move(*result.journal);
}

void require_exact_initial_publication() {
    const auto chain = make_chain();
    auto storage = JournalTestStorage::fresh();
    auto authority = create(storage, chain);
    const auto snapshot = storage.snapshot();
    require(snapshot.journal_bytes == frame(chain.genesis),
            "fresh journal frame bytes drifted");
    require(snapshot.authority_root_bytes ==
                chain.genesis_root.canonical_bytes(),
            "fresh authority-root bytes drifted");
    require(!snapshot.authority_root_bytes.empty() &&
                snapshot.authority_root_bytes.back() != '\n',
            "authority root gained a trailing line terminator");
    require(snapshot.all_authority_io_under_lock,
            "authority publication performed I/O outside the full lock");
    require(snapshot.lock_file_present && snapshot.lock_file_stable,
            "authority lock was removed, staged, or replaced");
    require(snapshot.open_handles.empty() && authority.available(),
            "fresh publication leaked a handle or consumed authority");

    auto initialized_storage = JournalTestStorage::fresh();
    const auto initialized_before = initialized_storage.snapshot();
    auto wrong_floor = initialized_storage.make_journal(generous_limits());
    auto wrong =
        wrong_floor.create_new(TrustedReplayFloor::exact_root(std::string(
                                   chain.genesis_root.canonical_bytes())),
                               chain.genesis);
    require_result(wrong, DurableJournalStatus::CorruptOrRollback,
                   "initialized floor authorized create_new");
    require(initialized_storage.snapshot() == initialized_before,
            "initialized-floor create_new changed fresh storage");

    storage.reset_observations();
    const auto duplicate_before = storage.snapshot();
    auto second = storage.make_journal(generous_limits());
    auto duplicate =
        second.create_new(TrustedReplayFloor::uninitialized(), chain.genesis);
    require_result(duplicate, DurableJournalStatus::ConflictBeforeWrite,
                   "nonfresh namespace did not conflict before write");
    require(
        same_persistent_state(storage.snapshot(), duplicate_before) &&
            storage.attempt_count(FaultOperation::FixedNamespaceInspect) == 1 &&
            storage.attempt_count(FaultOperation::RootOpen) == 0 &&
            storage.attempt_count(FaultOperation::JournalOpen) == 0 &&
            storage.attempt_count(FaultOperation::InitialJournalCreate) == 0 &&
            storage.attempt_count(FaultOperation::JournalWrite) == 0 &&
            storage.attempt_count(FaultOperation::RootStageCreate) == 0 &&
            storage.attempt_count(FaultOperation::RootReplace) == 0 &&
            !storage.snapshot().authority_mutation_attempted &&
            storage.snapshot().all_authority_io_under_lock &&
            storage.snapshot().open_handles.empty() &&
            storage.attempt_count(FaultOperation::AuthorityLockClose) == 1,
        "nonfresh create changed storage");
}

void require_durable_journal_moves() {
    const auto chain = make_chain();
    auto constructed_storage = JournalTestStorage::fresh();
    auto constructed_source =
        constructed_storage.make_journal(generous_limits());
    DurableJournal constructed(std::move(constructed_source));
    auto constructed_result = constructed.create_new(
        TrustedReplayFloor::uninitialized(), chain.genesis);
    require_result(constructed_result, DurableJournalStatus::Published,
                   "move-constructed DurableJournal was not operational");

    auto assigned_storage = JournalTestStorage::fresh();
    auto assigned_source = assigned_storage.make_journal(generous_limits());
    auto displaced_storage = JournalTestStorage::fresh();
    auto assigned = displaced_storage.make_journal(generous_limits());
    assigned = std::move(assigned_source);
    auto assigned_result =
        assigned.create_new(TrustedReplayFloor::uninitialized(), chain.genesis);
    require_result(assigned_result, DurableJournalStatus::Published,
                   "move-assigned DurableJournal was not operational");
}

void require_explicit_floor_states() {
    const auto chain = make_chain();
    for (const auto &floor_bytes : {std::string{}, std::string("{")}) {
        auto fresh = JournalTestStorage::fresh();
        const auto fresh_before = fresh.snapshot();
        auto fresh_journal = fresh.make_journal(generous_limits());
        auto create_result = fresh_journal.create_new(
            TrustedReplayFloor::exact_root(floor_bytes), chain.genesis);
        require_result(create_result, DurableJournalStatus::CorruptOrRollback,
                       "invalid exact floor authorized fresh create");
        require(same_persistent_state(fresh.snapshot(), fresh_before) &&
                    !fresh.snapshot().authority_mutation_attempted,
                "invalid exact floor mutated fresh storage");
        require_storage_released_after_return(
            fresh, "invalid create floor retained an authority handle");

        auto existing = JournalTestStorage::seeded(
            frame(chain.genesis),
            std::string(chain.genesis_root.canonical_bytes()));
        const auto existing_before = existing.snapshot();
        auto existing_journal = existing.make_journal(generous_limits());
        auto recover_result = existing_journal.recover_existing(
            TrustedReplayFloor::exact_root(floor_bytes));
        require_result(recover_result, DurableJournalStatus::CorruptOrRollback,
                       "invalid exact floor authorized recovery");
        require(same_persistent_state(existing.snapshot(), existing_before) &&
                    !existing.snapshot().authority_mutation_attempted,
                "invalid exact floor mutated existing storage");
        require_storage_released_after_return(
            existing, "invalid recovery floor retained an authority handle");
    }
}

void require_fresh_namespace_asymmetry() {
    const auto chain = make_chain();
    const std::vector<std::pair<FixedAuthorityChild, std::string>> blockers{
        {FixedAuthorityChild::Root,
         std::string(chain.genesis_root.canonical_bytes())},
        {FixedAuthorityChild::Journal, frame(chain.genesis)},
        {FixedAuthorityChild::RootStage, "root-stage"},
        {FixedAuthorityChild::JournalStage, "journal-stage"},
        {FixedAuthorityChild::Root, ""},
        {FixedAuthorityChild::Journal, ""},
    };
    for (const auto &[child, bytes] : blockers) {
        auto storage = JournalTestStorage::fresh();
        storage.overwrite_fixed_child_bytes(child, bytes);
        const auto before = storage.snapshot();
        auto journal = storage.make_journal(generous_limits());
        auto result = journal.create_new(TrustedReplayFloor::uninitialized(),
                                         chain.genesis);
        require_result(result, DurableJournalStatus::CorruptOrRollback,
                       "incomplete authority namespace was overwritten");
        const auto after = storage.snapshot();
        require(authority_files_unchanged(before, after) &&
                    !after.authority_mutation_attempted &&
                    after.all_authority_io_under_lock &&
                    after.open_handles.empty() &&
                    storage.attempt_count(
                        FaultOperation::FixedNamespaceInspect) == 1 &&
                    storage.attempt_count(FaultOperation::RootOpen) == 0 &&
                    storage.attempt_count(FaultOperation::JournalOpen) == 0 &&
                    storage.attempt_count(
                        FaultOperation::InitialJournalCreate) == 0 &&
                    storage.attempt_count(FaultOperation::RootStageCreate) == 0,
                "blocked create changed incomplete authority storage");
        require_storage_released_after_return(
            storage, "incomplete create retained an authority handle");
    }

    for (const auto &[stage, bytes] :
         {std::pair{FixedAuthorityChild::RootStage,
                    std::string("root-stage-with-complete-authority")},
          std::pair{FixedAuthorityChild::JournalStage,
                    std::string("journal-stage-with-complete-authority")}}) {
        auto storage = JournalTestStorage::seeded(
            frame(chain.genesis),
            std::string(chain.genesis_root.canonical_bytes()));
        storage.overwrite_fixed_child_bytes(stage, bytes);
        storage.reset_observations();
        const auto before = storage.snapshot();
        auto journal = storage.make_journal(generous_limits());
        auto result = journal.create_new(TrustedReplayFloor::uninitialized(),
                                         chain.genesis);
        require_result(result, DurableJournalStatus::CorruptOrRollback,
                       "stage residue did not override complete-create "
                       "conflict classification");
        const auto after = storage.snapshot();
        require(same_persistent_state(after, before) &&
                    authority_files_unchanged(before, after) &&
                    !after.authority_mutation_attempted &&
                    after.all_authority_io_under_lock &&
                    after.open_handles.empty() &&
                    storage.attempt_count(
                        FaultOperation::FixedNamespaceInspect) == 1 &&
                    storage.attempt_count(FaultOperation::RootOpen) == 0 &&
                    storage.attempt_count(FaultOperation::JournalOpen) == 0 &&
                    storage.attempt_count(
                        FaultOperation::InitialJournalCreate) == 0 &&
                    storage.attempt_count(FaultOperation::RootStageCreate) == 0,
                "complete authority with a stage was changed or treated as "
                "a duplicate");
        require_storage_released_after_return(
            storage, "complete-with-stage create retained a handle");
    }

    auto lock_only = JournalTestStorage::fresh();
    lock_only.overwrite_fixed_child_bytes(FixedAuthorityChild::Lock, {});
    static_cast<void>(create(lock_only, chain));

    auto sentinel = JournalTestStorage::fresh();
    sentinel.seed_untrusted_file("sentinel.bin", "outside-authority");
    static_cast<void>(create(sentinel, chain));
    require(sentinel.snapshot().live_file_bytes.at("sentinel.bin") ==
                    "outside-authority" &&
                sentinel.snapshot().durable_file_bytes.at("sentinel.bin") ==
                    "outside-authority",
            "fresh create changed unrelated sentinel bytes");
}

void require_unique_authority_moves() {
    const auto chain = make_chain();
    auto constructed_storage = JournalTestStorage::fresh();
    auto constructed_source = create(constructed_storage, chain);
    PublishedJournal constructed(std::move(constructed_source));
    require(!constructed_source.available() && constructed.available(),
            "move construction duplicated or lost authority");
    constructed_storage.reset_observations();
    const auto constructed_before = constructed_storage.snapshot();
    auto moved_from_construction_journal =
        constructed_storage.make_journal(generous_limits());
    auto moved_from_construction =
        moved_from_construction_journal.append_and_publish(
            std::move(constructed_source), chain.successor);
    require_result(moved_from_construction,
                   DurableJournalStatus::CorruptOrRollback,
                   "move-constructed source retained write authority");
    require(!constructed_source.available() && constructed.available() &&
                constructed_storage.snapshot() == constructed_before,
            "move-constructed source reached authority storage");
    auto constructed_journal =
        constructed_storage.make_journal(generous_limits());
    auto constructed_published = constructed_journal.append_and_publish(
        std::move(constructed), chain.successor);
    require_result(constructed_published, DurableJournalStatus::Published,
                   "move-constructed destination could not publish");
    require(!constructed.available() &&
                constructed_published.journal->available(),
            "move-constructed destination duplicated authority");

    auto compact_storage = JournalTestStorage::fresh();
    auto compact_source = create(compact_storage, chain);
    PublishedJournal compact_destination(std::move(compact_source));
    compact_storage.reset_observations();
    const auto compact_before = compact_storage.snapshot();
    auto moved_from_compaction_journal =
        compact_storage.make_journal(generous_limits());
    auto moved_from_compaction = moved_from_compaction_journal.compact_physical(
        std::move(compact_source));
    require_result(moved_from_compaction,
                   DurableJournalStatus::CorruptOrRollback,
                   "move-constructed source retained compaction authority");
    require(!compact_source.available() && compact_destination.available() &&
                compact_storage.snapshot() == compact_before,
            "moved-from compaction reached authority storage");
    auto compact_destination_journal =
        compact_storage.make_journal(generous_limits());
    auto compact_destination_published =
        compact_destination_journal.append_and_publish(
            std::move(compact_destination), chain.successor);
    require_result(compact_destination_published,
                   DurableJournalStatus::Published,
                   "compaction move destination could not publish");

    auto assigned_storage = JournalTestStorage::fresh();
    auto assigned_source = create(assigned_storage, chain);
    auto displaced_storage = JournalTestStorage::seeded(
        frame(chain.genesis),
        std::string(chain.genesis_root.canonical_bytes()));
    auto assigned =
        recover(displaced_storage, chain.genesis_root.canonical_bytes());
    assigned = std::move(assigned_source);
    require(!assigned_source.available() && assigned.available(),
            "move assignment duplicated or lost authority");
    assigned_storage.reset_observations();
    const auto assigned_before = assigned_storage.snapshot();
    auto moved_from_assignment_journal =
        assigned_storage.make_journal(generous_limits());
    auto moved_from_assignment =
        moved_from_assignment_journal.append_and_publish(
            std::move(assigned_source), chain.successor);
    require_result(moved_from_assignment,
                   DurableJournalStatus::CorruptOrRollback,
                   "move-assigned source retained write authority");
    require(!assigned_source.available() && assigned.available() &&
                assigned_storage.snapshot() == assigned_before,
            "move-assigned source reached authority storage");

    auto journal = assigned_storage.make_journal(generous_limits());
    auto published =
        journal.append_and_publish(std::move(assigned), chain.successor);
    require_result(published, DurableJournalStatus::Published,
                   "move-assigned destination could not publish");
    require(!assigned.available() && published.journal->available(),
            "move-assigned destination duplicated authority");

    auto export_storage = JournalTestStorage::fresh();
    auto export_source = create(export_storage, chain);
    auto export_displaced_storage = JournalTestStorage::seeded(
        frame(chain.genesis),
        std::string(chain.genesis_root.canonical_bytes()));
    auto export_destination =
        recover(export_displaced_storage, chain.genesis_root.canonical_bytes());
    export_destination = std::move(export_source);
    export_storage.reset_observations();
    const auto export_before = export_storage.snapshot();
    auto moved_from_export_journal =
        export_storage.make_journal(generous_limits());
    auto moved_from_export =
        moved_from_export_journal.export_exact_schema_candidate(
            export_source, supported_journal_schema,
            JournalQuiescence::Confirmed);
    require(moved_from_export.status ==
                    ExactSchemaExportStatus::CorruptOrRollback &&
                !moved_from_export.exported() &&
                !moved_from_export.candidate.has_value() &&
                !export_source.available() && export_destination.available() &&
                export_storage.snapshot() == export_before,
            "moved-from export reached authority storage");
    auto export_destination_journal =
        export_storage.make_journal(generous_limits());
    auto export_destination_published =
        export_destination_journal.append_and_publish(
            std::move(export_destination), chain.successor);
    require_result(export_destination_published,
                   DurableJournalStatus::Published,
                   "export move destination could not publish");
}

void require_fixed_paths() {
    const std::vector<std::string> journal_ids{
        "../escape",
        "/absolute",
        "C:\\root\\journal",
        "\\\\?\\C:\\device",
        "CON",
        "con",
        "A",
        "a",
        "%2e%2e%2fescape",
        "..\\escape",
        "aux.txt",
        "PRN",
        "COM1",
        "LPT1",
        "authority-root.json",
        "authority.lock",
        "JOURNAL.JSONL",
        "\\\\server\\share",
    };
    std::optional<std::set<std::string>> expected_paths;
    for (const auto &journal_id : journal_ids) {
        const auto chain = make_chain(journal_id);
        auto storage = JournalTestStorage::fresh();
        static_cast<void>(create(storage, chain));
        storage.restart();
        const auto restarted = storage.snapshot();
        const auto paths = restarted.relative_paths;
        require(paths.count("journal.jsonl") == 1 &&
                    paths.count("authority-root.json") == 1 &&
                    paths.count("authority.lock") == 1,
                "fixed authority filenames were absent");
        require(std::all_of(paths.begin(), paths.end(),
                            [](const std::string &path) {
                                return path.find('/') == std::string::npos &&
                                       path.find('\\') == std::string::npos &&
                                       path != "." && path != "..";
                            }),
                "journal identity influenced a storage path");
        require(restarted.lock_file_present && restarted.lock_file_stable,
                "authority lock did not survive unlock and restart unchanged");
        if (!expected_paths.has_value()) {
            expected_paths = paths;
        } else {
            require(paths == *expected_paths,
                    "case, encoding, or platform identity changed fixed paths");
        }
    }
}

void require_identifier_boundaries_precede_persistence() {
    std::vector<std::string> invalid_ids;
    invalid_ids.emplace_back("journal/\xc3\xa9");
    invalid_ids.emplace_back("journal/e\xcc\x81");
    auto embedded_nul = std::string("journal/a");
    embedded_nul.push_back('\0');
    embedded_nul.push_back('b');
    invalid_ids.push_back(std::move(embedded_nul));

    auto untouched = JournalTestStorage::fresh();
    const auto before = untouched.snapshot();
    for (auto &journal_id : invalid_ids) {
        auto rejected = seal_genesis(draft_for(
            std::move(journal_id), "resident/alpha", ResidentState::Prepared));
        require(!rejected.accepted() && !rejected.candidate.has_value() &&
                    rejected.status == JournalStatus::InvalidIdentifier,
                "non-codec identifier crossed the TASK-019 boundary");
    }
    require(untouched.snapshot() == before,
            "identifier rejection reached persistence");
}

void require_replay_and_floors() {
    const auto chain = make_chain();
    const auto one_frame = frame(chain.genesis);
    const auto two_frames = one_frame + frame(chain.successor);
    const auto three_frames = two_frames + frame(chain.third);

    auto exact = JournalTestStorage::seeded(
        two_frames, std::string(chain.successor_root.canonical_bytes()));
    auto exact_authority =
        recover(exact, chain.successor_root.canonical_bytes());
    require(exact_authority.root_bytes() ==
                chain.successor_root.canonical_bytes(),
            "exact floor recovered a different root");
    require(exact.snapshot().root_read_before_journal_read &&
                exact.snapshot().all_authority_io_under_lock,
            "recovery did not read root first under the full lock");

    auto descendant = JournalTestStorage::seeded(
        three_frames, std::string(chain.third_root.canonical_bytes()));
    auto descendant_authority =
        recover(descendant, chain.genesis_root.canonical_bytes());
    require(descendant_authority.root_bytes() ==
                chain.third_root.canonical_bytes(),
            "proven descendant floor did not recover the stored root");

    auto regressed = JournalTestStorage::seeded(
        one_frame, std::string(chain.genesis_root.canonical_bytes()));
    auto regressed_journal = regressed.make_journal(generous_limits());
    auto regressed_result =
        regressed_journal.recover_existing(TrustedReplayFloor::exact_root(
            std::string(chain.successor_root.canonical_bytes())));
    require_result(regressed_result, DurableJournalStatus::CorruptOrRollback,
                   "regressed root passed a newer independent floor");
    require_storage_released_after_return(
        regressed, "regressed recovery retained an authority handle");

    auto consistency_only = JournalTestStorage::seeded(
        one_frame, std::string(chain.genesis_root.canonical_bytes()));
    auto consistency_authority =
        recover(consistency_only, chain.genesis_root.canonical_bytes());
    require(consistency_authority.root_bytes() ==
                chain.genesis_root.canonical_bytes(),
            "coherently rolled-back storage and floor lost consistency-only "
            "recovery");

    auto divergent = JournalTestStorage::seeded(
        two_frames, std::string(chain.successor_root.canonical_bytes()));
    auto divergent_journal = divergent.make_journal(generous_limits());
    auto divergent_result =
        divergent_journal.recover_existing(TrustedReplayFloor::exact_root(
            std::string(chain.fork_root.canonical_bytes())));
    require_result(divergent_result, DurableJournalStatus::CorruptOrRollback,
                   "same-generation fork passed the independent floor");
    require_storage_released_after_return(
        divergent, "divergent recovery retained an authority handle");

    auto absent_floor = exact.make_journal(generous_limits());
    auto absent_result =
        absent_floor.recover_existing(TrustedReplayFloor::uninitialized());
    require_result(absent_result, DurableJournalStatus::CorruptOrRollback,
                   "uninitialized floor authorized existing storage");
    require_storage_released_after_return(
        exact, "uninitialized-floor recovery retained an authority handle");

    auto local_rollback = JournalTestStorage::seeded(
        one_frame, std::string(chain.genesis_root.canonical_bytes()));
    local_rollback.seed_untrusted_file(
        "local-floor.json", std::string(chain.genesis_root.canonical_bytes()));
    auto local_journal = local_rollback.make_journal(generous_limits());
    auto local_result =
        local_journal.recover_existing(TrustedReplayFloor::exact_root(
            std::string(chain.successor_root.canonical_bytes())));
    require_result(local_result, DurableJournalStatus::CorruptOrRollback,
                   "local rollback sidecar replaced the independent floor");
    require_storage_released_after_return(
        local_rollback, "rollback rejection retained an authority handle");

    auto root_ahead = JournalTestStorage::seeded(
        one_frame, std::string(chain.successor_root.canonical_bytes()));
    auto root_ahead_journal = root_ahead.make_journal(generous_limits());
    auto root_ahead_result =
        root_ahead_journal.recover_existing(TrustedReplayFloor::exact_root(
            std::string(chain.genesis_root.canonical_bytes())));
    require_result(root_ahead_result, DurableJournalStatus::CorruptOrRollback,
                   "root ahead of durable journal was authorized");
    require_storage_released_after_return(
        root_ahead, "root-ahead rejection retained an authority handle");

    auto missing_root = JournalTestStorage::seeded(one_frame, std::nullopt);
    auto missing_root_journal = missing_root.make_journal(generous_limits());
    auto missing_root_result =
        missing_root_journal.recover_existing(TrustedReplayFloor::exact_root(
            std::string(chain.genesis_root.canonical_bytes())));
    require_result(missing_root_result, DurableJournalStatus::CorruptOrRollback,
                   "journal bytes without a root implied genesis");
    require_storage_released_after_return(
        missing_root, "missing-root rejection retained an authority handle");

    auto committed_truncation = JournalTestStorage::seeded(
        two_frames.substr(0, two_frames.size() - 1),
        std::string(chain.successor_root.canonical_bytes()));
    auto truncation_journal =
        committed_truncation.make_journal(generous_limits());
    auto truncation_result =
        truncation_journal.recover_existing(TrustedReplayFloor::exact_root(
            std::string(chain.genesis_root.canonical_bytes())));
    require_result(truncation_result, DurableJournalStatus::CorruptOrRollback,
                   "missing committed LF terminator was authorized");
    require_storage_released_after_return(
        committed_truncation,
        "committed-truncation rejection retained an authority handle");
}

void require_surviving_stage_recovery() {
    const auto chain = make_chain();
    const auto old_bytes = frame(chain.genesis);
    const auto successor_bytes = old_bytes + frame(chain.successor);
    const auto seed_stages = [](JournalTestStorage &storage) {
        storage.overwrite_fixed_child_bytes(FixedAuthorityChild::RootStage,
                                            "surviving-root-stage");
        storage.overwrite_fixed_child_bytes(FixedAuthorityChild::JournalStage,
                                            "surviving-journal-stage");
    };

    for (const auto &[journal_bytes, root_bytes, expected_journal,
                      expected_root] :
         {std::tuple{
              old_bytes, std::string(chain.genesis_root.canonical_bytes()),
              old_bytes, std::string(chain.genesis_root.canonical_bytes())},
          std::tuple{successor_bytes,
                     std::string(chain.genesis_root.canonical_bytes()),
                     old_bytes,
                     std::string(chain.genesis_root.canonical_bytes())},
          std::tuple{successor_bytes,
                     std::string(chain.successor_root.canonical_bytes()),
                     successor_bytes,
                     std::string(chain.successor_root.canonical_bytes())}}) {
        auto storage = JournalTestStorage::seeded(journal_bytes, root_bytes);
        seed_stages(storage);
        const auto before = storage.snapshot();
        auto authority = recover(storage, chain.genesis_root.canonical_bytes());
        const auto after = storage.snapshot();
        require(authority.root_bytes() == expected_root &&
                    after.journal_bytes == expected_journal &&
                    stage_files_unchanged(before, after) &&
                    !has_stage_operation(after) && after.journal_durable &&
                    after.authority_root_durable &&
                    after.all_authority_io_under_lock,
                "safe surviving stages influenced or changed recovery");
    }

    auto append_storage = JournalTestStorage::seeded(
        old_bytes, std::string(chain.genesis_root.canonical_bytes()));
    seed_stages(append_storage);
    const auto append_before = append_storage.snapshot();
    auto append_authority =
        recover(append_storage, chain.genesis_root.canonical_bytes());
    auto append_journal = append_storage.make_journal(generous_limits());
    auto appended = append_journal.append_and_publish(
        std::move(append_authority), chain.successor);
    require_result(appended, DurableJournalStatus::Published,
                   "root publication could not replace a safe root stage");
    const auto append_after = append_storage.snapshot();
    require(
        append_after.relative_paths.count(".authority-root.json.stage") == 0 &&
            append_after.durable_paths.count(".authority-root.json.stage") ==
                0 &&
            fixed_file_unchanged(append_before, append_after,
                                 ".journal.jsonl.stage") &&
            append_after.journal_bytes == successor_bytes &&
            append_after.authority_root_bytes ==
                chain.successor_root.canonical_bytes() &&
            append_after.all_authority_io_under_lock,
        "root publication broadly cleaned or trusted surviving stages");

    auto compact_storage = JournalTestStorage::seeded(
        successor_bytes, std::string(chain.successor_root.canonical_bytes()));
    seed_stages(compact_storage);
    const auto compact_before = compact_storage.snapshot();
    auto compact_authority =
        recover(compact_storage, chain.genesis_root.canonical_bytes());
    auto compact_journal = compact_storage.make_journal(generous_limits());
    auto compacted =
        compact_journal.compact_physical(std::move(compact_authority));
    require_result(compacted, DurableJournalStatus::Published,
                   "compaction could not replace a safe journal stage");
    const auto compact_after = compact_storage.snapshot();
    require(compact_after.relative_paths.count(".journal.jsonl.stage") == 0 &&
                compact_after.durable_paths.count(".journal.jsonl.stage") ==
                    0 &&
                fixed_file_unchanged(compact_before, compact_after,
                                     ".authority-root.json.stage") &&
                compact_after.journal_bytes == successor_bytes &&
                compact_after.authority_root_bytes ==
                    chain.successor_root.canonical_bytes() &&
                compact_after.all_authority_io_under_lock,
            "compaction broadly cleaned or trusted surviving stages");

    auto root_ahead = JournalTestStorage::seeded(
        old_bytes, std::string(chain.successor_root.canonical_bytes()));
    seed_stages(root_ahead);
    const auto before = root_ahead.snapshot();
    auto journal = root_ahead.make_journal(generous_limits());
    auto result = journal.recover_existing(TrustedReplayFloor::exact_root(
        std::string(chain.genesis_root.canonical_bytes())));
    require_result(result, DurableJournalStatus::CorruptOrRollback,
                   "root-ahead crash state was authorized by stages");
    require(same_persistent_state(root_ahead.snapshot(), before) &&
                !root_ahead.snapshot().authority_mutation_attempted,
            "root-ahead rejection changed crash-state evidence");
    require_storage_released_after_return(
        root_ahead, "staged root-ahead rejection retained a handle");
}

void require_root_parser_is_task019() {
    const auto chain = make_chain();
    const auto committed = frame(chain.genesis);
    const auto exact_root = std::string(chain.genesis_root.canonical_bytes());
    std::vector<std::string> adversaries{
        exact_root + "\n",
        exact_root + " ",
        std::string("\xef\xbb\xbf") + exact_root,
        "{",
        std::string(max_journal_input_bytes + 1, 'x'),
        replace_once(exact_root, "{", "{\"unknown\":0,"),
        replace_once(exact_root, "{", "{\"schema\":{\"major\":1,\"minor\":0},"),
        replace_once(exact_root, "\"major\":1", "\"major\":2"),
        replace_once(exact_root, "\"generation\":1", "\"generation\":2"),
        replace_once(exact_root, "\"tip_sequence\":1", "\"tip_sequence\":2"),
        replace_once(exact_root, "\"journal_id\":\"journal/main\"",
                     "\"journal_id\":\"journal/other\""),
        replace_once(
            exact_root, chain.genesis_root.checksum_sha256(),
            "0000000000000000000000000000000000000000000000000000000000000000"),
    };
    for (const auto &root : adversaries) {
        auto storage = JournalTestStorage::seeded(committed, root);
        const auto before = storage.snapshot();
        auto journal = storage.make_journal(generous_limits());
        auto result = journal.recover_existing(
            TrustedReplayFloor::exact_root(exact_root));
        require_result(result, DurableJournalStatus::CorruptOrRollback,
                       "alternate authority-root encoding was accepted");
        require(same_persistent_state(storage.snapshot(), before) &&
                    !storage.snapshot().authority_mutation_attempted,
                "invalid authority root triggered repair or mutation");
        require_storage_released_after_return(
            storage, "invalid root rejection retained an authority handle");
    }
}

void require_committed_origin_verification() {
    const auto chain = make_chain();
    AllowingOriginVerifier allowing;
    const auto quarantine = make_quarantine_tail(chain);
    auto history =
        take_history(begin_history(chain.genesis), "origin genesis failed");
    history =
        take_history(advance_history(std::move(history), quarantine, &allowing),
                     "origin quarantine history failed");
    const auto root =
        take_root(seal_authority_root_candidate(history, &chain.genesis_root),
                  "origin quarantine root failed");
    const auto bytes = frame(chain.genesis) + frame(quarantine);

    RejectIfCalledVerifier rejecting;
    for (const RecoveryOriginVerifier *verifier :
         {static_cast<const RecoveryOriginVerifier *>(nullptr),
          static_cast<const RecoveryOriginVerifier *>(&rejecting)}) {
        auto storage = JournalTestStorage::seeded(
            bytes, std::string(root.canonical_bytes()));
        const auto before = storage.snapshot();
        auto journal = storage.make_journal(generous_limits());
        auto result =
            journal.recover_existing(TrustedReplayFloor::exact_root(std::string(
                                         chain.genesis_root.canonical_bytes())),
                                     verifier);
        require_result(
            result, DurableJournalStatus::CorruptOrRollback,
            "unavailable origin verification authorized committed history");
        require(same_persistent_state(storage.snapshot(), before) &&
                    !storage.snapshot().authority_mutation_attempted,
                "origin-verification failure changed committed storage");
        require_storage_released_after_return(
            storage,
            "origin-verification rejection retained an authority handle");
    }
    require(rejecting.called.load(),
            "rejecting verifier was not called for committed authority");

    auto accepted =
        JournalTestStorage::seeded(bytes, std::string(root.canonical_bytes()));
    auto authority = recover(accepted, chain.genesis_root.canonical_bytes(),
                             generous_limits(), &allowing);
    require(authority.root_bytes() == root.canonical_bytes(),
            "available origin verifier did not authorize committed history");
}

ParsedJournalRecord make_quarantine_tail(const Chain &chain) {
    AllowingOriginVerifier verifier;
    auto history = take_history(begin_history(chain.genesis),
                                "quarantine-tail genesis failed");
    return take_record(
        seal_successor(history,
                       draft_for(std::string(chain.genesis.journal_id()),
                                 "resident/alpha", ResidentState::Quarantined),
                       &verifier),
        "quarantine tail fixture was rejected");
}

struct QuarantinedGenesis {
    ParsedJournalRecord record;
    AuthorityRootCandidate root;
};

QuarantinedGenesis make_quarantined_genesis() {
    AllowingOriginVerifier verifier;
    auto record = take_record(
        seal_genesis(draft_for("journal/quarantine", "resident/quarantine",
                               ResidentState::Quarantined),
                     &verifier),
        "quarantined genesis fixture was rejected");
    auto history = take_history(begin_history(record, &verifier),
                                "quarantined genesis history was rejected");
    auto root = take_root(seal_authority_root_candidate(history),
                          "quarantined genesis root was rejected");
    return {std::move(record), std::move(root)};
}

void require_origin_verifier_publication_and_lifetime() {
    const auto chain = make_chain();
    const auto quarantined_genesis = make_quarantined_genesis();

    RejectIfCalledVerifier create_rejecting;
    for (const RecoveryOriginVerifier *verifier :
         {static_cast<const RecoveryOriginVerifier *>(nullptr),
          static_cast<const RecoveryOriginVerifier *>(&create_rejecting)}) {
        auto storage = JournalTestStorage::fresh();
        const auto before = storage.snapshot();
        auto journal = storage.make_journal(generous_limits());
        auto result = journal.create_new(TrustedReplayFloor::uninitialized(),
                                         quarantined_genesis.record, verifier);
        require_result(result, DurableJournalStatus::CorruptOrRollback,
                       "unverified quarantined genesis was published");
        require(same_persistent_state(storage.snapshot(), before) &&
                    !storage.snapshot().authority_mutation_attempted,
                "rejected quarantined genesis changed authority storage");
        require_storage_released_after_return(
            storage, "quarantined create rejection retained a handle");
    }
    require(create_rejecting.called.load(),
            "create_new did not invoke the rejecting origin verifier");

    AllowingOriginVerifier allowing;
    auto created_storage = JournalTestStorage::fresh();
    auto create_journal = created_storage.make_journal(generous_limits());
    auto created =
        create_journal.create_new(TrustedReplayFloor::uninitialized(),
                                  quarantined_genesis.record, &allowing);
    require_result(created, DurableJournalStatus::Published,
                   "verified quarantined genesis was not published");
    created_storage.restart();
    auto missing_recovery =
        created_storage.make_journal(generous_limits())
            .recover_existing(TrustedReplayFloor::exact_root(
                std::string(quarantined_genesis.root.canonical_bytes())));
    require_result(missing_recovery, DurableJournalStatus::CorruptOrRollback,
                   "quarantined genesis recovery skipped reverification");
    require_storage_released_after_return(
        created_storage,
        "quarantined recovery rejection retained an authority handle");
    static_cast<void>(recover(created_storage,
                              quarantined_genesis.root.canonical_bytes(),
                              generous_limits(), &allowing));

    const auto prove_existing_quarantine_does_not_borrow =
        [](JournalTestStorage &storage,
           std::optional<PublishedJournal> authority,
           const std::shared_ptr<VerifierLifetimeState> &state,
           std::size_t validated_calls, std::string_view label) {
            require(authority.has_value() && authority->available() &&
                        !state->alive.load() && validated_calls != 0,
                    label);
            auto compact_journal = storage.make_journal(generous_limits());
            auto compacted =
                compact_journal.compact_physical(std::move(*authority));
            require_result(compacted, DurableJournalStatus::Published, label);
            auto export_journal = storage.make_journal(generous_limits());
            auto exported = export_journal.export_exact_schema_candidate(
                *compacted.journal, supported_journal_schema,
                JournalQuiescence::Confirmed);
            require(exported.exported() && exported.candidate.has_value() &&
                        state->calls.load() == validated_calls,
                    label);
        };

    auto create_lifetime_storage = JournalTestStorage::fresh();
    auto create_lifetime_state = std::make_shared<VerifierLifetimeState>();
    std::optional<PublishedJournal> create_lifetime_authority;
    std::size_t create_lifetime_calls = 0;
    {
        LifetimeAllowingVerifier verifier(create_lifetime_state);
        auto journal = create_lifetime_storage.make_journal(generous_limits());
        auto result = journal.create_new(TrustedReplayFloor::uninitialized(),
                                         quarantined_genesis.record, &verifier);
        require_result(result, DurableJournalStatus::Published,
                       "scoped verifier create was rejected");
        create_lifetime_authority.emplace(std::move(*result.journal));
        create_lifetime_calls = create_lifetime_state->calls.load();
    }
    prove_existing_quarantine_does_not_borrow(
        create_lifetime_storage, std::move(create_lifetime_authority),
        create_lifetime_state, create_lifetime_calls,
        "create token retained a borrowed origin verifier");

    auto recover_lifetime_storage = JournalTestStorage::seeded(
        frame(quarantined_genesis.record),
        std::string(quarantined_genesis.root.canonical_bytes()));
    auto recover_lifetime_state = std::make_shared<VerifierLifetimeState>();
    std::optional<PublishedJournal> recover_lifetime_authority;
    std::size_t recover_lifetime_calls = 0;
    {
        LifetimeAllowingVerifier verifier(recover_lifetime_state);
        auto journal = recover_lifetime_storage.make_journal(generous_limits());
        auto result = journal.recover_existing(
            TrustedReplayFloor::exact_root(
                std::string(quarantined_genesis.root.canonical_bytes())),
            &verifier);
        require_result(result, DurableJournalStatus::Published,
                       "scoped verifier recovery was rejected");
        recover_lifetime_authority.emplace(std::move(*result.journal));
        recover_lifetime_calls = recover_lifetime_state->calls.load();
    }
    prove_existing_quarantine_does_not_borrow(
        recover_lifetime_storage, std::move(recover_lifetime_authority),
        recover_lifetime_state, recover_lifetime_calls,
        "recovery token retained a borrowed origin verifier");

    const auto quarantine = make_quarantine_tail(chain);
    RejectIfCalledVerifier append_rejecting;
    for (const RecoveryOriginVerifier *verifier :
         {static_cast<const RecoveryOriginVerifier *>(nullptr),
          static_cast<const RecoveryOriginVerifier *>(&append_rejecting)}) {
        auto storage = JournalTestStorage::seeded(
            frame(chain.genesis),
            std::string(chain.genesis_root.canonical_bytes()));
        auto authority = recover(storage, chain.genesis_root.canonical_bytes());
        storage.reset_observations();
        const auto before = storage.snapshot();
        auto journal = storage.make_journal(generous_limits());
        auto result = journal.append_and_publish(std::move(authority),
                                                 quarantine, verifier);
        require_result(result, DurableJournalStatus::CorruptOrRollback,
                       "unverified quarantined append was published");
        require(!authority.available() &&
                    same_persistent_state(storage.snapshot(), before) &&
                    !storage.snapshot().authority_mutation_attempted,
                "rejected quarantined append changed authority storage");
        require_storage_released_after_return(
            storage, "quarantined append rejection retained a handle");
    }
    require(append_rejecting.called.load(),
            "append did not invoke the rejecting origin verifier");

    auto lifetime_storage = JournalTestStorage::seeded(
        frame(chain.genesis),
        std::string(chain.genesis_root.canonical_bytes()));
    auto lifetime_authority =
        recover(lifetime_storage, chain.genesis_root.canonical_bytes());
    auto lifetime_state = std::make_shared<VerifierLifetimeState>();
    std::optional<PublishedJournal> verified_authority;
    std::size_t verified_calls = 0;
    {
        LifetimeAllowingVerifier lifetime_verifier(lifetime_state);
        auto journal = lifetime_storage.make_journal(generous_limits());
        auto result = journal.append_and_publish(
            std::move(lifetime_authority), quarantine, &lifetime_verifier);
        require_result(result, DurableJournalStatus::Published,
                       "verified quarantined append was rejected");
        verified_authority.emplace(std::move(*result.journal));
        verified_calls = lifetime_state->calls.load();
    }
    require(!lifetime_state->alive.load() && verified_calls != 0,
            "verifier lifetime fixture did not expire after validation");

    auto compact_journal = lifetime_storage.make_journal(generous_limits());
    auto compacted =
        compact_journal.compact_physical(std::move(*verified_authority));
    require_result(compacted, DurableJournalStatus::Published,
                   "compaction retained a borrowed verifier dependency");
    auto export_journal = lifetime_storage.make_journal(generous_limits());
    auto exported = export_journal.export_exact_schema_candidate(
        *compacted.journal, supported_journal_schema,
        JournalQuiescence::Confirmed);
    require(exported.exported() && exported.candidate.has_value() &&
                lifetime_state->calls.load() == verified_calls,
            "compaction or export called a destroyed verifier");

    lifetime_storage.restart();
    auto recovery_without_verifier =
        lifetime_storage.make_journal(generous_limits())
            .recover_existing(TrustedReplayFloor::exact_root(
                std::string(chain.genesis_root.canonical_bytes())));
    require_result(recovery_without_verifier,
                   DurableJournalStatus::CorruptOrRollback,
                   "quarantined append recovery skipped reverification");
    require_storage_released_after_return(
        lifetime_storage,
        "unverified quarantine recovery retained an authority handle");
    static_cast<void>(recover(lifetime_storage,
                              chain.genesis_root.canonical_bytes(),
                              generous_limits(), &allowing));
}

void require_tail_classification_and_repair() {
    const auto chain = make_chain();
    const auto committed = frame(chain.genesis);
    const auto quarantine = make_quarantine_tail(chain);
    const std::vector<std::string> tolerated_tails{
        frame(chain.successor),
        frame(chain.fork),
        frame(chain.successor) + frame(chain.third),
        frame(quarantine),
        frame(chain.successor).substr(0, frame(chain.successor).size() / 2),
        std::string("garbage\0tail", 12),
        std::string(max_journal_input_bytes + 1, 'x'),
        "\r\n\n",
    };

    for (const auto &tail : tolerated_tails) {
        auto limits = generous_limits();
        limits.max_crash_tail_bytes = tail.size();
        auto storage = JournalTestStorage::seeded(
            committed + tail,
            std::string(chain.genesis_root.canonical_bytes()));
        const auto before = storage.snapshot();
        RejectIfCalledVerifier verifier;
        auto authority = recover(storage, chain.genesis_root.canonical_bytes(),
                                 limits, &verifier);
        require(authority.root_bytes() == chain.genesis_root.canonical_bytes(),
                "tail recovery synthesized a root");
        require(!verifier.called.load(),
                "replay parsed past the stored authority root");
        const auto after = storage.snapshot();
        require(after.journal_bytes == committed &&
                    after.authority_root_bytes ==
                        chain.genesis_root.canonical_bytes() &&
                    after.stage_files_absent &&
                    after.durable_stage_files_absent &&
                    !has_stage_operation(after) &&
                    after.all_authority_io_under_lock,
                "tail was not durably repaired before authority returned");
        require(
            after.journal_durable && after.journal_closed,
            "tail repair returned before file durability and checked close");
        require(before.journal_bytes != after.journal_bytes,
                "tail-repair fixture did not mutate storage");
    }

    const auto second_resident =
        make_second_resident(chain, std::string(chain.genesis.journal_id()));
    for (auto limits : {[] {
                            auto selected = generous_limits();
                            selected.max_committed_records = 1;
                            return selected;
                        }(),
                        [] {
                            auto selected = generous_limits();
                            selected.max_resident_heads = 1;
                            return selected;
                        }()}) {
        const auto &tail_record = limits.max_committed_records == 1
                                      ? chain.successor
                                      : second_resident;
        const auto tail = frame(tail_record);
        limits.max_crash_tail_bytes = tail.size();
        auto storage = JournalTestStorage::seeded(
            committed + tail,
            std::string(chain.genesis_root.canonical_bytes()));
        RejectIfCalledVerifier verifier;
        auto authority = recover(storage, chain.genesis_root.canonical_bytes(),
                                 limits, &verifier);
        require(
            authority.root_bytes() == chain.genesis_root.canonical_bytes() &&
                storage.snapshot().journal_bytes == committed &&
                storage.snapshot().journal_durable &&
                storage.snapshot().journal_closed && !verifier.called.load(),
            "valid crash tail was counted against the authorized prefix");
    }

    const std::vector<std::string> malformed_committed{
        std::string("\xef\xbb\xbf") + std::string(genesis_wire) + '\n',
        std::string(genesis_wire) + "\r\n",
        std::string("\n") + std::string(genesis_wire) + '\n',
        std::string(genesis_wire),
    };
    for (const auto &journal_bytes : malformed_committed) {
        auto storage = JournalTestStorage::seeded(
            journal_bytes, std::string(chain.genesis_root.canonical_bytes()));
        const auto before = storage.snapshot();
        auto journal = storage.make_journal(generous_limits());
        auto result = journal.recover_existing(TrustedReplayFloor::exact_root(
            std::string(chain.genesis_root.canonical_bytes())));
        require_result(result, DurableJournalStatus::CorruptOrRollback,
                       "invalid committed LF framing was authorized");
        require(same_persistent_state(storage.snapshot(), before),
                "invalid committed LF framing triggered repair or mutation");
        require(!storage.snapshot().authority_mutation_attempted,
                "invalid committed LF framing attempted authority mutation");
        require_storage_released_after_return(
            storage,
            "invalid committed LF framing retained an authority handle");
    }
}

DurableJournalStatus recovery_status(JournalTestStorage &storage,
                                     std::string_view floor,
                                     JournalLimits limits) {
    auto journal = storage.make_journal(limits);
    auto result = journal.recover_existing(
        TrustedReplayFloor::exact_root(std::string(floor)));
    require(!result.journal.has_value() ||
                result.status == DurableJournalStatus::Published,
            "failed recovery carried authority");
    require_storage_released_after_return(
        storage, "recovery result retained an authority handle");
    return result.status;
}

void require_explicit_limits() {
    const auto chain = make_chain();
    const auto one_frame = frame(chain.genesis);
    const auto two_frames = one_frame + frame(chain.successor);

    for (std::size_t field = 0; field < 4; ++field) {
        auto limits = generous_limits();
        if (field == 0) {
            limits.max_committed_bytes = 0;
        } else if (field == 1) {
            limits.max_committed_records = 0;
        } else if (field == 2) {
            limits.max_resident_heads = 0;
        } else {
            limits.max_crash_tail_bytes = 0;
        }
        auto storage = JournalTestStorage::fresh();
        const auto before = storage.snapshot();
        auto journal = storage.make_journal(limits);
        auto result = journal.create_new(TrustedReplayFloor::uninitialized(),
                                         chain.genesis);
        require_result(result, DurableJournalStatus::LimitExceeded,
                       "zero journal limit did not fail closed");
        require(storage.snapshot() == before, "zero limit mutated storage");

        auto existing = JournalTestStorage::seeded(
            one_frame, std::string(chain.genesis_root.canonical_bytes()));
        const auto existing_before = existing.snapshot();
        require(recovery_status(existing, chain.genesis_root.canonical_bytes(),
                                limits) == DurableJournalStatus::LimitExceeded,
                "zero journal limit was accepted during recovery");
        require(existing.snapshot() == existing_before,
                "zero recovery limit changed committed storage");
    }

    auto byte_exact_limits = generous_limits();
    byte_exact_limits.max_committed_bytes = two_frames.size();
    auto byte_exact = JournalTestStorage::seeded(
        two_frames, std::string(chain.successor_root.canonical_bytes()));
    require(recovery_status(byte_exact, chain.genesis_root.canonical_bytes(),
                            byte_exact_limits) ==
                DurableJournalStatus::Published,
            "inclusive committed-byte limit was rejected");
    auto byte_over = JournalTestStorage::seeded(
        two_frames, std::string(chain.successor_root.canonical_bytes()));
    byte_exact_limits.max_committed_bytes = two_frames.size() - 1;
    const auto byte_over_before = byte_over.snapshot();
    require(recovery_status(byte_over, chain.genesis_root.canonical_bytes(),
                            byte_exact_limits) ==
                DurableJournalStatus::LimitExceeded,
            "committed-byte limit +1 was accepted");
    require(same_persistent_state(byte_over.snapshot(), byte_over_before) &&
                !byte_over.snapshot().authority_mutation_attempted,
            "committed-byte limit failure changed storage");

    auto record_limits = generous_limits();
    record_limits.max_committed_records = 2;
    auto record_exact = JournalTestStorage::seeded(
        two_frames, std::string(chain.successor_root.canonical_bytes()));
    require(recovery_status(record_exact, chain.genesis_root.canonical_bytes(),
                            record_limits) == DurableJournalStatus::Published,
            "inclusive committed-record limit was rejected");
    auto record_over = JournalTestStorage::seeded(
        two_frames, std::string(chain.successor_root.canonical_bytes()));
    record_limits.max_committed_records = 1;
    const auto record_over_before = record_over.snapshot();
    require(recovery_status(record_over, chain.genesis_root.canonical_bytes(),
                            record_limits) ==
                DurableJournalStatus::LimitExceeded,
            "committed-record limit +1 was accepted");
    require(same_persistent_state(record_over.snapshot(), record_over_before) &&
                !record_over.snapshot().authority_mutation_attempted,
            "committed-record limit failure changed storage");

    const auto beta = make_second_resident(chain, "journal/main");
    const auto beta_root = root_after(chain, beta);
    const auto beta_bytes = one_frame + frame(beta);
    auto head_limits = generous_limits();
    head_limits.max_resident_heads = 1;
    auto same_resident = JournalTestStorage::seeded(
        two_frames, std::string(chain.successor_root.canonical_bytes()));
    require(recovery_status(same_resident, chain.genesis_root.canonical_bytes(),
                            head_limits) == DurableJournalStatus::Published,
            "record count was mistaken for resident-head count");
    head_limits.max_resident_heads = 2;
    auto head_exact = JournalTestStorage::seeded(
        beta_bytes, std::string(beta_root.canonical_bytes()));
    require(recovery_status(head_exact, chain.genesis_root.canonical_bytes(),
                            head_limits) == DurableJournalStatus::Published,
            "inclusive resident-head limit was rejected");
    auto head_over = JournalTestStorage::seeded(
        beta_bytes, std::string(beta_root.canonical_bytes()));
    head_limits.max_resident_heads = 1;
    const auto head_over_before = head_over.snapshot();
    require(recovery_status(head_over, chain.genesis_root.canonical_bytes(),
                            head_limits) == DurableJournalStatus::LimitExceeded,
            "resident-head limit +1 was accepted");
    require(same_persistent_state(head_over.snapshot(), head_over_before) &&
                !head_over.snapshot().authority_mutation_attempted,
            "resident-head limit failure changed storage");

    const auto tail = frame(chain.successor);
    auto tail_limits = generous_limits();
    tail_limits.max_crash_tail_bytes = tail.size();
    auto tail_exact = JournalTestStorage::seeded(
        one_frame + tail, std::string(chain.genesis_root.canonical_bytes()));
    require(recovery_status(tail_exact, chain.genesis_root.canonical_bytes(),
                            tail_limits) == DurableJournalStatus::Published,
            "inclusive crash-tail limit was rejected");
    auto tail_over = JournalTestStorage::seeded(
        one_frame + tail, std::string(chain.genesis_root.canonical_bytes()));
    tail_limits.max_crash_tail_bytes = tail.size() - 1;
    const auto tail_over_before = tail_over.snapshot();
    require(recovery_status(tail_over, chain.genesis_root.canonical_bytes(),
                            tail_limits) == DurableJournalStatus::LimitExceeded,
            "crash-tail limit +1 was accepted");
    require(same_persistent_state(tail_over.snapshot(), tail_over_before) &&
                !tail_over.snapshot().authority_mutation_attempted,
            "crash-tail limit failure changed storage");

    for (const auto limits :
         {JournalLimits{std::numeric_limits<std::size_t>::max(), 128, 32, 1},
          JournalLimits{1, 128, 32, std::numeric_limits<std::size_t>::max()},
          JournalLimits{std::numeric_limits<std::size_t>::max() - 1, 128, 32,
                        1},
          JournalLimits{1, 128, 32,
                        std::numeric_limits<std::size_t>::max() - 1},
          JournalLimits{std::numeric_limits<std::size_t>::max(), 128, 32,
                        std::numeric_limits<std::size_t>::max()}}) {
        auto overflow = JournalTestStorage::seeded(
            one_frame, std::string(chain.genesis_root.canonical_bytes()));
        overflow.reset_observations();
        const auto before = overflow.snapshot();
        require(recovery_status(overflow, chain.genesis_root.canonical_bytes(),
                                limits) == DurableJournalStatus::LimitExceeded,
                "overflowing bounded-read arithmetic did not fail closed");
        require(same_persistent_state(overflow.snapshot(), before) &&
                    overflow.attempt_count(FaultOperation::RootOpen) == 0 &&
                    overflow.attempt_count(FaultOperation::JournalOpen) == 0 &&
                    !overflow.snapshot().authority_mutation_attempted,
                "overflowing bounds reached storage I/O or mutation");
    }

    auto nonzero_small = generous_limits();
    nonzero_small.max_committed_bytes = one_frame.size() - 1;
    auto small_create = JournalTestStorage::fresh();
    const auto small_before = small_create.snapshot();
    auto small_journal = small_create.make_journal(nonzero_small);
    auto small_result = small_journal.create_new(
        TrustedReplayFloor::uninitialized(), chain.genesis);
    require_result(small_result, DurableJournalStatus::LimitExceeded,
                   "nonzero undersized create limit reached publication");
    require(small_create.snapshot() == small_before,
            "undersized create limit performed storage I/O");

    const auto require_append_limit = [&](JournalLimits limits,
                                          const ParsedJournalRecord &candidate,
                                          std::string_view label) {
        auto storage = JournalTestStorage::seeded(
            one_frame, std::string(chain.genesis_root.canonical_bytes()));
        auto authority = recover(storage, chain.genesis_root.canonical_bytes());
        storage.reset_observations();
        const auto before = storage.snapshot();
        auto journal = storage.make_journal(limits);
        auto result =
            journal.append_and_publish(std::move(authority), candidate);
        require_result(result, DurableJournalStatus::LimitExceeded, label);
        require(!authority.available() && storage.snapshot() == before &&
                    !storage.snapshot().authority_mutation_attempted &&
                    storage.attempt_count(FaultOperation::RootOpen) == 0 &&
                    storage.attempt_count(FaultOperation::JournalOpen) == 0,
                "append limit failure was not write-free");
    };
    auto append_bytes = generous_limits();
    append_bytes.max_committed_bytes = one_frame.size();
    require_append_limit(append_bytes, chain.successor,
                         "append exceeded committed-byte limit");
    auto append_records = generous_limits();
    append_records.max_committed_records = 1;
    require_append_limit(append_records, chain.successor,
                         "append exceeded committed-record limit");
    auto append_heads = generous_limits();
    append_heads.max_resident_heads = 1;
    require_append_limit(append_heads, beta,
                         "append exceeded resident-head limit");

    const std::vector<std::tuple<std::string, std::string, JournalLimits>>
        token_limit_cases{
            {two_frames, std::string(chain.successor_root.canonical_bytes()),
             JournalLimits{two_frames.size() - 1, 128, 32,
                           generous_limits().max_crash_tail_bytes}},
            {two_frames, std::string(chain.successor_root.canonical_bytes()),
             JournalLimits{generous_limits().max_committed_bytes, 1, 32,
                           generous_limits().max_crash_tail_bytes}},
            {beta_bytes, std::string(beta_root.canonical_bytes()),
             JournalLimits{generous_limits().max_committed_bytes, 128, 1,
                           generous_limits().max_crash_tail_bytes}},
        };
    for (const auto &[bytes, root, limits] : token_limit_cases) {
        auto compact_storage = JournalTestStorage::seeded(bytes, root);
        auto compact_authority =
            recover(compact_storage, chain.genesis_root.canonical_bytes());
        compact_storage.reset_observations();
        const auto compact_before = compact_storage.snapshot();
        auto compact_journal = compact_storage.make_journal(limits);
        auto compact_result =
            compact_journal.compact_physical(std::move(compact_authority));
        require_result(compact_result, DurableJournalStatus::LimitExceeded,
                       "token limit did not refuse compaction");
        require(!compact_authority.available() &&
                    compact_storage.snapshot() == compact_before,
                "compaction limit reached storage I/O or mutation");

        auto export_storage = JournalTestStorage::seeded(bytes, root);
        auto export_authority =
            recover(export_storage, chain.genesis_root.canonical_bytes());
        export_storage.reset_observations();
        const auto export_before = export_storage.snapshot();
        auto export_journal = export_storage.make_journal(limits);
        auto export_result = export_journal.export_exact_schema_candidate(
            export_authority, supported_journal_schema,
            JournalQuiescence::Confirmed);
        require(export_result.status ==
                        ExactSchemaExportStatus::LimitExceeded &&
                    !export_result.exported() &&
                    !export_result.candidate.has_value() &&
                    export_authority.available(),
                "token limit returned an export candidate");
        require(export_storage.snapshot() == export_before,
                "export limit reached storage I/O or mutation");
    }
}

const std::vector<FaultOperation> pre_authority_operations{
    FaultOperation::AuthorityLockOpen,
    FaultOperation::AuthorityLockAcquire,
    FaultOperation::AuthorityIdentityRead,
    FaultOperation::PreflightProbe,
    FaultOperation::PreflightFlushCreate,
    FaultOperation::PreflightFlushWrite,
    FaultOperation::PreflightFlushFileFlush,
    FaultOperation::PreflightFlushClose,
    FaultOperation::PreflightTruncate,
    FaultOperation::PreflightTruncateFlush,
    FaultOperation::PreflightTruncateClose,
    FaultOperation::PreflightReplaceStageCreate,
    FaultOperation::PreflightReplaceStageWrite,
    FaultOperation::PreflightReplaceStageFlush,
    FaultOperation::PreflightReplaceStageClose,
    FaultOperation::PreflightReplace,
    FaultOperation::PreflightNamespaceDurability,
    FaultOperation::PreflightCleanup,
    FaultOperation::FixedNamespaceInspect,
};

const std::vector<FaultOperation> publication_operations = [] {
    auto operations = pre_authority_operations;
    operations.insert(operations.end(), {
                                            FaultOperation::RootOpen,
                                            FaultOperation::RootRead,
                                            FaultOperation::RootClose,
                                            FaultOperation::JournalOpen,
                                            FaultOperation::JournalRead,
                                            FaultOperation::JournalReadClose,
                                            FaultOperation::JournalAppendOpen,
                                            FaultOperation::JournalWrite,
                                            FaultOperation::JournalFlush,
                                            FaultOperation::JournalClose,
                                            FaultOperation::RootStageCreate,
                                            FaultOperation::RootStageWrite,
                                            FaultOperation::RootStageFlush,
                                            FaultOperation::RootStageClose,
                                            FaultOperation::RootReplace,
                                            FaultOperation::NamespaceDurability,
                                            FaultOperation::AuthorityUnlock,
                                            FaultOperation::AuthorityLockClose,
                                        });
    return operations;
}();

const std::vector<FaultOperation> creation_operations = [] {
    auto operations = pre_authority_operations;
    operations.insert(operations.end(),
                      {
                          FaultOperation::RootOpen,
                          FaultOperation::InitialJournalCreate,
                          FaultOperation::JournalWrite,
                          FaultOperation::JournalFlush,
                          FaultOperation::JournalClose,
                          FaultOperation::RootStageCreate,
                          FaultOperation::RootStageWrite,
                          FaultOperation::RootStageFlush,
                          FaultOperation::RootStageClose,
                          FaultOperation::RootReplace,
                          FaultOperation::NamespaceDurability,
                          FaultOperation::AuthorityUnlock,
                          FaultOperation::AuthorityLockClose,
                      });
    return operations;
}();

bool preflight_may_have_effect(FaultOperation operation,
                               FaultPosition position, FaultAction action) {
    const auto found = std::find(pre_authority_operations.begin(),
                                 pre_authority_operations.end(), operation);
    const auto first = std::find(pre_authority_operations.begin(),
                                 pre_authority_operations.end(),
                                 FaultOperation::PreflightProbe);
    return found >= first && found != pre_authority_operations.end() &&
           (position == FaultPosition::After ||
            action == FaultAction::EffectThenError);
}

void require_operations_in_order(
    const NamespaceSnapshot &snapshot,
    std::initializer_list<FaultOperation> operations,
    std::string_view failure_message) {
    std::size_t cursor = 0;
    for (const auto operation : operations) {
        const auto found = std::find_if(
            snapshot.observations.begin() + static_cast<std::ptrdiff_t>(cursor),
            snapshot.observations.end(),
            [&](const OperationObservation &observation) {
                return observation.operation == operation &&
                       observation.lock_held;
            });
        require(found != snapshot.observations.end(), failure_message);
        cursor = static_cast<std::size_t>(
                     std::distance(snapshot.observations.begin(), found)) +
                 1;
    }
}

void require_durability_ordering() {
    const auto chain = make_chain();

    auto create_storage = JournalTestStorage::fresh();
    static_cast<void>(create(create_storage, chain));
    const auto create_snapshot = create_storage.snapshot();
    require_operations_in_order(
        create_snapshot,
        {FaultOperation::InitialJournalCreate, FaultOperation::JournalWrite,
         FaultOperation::JournalFlush, FaultOperation::JournalClose,
         FaultOperation::RootStageCreate, FaultOperation::RootStageWrite,
         FaultOperation::RootStageFlush, FaultOperation::RootStageClose,
         FaultOperation::RootReplace, FaultOperation::NamespaceDurability},
        "create did not durably publish journal before root");

    auto append_storage = JournalTestStorage::seeded(
        frame(chain.genesis),
        std::string(chain.genesis_root.canonical_bytes()));
    auto append_authority =
        recover(append_storage, chain.genesis_root.canonical_bytes());
    append_storage.reset_observations();
    auto append_journal = append_storage.make_journal(generous_limits());
    auto append_result = append_journal.append_and_publish(
        std::move(append_authority), chain.successor);
    require_result(append_result, DurableJournalStatus::Published,
                   "ordering append was rejected");
    const auto append_snapshot = append_storage.snapshot();
    require_operations_in_order(
        append_snapshot,
        {FaultOperation::RootOpen, FaultOperation::RootRead,
         FaultOperation::RootClose, FaultOperation::JournalOpen,
         FaultOperation::JournalRead, FaultOperation::JournalReadClose,
         FaultOperation::JournalAppendOpen, FaultOperation::JournalWrite,
         FaultOperation::JournalFlush, FaultOperation::JournalClose,
         FaultOperation::RootStageCreate, FaultOperation::RootStageWrite,
         FaultOperation::RootStageFlush, FaultOperation::RootStageClose,
         FaultOperation::RootReplace, FaultOperation::NamespaceDurability},
        "append did not durably publish journal before root");

    auto tail_storage = JournalTestStorage::seeded(
        frame(chain.genesis) + frame(chain.successor).substr(0, 7),
        std::string(chain.genesis_root.canonical_bytes()));
    tail_storage.reset_observations();
    auto tail_journal = tail_storage.make_journal(generous_limits());
    auto tail_result =
        tail_journal.recover_existing(TrustedReplayFloor::exact_root(
            std::string(chain.genesis_root.canonical_bytes())));
    require_result(tail_result, DurableJournalStatus::Published,
                   "ordering tail repair was rejected");
    const auto tail_snapshot = tail_storage.snapshot();
    require_operations_in_order(
        tail_snapshot,
        {FaultOperation::RootOpen, FaultOperation::RootRead,
         FaultOperation::RootClose, FaultOperation::JournalOpen,
         FaultOperation::JournalRead, FaultOperation::JournalReadClose,
         FaultOperation::TailRepairOpen, FaultOperation::TailRepairTruncate,
         FaultOperation::TailRepairFlush, FaultOperation::TailRepairClose},
        "tail repair did not truncate, flush, and close in place");
    require(!has_stage_operation(tail_snapshot) &&
                tail_snapshot.stage_files_absent &&
                tail_snapshot.durable_stage_files_absent &&
                tail_snapshot.all_authority_io_under_lock,
            "tail repair used replacement or namespace durability");

    const auto committed = frame(chain.genesis) + frame(chain.successor);
    auto compact_storage = JournalTestStorage::seeded(
        committed, std::string(chain.successor_root.canonical_bytes()));
    auto compact_authority =
        recover(compact_storage, chain.successor_root.canonical_bytes());
    compact_storage.reset_observations();
    auto compact_journal = compact_storage.make_journal(generous_limits());
    auto compact_result =
        compact_journal.compact_physical(std::move(compact_authority));
    require_result(compact_result, DurableJournalStatus::Published,
                   "ordering compaction was rejected");
    const auto compact_snapshot = compact_storage.snapshot();
    require_operations_in_order(
        compact_snapshot,
        {FaultOperation::CompactionStageCreate, FaultOperation::CompactionWrite,
         FaultOperation::CompactionFlush, FaultOperation::CompactionClose,
         FaultOperation::CompactionReplace,
         FaultOperation::CompactionNamespaceDurability},
        "compaction did not flush and close its stage before replacement");
}

void require_stage_replacement_swap_is_fail_closed() {
    const auto chain = make_chain();
    const auto genesis_root =
        std::string(chain.genesis_root.canonical_bytes());
    const auto successor_root =
        std::string(chain.successor_root.canonical_bytes());

    for (const bool corrupt_content : {false, true}) {
        auto storage =
            JournalTestStorage::seeded(frame(chain.genesis), genesis_root);
        auto authority = recover(storage, genesis_root);
        storage.reset_observations();
        storage.pause_after(FaultOperation::RootStageClose);
        std::optional<DurableJournalResult> result;
        std::thread publisher([&] {
            auto journal = storage.make_journal(generous_limits());
            result.emplace(journal.append_and_publish(std::move(authority),
                                                       chain.successor));
        });
        require(storage.wait_until_paused(5000),
                "root replacement did not pause after stage close");
        const auto swapped_root =
            corrupt_content ? std::string("swapped-root-stage")
                            : successor_root;
        storage.replace_fixed_child_file(FixedAuthorityChild::RootStage,
                                         swapped_root);
        storage.release_pause();
        publisher.join();
        require(result.has_value(),
                "root replacement did not return after stage swap");
        require_result(*result, DurableJournalStatus::RecoveryRequired,
                       "root stage swap returned publication authority");
        require(!authority.available() && !result->journal.has_value() &&
                    storage.snapshot().journal_bytes ==
                        frame(chain.genesis) + frame(chain.successor) &&
                    storage.snapshot().authority_root_bytes == swapped_root &&
                    storage.snapshot().all_authority_io_under_lock,
                "root stage swap escaped fail-closed publication");
        require_storage_released_after_return(
            storage, "root stage swap retained an authority handle");

        auto recovery = storage.make_journal(generous_limits());
        auto recovered = recovery.recover_existing(
            TrustedReplayFloor::exact_root(genesis_root));
        if (corrupt_content) {
            require_result(recovered, DurableJournalStatus::CorruptOrRollback,
                           "corrupt swapped root remained authoritative");
            require_storage_released_after_return(
                storage, "corrupt swapped-root recovery retained a handle");
        } else {
            require_result(recovered, DurableJournalStatus::Published,
                           "identity-only root swap was not recoverable");
            auto continuation = storage.make_journal(generous_limits());
            auto continued = continuation.append_and_publish(
                std::move(*recovered.journal), chain.third);
            require_result(continued, DurableJournalStatus::Published,
                           "identity-only root swap could not continue");
        }
    }

    for (const bool corrupt_content : {false, true}) {
        auto storage =
            JournalTestStorage::seeded(frame(chain.genesis), genesis_root);
        auto authority = recover(storage, genesis_root);
        storage.reset_observations();
        storage.pause_after(FaultOperation::CompactionClose);
        std::optional<DurableJournalResult> result;
        std::thread compactor([&] {
            auto journal = storage.make_journal(generous_limits());
            result.emplace(
                journal.compact_physical(std::move(authority)));
        });
        require(storage.wait_until_paused(5000),
                "journal replacement did not pause after stage close");
        const auto swapped_journal =
            corrupt_content ? std::string("swapped-journal-stage")
                            : frame(chain.genesis);
        storage.replace_fixed_child_file(FixedAuthorityChild::JournalStage,
                                         swapped_journal);
        storage.release_pause();
        compactor.join();
        require(result.has_value(),
                "journal replacement did not return after stage swap");
        require_result(*result, DurableJournalStatus::RecoveryRequired,
                       "journal stage swap returned publication authority");
        require(!authority.available() && !result->journal.has_value() &&
                    storage.snapshot().journal_bytes == swapped_journal &&
                    storage.snapshot().authority_root_bytes == genesis_root &&
                    storage.snapshot().all_authority_io_under_lock,
                "journal stage swap escaped fail-closed publication");
        require_storage_released_after_return(
            storage, "journal stage swap retained an authority handle");

        auto recovery = storage.make_journal(generous_limits());
        auto recovered = recovery.recover_existing(
            TrustedReplayFloor::exact_root(genesis_root));
        if (corrupt_content) {
            require_result(recovered, DurableJournalStatus::CorruptOrRollback,
                           "corrupt swapped journal remained authoritative");
            require_storage_released_after_return(
                storage,
                "corrupt swapped-journal recovery retained a handle");
        } else {
            require_result(recovered, DurableJournalStatus::Published,
                           "identity-only journal swap was not recoverable");
            auto continuation = storage.make_journal(generous_limits());
            auto continued = continuation.append_and_publish(
                std::move(*recovered.journal), chain.successor);
            require_result(continued, DurableJournalStatus::Published,
                           "identity-only journal swap could not continue");
        }
    }
}

void require_paused_read_replacement_is_fail_closed() {
    const auto chain = make_chain();
    const auto genesis_root =
        std::string(chain.genesis_root.canonical_bytes());
    auto storage =
        JournalTestStorage::seeded(frame(chain.genesis), genesis_root);
    storage.pause_after(FaultOperation::JournalRead);
    std::optional<DurableJournalResult> result;
    bool threw = false;
    std::thread recoverer([&] {
        try {
            auto journal = storage.make_journal(generous_limits());
            result.emplace(journal.recover_existing(
                TrustedReplayFloor::exact_root(genesis_root)));
        } catch (...) {
            threw = true;
        }
    });
    require(storage.wait_until_paused(5000),
            "recovery did not pause during its journal read");
    storage.replace_fixed_child_file(FixedAuthorityChild::Journal, "x");
    storage.release_pause();
    recoverer.join();
    require(!threw, "paused journal read used stale mutable storage");
    require(result.has_value(), "paused journal read did not return");
    require_result(*result, DurableJournalStatus::RecoveryRequired,
                   "journal replacement during a read did not fail closed");
    require_storage_released_after_return(
        storage, "paused journal read retained an authority handle");
}

void require_paused_read_holds_authority_lock() {
    const auto chain = make_chain();
    const auto genesis_root =
        std::string(chain.genesis_root.canonical_bytes());
    auto storage =
        JournalTestStorage::seeded(frame(chain.genesis), genesis_root);
    storage.pause_after(FaultOperation::JournalRead);
    std::optional<DurableJournalResult> holder_result;
    std::optional<DurableJournalResult> waiter_result;
    std::thread holder([&] {
        auto journal = storage.make_journal(generous_limits());
        holder_result.emplace(journal.recover_existing(
            TrustedReplayFloor::exact_root(genesis_root)));
    });
    require(storage.wait_until_paused(5000),
            "lock holder did not pause during its journal read");
    std::thread waiter([&] {
        auto journal = storage.make_journal(generous_limits());
        waiter_result.emplace(journal.recover_existing(
            TrustedReplayFloor::exact_root(genesis_root)));
    });
    const auto waiter_blocked = storage.wait_until_lock_waiter(5000);
    storage.release_pause();
    holder.join();
    waiter.join();
    require(waiter_blocked,
            "journal read did not retain the stable authority lock");
    require(holder_result.has_value() && waiter_result.has_value(),
            "serialized journal readers did not return");
    require_result(*holder_result, DurableJournalStatus::Published,
                   "paused journal reader lost publication authority");
    require_result(*waiter_result, DurableJournalStatus::Published,
                   "waiting journal reader lost publication authority");
    require_storage_released_after_return(
        storage, "serialized journal readers retained an authority handle");
}

void require_paused_write_observes_completed_effect() {
    const auto chain = make_chain();
    const auto genesis_journal = frame(chain.genesis);
    const auto successor_frame = frame(chain.successor);
    const auto genesis_root =
        std::string(chain.genesis_root.canonical_bytes());
    auto storage = JournalTestStorage::seeded(genesis_journal, genesis_root);
    auto authority = recover(storage, genesis_root);
    storage.reset_observations();
    storage.pause_after(FaultOperation::JournalWrite);
    std::optional<DurableJournalResult> result;
    std::thread publisher([&] {
        auto journal = storage.make_journal(generous_limits());
        result.emplace(
            journal.append_and_publish(std::move(authority), chain.successor));
    });
    const auto paused = storage.wait_until_paused(5000);
    if (!paused) {
        storage.release_pause();
        publisher.join();
        require(false, "journal write did not pause after observation");
    }
    const auto observed = storage.snapshot();
    const auto write = std::find_if(
        observed.observations.begin(), observed.observations.end(),
        [](const OperationObservation &observation) {
            return observation.operation == FaultOperation::JournalWrite;
        });
    require(write != observed.observations.end() && write->lock_held &&
                write->mutation_attempted &&
                write->requested_bytes == successor_frame.size() &&
                write->transferred_bytes == successor_frame.size() &&
                observed.journal_bytes == genesis_journal + successor_frame &&
                observed.authority_root_bytes == genesis_root,
            "journal write pause preceded its successful observation");
    storage.release_pause();
    publisher.join();
    require(result.has_value(), "paused journal write did not return");
    require_result(*result, DurableJournalStatus::Published,
                   "paused journal write did not finish publication");
    require_storage_released_after_return(
        storage, "paused journal write retained an authority handle");
}

void require_fake_adapter_destructor_closes_open_lock() {
    auto storage = JournalTestStorage::fresh();
    require(storage.destroy_adapter_with_open_lock(false) &&
                storage.snapshot().open_handles.empty(),
            "fake adapter destructor leaked an open lock handle");

    require(storage.destroy_adapter_with_open_lock(true) &&
                storage.snapshot().open_handles.size() == 1,
            "simulated crash incorrectly closed the fake lock handle");
    storage.restart();
    require(storage.snapshot().open_handles.empty(),
            "restart retained a simulated-crash lock handle");
}

void require_fake_immutable_object_link_count_parity() {
    for (const auto platform :
         {PlatformContract::Linux, PlatformContract::MacOS}) {
        const auto probe =
            JournalTestStorage::probe_immutable_object_linked_publish_read(
                platform);
        require(probe.creation_result ==
                        HelperResultKind::EffectMayHaveOccurred &&
                    probe.linked_names_survived_restart,
                "fake immutable-object linked-publish fixture did not survive "
                "restart");
        require(!probe.read_succeeded && probe.read_was_unsupported &&
                    probe.authority_released,
                "fake immutable-object read accepted a two-link publish "
                "residue");
    }
}

void require_final_under_lock_revalidation() {
    const auto chain = make_chain();
    const auto genesis_journal = frame(chain.genesis);
    const auto successor_journal = genesis_journal + frame(chain.successor);
    const auto genesis_root =
        std::string(chain.genesis_root.canonical_bytes());
    const auto successor_root =
        std::string(chain.successor_root.canonical_bytes());

    {
        auto storage = JournalTestStorage::fresh();
        storage.pause_after_nth(FaultOperation::AuthorityIdentityRead, 2);
        std::optional<DurableJournalResult> result;
        std::thread creator([&] {
            auto journal = storage.make_journal(generous_limits());
            result.emplace(journal.create_new(
                TrustedReplayFloor::uninitialized(), chain.genesis));
        });
        require(storage.wait_until_paused(5000),
                "create did not pause for final authority validation");
        storage.replace_fixed_child_file(FixedAuthorityChild::Journal,
                                         "post-create-journal-swap");
        storage.release_pause();
        creator.join();
        require(result.has_value(), "paused create did not return");
        require_result(*result, DurableJournalStatus::RecoveryRequired,
                       "create minted authority after a final journal swap");
        require(!result->journal.has_value() &&
                    storage.attempt_count(
                        FaultOperation::AuthorityIdentityRead) == 2,
                "create skipped its final composite authority validation");
        require_storage_released_after_return(
            storage, "final create validation retained an authority handle");
        storage.replace_fixed_child_file(FixedAuthorityChild::Journal,
                                         genesis_journal);
        static_cast<void>(recover(storage, genesis_root));
    }

    {
        auto storage = JournalTestStorage::seeded(
            genesis_journal, genesis_root, PlatformContract::Linux);
        storage.reset_observations();
        storage.pause_after_nth(FaultOperation::AuthorityIdentityRead, 2);
        std::optional<DurableJournalResult> result;
        std::thread recoverer([&] {
            auto journal = storage.make_journal(generous_limits());
            result.emplace(journal.recover_existing(
                TrustedReplayFloor::exact_root(genesis_root)));
        });
        require(storage.wait_until_paused(5000),
                "recovery did not pause for final authority validation");
        const auto old_lock_identity = storage.lock_identity();
        require(old_lock_identity != 0 && storage.try_unlink_lock_file() &&
                    storage.try_recreate_lock_file() &&
                    storage.lock_identity() != old_lock_identity,
                "recovery final validation fixture did not replace the lock");
        storage.release_pause();
        recoverer.join();
        require(result.has_value(), "paused recovery did not return");
        require_result(*result, DurableJournalStatus::RecoveryRequired,
                       "recovery minted authority after final lock replacement");
        require(!result->journal.has_value() &&
                    storage.attempt_count(
                        FaultOperation::AuthorityIdentityRead) == 2,
                "recovery skipped its final composite authority validation");
        require_storage_released_after_return(
            storage,
            "final recovery validation retained an authority handle");
        static_cast<void>(recover(storage, genesis_root));
    }

    {
        auto storage =
            JournalTestStorage::seeded(genesis_journal, genesis_root);
        auto authority = recover(storage, genesis_root);
        storage.reset_observations();
        storage.pause_after_nth(FaultOperation::AuthorityIdentityRead, 2);
        std::optional<DurableJournalResult> result;
        std::thread publisher([&] {
            auto journal = storage.make_journal(generous_limits());
            result.emplace(journal.append_and_publish(std::move(authority),
                                                       chain.successor));
        });
        require(storage.wait_until_paused(5000),
                "append did not pause for final authority validation");
        storage.replace_fixed_child_file(FixedAuthorityChild::Root,
                                         "post-append-root-swap");
        storage.release_pause();
        publisher.join();
        require(result.has_value(), "paused append did not return");
        require_result(*result, DurableJournalStatus::RecoveryRequired,
                       "append minted authority after a final root swap");
        require(!authority.available() && !result->journal.has_value() &&
                    storage.attempt_count(
                        FaultOperation::AuthorityIdentityRead) == 2,
                "append skipped its final composite authority validation");
        require_storage_released_after_return(
            storage, "final append validation retained an authority handle");
        storage.replace_fixed_child_file(FixedAuthorityChild::Root,
                                         successor_root);
        static_cast<void>(recover(storage, successor_root));
    }

    {
        auto storage =
            JournalTestStorage::seeded(successor_journal, successor_root);
        auto authority = recover(storage, successor_root);
        storage.reset_observations();
        storage.pause_after_nth(FaultOperation::AuthorityIdentityRead, 2);
        std::optional<DurableJournalResult> result;
        std::thread compactor([&] {
            auto journal = storage.make_journal(generous_limits());
            result.emplace(journal.compact_physical(std::move(authority)));
        });
        require(storage.wait_until_paused(5000),
                "compaction did not pause for final authority validation");
        storage.replace_fixed_child_file(FixedAuthorityChild::JournalStage,
                                         successor_journal);
        storage.release_pause();
        compactor.join();
        require(result.has_value(), "paused compaction did not return");
        require_result(
            *result, DurableJournalStatus::RecoveryRequired,
            "compaction minted authority after final stage replacement");
        require(!authority.available() && !result->journal.has_value() &&
                    storage.attempt_count(
                        FaultOperation::AuthorityIdentityRead) == 2,
                "compaction skipped its final composite authority validation");
        require_storage_released_after_return(
            storage,
            "final compaction validation retained an authority handle");
        auto recovered = recover(storage, successor_root);
        auto cleanup = storage.make_journal(generous_limits());
        auto cleaned = cleanup.compact_physical(std::move(recovered));
        require_result(cleaned, DurableJournalStatus::Published,
                       "stage-drift recovery could not continue");
        require(storage.snapshot().stage_files_absent,
                "stage-drift continuation did not consume its stage");
    }
}

void require_preflight_and_bounded_read_trace() {
    const auto chain = make_chain();
    const auto limits = generous_limits();
    auto storage = JournalTestStorage::seeded(
        frame(chain.genesis),
        std::string(chain.genesis_root.canonical_bytes()));
    storage.reset_observations();
    auto journal = storage.make_journal(limits);
    auto result = journal.recover_existing(TrustedReplayFloor::exact_root(
        std::string(chain.genesis_root.canonical_bytes())));
    require_result(result, DurableJournalStatus::Published,
                   "trace recovery was rejected");
    const auto snapshot = storage.snapshot();

    const std::vector<FaultOperation> ordered_preflight{
        FaultOperation::PreflightProbe,
        FaultOperation::PreflightFlushCreate,
        FaultOperation::PreflightFlushWrite,
        FaultOperation::PreflightFlushFileFlush,
        FaultOperation::PreflightFlushClose,
        FaultOperation::PreflightTruncate,
        FaultOperation::PreflightTruncateFlush,
        FaultOperation::PreflightTruncateClose,
        FaultOperation::PreflightReplaceStageCreate,
        FaultOperation::PreflightReplaceStageWrite,
        FaultOperation::PreflightReplaceStageFlush,
        FaultOperation::PreflightReplaceStageClose,
        FaultOperation::PreflightReplace,
        FaultOperation::PreflightNamespaceDurability,
        FaultOperation::PreflightCleanup,
    };
    std::size_t cursor = 0;
    for (const auto operation : ordered_preflight) {
        const auto found = std::find_if(
            snapshot.observations.begin() + static_cast<std::ptrdiff_t>(cursor),
            snapshot.observations.end(),
            [&](const OperationObservation &observation) {
                return observation.operation == operation &&
                       observation.lock_held;
            });
        require(found != snapshot.observations.end(),
                "preflight primitive was absent or out of order");
        cursor = static_cast<std::size_t>(
                     std::distance(snapshot.observations.begin(), found)) +
                 1;
    }
    const auto first_authority_open = std::find_if(
        snapshot.observations.begin(), snapshot.observations.end(),
        [](const OperationObservation &observation) {
            return observation.operation == FaultOperation::RootOpen ||
                   observation.operation == FaultOperation::JournalOpen;
        });
    const auto fixed_namespace_inspection =
        std::find_if(snapshot.observations.begin(), snapshot.observations.end(),
                     [](const OperationObservation &observation) {
                         return observation.operation ==
                                FaultOperation::FixedNamespaceInspect;
                     });
    require(first_authority_open != snapshot.observations.end() &&
                fixed_namespace_inspection != snapshot.observations.end() &&
                fixed_namespace_inspection->lock_held &&
                cursor <= static_cast<std::size_t>(
                              std::distance(snapshot.observations.begin(),
                                            fixed_namespace_inspection)) &&
                fixed_namespace_inspection < first_authority_open,
            "fixed namespace was not inspected after preflight and before "
            "authority I/O");

    const auto identity =
        std::find_if(snapshot.observations.begin(), snapshot.observations.end(),
                     [](const OperationObservation &observation) {
                         return observation.operation ==
                                FaultOperation::AuthorityIdentityRead;
                     });
    const auto first_preflight = std::find_if(
        snapshot.observations.begin(), snapshot.observations.end(),
        [](const OperationObservation &observation) {
            return observation.operation == FaultOperation::PreflightProbe;
        });
    require(identity != snapshot.observations.end() && identity->lock_held &&
                first_preflight != snapshot.observations.end() &&
                identity < first_preflight,
            "authority identity was not validated under lock before "
            "preflight");

    const auto root_read = std::find_if(
        snapshot.observations.begin(), snapshot.observations.end(),
        [](const OperationObservation &observation) {
            return observation.operation == FaultOperation::RootRead;
        });
    const auto journal_read = std::find_if(
        snapshot.observations.begin(), snapshot.observations.end(),
        [](const OperationObservation &observation) {
            return observation.operation == FaultOperation::JournalRead;
        });
    require(root_read != snapshot.observations.end() &&
                journal_read != snapshot.observations.end() &&
                root_read < journal_read &&
                root_read->requested_bytes <= max_journal_input_bytes + 1 &&
                root_read->requested_bytes >=
                    chain.genesis_root.canonical_bytes().size() &&
                journal_read->requested_bytes <=
                    limits.max_committed_bytes + limits.max_crash_tail_bytes +
                        1 &&
                journal_read->requested_bytes >= frame(chain.genesis).size(),
            "recovery did not use bounded root-first reads");
    require(
        storage.attempt_count(FaultOperation::RootClose) == 2 &&
            storage.attempt_count(FaultOperation::JournalReadClose) == 2 &&
            storage.attempt_count(FaultOperation::PreflightFlushClose) == 1 &&
            storage.attempt_count(FaultOperation::PreflightTruncateClose) ==
                1 &&
            storage.attempt_count(FaultOperation::PreflightReplaceStageClose) ==
                1 &&
            storage.attempt_count(FaultOperation::AuthorityLockClose) == 1 &&
            snapshot.open_handles.empty() &&
            !snapshot.authority_mutation_attempted,
        "bounded recovery did not checked-close each read handle once");
}

bool creation_may_have_mutated(FaultOperation operation, FaultPosition position,
                               FaultAction action) {
    if (preflight_may_have_effect(operation, position, action)) {
        return true;
    }
    if (operation == FaultOperation::InitialJournalCreate) {
        return position == FaultPosition::After ||
               action == FaultAction::EffectThenError;
    }
    const auto found = std::find(creation_operations.begin(),
                                 creation_operations.end(), operation);
    const auto create =
        std::find(creation_operations.begin(), creation_operations.end(),
                  FaultOperation::InitialJournalCreate);
    return found > create;
}

void require_create_faults() {
    const auto chain = make_chain();
    bool observed_journal_close_residue = false;
    bool observed_root_stage_close_residue = false;
    for (const auto operation : creation_operations) {
        for (const auto position :
             {FaultPosition::Before, FaultPosition::After}) {
            for (const auto action : {FaultAction::Error, FaultAction::Crash,
                                      FaultAction::EffectThenError}) {
                auto storage = JournalTestStorage::fresh();
                const auto initial = storage.snapshot();
                storage.arm_fault(operation, position, action);
                auto journal = storage.make_journal(generous_limits());
                auto result = journal.create_new(
                    TrustedReplayFloor::uninitialized(), chain.genesis);
                const auto expected =
                    creation_may_have_mutated(operation, position, action)
                        ? DurableJournalStatus::RecoveryRequired
                        : DurableJournalStatus::UnsupportedStorage;
                const auto operation_position =
                    std::find(creation_operations.begin(),
                              creation_operations.end(), operation);
                const auto create_position =
                    std::find(creation_operations.begin(),
                              creation_operations.end(),
                              FaultOperation::InitialJournalCreate);
                const bool retained_process_fence =
                    preflight_may_have_effect(operation, position, action) ||
                    operation_position > create_position ||
                    (operation_position == create_position &&
                     (position == FaultPosition::After ||
                      action == FaultAction::EffectThenError));
                require_result(
                    result, expected,
                    "create fault returned the wrong stable outcome");
                const auto after_failure = storage.snapshot();
                if (action != FaultAction::Crash) {
                    const bool boundary_effect_happened =
                        position == FaultPosition::After ||
                        action == FaultAction::EffectThenError;
                    const auto expected_preflight_close =
                        [&](FaultOperation open_operation) {
                            const auto fault =
                                std::find(creation_operations.begin(),
                                          creation_operations.end(), operation);
                            const auto open = std::find(
                                creation_operations.begin(),
                                creation_operations.end(), open_operation);
                            return static_cast<std::size_t>(
                                fault > open ||
                                (fault == open && boundary_effect_happened));
                        };
                    const auto expected_lock_close = static_cast<std::size_t>(
                        operation != FaultOperation::AuthorityLockOpen ||
                        boundary_effect_happened);
                    require(
                        after_failure.open_handles.empty() &&
                            storage.attempt_count(
                                FaultOperation::AuthorityLockClose) ==
                                expected_lock_close &&
                            storage.attempt_count(
                                FaultOperation::PreflightFlushClose) ==
                                expected_preflight_close(
                                    FaultOperation::PreflightFlushCreate) &&
                            storage.attempt_count(
                                FaultOperation::PreflightTruncateClose) ==
                                expected_preflight_close(
                                    FaultOperation::PreflightTruncate) &&
                            storage.attempt_count(
                                FaultOperation::PreflightReplaceStageClose) ==
                                expected_preflight_close(
                                    FaultOperation::
                                        PreflightReplaceStageCreate) &&
                            storage.attempt_count(
                                FaultOperation::JournalClose) ==
                                expected_preflight_close(
                                    FaultOperation::InitialJournalCreate) &&
                            storage.attempt_count(
                                FaultOperation::RootStageClose) ==
                                expected_preflight_close(
                                    FaultOperation::RootStageCreate),
                        "create fault leaked or retried a handle before "
                        "restart");
                }
                if (expected == DurableJournalStatus::UnsupportedStorage) {
                    require(authority_files_unchanged(initial, after_failure) &&
                                !after_failure.authority_mutation_attempted,
                            "pre-authority failure changed authority files");
                }
                const auto established_lock_identity = storage.lock_identity();
                storage.restart();
                const auto snapshot = storage.snapshot();
                require(snapshot.authority_root_bytes.empty() ||
                            snapshot.authority_root_bytes ==
                                chain.genesis_root.canonical_bytes(),
                        "failed create exposed a synthesized root");
                require(snapshot.all_authority_io_under_lock,
                        "failed create performed authority I/O outside the "
                        "full lock");
                if (snapshot.authority_root_bytes ==
                    chain.genesis_root.canonical_bytes()) {
                    storage.reset_observations();
                    auto recovered =
                        recover(storage, chain.genesis_root.canonical_bytes());
                    require(recovered.root_bytes() ==
                                    chain.genesis_root.canonical_bytes() &&
                                storage.snapshot().journal_bytes ==
                                    frame(chain.genesis) &&
                                storage.snapshot().journal_durable &&
                                storage.snapshot().journal_closed &&
                                storage.snapshot().stage_files_absent &&
                                storage.snapshot().durable_stage_files_absent,
                            "visible genesis root was not exactly recoverable");
                } else {
                    const auto has_residue =
                        snapshot.relative_paths.count("journal.jsonl") != 0 ||
                        snapshot.relative_paths.count(".journal.jsonl.stage") !=
                            0 ||
                        snapshot.relative_paths.count(
                            ".authority-root.json.stage") != 0 ||
                        snapshot.durable_file_bytes.count("journal.jsonl") !=
                            0 ||
                        snapshot.durable_file_bytes.count(
                            ".journal.jsonl.stage") != 0 ||
                        snapshot.durable_file_bytes.count(
                            ".authority-root.json.stage") != 0;
                    storage.reset_observations();
                    const auto fenced_before = storage.snapshot();
                    auto retry = storage.make_journal(generous_limits());
                    auto retry_result = retry.create_new(
                        TrustedReplayFloor::uninitialized(), chain.genesis);
                    if (has_residue) {
                        observed_journal_close_residue |=
                            operation == FaultOperation::JournalClose &&
                            position == FaultPosition::After &&
                            action == FaultAction::Crash;
                        observed_root_stage_close_residue |=
                            operation == FaultOperation::RootStageClose &&
                            position == FaultPosition::After &&
                            action == FaultAction::Crash;
                        require_result(
                            retry_result,
                            retained_process_fence
                                ? DurableJournalStatus::RecoveryRequired
                                : DurableJournalStatus::CorruptOrRollback,
                            "rootless authority residue was not rejected");
                        const auto after_create_rejection = storage.snapshot();
                        require(
                            same_persistent_state(after_create_rejection,
                                                  fenced_before) &&
                                !after_create_rejection
                                     .authority_mutation_attempted,
                            "rootless create rejection changed crash evidence");
                        require_storage_released_after_return(
                            storage,
                            "rootless create rejection retained a handle");
                        storage.reset_observations();
                        const auto before_recovery_rejection =
                            storage.snapshot();
                        auto recovery = storage.make_journal(generous_limits());
                        auto recovery_result = recovery.recover_existing(
                            TrustedReplayFloor::uninitialized());
                        require_result(
                            recovery_result,
                            DurableJournalStatus::CorruptOrRollback,
                            "rootless authority residue minted recovery");
                        const auto after_recovery_rejection =
                            storage.snapshot();
                        require(
                            same_persistent_state(after_recovery_rejection,
                                                  before_recovery_rejection) &&
                                !after_recovery_rejection
                                     .authority_mutation_attempted &&
                                after_recovery_rejection.authority_root_bytes
                                    .empty(),
                            "rootless recovery rejection changed crash "
                            "evidence");
                        require_storage_released_after_return(
                            storage,
                            "rootless recovery rejection retained a handle");
                    } else {
                        require_result(
                            retry_result, DurableJournalStatus::Published,
                            "provably empty namespace could not retry create");
                        const auto retried = storage.snapshot();
                        require((established_lock_identity == 0 ||
                                 storage.lock_identity() ==
                                     established_lock_identity) &&
                                    retried.relative_paths.count(
                                        ".durability-probe") == 0 &&
                                    retried.relative_paths.count(
                                        ".durability-probe.stage") == 0 &&
                                    retried.open_handles.empty(),
                                "create retry changed stable lock or retained "
                                "preflight residue");
                    }
                }
            }
        }
    }
    require(observed_journal_close_residue && observed_root_stage_close_residue,
            "create fault matrix did not exercise rootless durable residue");
}

bool publication_may_have_mutated(FaultOperation operation,
                                  FaultPosition position, FaultAction action) {
    if (operation == FaultOperation::JournalWrite) {
        return position == FaultPosition::After ||
               action == FaultAction::EffectThenError;
    }
    const auto found = std::find(publication_operations.begin(),
                                 publication_operations.end(), operation);
    const auto write =
        std::find(publication_operations.begin(), publication_operations.end(),
                  FaultOperation::JournalWrite);
    return found > write;
}

void require_append_and_fault_recovery() {
    const auto chain = make_chain();
    auto baseline = JournalTestStorage::fresh();
    static_cast<void>(create(baseline, chain));

    auto success = baseline.clone();
    auto success_authority =
        recover(success, chain.genesis_root.canonical_bytes());
    auto success_journal = success.make_journal(generous_limits());
    auto published = success_journal.append_and_publish(
        std::move(success_authority), chain.successor);
    require_result(published, DurableJournalStatus::Published,
                   "valid append was not published");
    require(published.journal->root_bytes() ==
                chain.successor_root.canonical_bytes(),
            "append published the wrong root");
    require(success.snapshot().journal_bytes ==
                    frame(chain.genesis) + frame(chain.successor) &&
                success.snapshot().authority_root_bytes ==
                    chain.successor_root.canonical_bytes() &&
                success.snapshot().open_handles.empty(),
            "append publication bytes drifted");
    require(success.snapshot().all_authority_io_under_lock,
            "append publication performed authority I/O outside the full lock");

    for (const auto operation : publication_operations) {
        for (const auto position :
             {FaultPosition::Before, FaultPosition::After}) {
            for (const auto action : {FaultAction::Error, FaultAction::Crash,
                                      FaultAction::EffectThenError}) {
                auto storage = baseline.clone();
                auto authority =
                    recover(storage, chain.genesis_root.canonical_bytes());
                auto peer =
                    recover(storage, chain.genesis_root.canonical_bytes());
                storage.reset_observations();
                storage.arm_fault(operation, position, action);
                auto journal = storage.make_journal(generous_limits());
                auto result = journal.append_and_publish(std::move(authority),
                                                         chain.successor);
                const bool authority_may_have_mutated =
                    publication_may_have_mutated(operation, position, action);
                const auto expected =
                    (authority_may_have_mutated || preflight_may_have_effect(
                                                       operation, position,
                                                       action))
                        ? DurableJournalStatus::RecoveryRequired
                        : DurableJournalStatus::UnsupportedStorage;
                require_result(
                    result, expected,
                    "publication fault returned the wrong stable outcome");
                require(
                    !authority.available(),
                    "publication fault left the consumed handle write-capable");
                if (action != FaultAction::Crash) {
                    require_storage_released_after_return(
                        storage,
                        "publication fault retained an authority handle");
                }
                if (authority_may_have_mutated ||
                    preflight_may_have_effect(operation, position, action)) {
                    const auto before_fence = storage.snapshot();
                    storage.reset_observations();
                    auto fence = storage.make_journal(generous_limits());
                    auto fenced = fence.append_and_publish(std::move(peer),
                                                           chain.successor);
                    require_result(
                        fenced, DurableJournalStatus::RecoveryRequired,
                        "possibly-effectful publication did not fence a "
                        "pre-fault authority token");
                    const auto after_fence = storage.snapshot();
                    require(
                        !peer.available() &&
                            same_persistent_state(after_fence, before_fence) &&
                            !after_fence.authority_mutation_attempted,
                        "publication fence attempt changed authority");
                    if (action != FaultAction::Crash) {
                        require_storage_released_after_return(
                            storage,
                            "publication fence retained an authority handle");
                    }
                }
                storage.restart();
                const auto restart_snapshot = storage.snapshot();
                storage.reset_observations();
                auto recovered =
                    recover(storage, chain.genesis_root.canonical_bytes());
                const bool prior = recovered.root_bytes() ==
                                   chain.genesis_root.canonical_bytes();
                const bool successor = recovered.root_bytes() ==
                                       chain.successor_root.canonical_bytes();
                require(prior || successor,
                        "publication crash recovered a synthesized root");
                const auto recovered_snapshot = storage.snapshot();
                require(
                    recovered_snapshot.journal_bytes ==
                            (prior ? frame(chain.genesis)
                                   : frame(chain.genesis) +
                                         frame(chain.successor)) &&
                        recovered_snapshot.authority_root_bytes ==
                            (prior ? chain.genesis_root.canonical_bytes()
                                   : chain.successor_root.canonical_bytes()) &&
                        stage_files_unchanged(restart_snapshot,
                                              recovered_snapshot) &&
                        recovered_snapshot.journal_durable,
                    "recovery returned before exact fault-state repair was "
                    "durable");
                auto continuation = storage.make_journal(generous_limits());
                auto continued = continuation.append_and_publish(
                    std::move(recovered),
                    prior ? chain.successor : chain.third);
                require_result(
                    continued, DurableJournalStatus::Published,
                    "recovered authority could not continue appending");
            }
        }
    }
}

void require_stale_cas_is_write_free() {
    const auto chain = make_chain();
    auto storage = JournalTestStorage::fresh();
    static_cast<void>(create(storage, chain));
    auto stale = recover(storage, chain.genesis_root.canonical_bytes());
    auto stale_compaction =
        recover(storage, chain.genesis_root.canonical_bytes());
    auto winner = recover(storage, chain.genesis_root.canonical_bytes());
    auto winning_journal = storage.make_journal(generous_limits());
    auto published =
        winning_journal.append_and_publish(std::move(winner), chain.successor);
    require_result(published, DurableJournalStatus::Published,
                   "CAS winner did not publish");

    storage.reset_observations();
    const auto before = storage.snapshot();
    auto losing_journal = storage.make_journal(generous_limits());
    auto conflict =
        losing_journal.append_and_publish(std::move(stale), chain.successor);
    require_result(conflict, DurableJournalStatus::ConflictBeforeWrite,
                   "stale expected root did not conflict before write");
    const auto after = storage.snapshot();
    require(!stale.available() &&
                after.authority_root_bytes == before.authority_root_bytes &&
                after.journal_bytes == before.journal_bytes &&
                !after.authority_mutation_attempted,
            "CAS loser attempted an authority mutation");
    require(after.root_read_before_journal_read &&
                after.all_authority_io_under_lock,
            "CAS comparison did not run root-first under the full lock");
    require_storage_released_after_return(
        storage, "stale append retained an authority handle");

    storage.reset_observations();
    const auto compaction_before = storage.snapshot();
    auto stale_compaction_journal = storage.make_journal(generous_limits());
    auto compact_conflict =
        stale_compaction_journal.compact_physical(std::move(stale_compaction));
    require_result(compact_conflict, DurableJournalStatus::ConflictBeforeWrite,
                   "stale authority compacted current storage");
    const auto compaction_after = storage.snapshot();
    require(!stale_compaction.available() &&
                same_persistent_state(compaction_after, compaction_before) &&
                !compaction_after.authority_mutation_attempted &&
                compaction_after.all_authority_io_under_lock &&
                storage.attempt_count(FaultOperation::RootRead) > 0 &&
                storage.attempt_count(FaultOperation::JournalOpen) == 0 &&
                storage.attempt_count(FaultOperation::JournalRead) == 0 &&
                !has_stage_operation(compaction_after),
            "stale compaction read or staged beyond the root CAS");
    require_storage_released_after_return(
        storage, "stale compaction retained an authority handle");

    for (const auto stage : {FixedAuthorityChild::JournalStage,
                             FixedAuthorityChild::RootStage}) {
        auto stage_storage = JournalTestStorage::seeded(
            frame(chain.genesis),
            std::string(chain.genesis_root.canonical_bytes()));
        auto stage_authority =
            recover(stage_storage, chain.genesis_root.canonical_bytes());
        stage_storage.overwrite_fixed_child_bytes(
            stage, stage == FixedAuthorityChild::JournalStage
                       ? "peer-journal-stage"
                       : "peer-root-stage");
        stage_storage.reset_observations();
        const auto stage_before = stage_storage.snapshot();
        auto stage_journal = stage_storage.make_journal(generous_limits());
        auto stage_conflict =
            stage_journal.compact_physical(std::move(stage_authority));
        const auto stage_after = stage_storage.snapshot();
        require_result(stage_conflict,
                       DurableJournalStatus::RecoveryRequired,
                       "compaction ignored peer stage-presence drift");
        require(!stage_authority.available() &&
                    same_persistent_state(stage_after, stage_before) &&
                    !stage_after.authority_mutation_attempted &&
                    stage_after.all_authority_io_under_lock &&
                    stage_storage.attempt_count(FaultOperation::RootOpen) == 0 &&
                    stage_storage.attempt_count(FaultOperation::JournalOpen) ==
                        0 &&
                    !has_stage_operation(stage_after),
                "stage-drift compaction read or mutated authority");
        require_storage_released_after_return(
            stage_storage, "stage-drift compaction retained an authority handle");
        auto stage_recovered = recover(
            stage_storage, chain.genesis_root.canonical_bytes());
        static_cast<void>(stage_recovered);
    }
}

void require_history_invalid_append_is_write_free() {
    const auto chain = make_chain();
    const auto other = make_chain("journal/other");
    const auto wrong_predecessor = take_record(
        parse_record_candidate(wrong_predecessor_wire),
        "wrong-predecessor fixture was not independently canonical");
    const auto illegal_transition = take_record(
        parse_record_candidate(illegal_transition_wire),
        "illegal-transition fixture was not independently canonical");
    for (const ParsedJournalRecord *candidate :
         {&chain.genesis, &chain.third, &other.successor, &wrong_predecessor,
          &illegal_transition}) {
        auto storage = JournalTestStorage::seeded(
            frame(chain.genesis),
            std::string(chain.genesis_root.canonical_bytes()));
        auto authority = recover(storage, chain.genesis_root.canonical_bytes());
        storage.reset_observations();
        const auto before = storage.snapshot();
        auto journal = storage.make_journal(generous_limits());
        auto result =
            journal.append_and_publish(std::move(authority), *candidate);
        require_result(result, DurableJournalStatus::CorruptOrRollback,
                       "history-invalid canonical append was accepted");
        require(!authority.available() &&
                    same_persistent_state(storage.snapshot(), before) &&
                    !storage.snapshot().authority_mutation_attempted &&
                    storage.attempt_count(FaultOperation::JournalWrite) == 0,
                "history-invalid append reached authority mutation");
        require_storage_released_after_return(
            storage, "history-invalid append retained an authority handle");
    }
}

void require_partial_io_and_interrupt_retries() {
    const auto chain = make_chain();
    const auto one_frame = frame(chain.genesis);

    const auto append_fault = [&](FaultOperation operation,
                                  std::vector<FaultAction> actions,
                                  std::string_view label) {
        auto storage = JournalTestStorage::seeded(
            one_frame, std::string(chain.genesis_root.canonical_bytes()));
        auto authority = recover(storage, chain.genesis_root.canonical_bytes());
        storage.reset_observations();
        storage.arm_fault_sequence(operation, FaultPosition::Before,
                                   std::move(actions));
        auto journal = storage.make_journal(generous_limits());
        auto published =
            journal.append_and_publish(std::move(authority), chain.successor);
        require_result(published, DurableJournalStatus::Published, label);
        const auto snapshot = storage.snapshot();
        require(snapshot.journal_bytes == one_frame + frame(chain.successor) &&
                    snapshot.authority_root_bytes ==
                        chain.successor_root.canonical_bytes() &&
                    snapshot.journal_closed && snapshot.open_handles.empty() &&
                    storage.attempt_count(operation) > 1,
                "retried publication did not preserve exact closed bytes");
        require(storage.attempt_count(FaultOperation::JournalClose) == 1 &&
                    storage.attempt_count(FaultOperation::RootStageClose) == 1,
                "publication close was retried or omitted");
    };

    for (const auto operation :
         {FaultOperation::JournalWrite, FaultOperation::RootStageWrite}) {
        append_fault(operation, {FaultAction::ShortWrite},
                     "short publication write did not complete");
        append_fault(
            operation,
            {FaultAction::InterruptedOnce, FaultAction::InterruptedOnce},
            "interrupted publication write did not retry");
    }
    for (const auto operation :
         {FaultOperation::JournalFlush, FaultOperation::RootStageFlush,
          FaultOperation::NamespaceDurability}) {
        append_fault(
            operation,
            {FaultAction::InterruptedOnce, FaultAction::InterruptedOnce},
            "interrupted publication durability did not retry");
    }

    auto lock_storage = JournalTestStorage::seeded(
        one_frame, std::string(chain.genesis_root.canonical_bytes()));
    lock_storage.arm_fault_sequence(
        FaultOperation::AuthorityLockAcquire, FaultPosition::Before,
        {FaultAction::InterruptedOnce, FaultAction::InterruptedOnce});
    static_cast<void>(
        recover(lock_storage, chain.genesis_root.canonical_bytes()));
    require(lock_storage.attempt_count(FaultOperation::AuthorityLockAcquire) ==
                3,
            "interrupted whole-file lock was not retried");

    for (const auto operation : {FaultOperation::TailRepairTruncate,
                                 FaultOperation::TailRepairFlush}) {
        auto storage = JournalTestStorage::seeded(
            one_frame + frame(chain.successor),
            std::string(chain.genesis_root.canonical_bytes()));
        storage.arm_fault_sequence(
            operation, FaultPosition::Before,
            {FaultAction::InterruptedOnce, FaultAction::InterruptedOnce});
        static_cast<void>(
            recover(storage, chain.genesis_root.canonical_bytes()));
        require(storage.snapshot().journal_bytes == one_frame &&
                    storage.attempt_count(operation) == 3 &&
                    storage.attempt_count(FaultOperation::TailRepairClose) == 1,
                "interrupted tail repair did not retry and close exactly");
    }

    for (const auto action :
         {FaultAction::ShortWrite, FaultAction::InterruptedOnce}) {
        auto storage = JournalTestStorage::seeded(
            one_frame + frame(chain.successor),
            std::string(chain.successor_root.canonical_bytes()));
        auto authority = recover(storage, chain.genesis_root.canonical_bytes());
        storage.reset_observations();
        storage.arm_fault(FaultOperation::CompactionWrite,
                          FaultPosition::Before, action);
        auto journal = storage.make_journal(generous_limits());
        auto compacted = journal.compact_physical(std::move(authority));
        require_result(compacted, DurableJournalStatus::Published,
                       "retryable compaction write did not complete");
        require(storage.snapshot().journal_bytes ==
                        one_frame + frame(chain.successor) &&
                    storage.snapshot().authority_root_bytes ==
                        chain.successor_root.canonical_bytes() &&
                    storage.attempt_count(FaultOperation::CompactionWrite) >
                        1 &&
                    storage.attempt_count(FaultOperation::CompactionClose) == 1,
                "compaction retry changed bytes or close discipline");
    }

    for (const auto operation :
         {FaultOperation::CompactionFlush,
          FaultOperation::CompactionNamespaceDurability}) {
        auto storage = JournalTestStorage::seeded(
            one_frame + frame(chain.successor),
            std::string(chain.successor_root.canonical_bytes()));
        auto authority = recover(storage, chain.genesis_root.canonical_bytes());
        storage.reset_observations();
        storage.arm_fault_sequence(
            operation, FaultPosition::Before,
            {FaultAction::InterruptedOnce, FaultAction::InterruptedOnce});
        auto journal = storage.make_journal(generous_limits());
        auto compacted = journal.compact_physical(std::move(authority));
        require_result(compacted, DurableJournalStatus::Published,
                       "interrupted compaction durability did not retry");
        const auto snapshot = storage.snapshot();
        require(snapshot.journal_bytes == one_frame + frame(chain.successor) &&
                    snapshot.authority_root_bytes ==
                        chain.successor_root.canonical_bytes() &&
                    storage.attempt_count(operation) > 1 &&
                    storage.attempt_count(FaultOperation::CompactionClose) == 1,
                "compaction durability retry changed exact closed bytes");
    }

    const auto require_create_close = [&](FaultOperation operation,
                                          FaultAction action,
                                          FaultOperation close_operation) {
        auto storage = JournalTestStorage::fresh();
        storage.arm_fault(operation, FaultPosition::Before, action);
        auto journal = storage.make_journal(generous_limits());
        auto result = journal.create_new(TrustedReplayFloor::uninitialized(),
                                         chain.genesis);
        require_result(result, DurableJournalStatus::RecoveryRequired,
                       "create close-path fault changed outcome");
        require(storage.attempt_count(close_operation) == 1 &&
                    storage.snapshot().open_handles.empty(),
                "create failure omitted or retried checked close");
    };
    require_create_close(FaultOperation::InitialJournalCreate,
                         FaultAction::EffectThenError,
                         FaultOperation::JournalClose);
    require_create_close(FaultOperation::JournalWrite, FaultAction::Error,
                         FaultOperation::JournalClose);
    require_create_close(FaultOperation::JournalFlush, FaultAction::Error,
                         FaultOperation::JournalClose);
    require_create_close(FaultOperation::JournalClose, FaultAction::Error,
                         FaultOperation::JournalClose);
    require_create_close(FaultOperation::RootStageCreate,
                         FaultAction::EffectThenError,
                         FaultOperation::RootStageClose);
    require_create_close(FaultOperation::RootStageWrite, FaultAction::Error,
                         FaultOperation::RootStageClose);
    require_create_close(FaultOperation::RootStageFlush, FaultAction::Error,
                         FaultOperation::RootStageClose);
    require_create_close(FaultOperation::RootStageClose, FaultAction::Error,
                         FaultOperation::RootStageClose);

    const auto require_append_close = [&](FaultOperation operation,
                                          FaultPosition position,
                                          FaultAction action,
                                          FaultOperation close_operation,
                                          DurableJournalStatus expected) {
        auto storage = JournalTestStorage::seeded(
            one_frame, std::string(chain.genesis_root.canonical_bytes()));
        auto authority = recover(storage, chain.genesis_root.canonical_bytes());
        storage.reset_observations();
        const auto before = storage.snapshot();
        storage.arm_fault(operation, position, action);
        auto journal = storage.make_journal(generous_limits());
        auto result =
            journal.append_and_publish(std::move(authority), chain.successor);
        require_result(result, expected,
                       "publication close-path fault changed outcome");
        require(!authority.available() &&
                    storage.attempt_count(close_operation) == 1 &&
                    storage.snapshot().open_handles.empty(),
                "publication failure omitted or retried checked close");
        if (operation == FaultOperation::JournalAppendOpen) {
            require(
                same_persistent_state(storage.snapshot(), before) &&
                    !storage.snapshot().authority_mutation_attempted &&
                    storage.attempt_count(FaultOperation::JournalWrite) == 0 &&
                    storage.attempt_count(FaultOperation::RootStageCreate) == 0,
                "journal-open failure reached publication mutation");
        }
    };
    require_append_close(FaultOperation::JournalAppendOpen,
                         FaultPosition::Before, FaultAction::EffectThenError,
                         FaultOperation::JournalClose,
                         DurableJournalStatus::UnsupportedStorage);
    require_append_close(FaultOperation::JournalAppendOpen,
                         FaultPosition::After, FaultAction::Error,
                         FaultOperation::JournalClose,
                         DurableJournalStatus::UnsupportedStorage);
    require_append_close(FaultOperation::JournalWrite, FaultPosition::Before,
                         FaultAction::Error, FaultOperation::JournalClose,
                         DurableJournalStatus::UnsupportedStorage);
    require_append_close(FaultOperation::JournalFlush, FaultPosition::Before,
                         FaultAction::Error, FaultOperation::JournalClose,
                         DurableJournalStatus::RecoveryRequired);
    require_append_close(FaultOperation::JournalClose, FaultPosition::Before,
                         FaultAction::Error, FaultOperation::JournalClose,
                         DurableJournalStatus::RecoveryRequired);
    require_append_close(FaultOperation::RootStageCreate, FaultPosition::Before,
                         FaultAction::EffectThenError,
                         FaultOperation::RootStageClose,
                         DurableJournalStatus::RecoveryRequired);
    require_append_close(FaultOperation::RootStageWrite, FaultPosition::Before,
                         FaultAction::Error, FaultOperation::RootStageClose,
                         DurableJournalStatus::RecoveryRequired);
    require_append_close(FaultOperation::RootStageFlush, FaultPosition::Before,
                         FaultAction::Error, FaultOperation::RootStageClose,
                         DurableJournalStatus::RecoveryRequired);
    require_append_close(FaultOperation::RootStageClose, FaultPosition::Before,
                         FaultAction::Error, FaultOperation::RootStageClose,
                         DurableJournalStatus::RecoveryRequired);

    for (const auto &[position, action] :
         {std::pair{FaultPosition::Before, FaultAction::EffectThenError},
          std::pair{FaultPosition::After, FaultAction::Error}}) {
        auto storage = JournalTestStorage::seeded(
            one_frame + frame(chain.successor),
            std::string(chain.genesis_root.canonical_bytes()));
        storage.reset_observations();
        const auto before = storage.snapshot();
        storage.arm_fault(FaultOperation::TailRepairOpen, position, action);
        auto journal = storage.make_journal(generous_limits());
        auto result = journal.recover_existing(TrustedReplayFloor::exact_root(
            std::string(chain.genesis_root.canonical_bytes())));
        require_result(result, DurableJournalStatus::UnsupportedStorage,
                       "tail-open close-path fault changed outcome");
        const auto after = storage.snapshot();
        require(storage.attempt_count(FaultOperation::TailRepairClose) == 1 &&
                    storage.attempt_count(FaultOperation::TailRepairTruncate) ==
                        0 &&
                    after.open_handles.empty() &&
                    same_persistent_state(after, before) &&
                    !after.authority_mutation_attempted,
                "tail-open failure omitted close or reached truncation");
    }

    for (const auto &[operation, expected] :
         {std::pair{FaultOperation::TailRepairTruncate,
                    DurableJournalStatus::UnsupportedStorage},
          std::pair{FaultOperation::TailRepairFlush,
                    DurableJournalStatus::RecoveryRequired},
          std::pair{FaultOperation::TailRepairClose,
                    DurableJournalStatus::RecoveryRequired}}) {
        auto storage = JournalTestStorage::seeded(
            one_frame + frame(chain.successor),
            std::string(chain.genesis_root.canonical_bytes()));
        storage.reset_observations();
        storage.arm_fault(operation, FaultPosition::Before, FaultAction::Error);
        auto journal = storage.make_journal(generous_limits());
        auto result = journal.recover_existing(TrustedReplayFloor::exact_root(
            std::string(chain.genesis_root.canonical_bytes())));
        require_result(result, expected,
                       "tail close-path fault changed outcome");
        require(storage.attempt_count(FaultOperation::TailRepairClose) == 1 &&
                    storage.snapshot().open_handles.empty(),
                "tail repair failure omitted or retried checked close");
    }

    for (const auto operation :
         {FaultOperation::CompactionStageCreate,
          FaultOperation::CompactionWrite, FaultOperation::CompactionFlush,
          FaultOperation::CompactionClose}) {
        auto storage = JournalTestStorage::seeded(
            one_frame + frame(chain.successor),
            std::string(chain.successor_root.canonical_bytes()));
        auto authority = recover(storage, chain.genesis_root.canonical_bytes());
        storage.reset_observations();
        storage.arm_fault(operation, FaultPosition::Before,
                          operation == FaultOperation::CompactionStageCreate
                              ? FaultAction::EffectThenError
                              : FaultAction::Error);
        auto journal = storage.make_journal(generous_limits());
        auto result = journal.compact_physical(std::move(authority));
        require_result(result, DurableJournalStatus::RecoveryRequired,
                       "compaction close-path fault changed outcome");
        require(storage.attempt_count(FaultOperation::CompactionClose) == 1 &&
                    storage.snapshot().open_handles.empty(),
                "compaction failure omitted or retried checked close");
    }
}

void require_stable_lock_identity() {
    const auto chain = make_chain();
    for (const auto platform :
         {PlatformContract::Linux, PlatformContract::Windows,
          PlatformContract::MacOS}) {
        auto storage = JournalTestStorage::seeded(
            frame(chain.genesis),
            std::string(chain.genesis_root.canonical_bytes()), platform);
        storage.pause_after(FaultOperation::AuthorityLockAcquire);

        std::optional<DurableJournalStatus> holder_status;
        std::optional<DurableJournalStatus> waiter_status;
        std::thread holder([&] {
            auto journal = storage.make_journal(generous_limits());
            holder_status =
                journal
                    .recover_existing(TrustedReplayFloor::exact_root(
                        std::string(chain.genesis_root.canonical_bytes())))
                    .status;
        });
        require(storage.wait_until_paused(5000),
                "lock holder did not reach the deterministic pause");
        const auto old_identity = storage.lock_identity();

        std::thread waiter([&] {
            auto journal = storage.make_journal(generous_limits());
            waiter_status =
                journal
                    .recover_existing(TrustedReplayFloor::exact_root(
                        std::string(chain.genesis_root.canonical_bytes())))
                    .status;
        });
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (storage.attempt_count(FaultOperation::AuthorityLockOpen) < 2) {
            require(std::chrono::steady_clock::now() < deadline,
                    "lock waiter did not open the stable entry");
            std::this_thread::yield();
        }

        if (platform == PlatformContract::Windows) {
            require(!storage.try_unlink_lock_file() &&
                        !storage.try_recreate_lock_file() &&
                        !storage.try_replace_lock_with_reparse_point(),
                    "Windows lock handle allowed delete-sharing replacement");
        } else {
            require(storage.try_unlink_lock_file() &&
                        storage.try_recreate_lock_file() &&
                        storage.lock_identity() != old_identity,
                    "POSIX lock replacement did not create a new identity");
        }
        storage.release_pause();
        holder.join();
        waiter.join();

        if (platform == PlatformContract::Windows) {
            require(holder_status == DurableJournalStatus::Published &&
                        waiter_status == DurableJournalStatus::Published &&
                        storage.lock_identity() == old_identity,
                    "Windows stable lock did not serialize both readers");
            auto unlocked = JournalTestStorage::fresh(platform);
            require(unlocked.try_recreate_lock_file() &&
                        unlocked.try_replace_lock_with_reparse_point(),
                    "closed Windows lock lacked replacement positive control");
        } else {
            const std::multiset<DurableJournalStatus> statuses{*holder_status,
                                                               *waiter_status};
            const std::multiset<DurableJournalStatus> expected{
                DurableJournalStatus::UnsupportedStorage,
                DurableJournalStatus::UnsupportedStorage};
            require(statuses == expected,
                    "POSIX replaced-lock waiters did not fail closed");
            const auto failed_waiters = storage.snapshot();
            require(std::none_of(failed_waiters.observations.begin(),
                                 failed_waiters.observations.end(),
                                 [](const OperationObservation &observation) {
                                     return observation.operation ==
                                                FaultOperation::RootOpen ||
                                            observation.operation ==
                                                FaultOperation::RootRead ||
                                            observation.operation ==
                                                FaultOperation::JournalOpen ||
                                            observation.operation ==
                                                FaultOperation::JournalRead;
                                 }) &&
                        failed_waiters.open_handles.empty() &&
                        !failed_waiters.authority_mutation_attempted,
                    "replaced-lock waiter reached authority I/O before "
                    "failing closed");
            const auto replacement_identity = storage.lock_identity();
            storage.reset_observations();
            auto retry = storage.make_journal(generous_limits());
            auto retried =
                retry.recover_existing(TrustedReplayFloor::exact_root(
                    std::string(chain.genesis_root.canonical_bytes())));
            require_result(retried, DurableJournalStatus::Published,
                           "stable replacement identity was not recoverable");
            const auto retry_snapshot = storage.snapshot();
            require(
                std::all_of(
                    retry_snapshot.observations.begin(),
                    retry_snapshot.observations.end(),
                    [&](const OperationObservation &observation) {
                        if (observation.operation ==
                                FaultOperation::AuthorityIdentityRead ||
                            observation.operation == FaultOperation::RootOpen ||
                            observation.operation == FaultOperation::RootRead ||
                            observation.operation ==
                                FaultOperation::JournalOpen ||
                            observation.operation ==
                                FaultOperation::JournalRead) {
                            return observation.lock_held &&
                                   observation.lock_identity ==
                                       replacement_identity;
                        }
                        return true;
                    }),
                "new caller used the stale lock identity for authority I/O");
        }
        require(storage.snapshot().open_handles.empty() &&
                    !storage.snapshot().authority_mutation_attempted,
                "lock replacement leaked a handle or mutated authority");
    }
}

void require_fence_epoch_cas_clear() {
    const auto chain = make_chain();
    const auto root = std::string(chain.genesis_root.canonical_bytes());
    auto storage = JournalTestStorage::seeded(frame(chain.genesis), root);
    auto during_unlock_peer = recover(storage, root);
    auto after_unlock_peer = recover(storage, root);
    storage.overwrite_fixed_child_bytes(
        FixedAuthorityChild::Journal,
        frame(chain.genesis) + frame(chain.successor));

    storage.reset_observations();
    storage.arm_fault(FaultOperation::AuthorityUnlock, FaultPosition::After,
                      FaultAction::Error);
    storage.pause_after(FaultOperation::AuthorityUnlock);
    std::optional<DurableJournalResult> repairing_result;
    std::thread repairing([&] {
        auto journal = storage.make_journal(generous_limits());
        repairing_result.emplace(journal.recover_existing(
            TrustedReplayFloor::exact_root(root)));
    });
    require(storage.wait_until_paused(5000),
            "tail repair did not pause after the unlock effect");

    storage.reset_observations();
    const auto during_unlock_before = storage.snapshot();
    auto during_unlock_journal = storage.make_journal(generous_limits());
    auto blocked_during_unlock = during_unlock_journal.append_and_publish(
        std::move(during_unlock_peer), chain.successor);
    const auto during_unlock_after = storage.snapshot();
    require_result(blocked_during_unlock,
                   DurableJournalStatus::RecoveryRequired,
                   "tail repair exposed an unlock-before-fence window");
    require(!during_unlock_peer.available() &&
                storage.attempt_count(FaultOperation::AuthorityLockOpen) == 0 &&
                same_persistent_state(during_unlock_after,
                                      during_unlock_before) &&
                !during_unlock_after.authority_mutation_attempted,
            "tail-repair provisional fence allowed authority I/O");

    storage.release_pause();
    repairing.join();
    require(repairing_result.has_value(),
            "paused tail repair did not return after unlock");
    require_result(*repairing_result, DurableJournalStatus::RecoveryRequired,
                   "effectful unlock failure did not retain its fence");

    storage.reset_observations();
    const auto fenced_before = storage.snapshot();
    auto blocked_writer = storage.make_journal(generous_limits());
    auto blocked = blocked_writer.append_and_publish(
        std::move(after_unlock_peer), chain.successor);
    const auto fenced_after = storage.snapshot();
    require_result(blocked, DurableJournalStatus::RecoveryRequired,
                   "unlock failure did not retain its provisional fence");
    require(!after_unlock_peer.available() &&
                storage.attempt_count(FaultOperation::AuthorityLockOpen) == 0 &&
                same_persistent_state(fenced_after, fenced_before) &&
                !fenced_after.authority_mutation_attempted,
            "retained process fence allowed authority I/O or mutation");

    auto recovered = recover(storage, root);
    auto continuation = storage.make_journal(generous_limits());
    auto continued = continuation.append_and_publish(std::move(recovered),
                                                     chain.successor);
    require_result(continued, DurableJournalStatus::Published,
                   "locked recovery did not clear the retained process fence");

    auto epoch_storage =
        JournalTestStorage::seeded(frame(chain.genesis), root);
    auto before_resume_peer = recover(epoch_storage, root);
    auto after_resume_peer = recover(epoch_storage, root);
    epoch_storage.overwrite_fixed_child_bytes(
        FixedAuthorityChild::Journal,
        frame(chain.genesis) + frame(chain.successor));

    epoch_storage.reset_observations();
    epoch_storage.pause_after(FaultOperation::AuthorityUnlock);
    std::optional<DurableJournalResult> epoch_clearing_result;
    std::thread epoch_clearing([&] {
        auto journal = epoch_storage.make_journal(generous_limits());
        epoch_clearing_result.emplace(journal.recover_existing(
            TrustedReplayFloor::exact_root(root)));
    });
    require(epoch_storage.wait_until_paused(5000),
            "epoch-clearing recovery did not pause after unlock");

    epoch_storage.overwrite_fixed_child_bytes(
        FixedAuthorityChild::Journal,
        frame(chain.genesis) + frame(chain.successor));
    epoch_storage.arm_fault(FaultOperation::TailRepairTruncate,
                            FaultPosition::After, FaultAction::Error);
    auto replacement_journal = epoch_storage.make_journal(generous_limits());
    auto replacement = replacement_journal.recover_existing(
        TrustedReplayFloor::exact_root(root));
    require_result(replacement, DurableJournalStatus::RecoveryRequired,
                   "effectful tail repair did not publish a replacement epoch");

    epoch_storage.reset_observations();
    const auto before_resume = epoch_storage.snapshot();
    auto before_resume_journal =
        epoch_storage.make_journal(generous_limits());
    auto blocked_before_resume = before_resume_journal.append_and_publish(
        std::move(before_resume_peer), chain.successor);
    const auto after_blocked_before_resume = epoch_storage.snapshot();
    require_result(blocked_before_resume, DurableJournalStatus::RecoveryRequired,
                   "replacement epoch did not fence before older recovery");
    require(!before_resume_peer.available() &&
                epoch_storage.attempt_count(
                    FaultOperation::AuthorityLockOpen) == 0 &&
                same_persistent_state(after_blocked_before_resume,
                                      before_resume) &&
                !after_blocked_before_resume.authority_mutation_attempted,
            "replacement epoch allowed pre-resume authority I/O");

    epoch_storage.release_pause();
    epoch_clearing.join();
    require(epoch_clearing_result.has_value() &&
                epoch_clearing_result->published(),
            "older epoch recovery did not complete after successful unlock");

    epoch_storage.reset_observations();
    const auto after_resume = epoch_storage.snapshot();
    auto after_resume_journal = epoch_storage.make_journal(generous_limits());
    auto blocked_after_resume = after_resume_journal.append_and_publish(
        std::move(after_resume_peer), chain.successor);
    const auto after_blocked_after_resume = epoch_storage.snapshot();
    require_result(blocked_after_resume, DurableJournalStatus::RecoveryRequired,
                   "older recovery erased its replacement epoch");
    require(!after_resume_peer.available() &&
                epoch_storage.attempt_count(
                    FaultOperation::AuthorityLockOpen) == 0 &&
                same_persistent_state(after_blocked_after_resume,
                                      after_resume) &&
                !after_blocked_after_resume.authority_mutation_attempted,
            "replacement epoch allowed post-resume authority I/O");

    auto epoch_recovered = recover(epoch_storage, root);
    auto epoch_continuation = epoch_storage.make_journal(generous_limits());
    auto epoch_continued = epoch_continuation.append_and_publish(
        std::move(epoch_recovered), chain.successor);
    require_result(epoch_continued, DurableJournalStatus::Published,
                   "locked recovery did not clear the replacement epoch");
}

void require_fake_storage_identity_binding() {
    const auto chain = make_chain();
    const auto committed = frame(chain.genesis);
    const auto root = std::string(chain.genesis_root.canonical_bytes());
    auto storage_a = JournalTestStorage::seeded(committed, root);
    auto storage_b = JournalTestStorage::seeded(committed, root);

    auto initialize_a = recover(storage_a, root);
    auto initialize_b = recover(storage_b, root);
    static_cast<void>(initialize_a);
    static_cast<void>(initialize_b);
    storage_b.alias_lock_identity_from(storage_a);

    auto append_token = recover(storage_a, root);
    storage_b.reset_observations();
    const auto append_before = storage_b.snapshot();
    auto b_append = storage_b.make_journal(generous_limits());
    auto append_result =
        b_append.append_and_publish(std::move(append_token), chain.successor);
    require_result(append_result, DurableJournalStatus::CorruptOrRollback,
                   "cross-storage append accepted byte-identical authority");
    require(!append_token.available() &&
                same_persistent_state(storage_b.snapshot(), append_before) &&
                !storage_b.snapshot().authority_mutation_attempted &&
                storage_b.attempt_count(
                    FaultOperation::AuthorityIdentityRead) == 1 &&
                storage_b.attempt_count(FaultOperation::RootOpen) == 0 &&
                storage_b.attempt_count(FaultOperation::RootRead) == 0 &&
                storage_b.attempt_count(FaultOperation::JournalOpen) == 0 &&
                storage_b.attempt_count(FaultOperation::JournalRead) == 0 &&
                storage_b.snapshot().all_authority_io_under_lock,
            "cross-storage append read or mutated journal authority");
    require_storage_released_after_return(
        storage_b, "cross-storage append retained an authority handle");

    auto compact_token = recover(storage_a, root);
    storage_b.reset_observations();
    const auto compact_before = storage_b.snapshot();
    auto b_compact = storage_b.make_journal(generous_limits());
    auto compact_result = b_compact.compact_physical(std::move(compact_token));
    require_result(
        compact_result, DurableJournalStatus::CorruptOrRollback,
        "cross-storage compaction accepted byte-identical authority");
    require(!compact_token.available() &&
                same_persistent_state(storage_b.snapshot(), compact_before) &&
                !storage_b.snapshot().authority_mutation_attempted &&
                storage_b.attempt_count(
                    FaultOperation::AuthorityIdentityRead) == 1 &&
                storage_b.attempt_count(FaultOperation::RootOpen) == 0 &&
                storage_b.attempt_count(FaultOperation::RootRead) == 0 &&
                storage_b.attempt_count(FaultOperation::JournalOpen) == 0 &&
                storage_b.attempt_count(FaultOperation::JournalRead) == 0 &&
                storage_b.snapshot().all_authority_io_under_lock,
            "cross-storage compaction read or mutated journal authority");
    require_storage_released_after_return(
        storage_b, "cross-storage compaction retained an authority handle");

    auto export_token = recover(storage_a, root);
    storage_b.reset_observations();
    const auto export_before = storage_b.snapshot();
    auto b_export = storage_b.make_journal(generous_limits());
    auto export_result = b_export.export_exact_schema_candidate(
        export_token, supported_journal_schema, JournalQuiescence::Confirmed);
    require(
        export_result.status == ExactSchemaExportStatus::CorruptOrRollback &&
            !export_result.exported() && !export_result.candidate.has_value() &&
            export_token.available() &&
            same_persistent_state(storage_b.snapshot(), export_before) &&
            !storage_b.snapshot().authority_mutation_attempted &&
            storage_b.attempt_count(FaultOperation::AuthorityIdentityRead) ==
                1 &&
            storage_b.attempt_count(FaultOperation::RootOpen) == 0 &&
            storage_b.attempt_count(FaultOperation::RootRead) == 0 &&
            storage_b.attempt_count(FaultOperation::JournalOpen) == 0 &&
            storage_b.attempt_count(FaultOperation::JournalRead) == 0 &&
            storage_b.snapshot().all_authority_io_under_lock,
        "cross-storage export authorized or read byte-identical storage");
    require_storage_released_after_return(
        storage_b, "cross-storage export retained an authority handle");

    auto same_storage = storage_a.make_journal(generous_limits());
    auto published = same_storage.append_and_publish(std::move(export_token),
                                                     chain.successor);
    require_result(published, DurableJournalStatus::Published,
                   "fresh adapter for the same storage rejected its token");
}

void require_fake_lock_recreation_invalidates_authority() {
    const auto chain = make_chain();
    const auto committed = frame(chain.genesis);
    const auto root = std::string(chain.genesis_root.canonical_bytes());

    const auto replace_lock = [](JournalTestStorage &storage) {
        const auto old_identity = storage.lock_identity();
        require(old_identity != 0 && storage.try_unlink_lock_file() &&
                    storage.try_recreate_lock_file() &&
                    storage.lock_identity() != 0 &&
                    storage.lock_identity() != old_identity,
                "fake stable lock was not recreated with a new identity");
        storage.reset_observations();
    };
    const auto require_rejection_trace = [](JournalTestStorage &storage,
                                            const NamespaceSnapshot &before,
                                            std::string_view label) {
        const auto after = storage.snapshot();
        require(same_persistent_state(after, before) &&
                    !after.authority_mutation_attempted &&
                    after.all_authority_io_under_lock &&
                    storage.attempt_count(
                        FaultOperation::AuthorityIdentityRead) == 1 &&
                    storage.attempt_count(FaultOperation::RootOpen) == 0 &&
                    storage.attempt_count(FaultOperation::RootRead) == 0 &&
                    storage.attempt_count(FaultOperation::JournalOpen) == 0 &&
                    storage.attempt_count(FaultOperation::JournalRead) == 0,
                label);
        require_storage_released_after_return(
            storage, "recreated-lock rejection retained an authority handle");
        auto rebound = recover(storage, before.authority_root_bytes);
        require(rebound.available(),
                "fresh recovery did not bind the recreated stable lock");
    };

    auto append_storage = JournalTestStorage::seeded(committed, root);
    auto append_token = recover(append_storage, root);
    replace_lock(append_storage);
    const auto append_before = append_storage.snapshot();
    auto append_journal = append_storage.make_journal(generous_limits());
    auto append_result = append_journal.append_and_publish(
        std::move(append_token), chain.successor);
    require_result(append_result, DurableJournalStatus::UnsupportedStorage,
                   "recreated stable lock accepted an old append token");
    require(!append_token.available(),
            "recreated-lock append did not consume its token");
    require_rejection_trace(append_storage, append_before,
                            "recreated-lock append reached authority I/O");

    auto compact_storage = JournalTestStorage::seeded(committed, root);
    auto compact_token = recover(compact_storage, root);
    replace_lock(compact_storage);
    const auto compact_before = compact_storage.snapshot();
    auto compact_journal = compact_storage.make_journal(generous_limits());
    auto compact_result =
        compact_journal.compact_physical(std::move(compact_token));
    require_result(compact_result, DurableJournalStatus::UnsupportedStorage,
                   "recreated stable lock accepted an old compaction token");
    require(!compact_token.available(),
            "recreated-lock compaction did not consume its token");
    require_rejection_trace(compact_storage, compact_before,
                            "recreated-lock compaction reached authority I/O");

    auto export_storage = JournalTestStorage::seeded(committed, root);
    auto export_token = recover(export_storage, root);
    replace_lock(export_storage);
    const auto export_before = export_storage.snapshot();
    auto export_journal = export_storage.make_journal(generous_limits());
    auto export_result = export_journal.export_exact_schema_candidate(
        export_token, supported_journal_schema, JournalQuiescence::Confirmed);
    require(
        export_result.status == ExactSchemaExportStatus::UnsupportedStorage &&
            !export_result.exported() && !export_result.candidate.has_value() &&
            export_token.available(),
        "recreated stable lock accepted or consumed an old export token");
    require_rejection_trace(export_storage, export_before,
                            "recreated-lock export reached authority I/O");
}

void require_directory_lineage_fence_cleanup() {
    const auto chain = make_chain();
    const auto committed = frame(chain.genesis);
    const auto root = std::string(chain.genesis_root.canonical_bytes());
    auto storage = JournalTestStorage::seeded(committed, root);
    auto failing_authority = recover(storage, root);
    auto stale_authority = recover(storage, root);

    storage.arm_fault(FaultOperation::JournalWrite, FaultPosition::After,
                      FaultAction::Error);
    auto failing_journal = storage.make_journal(generous_limits());
    auto failed = failing_journal.append_and_publish(
        std::move(failing_authority), chain.successor);
    require_result(failed, DurableJournalStatus::RecoveryRequired,
                   "effectful append did not install a lineage fence");
    require(storage.snapshot().journal_bytes ==
                committed + frame(chain.successor) &&
                storage.snapshot().authority_root_bytes == root,
            "lineage-fence setup did not leave a recoverable tail");

    const auto old_lock_identity = storage.lock_identity();
    require(old_lock_identity != 0 && storage.try_unlink_lock_file() &&
                storage.try_recreate_lock_file() &&
                storage.lock_identity() != old_lock_identity,
            "lineage-fence setup did not replace the stable lock");
    auto rebound = recover(storage, root);

    storage.reset_observations();
    auto stale_journal = storage.make_journal(generous_limits());
    auto stale = stale_journal.append_and_publish(std::move(stale_authority),
                                                   chain.successor);
    require_result(stale, DurableJournalStatus::UnsupportedStorage,
                   "cleared lineage fence reauthorized an old lock token");
    require(!stale_authority.available() &&
                storage.attempt_count(FaultOperation::AuthorityLockOpen) == 1 &&
                storage.attempt_count(
                    FaultOperation::AuthorityIdentityRead) == 1 &&
                storage.attempt_count(FaultOperation::RootOpen) == 0 &&
                storage.attempt_count(FaultOperation::JournalOpen) == 0,
            "directory-lineage cleanup retained an obsolete full-identity "
            "fence");

    auto continuation = storage.make_journal(generous_limits());
    auto continued = continuation.append_and_publish(std::move(rebound),
                                                      chain.successor);
    require_result(continued, DurableJournalStatus::Published,
                   "directory-lineage recovery did not clear its fence");
}

void require_fenced_fresh_create_reconciles() {
    const auto chain = make_chain();
    auto storage = JournalTestStorage::fresh();
    storage.arm_fault(FaultOperation::InitialJournalCreate,
                      FaultPosition::After, FaultAction::Error);
    auto failed_create = storage.make_journal(generous_limits());
    auto failed = failed_create.create_new(
        TrustedReplayFloor::uninitialized(), chain.genesis);
    require_result(failed, DurableJournalStatus::RecoveryRequired,
                   "effectful create did not install a process fence");
    storage.remove_fixed_child_file(FixedAuthorityChild::Journal);

    storage.reset_observations();
    const auto before_failed_inspection = storage.snapshot();
    storage.arm_fault(FaultOperation::FixedNamespaceInspect,
                      FaultPosition::Before, FaultAction::Error);
    auto failed_inspector = storage.make_journal(generous_limits());
    auto inspection_failure = failed_inspector.create_new(
        TrustedReplayFloor::uninitialized(), chain.genesis);
    require_result(
        inspection_failure, DurableJournalStatus::RecoveryRequired,
        "failed fresh-namespace inspection discarded a retained fence");
    require(!inspection_failure.journal.has_value() &&
                same_persistent_state(storage.snapshot(),
                                      before_failed_inspection) &&
                !storage.snapshot().authority_mutation_attempted,
            "failed fresh-namespace inspection changed authority");
    require_storage_released_after_return(
        storage, "failed fresh-namespace inspection retained a handle");

    storage.reset_observations();
    const auto before = storage.snapshot();
    auto retried_create = storage.make_journal(generous_limits());
    auto retried = retried_create.create_new(
        TrustedReplayFloor::uninitialized(), chain.genesis);
    const auto after = storage.snapshot();
    require_result(retried, DurableJournalStatus::Published,
                   "fresh namespace create did not reconcile its fence");
    require(retried.journal.has_value() && retried.journal->available() &&
                !same_persistent_state(after, before) &&
                after.authority_mutation_attempted &&
                after.journal_bytes == frame(chain.genesis) &&
                after.authority_root_bytes ==
                    chain.genesis_root.canonical_bytes() &&
                after.all_authority_io_under_lock,
            "fenced create did not verify and publish a fresh namespace");
    require_storage_released_after_return(
        storage, "fenced create retained an authority handle");
}

template <typename Mutation>
DurableJournalResult create_after_paused_operation(
    JournalTestStorage &storage, const Chain &chain, FaultOperation operation,
    Mutation mutation, std::string_view pause_failure,
    std::size_t occurrence = 1) {
    storage.pause_after_nth(operation, occurrence);
    std::optional<DurableJournalResult> result;
    std::thread creator([&] {
        auto journal = storage.make_journal(generous_limits());
        result.emplace(journal.create_new(TrustedReplayFloor::uninitialized(),
                                          chain.genesis));
    });
    if (!storage.wait_until_paused(5000)) {
        storage.release_pause();
        creator.join();
        require(false, pause_failure);
    }
    mutation();
    storage.release_pause();
    creator.join();
    require(result.has_value(), "paused create did not return");
    return std::move(*result);
}

void require_create_post_failure_clearance_proof() {
    const auto chain = make_chain();

    {
        auto storage = JournalTestStorage::fresh();
        storage.arm_fault(FaultOperation::InitialJournalCreate,
                          FaultPosition::Before, FaultAction::Error);
        auto journal = storage.make_journal(generous_limits());
        auto result = journal.create_new(TrustedReplayFloor::uninitialized(),
                                         chain.genesis);
        require_result(result, DurableJournalStatus::UnsupportedStorage,
                       "proven before-effect create failure stayed fenced");
        const auto after = storage.snapshot();
        require(after.journal_bytes.empty() &&
                    after.authority_root_bytes.empty() &&
                    after.stage_files_absent &&
                    !after.authority_mutation_attempted,
                "before-effect create failure changed authority");
        auto retry = storage.make_journal(generous_limits());
        auto retried = retry.create_new(TrustedReplayFloor::uninitialized(),
                                        chain.genesis);
        require_result(retried, DurableJournalStatus::Published,
                       "proven before-effect create failure retained its fence");
    }

    {
        auto storage = JournalTestStorage::fresh();
        storage.arm_fault(FaultOperation::InitialJournalCreate,
                          FaultPosition::After, FaultAction::Error);
        auto result = create_after_paused_operation(
            storage, chain, FaultOperation::FixedNamespaceInspect,
            [&] {
                storage.remove_fixed_child_file(FixedAuthorityChild::Journal);
            },
            "create did not pause after an effect-uncertain failure", 2);
        require_result(result, DurableJournalStatus::RecoveryRequired,
                       "effect-uncertain create cleared a fresh fence");
        const auto after = storage.snapshot();
        require(after.journal_bytes.empty() &&
                    after.authority_root_bytes.empty() &&
                    after.stage_files_absent &&
                    after.authority_mutation_attempted,
                "effect-uncertain create fixture changed its final namespace");
        auto recovery = storage.make_journal(generous_limits());
        auto recovered = recovery.create_new(
            TrustedReplayFloor::uninitialized(), chain.genesis);
        require_result(recovered, DurableJournalStatus::Published,
                       "locked create did not clear an effect-uncertain fence");
    }

    {
        auto storage = JournalTestStorage::fresh();
        auto stale_peer = create(storage, chain);
        storage.remove_fixed_child_file(FixedAuthorityChild::Journal);
        storage.remove_fixed_child_file(FixedAuthorityChild::Root);
        storage.reset_observations();
        storage.arm_fault(FaultOperation::InitialJournalCreate,
                          FaultPosition::Before, FaultAction::Error);
        auto result = create_after_paused_operation(
            storage, chain, FaultOperation::FixedNamespaceInspect,
            [&] {
                storage.arm_fault(FaultOperation::FixedNamespaceInspect,
                                  FaultPosition::After, FaultAction::Error);
            },
            "create did not pause during post-create inspection", 2);
        require_result(
            result, DurableJournalStatus::RecoveryRequired,
            "failed post-create inspection cleared a provisional fence");
        const auto fenced = storage.snapshot();
        require(fenced.journal_bytes.empty() &&
                    fenced.authority_root_bytes.empty() &&
                    fenced.stage_files_absent &&
                    !fenced.authority_mutation_attempted,
                "failed post-create inspection changed authority");
        storage.reset_observations();
        const auto before_peer = storage.snapshot();
        auto peer = storage.make_journal(generous_limits());
        auto blocked = peer.append_and_publish(std::move(stale_peer),
                                               chain.successor);
        require_result(blocked, DurableJournalStatus::RecoveryRequired,
                       "failed post-create inspection did not retain its fence");
        const auto after_peer = storage.snapshot();
        require(!stale_peer.available() &&
                    storage.attempt_count(
                        FaultOperation::AuthorityLockOpen) == 0 &&
                    same_persistent_state(after_peer, before_peer) &&
                    !after_peer.authority_mutation_attempted,
                "post-create inspection fence allowed peer authority I/O");
        auto recovery = storage.make_journal(generous_limits());
        auto recovered = recovery.create_new(
            TrustedReplayFloor::uninitialized(), chain.genesis);
        require_result(recovered, DurableJournalStatus::Published,
                       "locked create did not clear an inspection fence");
    }

    {
        auto storage = JournalTestStorage::fresh();
        auto stale_peer = create(storage, chain);
        storage.remove_fixed_child_file(FixedAuthorityChild::Journal);
        storage.remove_fixed_child_file(FixedAuthorityChild::Root);
        storage.reset_observations();
        storage.arm_fault(FaultOperation::InitialJournalCreate,
                          FaultPosition::Before, FaultAction::Error);
        auto result = create_after_paused_operation(
            storage, chain, FaultOperation::FixedNamespaceInspect,
            [&] {
                storage.arm_fault(FaultOperation::AuthorityIdentityRead,
                                  FaultPosition::Before,
                                  FaultAction::Error);
            },
            "create did not pause before post-create identity validation", 2);
        require_result(result, DurableJournalStatus::RecoveryRequired,
                       "failed post-create identity read cleared its fence");
        require(!result.published() && !result.journal.has_value(),
                "failed post-create identity read returned authority");
        const auto fenced = storage.snapshot();
        require(fenced.journal_bytes.empty() &&
                    fenced.authority_root_bytes.empty() &&
                    fenced.stage_files_absent &&
                    !fenced.authority_mutation_attempted,
                "failed post-create identity read changed authority");
        storage.reset_observations();
        const auto before_peer = storage.snapshot();
        auto peer = storage.make_journal(generous_limits());
        auto blocked = peer.append_and_publish(std::move(stale_peer),
                                               chain.successor);
        require_result(blocked, DurableJournalStatus::RecoveryRequired,
                       "failed identity read did not retain its fence");
        const auto after_peer = storage.snapshot();
        require(!stale_peer.available() &&
                    storage.attempt_count(
                        FaultOperation::AuthorityLockOpen) == 0 &&
                    same_persistent_state(after_peer, before_peer) &&
                    !after_peer.authority_mutation_attempted,
                "identity-read fence allowed peer authority I/O");
        auto recovery = storage.make_journal(generous_limits());
        auto recovered = recovery.create_new(
            TrustedReplayFloor::uninitialized(), chain.genesis);
        require_result(recovered, DurableJournalStatus::Published,
                       "locked create did not clear an identity-read fence");
    }

    {
        auto storage = JournalTestStorage::fresh();
        storage.arm_fault(FaultOperation::InitialJournalCreate,
                          FaultPosition::Before, FaultAction::Error);
        auto result = create_after_paused_operation(
            storage, chain, FaultOperation::FixedNamespaceInspect,
            [&] {
                storage.seed_untrusted_file("journal.jsonl",
                                            "post-create-journal");
            },
            "create did not pause during post-create journal inspection", 2);
        require_result(result, DurableJournalStatus::RecoveryRequired,
                       "post-create journal residue cleared its fence");
        const auto fenced = storage.snapshot();
        require(fenced.journal_bytes == "post-create-journal" &&
                    fenced.authority_root_bytes.empty() &&
                    fenced.stage_files_absent &&
                    !fenced.authority_mutation_attempted,
                "before-effect create changed post-create journal residue");
        auto peer = storage.make_journal(generous_limits());
        auto blocked = peer.create_new(TrustedReplayFloor::uninitialized(),
                                       chain.genesis);
        require_result(blocked, DurableJournalStatus::RecoveryRequired,
                       "post-create journal residue did not retain its fence");
        require(same_persistent_state(storage.snapshot(), fenced),
                "fenced peer changed post-create journal residue");
        storage.remove_fixed_child_file(FixedAuthorityChild::Journal);
        auto recovery = storage.make_journal(generous_limits());
        auto recovered = recovery.create_new(
            TrustedReplayFloor::uninitialized(), chain.genesis);
        require_result(recovered, DurableJournalStatus::Published,
                       "locked create did not clear a journal residue fence");
    }

    for (const auto child : {FixedAuthorityChild::Root,
                             FixedAuthorityChild::JournalStage,
                             FixedAuthorityChild::RootStage}) {
        auto storage = JournalTestStorage::fresh();
        storage.arm_fault(FaultOperation::InitialJournalCreate,
                          FaultPosition::Before, FaultAction::Error);
        auto result = create_after_paused_operation(
            storage, chain, FaultOperation::FixedNamespaceInspect,
            [&] {
                storage.replace_fixed_child_file(child,
                                                 "compound-fixed-residue");
            },
            "create did not pause during compound namespace inspection", 2);
        require_result(result, DurableJournalStatus::RecoveryRequired,
                       "compound fixed residue cleared a provisional fence");
        const auto fenced = storage.snapshot();
        const auto compound_entries = static_cast<std::size_t>(std::count_if(
            fenced.live_file_bytes.begin(), fenced.live_file_bytes.end(),
            [](const auto &entry) {
                return entry.second == "compound-fixed-residue";
            }));
        require(fenced.journal_bytes.empty() &&
                    compound_entries == 1 &&
                    !fenced.authority_mutation_attempted,
                "before-effect create changed compound fixed residue");
        auto peer = storage.make_journal(generous_limits());
        auto blocked = peer.create_new(TrustedReplayFloor::uninitialized(),
                                       chain.genesis);
        require_result(blocked, DurableJournalStatus::RecoveryRequired,
                       "compound fixed residue did not retain its fence");
        require(same_persistent_state(storage.snapshot(), fenced),
                "fenced peer changed compound fixed residue");
        storage.remove_fixed_child_file(child);
        auto recovery = storage.make_journal(generous_limits());
        auto recovered = recovery.create_new(
            TrustedReplayFloor::uninitialized(), chain.genesis);
        require_result(recovered, DurableJournalStatus::Published,
                       "locked create did not clear a compound residue fence");
    }

    {
        auto storage = JournalTestStorage::fresh();
        storage.arm_fault(FaultOperation::InitialJournalCreate,
                          FaultPosition::Before, FaultAction::Error);
        auto result = create_after_paused_operation(
            storage, chain, FaultOperation::FixedNamespaceInspect,
            [&] {
                storage.arm_fault(FaultOperation::AuthorityUnlock,
                                  FaultPosition::After, FaultAction::Error);
            },
            "create did not pause before its post-create unlock", 2);
        require_result(result, DurableJournalStatus::RecoveryRequired,
                       "failed post-create unlock cleared its fence");
        const auto after = storage.snapshot();
        require(after.journal_bytes.empty() &&
                    after.authority_root_bytes.empty() &&
                    after.stage_files_absent &&
                    !after.authority_mutation_attempted,
                "failed post-create unlock changed authority");
        auto recovery = storage.make_journal(generous_limits());
        auto recovered = recovery.create_new(
            TrustedReplayFloor::uninitialized(), chain.genesis);
        require_result(recovered, DurableJournalStatus::Published,
                       "locked create did not clear an unlock fence");
    }

    {
        auto storage = JournalTestStorage::fresh(PlatformContract::Linux);
        storage.arm_fault(FaultOperation::InitialJournalCreate,
                          FaultPosition::Before, FaultAction::Error);
        auto result = create_after_paused_operation(
            storage, chain, FaultOperation::FixedNamespaceInspect,
            [&] {
                require(storage.try_unlink_lock_file(),
                        "post-create lock fixture did not remove the lock");
            },
            "create did not pause during post-create lock validation", 2);
        require_result(result, DurableJournalStatus::RecoveryRequired,
                       "missing post-create lock cleared its fence");
        const auto after = storage.snapshot();
        require(after.journal_bytes.empty() &&
                    after.authority_root_bytes.empty() &&
                    after.stage_files_absent &&
                    !after.authority_mutation_attempted,
                "missing post-create lock changed authority");
        require(storage.try_recreate_lock_file(),
                "post-create lock fixture did not restore the lock");
        auto recovery = storage.make_journal(generous_limits());
        auto recovered = recovery.create_new(
            TrustedReplayFloor::uninitialized(), chain.genesis);
        require_result(recovered, DurableJournalStatus::Published,
                       "locked create did not clear a missing-lock fence");
    }

    {
        auto storage = JournalTestStorage::fresh(PlatformContract::Linux);
        storage.arm_fault(FaultOperation::InitialJournalCreate,
                          FaultPosition::Before, FaultAction::Error);
        auto result = create_after_paused_operation(
            storage, chain, FaultOperation::FixedNamespaceInspect,
            [&] {
                const auto original_identity = storage.lock_identity();
                require(original_identity != 0 &&
                            storage.try_unlink_lock_file() &&
                            storage.try_recreate_lock_file() &&
                            storage.lock_identity() != original_identity,
                        "post-create identity fixture did not rebind the lock");
            },
            "create did not pause during post-create identity validation", 2);
        require_result(result, DurableJournalStatus::RecoveryRequired,
                       "rebound post-create identity cleared its fence");
        const auto after = storage.snapshot();
        require(after.journal_bytes.empty() &&
                    after.authority_root_bytes.empty() &&
                    after.stage_files_absent &&
                    !after.authority_mutation_attempted,
                "rebound post-create identity changed authority");
        auto recovery = storage.make_journal(generous_limits());
        auto recovered = recovery.create_new(
            TrustedReplayFloor::uninitialized(), chain.genesis);
        require_result(recovered, DurableJournalStatus::Published,
                       "locked create did not clear a rebound identity fence");
    }
}

void require_retained_fresh_create_requires_live_lock() {
    const auto chain = make_chain();
    auto storage = JournalTestStorage::fresh(PlatformContract::Linux);
    storage.arm_fault(FaultOperation::InitialJournalCreate,
                      FaultPosition::After, FaultAction::Error);
    auto faulting = storage.make_journal(generous_limits());
    auto failed = faulting.create_new(TrustedReplayFloor::uninitialized(),
                                      chain.genesis);
    require_result(failed, DurableJournalStatus::RecoveryRequired,
                   "effectful create did not establish a retained fence");
    storage.remove_fixed_child_file(FixedAuthorityChild::Journal);
    storage.reset_observations();

    auto blocked = create_after_paused_operation(
        storage, chain, FaultOperation::FixedNamespaceInspect,
        [&] {
            require(storage.try_unlink_lock_file(),
                    "retained-fence fixture did not remove the live lock");
        },
        "retained create did not pause during fixed inspection");
    require_result(blocked, DurableJournalStatus::RecoveryRequired,
                   "retained create accepted a missing live lock");
    const auto after = storage.snapshot();
    require(after.journal_bytes.empty() && after.authority_root_bytes.empty() &&
                after.stage_files_absent &&
                !after.authority_mutation_attempted,
            "retained create mutated a namespace without its live lock");
    require(storage.try_recreate_lock_file(),
            "retained-fence fixture did not restore the live lock");
    auto recovery = storage.make_journal(generous_limits());
    auto recovered = recovery.create_new(TrustedReplayFloor::uninitialized(),
                                         chain.genesis);
    require_result(recovered, DurableJournalStatus::Published,
                   "locked create did not clear a missing-lock fence");
}

const std::vector<FaultOperation> repair_operations = [] {
    auto operations = pre_authority_operations;
    operations.insert(operations.end(), {
                                            FaultOperation::RootOpen,
                                            FaultOperation::RootRead,
                                            FaultOperation::RootClose,
                                            FaultOperation::JournalOpen,
                                            FaultOperation::JournalRead,
                                            FaultOperation::JournalReadClose,
                                            FaultOperation::TailRepairOpen,
                                            FaultOperation::TailRepairTruncate,
                                            FaultOperation::TailRepairFlush,
                                            FaultOperation::TailRepairClose,
                                            FaultOperation::AuthorityUnlock,
                                            FaultOperation::AuthorityLockClose,
                                        });
    return operations;
}();

const std::vector<FaultOperation> compaction_operations = [] {
    auto operations = pre_authority_operations;
    operations.insert(operations.end(),
                      {
                          FaultOperation::RootOpen,
                          FaultOperation::RootRead,
                          FaultOperation::RootClose,
                          FaultOperation::JournalOpen,
                          FaultOperation::JournalRead,
                          FaultOperation::JournalReadClose,
                          FaultOperation::CompactionStageCreate,
                          FaultOperation::CompactionWrite,
                          FaultOperation::CompactionFlush,
                          FaultOperation::CompactionClose,
                          FaultOperation::CompactionReplace,
                          FaultOperation::CompactionNamespaceDurability,
                          FaultOperation::AuthorityUnlock,
                          FaultOperation::AuthorityLockClose,
                      });
    return operations;
}();

const std::vector<FaultOperation> read_only_recovery_operations = [] {
    auto operations = pre_authority_operations;
    operations.insert(operations.end(), {
                                            FaultOperation::RootOpen,
                                            FaultOperation::RootRead,
                                            FaultOperation::RootClose,
                                            FaultOperation::JournalOpen,
                                            FaultOperation::JournalRead,
                                            FaultOperation::JournalReadClose,
                                            FaultOperation::AuthorityUnlock,
                                            FaultOperation::AuthorityLockClose,
                                        });
    return operations;
}();

void require_read_only_recovery_faults() {
    const auto chain = make_chain();
    const auto committed = frame(chain.genesis);
    for (const auto operation : read_only_recovery_operations) {
        for (const auto position :
             {FaultPosition::Before, FaultPosition::After}) {
            auto storage = JournalTestStorage::seeded(
                committed, std::string(chain.genesis_root.canonical_bytes()));
            auto peer =
                recover(storage, chain.genesis_root.canonical_bytes());
            storage.reset_observations();
            const auto before = storage.snapshot();
            storage.arm_fault(operation, position, FaultAction::Error);
            auto journal = storage.make_journal(generous_limits());
            auto result =
                journal.recover_existing(TrustedReplayFloor::exact_root(
                    std::string(chain.genesis_root.canonical_bytes())));
            const auto expected =
                preflight_may_have_effect(operation, position,
                                          FaultAction::Error)
                    ? DurableJournalStatus::RecoveryRequired
                    : DurableJournalStatus::UnsupportedStorage;
            require_result(result, expected,
                           "read-only recovery fault changed stable outcome");
            const auto after = storage.snapshot();
            require(after.journal_bytes == before.journal_bytes &&
                        after.authority_root_bytes ==
                            before.authority_root_bytes &&
                        !after.authority_mutation_attempted &&
                        after.open_handles.empty() && after.stage_files_absent,
                    "read-only recovery fault mutated or leaked authority");
            if (operation == FaultOperation::RootClose ||
                operation == FaultOperation::JournalReadClose ||
                operation == FaultOperation::AuthorityLockClose) {
                require(storage.attempt_count(operation) == 1,
                        "read-only checked close was retried or omitted");
            }
            require_storage_released_after_return(
                storage, "read-only recovery fault retained a handle");

            if (preflight_may_have_effect(operation, position,
                                          FaultAction::Error)) {
                storage.reset_observations();
                const auto before_fenced_write = storage.snapshot();
                auto writer = storage.make_journal(generous_limits());
                auto blocked = writer.append_and_publish(
                    std::move(peer), chain.successor);
                require_result(
                    blocked, DurableJournalStatus::RecoveryRequired,
                    "effect-uncertain preflight did not retain its fence");
                const auto after_fenced_write = storage.snapshot();
                require(
                    !peer.available() &&
                        same_persistent_state(after_fenced_write,
                                              before_fenced_write) &&
                        !after_fenced_write.authority_mutation_attempted,
                    "preflight fence allowed a peer write");
                require_storage_released_after_return(
                    storage, "preflight fence retained a handle");
            }

            storage.reset_observations();
            auto retry = storage.make_journal(generous_limits());
            auto retried =
                retry.recover_existing(TrustedReplayFloor::exact_root(
                    std::string(chain.genesis_root.canonical_bytes())));
            require_result(retried, DurableJournalStatus::Published,
                           "read-only recovery did not retry cleanly");
            require(storage.snapshot().journal_bytes == committed &&
                        storage.snapshot().stage_files_absent &&
                        storage.snapshot().durable_stage_files_absent &&
                        storage.snapshot().open_handles.empty(),
                    "read-only retry left probe or handle residue");
            require_storage_released_after_return(
                storage, "read-only recovery retry retained a handle");
        }
    }
}

bool flow_may_have_mutated(const std::vector<FaultOperation> &operations,
                           FaultOperation first_mutation,
                           FaultOperation operation, FaultPosition position,
                           FaultAction action) {
    if (operation == first_mutation) {
        return position == FaultPosition::After ||
               action == FaultAction::EffectThenError;
    }
    const auto found =
        std::find(operations.begin(), operations.end(), operation);
    const auto mutation =
        std::find(operations.begin(), operations.end(), first_mutation);
    return found > mutation;
}

void require_repair_faults() {
    const auto chain = make_chain();
    const auto committed = frame(chain.genesis);
    const auto with_tail =
        committed + frame(chain.successor) + frame(chain.fork);

    for (const auto operation : repair_operations) {
        for (const auto position :
             {FaultPosition::Before, FaultPosition::After}) {
            for (const auto action : {FaultAction::Error, FaultAction::Crash,
                                      FaultAction::EffectThenError}) {
                auto storage = JournalTestStorage::seeded(
                    committed,
                    std::string(chain.genesis_root.canonical_bytes()));
                auto peer =
                    recover(storage, chain.genesis_root.canonical_bytes());
                storage.overwrite_fixed_child_bytes(
                    FixedAuthorityChild::Journal, with_tail);
                storage.reset_observations();
                storage.arm_fault(operation, position, action);
                auto journal = storage.make_journal(generous_limits());
                auto result =
                    journal.recover_existing(TrustedReplayFloor::exact_root(
                        std::string(chain.genesis_root.canonical_bytes())));
                const bool authority_may_have_mutated = flow_may_have_mutated(
                    repair_operations, FaultOperation::TailRepairTruncate,
                    operation, position, action);
                const auto expected =
                    (authority_may_have_mutated || preflight_may_have_effect(
                                                       operation, position,
                                                       action))
                        ? DurableJournalStatus::RecoveryRequired
                        : DurableJournalStatus::UnsupportedStorage;
                require_result(result, expected,
                               "tail-repair fault returned the wrong outcome");
                if (action != FaultAction::Crash) {
                    require_storage_released_after_return(
                        storage,
                        "tail-repair fault retained an authority handle");
                }
                if (authority_may_have_mutated ||
                    preflight_may_have_effect(operation, position, action)) {
                    const auto before_fence = storage.snapshot();
                    storage.reset_observations();
                    auto fence = storage.make_journal(generous_limits());
                    auto fenced = fence.append_and_publish(std::move(peer),
                                                           chain.successor);
                    require_result(
                        fenced, DurableJournalStatus::RecoveryRequired,
                        "possibly-effectful repair did not fence a pre-fault "
                        "authority token");
                    const auto after_fence = storage.snapshot();
                    require(
                        !peer.available() &&
                            same_persistent_state(after_fence, before_fence) &&
                            !after_fence.authority_mutation_attempted,
                        "repair fence attempt changed authority");
                    if (action != FaultAction::Crash) {
                        require_storage_released_after_return(
                            storage,
                            "repair fence retained an authority handle");
                    }
                }
                storage.restart();
                storage.reset_observations();
                auto recovered =
                    recover(storage, chain.genesis_root.canonical_bytes());
                require(recovered.root_bytes() ==
                            chain.genesis_root.canonical_bytes(),
                        "tail-repair crash changed authority");
                require(
                    storage.snapshot().journal_bytes == committed &&
                        storage.snapshot().journal_durable &&
                        storage.snapshot().journal_closed &&
                        storage.snapshot().stage_files_absent,
                    "tail repair was not durable before write-ready recovery");
                auto continuation = storage.make_journal(generous_limits());
                auto continued = continuation.append_and_publish(
                    std::move(recovered), chain.successor);
                require_result(continued, DurableJournalStatus::Published,
                               "repaired journal could not continue appending");
            }
        }
    }
}

void require_physical_compaction() {
    const auto chain = make_chain();
    const auto committed = frame(chain.genesis) + frame(chain.successor);
    auto baseline = JournalTestStorage::seeded(
        committed, std::string(chain.successor_root.canonical_bytes()));
    const auto exact_snapshot = baseline.snapshot();

    auto success = baseline.clone();
    auto authority = recover(success, chain.genesis_root.canonical_bytes());
    auto journal = success.make_journal(generous_limits());
    auto compacted = journal.compact_physical(std::move(authority));
    require_result(compacted, DurableJournalStatus::Published,
                   "physical compaction was rejected");
    require(!authority.available() && compacted.journal->available() &&
                success.snapshot().journal_bytes ==
                    exact_snapshot.journal_bytes &&
                success.snapshot().authority_root_bytes ==
                    exact_snapshot.authority_root_bytes &&
                success.snapshot().open_handles.empty() &&
                success.snapshot().all_authority_io_under_lock,
            "physical compaction duplicated or changed authority");

    for (const auto operation : compaction_operations) {
        for (const auto position :
             {FaultPosition::Before, FaultPosition::After}) {
            for (const auto action : {FaultAction::Error, FaultAction::Crash,
                                      FaultAction::EffectThenError}) {
                auto storage = baseline.clone();
                auto live =
                    recover(storage, chain.genesis_root.canonical_bytes());
                auto peer =
                    recover(storage, chain.genesis_root.canonical_bytes());
                storage.reset_observations();
                storage.arm_fault(operation, position, action);
                auto faulting_journal = storage.make_journal(generous_limits());
                auto result =
                    faulting_journal.compact_physical(std::move(live));
                const bool authority_may_have_mutated = flow_may_have_mutated(
                    compaction_operations,
                    FaultOperation::CompactionStageCreate, operation, position,
                    action);
                const auto expected =
                    (authority_may_have_mutated || preflight_may_have_effect(
                                                       operation, position,
                                                       action))
                        ? DurableJournalStatus::RecoveryRequired
                        : DurableJournalStatus::UnsupportedStorage;
                require_result(
                    result, expected,
                    "compaction fault returned the wrong stable outcome");
                require(!live.available(),
                        "compaction fault retained a write-capable handle");
                if (action != FaultAction::Crash) {
                    require_storage_released_after_return(
                        storage,
                        "compaction fault retained an authority handle");
                }
                if (authority_may_have_mutated ||
                    preflight_may_have_effect(operation, position, action)) {
                    const auto before_fence = storage.snapshot();
                    storage.reset_observations();
                    auto fence = storage.make_journal(generous_limits());
                    auto fenced =
                        fence.append_and_publish(std::move(peer), chain.third);
                    require_result(
                        fenced, DurableJournalStatus::RecoveryRequired,
                        "possibly-effectful compaction did not fence a "
                        "pre-fault authority token");
                    const auto after_fence = storage.snapshot();
                    require(
                        !peer.available() &&
                            same_persistent_state(after_fence, before_fence) &&
                            !after_fence.authority_mutation_attempted,
                        "compaction fence attempt changed authority");
                    if (action != FaultAction::Crash) {
                        require_storage_released_after_return(
                            storage,
                            "compaction fence retained an authority handle");
                    }
                }
                storage.restart();
                const auto restart_snapshot = storage.snapshot();
                storage.reset_observations();
                auto recovered =
                    recover(storage, chain.genesis_root.canonical_bytes());
                require(recovered.root_bytes() ==
                            chain.successor_root.canonical_bytes(),
                        "compaction crash changed authority");
                require(
                    storage.snapshot().journal_bytes ==
                            exact_snapshot.journal_bytes &&
                        storage.snapshot().authority_root_bytes ==
                            exact_snapshot.authority_root_bytes &&
                        stage_files_unchanged(restart_snapshot,
                                              storage.snapshot()) &&
                        storage.snapshot().journal_durable,
                    "compaction crash did not preserve byte-identical storage");
                auto continuation = storage.make_journal(generous_limits());
                auto continued = continuation.append_and_publish(
                    std::move(recovered), chain.third);
                require_result(
                    continued, DurableJournalStatus::Published,
                    "compaction recovery could not continue appending");
            }
        }
    }
}

void require_exact_schema_export() {
    const auto chain = make_chain();
    const auto committed = frame(chain.genesis) + frame(chain.successor);
    auto storage = JournalTestStorage::seeded(
        committed, std::string(chain.successor_root.canonical_bytes()));
    auto authority = recover(storage, chain.genesis_root.canonical_bytes());
    const auto before = storage.snapshot();
    auto journal = storage.make_journal(generous_limits());

    auto unconfirmed = journal.export_exact_schema_candidate(
        authority, supported_journal_schema, JournalQuiescence::Unconfirmed);
    require(!unconfirmed.exported() && !unconfirmed.candidate.has_value() &&
                unconfirmed.status ==
                    ExactSchemaExportStatus::QuiescenceRequired,
            "unconfirmed export returned a candidate");
    require(storage.snapshot() == before,
            "unconfirmed export readied or changed storage");

    auto exported = journal.export_exact_schema_candidate(
        authority, supported_journal_schema, JournalQuiescence::Confirmed);
    require(exported.status == ExactSchemaExportStatus::ExportedCandidate &&
                exported.exported() && exported.candidate.has_value(),
            "confirmed exact-schema export was rejected");
    require(exported.candidate->schema.major ==
                    supported_journal_schema.major &&
                exported.candidate->schema.minor ==
                    supported_journal_schema.minor &&
                exported.candidate->journal_bytes == committed &&
                exported.candidate->authority_root_bytes ==
                    chain.successor_root.canonical_bytes(),
            "exact-schema export changed bytes or schema");
    require(authority.available(),
            "nonauthorizing export consumed live authority");
    require(same_persistent_state(storage.snapshot(), before) &&
                storage.snapshot().open_handles.empty() &&
                storage.snapshot().all_authority_io_under_lock,
            "exact-schema export changed or leaked live storage");

    for (const auto operation : read_only_recovery_operations) {
        for (const auto position :
             {FaultPosition::Before, FaultPosition::After}) {
            auto fault_storage = JournalTestStorage::seeded(
                committed, std::string(chain.successor_root.canonical_bytes()));
            auto fault_authority =
                recover(fault_storage, chain.genesis_root.canonical_bytes());
            auto fault_peer =
                recover(fault_storage, chain.genesis_root.canonical_bytes());
            fault_storage.reset_observations();
            const auto fault_before = fault_storage.snapshot();
            fault_storage.arm_fault(operation, position, FaultAction::Error);
            auto fault_journal = fault_storage.make_journal(generous_limits());
            auto fault_export = fault_journal.export_exact_schema_candidate(
                fault_authority, supported_journal_schema,
                JournalQuiescence::Confirmed);
            const auto expected =
                preflight_may_have_effect(operation, position,
                                          FaultAction::Error)
                    ? ExactSchemaExportStatus::RecoveryRequired
                    : ExactSchemaExportStatus::UnsupportedStorage;
            require(fault_export.status ==
                            expected &&
                        !fault_export.exported() &&
                        !fault_export.candidate.has_value() &&
                        fault_authority.available(),
                    "export storage fault returned authority bytes");
            const auto fault_after = fault_storage.snapshot();
            require(authority_files_unchanged(fault_before, fault_after) &&
                        !fault_after.authority_mutation_attempted &&
                        fault_after.open_handles.empty(),
                    "export storage fault changed or leaked authority");
            require_storage_released_after_return(
                fault_storage, "export storage fault retained a handle");

            if (preflight_may_have_effect(operation, position,
                                          FaultAction::Error)) {
                fault_storage.reset_observations();
                const auto before_fenced_write = fault_storage.snapshot();
                auto writer = fault_storage.make_journal(generous_limits());
                auto blocked = writer.append_and_publish(
                    std::move(fault_peer), chain.third);
                require_result(
                    blocked, DurableJournalStatus::RecoveryRequired,
                    "effect-uncertain export preflight did not retain its "
                    "fence");
                const auto after_fenced_write = fault_storage.snapshot();
                require(
                    !fault_peer.available() &&
                        same_persistent_state(after_fenced_write,
                                              before_fenced_write) &&
                        !after_fenced_write.authority_mutation_attempted,
                    "export preflight fence allowed a peer write");
                require_storage_released_after_return(
                    fault_storage, "export preflight fence retained a handle");

                fault_storage.reset_observations();
                const auto before_fenced_export = fault_storage.snapshot();
                auto fenced_exporter =
                    fault_storage.make_journal(generous_limits());
                auto fenced_export =
                    fenced_exporter.export_exact_schema_candidate(
                        fault_authority, supported_journal_schema,
                        JournalQuiescence::Confirmed);
                require(
                    fenced_export.status ==
                            ExactSchemaExportStatus::RecoveryRequired &&
                        !fenced_export.exported() &&
                        !fenced_export.candidate.has_value() &&
                        fault_authority.available() &&
                        same_persistent_state(fault_storage.snapshot(),
                                              before_fenced_export) &&
                        !fault_storage.snapshot().authority_mutation_attempted,
                    "fenced export consumed authority or read storage");
                require_storage_released_after_return(
                    fault_storage, "fenced export retained a handle");

                fault_storage.reset_observations();
                auto recovered = recover(
                    fault_storage, chain.genesis_root.canonical_bytes());
                require(recovered.root_bytes() ==
                            chain.successor_root.canonical_bytes(),
                        "locked recovery did not clear the export preflight "
                        "fence");
            }

            fault_storage.reset_observations();
            auto retry = fault_storage.make_journal(generous_limits());
            auto retried = retry.export_exact_schema_candidate(
                fault_authority, supported_journal_schema,
                JournalQuiescence::Confirmed);
            require(retried.status ==
                            ExactSchemaExportStatus::ExportedCandidate &&
                        retried.exported() && retried.candidate.has_value() &&
                        same_persistent_state(fault_storage.snapshot(),
                                              fault_before),
                    "export storage fault did not retry cleanly");
            require_storage_released_after_return(
                fault_storage, "export retry retained an authority handle");
        }
    }

    storage.reset_observations();
    const auto schema_before = storage.snapshot();
    for (const auto schema :
         {SchemaVersion{0, 0},
          SchemaVersion{supported_journal_schema.major + 1, 0},
          SchemaVersion{supported_journal_schema.major,
                        supported_journal_schema.minor + 1}}) {
        auto rejected = journal.export_exact_schema_candidate(
            authority, schema, JournalQuiescence::Confirmed);
        require(rejected.status == ExactSchemaExportStatus::SchemaMismatch &&
                    !rejected.exported() && !rejected.candidate.has_value() &&
                    authority.available(),
                "different-schema export returned a candidate");
        require(storage.snapshot() == schema_before,
                "different-schema export changed live storage");
    }

    auto stale_storage = JournalTestStorage::seeded(
        frame(chain.genesis),
        std::string(chain.genesis_root.canonical_bytes()));
    auto stale = recover(stale_storage, chain.genesis_root.canonical_bytes());
    auto winner = recover(stale_storage, chain.genesis_root.canonical_bytes());
    auto winning_journal = stale_storage.make_journal(generous_limits());
    auto advanced =
        winning_journal.append_and_publish(std::move(winner), chain.successor);
    require_result(advanced, DurableJournalStatus::Published,
                   "export stale-authority fixture did not advance");
    stale_storage.reset_observations();
    const auto stale_before = stale_storage.snapshot();
    auto stale_journal = stale_storage.make_journal(generous_limits());
    auto stale_export = stale_journal.export_exact_schema_candidate(
        stale, supported_journal_schema, JournalQuiescence::Confirmed);
    require(stale_export.status ==
                    ExactSchemaExportStatus::ConflictBeforeRead &&
                !stale_export.exported() &&
                !stale_export.candidate.has_value() && stale.available() &&
                stale_storage.attempt_count(FaultOperation::RootRead) > 0 &&
                stale_storage.attempt_count(FaultOperation::JournalOpen) == 0 &&
                stale_storage.attempt_count(FaultOperation::JournalRead) == 0 &&
                !stale_storage.snapshot().authority_mutation_attempted &&
                stale_storage.snapshot().all_authority_io_under_lock,
            "stale authority exported a candidate");
    require(same_persistent_state(stale_storage.snapshot(), stale_before),
            "stale export changed authority storage");
    require_storage_released_after_return(
        stale_storage, "stale export retained an authority handle");

    auto unsupported_storage = JournalTestStorage::seeded(
        committed, std::string(chain.successor_root.canonical_bytes()));
    auto unsupported_authority =
        recover(unsupported_storage, chain.genesis_root.canonical_bytes());
    unsupported_storage.arm_fault(FaultOperation::RootOpen,
                                  FaultPosition::Before, FaultAction::Error);
    const auto unsupported_before = unsupported_storage.snapshot();
    auto unsupported_journal =
        unsupported_storage.make_journal(generous_limits());
    auto unsupported_export = unsupported_journal.export_exact_schema_candidate(
        unsupported_authority, supported_journal_schema,
        JournalQuiescence::Confirmed);
    require(unsupported_export.status ==
                    ExactSchemaExportStatus::UnsupportedStorage &&
                !unsupported_export.exported() &&
                !unsupported_export.candidate.has_value(),
            "export storage failure returned a candidate");
    require(unsupported_storage.snapshot().journal_bytes ==
                    unsupported_before.journal_bytes &&
                unsupported_storage.snapshot().authority_root_bytes ==
                    unsupported_before.authority_root_bytes,
            "unsupported export changed authority storage");

    auto limited_storage = JournalTestStorage::seeded(
        committed, std::string(chain.successor_root.canonical_bytes()));
    auto limited_authority =
        recover(limited_storage, chain.genesis_root.canonical_bytes());
    auto export_limits = generous_limits();
    export_limits.max_committed_bytes = committed.size() - 1;
    const auto limited_before = limited_storage.snapshot();
    auto limited_journal = limited_storage.make_journal(export_limits);
    auto limited_export = limited_journal.export_exact_schema_candidate(
        limited_authority, supported_journal_schema,
        JournalQuiescence::Confirmed);
    require(limited_export.status == ExactSchemaExportStatus::LimitExceeded &&
                !limited_export.exported() &&
                !limited_export.candidate.has_value(),
            "over-limit export returned a candidate");
    require(limited_storage.snapshot() == limited_before,
            "over-limit export changed authority storage");

    auto corrupt_storage = JournalTestStorage::seeded(
        committed, std::string(chain.successor_root.canonical_bytes()));
    auto corrupt_authority =
        recover(corrupt_storage, chain.genesis_root.canonical_bytes());
    corrupt_storage.overwrite_fixed_child_bytes(FixedAuthorityChild::Root, "{");
    corrupt_storage.reset_observations();
    const auto corrupt_before = corrupt_storage.snapshot();
    auto corrupt_journal = corrupt_storage.make_journal(generous_limits());
    auto corrupt_export = corrupt_journal.export_exact_schema_candidate(
        corrupt_authority, supported_journal_schema,
        JournalQuiescence::Confirmed);
    require(
        corrupt_export.status == ExactSchemaExportStatus::CorruptOrRollback &&
            !corrupt_export.exported() &&
            !corrupt_export.candidate.has_value() &&
            corrupt_authority.available() &&
            same_persistent_state(corrupt_storage.snapshot(), corrupt_before) &&
            !corrupt_storage.snapshot().authority_mutation_attempted,
        "corrupt stored authority exported or triggered mutation");
}

void require_unsupported_preflight() {
    const auto chain = make_chain();
    for (const auto platform :
         {PlatformContract::Linux, PlatformContract::Windows,
          PlatformContract::MacOS}) {
        for (const auto capability :
             {DurabilityCapability::RegularFileFlush,
              DurabilityCapability::TailTruncateAndFlush,
              DurabilityCapability::SameDirectoryReplace,
              DurabilityCapability::NamespaceDurability}) {
            auto storage = JournalTestStorage::fresh(platform);
            storage.make_capability_unavailable(capability);
            const auto before = storage.snapshot();
            auto journal = storage.make_journal(generous_limits());
            auto result = journal.create_new(
                TrustedReplayFloor::uninitialized(), chain.genesis);
            require_result(
                result, DurableJournalStatus::UnsupportedStorage,
                "unavailable native durability did not refuse publication");
            const auto after = storage.snapshot();
            require(authority_files_unchanged(before, after) &&
                        !after.authority_mutation_attempted &&
                        after.open_handles.empty(),
                    "unsupported adapter changed authority during create "
                    "preflight");

            auto existing = JournalTestStorage::seeded(
                frame(chain.genesis),
                std::string(chain.genesis_root.canonical_bytes()), platform);
            existing.make_capability_unavailable(capability);
            const auto existing_before = existing.snapshot();
            auto existing_journal = existing.make_journal(generous_limits());
            auto existing_result = existing_journal.recover_existing(
                TrustedReplayFloor::exact_root(
                    std::string(chain.genesis_root.canonical_bytes())));
            require_result(
                existing_result, DurableJournalStatus::UnsupportedStorage,
                "unavailable native durability did not refuse recovery");
            const auto existing_after = existing.snapshot();
            require(
                authority_files_unchanged(existing_before, existing_after) &&
                    !existing_after.authority_mutation_attempted &&
                    existing_after.open_handles.empty(),
                "unsupported adapter changed authority during recovery "
                "preflight");
        }

        for (const auto child :
             {FixedAuthorityChild::Journal, FixedAuthorityChild::Root,
              FixedAuthorityChild::Lock, FixedAuthorityChild::JournalStage,
              FixedAuthorityChild::RootStage}) {
            for (const auto kind : {UnsupportedEntryKind::Symlink,
                                    UnsupportedEntryKind::NonRegular,
                                    UnsupportedEntryKind::ReparsePoint}) {
                auto storage = JournalTestStorage::fresh(platform);
                storage.poison_fixed_child(child, kind);
                const auto before = storage.snapshot();
                auto journal = storage.make_journal(generous_limits());
                auto result = journal.create_new(
                    TrustedReplayFloor::uninitialized(), chain.genesis);
                require_result(result, DurableJournalStatus::UnsupportedStorage,
                               "unsafe fixed child was accepted");
                const auto after = storage.snapshot();
                require(authority_files_unchanged(before, after) &&
                            !after.authority_mutation_attempted &&
                            after.open_handles.empty(),
                        "unsafe fixed-child preflight changed authority");
                if (child != FixedAuthorityChild::Lock) {
                    require(storage.attempt_count(
                                FaultOperation::PreflightProbe) > 0 &&
                                storage.attempt_count(
                                    FaultOperation::RootOpen) == 0 &&
                                storage.attempt_count(
                                    FaultOperation::JournalOpen) == 0,
                            "unsafe fixed child was discovered after authority "
                            "I/O");
                }
            }
        }

        auto missing =
            JournalTestStorage::missing_authority_directory(platform);
        const auto missing_before = missing.snapshot();
        auto missing_journal = missing.make_journal(generous_limits());
        auto missing_result = missing_journal.create_new(
            TrustedReplayFloor::uninitialized(), chain.genesis);
        require_result(missing_result, DurableJournalStatus::UnsupportedStorage,
                       "missing authority directory was created implicitly");
        require(same_persistent_state(missing.snapshot(), missing_before),
                "missing authority directory changed its parent");
    }
}

std::filesystem::path extended_length_path(const std::filesystem::path &path) {
#ifdef _WIN32
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    const auto native = absolute.native();
    if (native.rfind(L"\\\\?\\", 0) == 0) {
        return absolute;
    }
    if (native.rfind(L"\\\\", 0) == 0) {
        return std::filesystem::path(L"\\\\?\\UNC\\" + native.substr(2));
    }
    return std::filesystem::path(L"\\\\?\\" + native);
#else
    return path;
#endif
}

class NativeDirectory {
public:
    explicit NativeDirectory(std::string_view label) {
        const auto nonce =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                (std::string("lemonade-") + std::string(label) + "-" +
                 std::to_string(nonce));
        std::filesystem::create_directories(path_);
    }

    ~NativeDirectory() {
        std::error_code error;
        std::filesystem::remove_all(extended_length_path(path_), error);
    }

    NativeDirectory(const NativeDirectory &) = delete;
    NativeDirectory &operator=(const NativeDirectory &) = delete;

    const std::filesystem::path &path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

class ScopedCurrentPath {
public:
    ScopedCurrentPath() : lock_(mutex()), original_(std::filesystem::current_path()) {}

    ~ScopedCurrentPath() {
        std::error_code error;
        std::filesystem::current_path(original_, error);
        require(!error, "test current directory could not be restored");
    }

    ScopedCurrentPath(const ScopedCurrentPath &) = delete;
    ScopedCurrentPath &operator=(const ScopedCurrentPath &) = delete;

    void change_to(const std::filesystem::path &path) {
        std::filesystem::current_path(path);
    }

private:
    static std::mutex &mutex() {
        static std::mutex value;
        return value;
    }

    std::unique_lock<std::mutex> lock_;
    std::filesystem::path original_;
};

std::string read_bytes(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
}

void write_bytes(const std::filesystem::path &path, std::string_view bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(stream.good(), "test control file write failed");
}

void append_bytes(const std::filesystem::path &path, std::string_view bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::app);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    require(stream.good(), "test control file append failed");
}

std::map<std::string, std::string>
snapshot_regular_files(const std::filesystem::path &root) {
    std::map<std::string, std::string> snapshot;
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            snapshot.emplace(
                entry.path().lexically_relative(root).generic_string(),
                read_bytes(entry.path()));
        }
    }
    return snapshot;
}

PublishedJournal create_native(const std::filesystem::path &directory,
                               const Chain &chain) {
    auto journal = DurableJournal::native(directory, generous_limits());
    auto result =
        journal.create_new(TrustedReplayFloor::uninitialized(), chain.genesis);
    require_result(result, DurableJournalStatus::Published,
                   "native create did not publish authority");
    return std::move(*result.journal);
}

PublishedJournal recover_native(const std::filesystem::path &directory,
                                std::string_view floor) {
    auto journal = DurableJournal::native(directory, generous_limits());
    auto result = journal.recover_existing(
        TrustedReplayFloor::exact_root(std::string(floor)));
    require_result(result, DurableJournalStatus::Published,
                   "native recovery did not return authority");
    return std::move(*result.journal);
}

void require_native_preflight_identity_phase_residue() {
    const auto chain = make_chain();
    using lemon::residency::detail::DurablePreflightTestFault;
    for (const auto fault :
         {DurablePreflightTestFault::ProbeIdentityCaptureFailureOnce,
          DurablePreflightTestFault::StageIdentityCaptureFailureOnce}) {
        NativeDirectory directory("task020-native-preflight-phase-failure");
        auto adapter = lemon::residency::detail::
            make_platform_durable_file_adapter_for_test(directory.path(),
                                                        fault);
        auto journal = lemon::residency::detail::make_durable_journal_for_test(
            std::move(adapter), generous_limits());

        auto result = journal.create_new(TrustedReplayFloor::uninitialized(),
                                         chain.genesis);
        require_result(result, DurableJournalStatus::RecoveryRequired,
                       "post-create identity failure was not effect-uncertain");
        require(!result.published() && !result.journal.has_value(),
                "post-create identity failure returned authority");

        const auto after_failure = snapshot_regular_files(directory.path());
        const auto anonymous_residue =
            std::find_if(after_failure.begin(), after_failure.end(),
                         [](const auto &entry) {
                             return entry.first != "authority.lock" &&
                                    entry.second.empty();
                         });
        const bool stage_residue =
            anonymous_residue != after_failure.end() &&
            std::filesystem::path(anonymous_residue->first).extension() ==
                ".stage";
        const bool expected_stage_residue =
            fault ==
            DurablePreflightTestFault::StageIdentityCaptureFailureOnce;
        require(after_failure.size() == 2 &&
                    anonymous_residue != after_failure.end() &&
                    stage_residue == expected_stage_residue &&
                    after_failure.count("authority.lock") == 1,
                "post-create identity failure changed anonymous residue");
        const auto uncertain_residue_path = anonymous_residue->first;
        const auto uncertain_residue_bytes = anonymous_residue->second;

        auto reconciler = DurableJournal::native(directory.path(),
                                                 generous_limits());
        auto reconciled = reconciler.create_new(
            TrustedReplayFloor::uninitialized(), chain.genesis);
        require_result(reconciled, DurableJournalStatus::Published,
                       "locked create did not reconcile a fresh fence");
        const auto published = snapshot_regular_files(directory.path());
        const auto retained_residue =
            published.find(uncertain_residue_path);
        require(published.size() == 4 &&
                    published.at("journal.jsonl") == frame(chain.genesis) &&
                    published.at("authority-root.json") ==
                        chain.genesis_root.canonical_bytes() &&
                    published.count("authority.lock") == 1 &&
                    retained_residue != published.end() &&
                    retained_residue->second == uncertain_residue_bytes,
                "fresh fence reconciliation changed authority or residue");
    }
}

void require_native_preflight_fence_blocks_nonfresh_authority() {
    const auto chain = make_chain();
    NativeDirectory directory("task020-native-preflight-nonfresh-fence");
    auto peer = create_native(directory.path(), chain);
    const auto stable = snapshot_regular_files(directory.path());
    auto adapter = lemon::residency::detail::
        make_platform_durable_file_adapter_for_test(
            directory.path(), lemon::residency::detail::
                                  DurablePreflightTestFault::
                                      ProbeIdentityCaptureFailureOnce);
    auto journal = lemon::residency::detail::make_durable_journal_for_test(
        std::move(adapter), generous_limits());

    auto result = journal.recover_existing(TrustedReplayFloor::exact_root(
        std::string(chain.genesis_root.canonical_bytes())));
    require_result(result, DurableJournalStatus::RecoveryRequired,
                   "post-create identity failure was not effect-uncertain");
    require(!result.published() && !result.journal.has_value(),
            "post-create identity failure returned authority");

    const auto after_failure = snapshot_regular_files(directory.path());
    const auto uncertain_entries = static_cast<std::size_t>(std::count_if(
        after_failure.begin(), after_failure.end(), [&](const auto &entry) {
            return stable.count(entry.first) == 0 && entry.second.empty();
        }));
    require(after_failure.size() == stable.size() + 1 &&
                uncertain_entries == 1 &&
                after_failure.at("journal.jsonl") == frame(chain.genesis) &&
                after_failure.at("authority-root.json") ==
                    chain.genesis_root.canonical_bytes(),
            "post-create identity failure did not preserve uncertain residue");

    auto blocked_create = DurableJournal::native(directory.path(),
                                                 generous_limits());
    auto create_result = blocked_create.create_new(
        TrustedReplayFloor::uninitialized(), chain.genesis);
    require_result(create_result, DurableJournalStatus::RecoveryRequired,
                   "fenced existing authority admitted a fresh create");
    require(snapshot_regular_files(directory.path()) == after_failure,
            "fenced create changed existing authority");

    auto blocked_writer = DurableJournal::native(directory.path(),
                                                 generous_limits());
    auto blocked = blocked_writer.append_and_publish(std::move(peer),
                                                     chain.successor);
    require_result(blocked, DurableJournalStatus::RecoveryRequired,
                   "post-create identity failure did not fence peer writes");
    require(!peer.available() &&
                snapshot_regular_files(directory.path()) == after_failure,
            "preflight identity fence changed authority");

    auto recovered =
        recover_native(directory.path(), chain.genesis_root.canonical_bytes());
    auto continuation = DurableJournal::native(directory.path(),
                                               generous_limits());
    auto continued = continuation.append_and_publish(std::move(recovered),
                                                     chain.successor);
    require_result(continued, DurableJournalStatus::Published,
                   "locked recovery did not clear the preflight fence");
}

void require_native_preflight_preserves_siblings() {
    const auto chain = make_chain();
    NativeDirectory directory("task020-native-preflight-siblings");

    write_bytes(directory.path() / ".durability-probe", "legacy-primary");
    write_bytes(directory.path() / ".durability-probe.stage", "legacy-stage");

    static_cast<void>(create_native(directory.path(), chain));

    const auto files = snapshot_regular_files(directory.path());
    require(files.size() == 5 &&
                files.at(".durability-probe") == "legacy-primary" &&
                files.at(".durability-probe.stage") == "legacy-stage" &&
                files.at("journal.jsonl") == frame(chain.genesis) &&
                files.at("authority-root.json") ==
                    chain.genesis_root.canonical_bytes() &&
                files.count("authority.lock") == 1,
            "native preflight changed sibling files or left residue");
}

void require_native_preflight_stage_collision_exhaustion_is_fenced() {
    const auto chain = make_chain();
    NativeDirectory directory("task020-native-preflight-stage-collisions");
    auto adapter = lemon::residency::detail::
        make_platform_durable_file_adapter_for_test(
            directory.path(), lemon::residency::detail::
                                  DurablePreflightTestFault::
                                      StageCreateCollisionExhaustion);
    auto journal = lemon::residency::detail::make_durable_journal_for_test(
        std::move(adapter), generous_limits());

    auto result = journal.create_new(TrustedReplayFloor::uninitialized(),
                                     chain.genesis);
    require_result(result, DurableJournalStatus::RecoveryRequired,
                   "exhausted stage collisions were not effect-uncertain");
    require(!result.published() && !result.journal.has_value(),
            "exhausted stage collisions returned authority");

    const auto after_failure = snapshot_regular_files(directory.path());
    std::map<std::string, std::string> collision_sentinels;
    for (const auto &[name, bytes] : after_failure) {
        if (name != "authority.lock") {
            collision_sentinels.emplace(name, bytes);
        }
    }
    require(after_failure.size() == collision_sentinels.size() + 1 &&
                after_failure.count("authority.lock") == 1 &&
                !collision_sentinels.empty() &&
                std::all_of(collision_sentinels.begin(),
                            collision_sentinels.end(), [](const auto &entry) {
                                return entry.second ==
                                       "caller-owned-stage-collision";
                            }),
            "stage collision exhaustion changed sentinels or left residue");

    auto reconciler = DurableJournal::native(directory.path(),
                                             generous_limits());
    auto reconciled = reconciler.create_new(
        TrustedReplayFloor::uninitialized(), chain.genesis);
    require_result(reconciled, DurableJournalStatus::Published,
                   "locked create did not reconcile the collision fence");
    const auto published = snapshot_regular_files(directory.path());
    require(published.size() == collision_sentinels.size() + 3 &&
                published.at("journal.jsonl") == frame(chain.genesis) &&
                published.at("authority-root.json") ==
                    chain.genesis_root.canonical_bytes() &&
                published.count("authority.lock") == 1,
            "collision fence reconciliation did not publish authority");
    for (const auto &[name, bytes] : collision_sentinels) {
        const auto preserved = published.find(name);
        require(preserved != published.end() && preserved->second == bytes,
                "collision fence reconciliation changed a sentinel");
    }
}

void require_native_relative_directory_binding() {
    const auto chain = make_chain();
    NativeDirectory parent("task020-relative-directory-binding");
    const auto original_parent = parent.path() / "original";
    const auto decoy_parent = parent.path() / "decoy";
    const auto original_authority = original_parent / "authority";
    const auto decoy_authority = decoy_parent / "authority";
    std::filesystem::create_directories(original_authority);
    std::filesystem::create_directories(decoy_authority);
    write_bytes(original_parent / "sentinel.bin", "original-sentinel");
    write_bytes(decoy_parent / "sentinel.bin", "decoy-sentinel");

    {
        ScopedCurrentPath current_path;
        current_path.change_to(original_parent);
        auto journal =
            DurableJournal::native("authority", generous_limits());
        current_path.change_to(decoy_parent);
        auto result = journal.create_new(TrustedReplayFloor::uninitialized(),
                                         chain.genesis);
        require_result(result, DurableJournalStatus::Published,
                       "relative native authority did not publish");
    }

    const auto original_files = snapshot_regular_files(original_authority);
    require(original_files.size() == 3 &&
                original_files.at("journal.jsonl") == frame(chain.genesis) &&
                original_files.at("authority-root.json") ==
                    chain.genesis_root.canonical_bytes() &&
                original_files.count("authority.lock") == 1,
            "relative native authority drifted from its opened directory");
    require(snapshot_regular_files(decoy_authority).empty() &&
                read_bytes(original_parent / "sentinel.bin") ==
                    "original-sentinel" &&
                read_bytes(decoy_parent / "sentinel.bin") ==
                    "decoy-sentinel",
            "current-directory drift redirected native authority I/O");
}

void require_native_unbound_adapter_preserves_working_directory() {
    NativeDirectory parent("task020-native-unbound-adapter");
    const auto blocking_file = parent.path() / "not-a-directory";
    const auto working_directory = parent.path() / "working-directory";
    write_bytes(blocking_file, "blocking-file");
    std::filesystem::create_directories(working_directory);
    write_bytes(working_directory / "journal.jsonl", "working-journal");
    write_bytes(working_directory / "authority-root.json", "working-root");
    write_bytes(working_directory / "journal.jsonl.stage",
                "working-journal-stage");
    write_bytes(working_directory / "authority-root.json.stage",
                "working-root-stage");
    write_bytes(working_directory / "authority.lock", "working-lock");
    const auto before = snapshot_regular_files(working_directory);

    auto adapter =
        lemon::residency::detail::make_platform_durable_file_adapter(
            blocking_file / "authority");
    {
        ScopedCurrentPath current_path;
        current_path.change_to(working_directory);
        const auto inspected = adapter->inspect_fixed_namespace();
        const auto root = adapter->read_root(1024);
        const auto journal = adapter->read_journal(1024);
        const std::string object_digest(64, 'a');
        const auto object =
            adapter->read_immutable_object(object_digest, 1024);
        const auto created_object =
            adapter->create_immutable_object(object_digest, "object");
        const auto created = adapter->create_journal("created");
        const auto appended = adapter->append_journal("appended");
        const auto truncated = adapter->truncate_journal(0);
        const auto replaced_journal = adapter->replace_journal("replacement");
        const auto replaced_root = adapter->replace_root("replacement");
        require(!inspected.result.succeeded() && !root.result.succeeded() &&
                    !journal.result.succeeded() &&
                    !object.result.succeeded() &&
                    !created_object.succeeded() && !created.succeeded() &&
                    !appended.succeeded() && !truncated.succeeded() &&
                    !replaced_journal.succeeded() &&
                    !replaced_root.succeeded(),
                "unbound native adapter accepted a path operation");
    }
    require(snapshot_regular_files(working_directory) == before,
            "unbound native adapter changed the working directory");
}

void require_native_immutable_objects() {
    using lemon::residency::detail::DurableFileStatus;
    using lemon::residency::detail::durable_immutable_object_filename;
    using lemon::residency::detail::durable_immutable_object_stage_filename;
    using lemon::residency::detail::make_platform_durable_file_adapter;

    NativeDirectory directory("task119-native-immutable-objects");
    auto adapter = make_platform_durable_file_adapter(directory.path());
    require(adapter->lock_authority().succeeded(),
            "native immutable-object authority lock failed");
    require(adapter->preflight_capabilities().succeeded(),
            "native immutable-object durability preflight failed");

    const std::string digest(64, 'a');
    const std::string bytes = R"({"overlay":"alpha"})";
    require(adapter->create_immutable_object(digest, bytes).succeeded(),
            "native immutable-object create failed");
    const auto exact = adapter->read_immutable_object(digest, bytes.size());
    require(exact.result.succeeded() && exact.bytes == bytes &&
                !exact.truncated,
            "native immutable-object exact read failed");
    const auto bounded =
        adapter->read_immutable_object(digest, bytes.size() - 1);
    require(bounded.result.succeeded() && bounded.truncated &&
                bounded.bytes.size() == bytes.size() - 1,
            "native immutable-object bounded read failed");
    require(adapter->create_immutable_object(digest, "different").status ==
                DurableFileStatus::AlreadyExists,
            "native immutable-object create replaced an existing digest");
    auto slash_digest = std::string(64, 'a');
    slash_digest[7] = '/';
    auto backslash_digest = std::string(64, 'a');
    backslash_digest[7] = '\\';
    auto colon_digest = std::string(64, 'a');
    colon_digest[7] = ':';
    auto nul_digest = std::string(64, 'a');
    nul_digest[7] = '\0';
    for (const auto &invalid_digest :
         {std::string(64, 'A'), slash_digest, backslash_digest, colon_digest,
          nul_digest, std::string(63, 'a'), std::string(65, 'a')}) {
        require(adapter->create_immutable_object(invalid_digest, bytes)
                    .status == DurableFileStatus::Unsupported,
                "native immutable-object create accepted a noncanonical "
                "digest");
        require(adapter->read_immutable_object(invalid_digest, 1024)
                    .result.status == DurableFileStatus::Unsupported,
                "native immutable-object read accepted a noncanonical "
                "digest");
    }
    require(adapter->read_immutable_object(std::string(64, 'd'), 1024)
                .result.status == DurableFileStatus::NotFound,
            "native immutable-object missing read was not classified");

    const std::string abandoned_digest(64, 'b');
    const auto abandoned_name =
        durable_immutable_object_filename(abandoned_digest);
    const auto abandoned_stage_name =
        durable_immutable_object_stage_filename(abandoned_digest);
    require(abandoned_name.has_value() && abandoned_stage_name.has_value(),
            "native immutable-object fixture digest was rejected");
    write_bytes(directory.path() / *abandoned_stage_name, "partial");
    const std::string recovered_bytes = R"({"overlay":"recovered"})";
    require(adapter
                ->create_immutable_object(abandoned_digest, recovered_bytes)
                .succeeded(),
            "native immutable-object abandoned stage was not recovered");
    require(!std::filesystem::exists(directory.path() /
                                     *abandoned_stage_name) &&
                read_bytes(directory.path() / *abandoned_name) ==
                    recovered_bytes,
            "native immutable-object recovery exposed partial stage bytes");

#ifndef _WIN32
    const std::string linked_digest(64, 'c');
    const auto linked_name = durable_immutable_object_filename(linked_digest);
    const auto linked_stage_name =
        durable_immutable_object_stage_filename(linked_digest);
    require(linked_name.has_value() && linked_stage_name.has_value(),
            "native linked-object fixture digest was rejected");
    const std::string linked_bytes = R"({"overlay":"linked"})";
    write_bytes(directory.path() / *linked_stage_name, linked_bytes);
    std::error_code link_error;
    std::filesystem::create_hard_link(directory.path() / *linked_stage_name,
                                      directory.path() / *linked_name,
                                      link_error);
    require(!link_error,
            "native immutable-object linked-stage fixture failed");
    require(adapter->create_immutable_object(linked_digest, linked_bytes)
                .succeeded(),
            "native immutable-object linked publish was not recovered");
    require(!std::filesystem::exists(directory.path() / *linked_stage_name) &&
                read_bytes(directory.path() / *linked_name) == linked_bytes,
            "native immutable-object linked recovery changed object bytes");
#endif

    require(adapter->unlock_authority().succeeded(),
            "native immutable-object authority unlock failed");
}

constexpr bool should_block_fixed_namespace_publish_attempt(
    std::size_t, bool moved) {
    return !moved;
}

#ifdef _WIN32
class FixedNamespacePublishBarrier final
    : public lemon::residency::detail::DurableFixedNamespaceConvergenceProbe {
public:
    void creator_started() {
        std::lock_guard lock(mutex_);
        ++started_creators_;
        condition_.notify_all();
    }

    void after_publish_attempt(std::size_t attempt, bool moved) override {
        const bool block =
            should_block_fixed_namespace_publish_attempt(attempt, moved);
        std::unique_lock lock(mutex_);
        ++publish_attempts_;
        condition_.notify_all();
        if (!block) {
            return;
        }
        condition_.wait(lock, [&] { return released_; });
    }

    bool wait_for_publish_attempts(std::size_t expected_creators,
                                   std::size_t expected_attempts,
                                   std::uint64_t timeout_milliseconds) {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(
            lock, std::chrono::milliseconds(timeout_milliseconds),
            [&] {
                return started_creators_ >= expected_creators &&
                       publish_attempts_ >= expected_attempts;
            });
    }

    void release() {
        std::lock_guard lock(mutex_);
        released_ = true;
        condition_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::size_t started_creators_ = 0;
    std::size_t publish_attempts_ = 0;
    bool released_ = false;
};
#endif

void require_fixed_namespace_publish_barrier_tracks_retries() {
    require(should_block_fixed_namespace_publish_attempt(2, false) &&
                should_block_fixed_namespace_publish_attempt(7, false) &&
                !should_block_fixed_namespace_publish_attempt(3, true),
            "fixed-namespace publish barrier depends on the retry ordinal");
}

void require_native_fixed_namespace_factory() {
    using lemon::residency::detail::make_platform_durable_file_adapter_in_fixed_namespace;

    constexpr std::string_view child_name = "residency-local-overlay";
    NativeDirectory parent("task119-fixed-namespace");
    const auto child = parent.path() / child_name;

    auto created = make_platform_durable_file_adapter_in_fixed_namespace(
        parent.path(), child_name);
    require(std::filesystem::is_directory(child),
            "native fixed-namespace factory did not create its child");
    require(created->lock_authority().succeeded() &&
                created->preflight_capabilities().succeeded(),
            "native fixed-namespace factory did not bind its created child");
    const auto created_identity = created->authority_identity();
    require(created_identity.result.succeeded(),
            "native created child did not expose its bound identity");
    require(created->unlock_authority().succeeded(),
            "native created-child authority unlock failed");

    auto reused = make_platform_durable_file_adapter_in_fixed_namespace(
        parent.path(), child_name);
    require(reused->lock_authority().succeeded() &&
                reused->preflight_capabilities().succeeded(),
            "native fixed-namespace factory did not reuse its child");
    const auto reused_identity = reused->authority_identity();
    require(reused_identity.result.succeeded() &&
                reused_identity.identity == created_identity.identity,
            "native fixed-namespace reuse rebound a different authority");
    require(reused->unlock_authority().succeeded(),
            "native reused-child authority unlock failed");

    NativeDirectory concurrent_parent("task119-fixed-namespace-race");
    std::vector<std::unique_ptr<
        lemon::residency::detail::DurableFileAdapter>>
        concurrent_adapters(8);
    std::vector<std::thread> creators;
    for (std::size_t index = 0; index < concurrent_adapters.size(); ++index) {
        creators.emplace_back([&, index] {
            concurrent_adapters[index] =
                make_platform_durable_file_adapter_in_fixed_namespace(
                    concurrent_parent.path(), child_name);
        });
    }
    for (auto &creator : creators) {
        creator.join();
    }
    std::optional<std::string> concurrent_identity;
    for (auto &adapter : concurrent_adapters) {
        require(adapter->lock_authority().succeeded() &&
                    adapter->preflight_capabilities().succeeded(),
                "concurrent native fixed-namespace creator was not bound");
        const auto identity = adapter->authority_identity();
        require(identity.result.succeeded(),
                "concurrent native fixed namespace had no identity");
        if (!concurrent_identity.has_value()) {
            concurrent_identity = identity.identity;
        }
        require(identity.identity == *concurrent_identity,
                "concurrent native fixed-namespace creators diverged");
        require(adapter->unlock_authority().succeeded(),
                "concurrent native fixed-namespace unlock failed");
    }

#ifdef _WIN32
    DWORD convergence_handles_before = 0;
    DWORD convergence_handles_after = 0;
    require(::GetProcessHandleCount(::GetCurrentProcess(),
                                    &convergence_handles_before) != 0,
            "Windows convergence handle count was unavailable");
    {
        NativeDirectory convergence_parent(
            "task119-fixed-namespace-held-stage-race");
        const auto convergence_stage =
            convergence_parent.path() /
            ".residency-local-overlay.directory-stage";
        require(::CreateDirectoryW(convergence_stage.c_str(), nullptr) != 0,
                "Windows convergence stage could not be created");
        const auto held_stage = ::CreateFileW(
            convergence_stage.c_str(), FILE_READ_ATTRIBUTES | SYNCHRONIZE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        require(held_stage != INVALID_HANDLE_VALUE,
                "Windows convergence stage could not be held against rename");

        FixedNamespacePublishBarrier barrier;
        std::vector<std::unique_ptr<
            lemon::residency::detail::DurableFileAdapter>>
            converged_adapters(2);
        std::vector<std::thread> converging_creators;
        for (std::size_t index = 0; index < converged_adapters.size(); ++index) {
            converging_creators.emplace_back([&, index] {
                barrier.creator_started();
                converged_adapters[index] = lemon::residency::detail::
                    make_platform_durable_file_adapter_in_fixed_namespace_for_test(
                        convergence_parent.path(), child_name, barrier);
            });
        }
        require(
            barrier.wait_for_publish_attempts(converged_adapters.size(), 1, 5000),
            "Windows concurrent creators did not reach a held-stage "
            "publication attempt after both creators started");
        require(::CloseHandle(held_stage) != 0,
                "Windows convergence stage handle did not close");
        barrier.release();
        for (auto &creator : converging_creators) {
            creator.join();
        }

        std::optional<std::string> winning_identity;
        for (auto &adapter : converged_adapters) {
            require(adapter != nullptr,
                    "Windows held-stage creator returned no adapter");
            require(adapter->lock_authority().succeeded() &&
                        adapter->preflight_capabilities().succeeded(),
                    "Windows held-stage creator did not converge on the "
                    "winning child");
            const auto identity = adapter->authority_identity();
            require(identity.result.succeeded(),
                    "Windows converged child had no identity");
            if (!winning_identity.has_value()) {
                winning_identity = identity.identity;
            }
            require(identity.identity == *winning_identity,
                    "Windows held-stage creators bound different children");
            require(adapter->unlock_authority().succeeded(),
                    "Windows converged-child authority unlock failed");
        }
    }
    require(::GetProcessHandleCount(::GetCurrentProcess(),
                                    &convergence_handles_after) != 0 &&
                convergence_handles_after <= convergence_handles_before + 8,
            "Windows held-stage convergence leaked bound handles");
#endif

    for (const auto invalid : {std::string_view{}, std::string_view{"."},
                               std::string_view{".."},
                               std::string_view{"nested/child"},
                               std::string_view{"Nested"}}) {
        auto rejected = make_platform_durable_file_adapter_in_fixed_namespace(
            parent.path(), invalid);
        require(!rejected->lock_authority().succeeded(),
                "native fixed-namespace factory accepted an unsafe name");
    }

    NativeDirectory poisoned_parent("task119-fixed-namespace-poison");
    const auto poisoned_child = poisoned_parent.path() / child_name;
    write_bytes(poisoned_child, "caller-owned");
    auto poisoned = make_platform_durable_file_adapter_in_fixed_namespace(
        poisoned_parent.path(), child_name);
    require(!poisoned->lock_authority().succeeded() &&
                read_bytes(poisoned_child) == "caller-owned",
            "native fixed-namespace factory changed an unsafe child");

#ifndef _WIN32
    NativeDirectory linked_parent("task119-fixed-namespace-link");
    NativeDirectory outside("task119-fixed-namespace-outside");
    const auto linked_child = linked_parent.path() / child_name;
    std::error_code link_error;
    std::filesystem::create_directory_symlink(outside.path(), linked_child,
                                              link_error);
    require(!link_error,
            "native fixed-namespace symlink fixture could not be created");
    auto linked = make_platform_durable_file_adapter_in_fixed_namespace(
        linked_parent.path(), child_name);
    require(!linked->lock_authority().succeeded() &&
                std::filesystem::is_symlink(
                    std::filesystem::symlink_status(linked_child)),
            "native fixed-namespace factory followed or replaced a symlink");
#else
    NativeDirectory staged_parent("task119-fixed-namespace-stage");
    const auto stage = staged_parent.path() /
                       ".residency-local-overlay.directory-stage";
    write_bytes(stage, "caller-owned-stage");
    auto staged = make_platform_durable_file_adapter_in_fixed_namespace(
        staged_parent.path(), child_name);
    require(!staged->lock_authority().succeeded() &&
                read_bytes(stage) == "caller-owned-stage" &&
                !std::filesystem::exists(staged_parent.path() / child_name),
            "native fixed-namespace factory changed an unsafe stage");

    NativeDirectory lifetime_root("task119-fixed-namespace-lifetime");
    const auto original_lineage = lifetime_root.path() / "original-lineage";
    const auto moved_lineage = lifetime_root.path() / "moved-lineage";
    const auto original_parent = original_lineage / "cache";
    std::filesystem::create_directories(original_parent);
    auto retained = make_platform_durable_file_adapter_in_fixed_namespace(
        original_parent, child_name);
    require(retained->lock_authority().succeeded(),
            "Windows fixed namespace did not acquire its initial authority");
    const auto retained_identity = retained->authority_identity();
    require(retained_identity.result.succeeded() &&
                retained->unlock_authority().succeeded(),
            "Windows fixed namespace did not expose its initial authority");

    std::error_code rename_error;
    std::filesystem::rename(original_lineage, moved_lineage, rename_error);
    require(rename_error && std::filesystem::is_directory(original_lineage) &&
                !std::filesystem::exists(moved_lineage),
            "Windows live descendant handle did not pin its ancestor");
    require(retained->lock_authority().succeeded(),
            "Windows fixed namespace lost authority after rejected rename");
    const auto unchanged_identity = retained->authority_identity();
    require(unchanged_identity.result.succeeded() &&
                unchanged_identity.identity == retained_identity.identity &&
                retained->unlock_authority().succeeded(),
            "Windows rejected rename changed the fixed namespace lineage");

    NativeDirectory missing_child_stage_parent(
        "task119-fixed-namespace-stage-handle-missing-child");
    const auto missing_child_stage =
        missing_child_stage_parent.path() /
        ".residency-local-overlay.directory-stage";
    std::filesystem::create_directory(missing_child_stage);
    write_bytes(missing_child_stage / "caller-owned", "sentinel");

    NativeDirectory existing_child_stage_parent(
        "task119-fixed-namespace-stage-handle-existing-child");
    std::filesystem::create_directory(existing_child_stage_parent.path() /
                                      child_name);
    const auto existing_child_stage =
        existing_child_stage_parent.path() /
        ".residency-local-overlay.directory-stage";
    std::filesystem::create_directory(existing_child_stage);
    write_bytes(existing_child_stage / "caller-owned", "sentinel");

    DWORD handles_before = 0;
    DWORD handles_after = 0;
    require(::GetProcessHandleCount(::GetCurrentProcess(), &handles_before) != 0,
            "Windows process handle count was unavailable");
    for (int attempt = 0; attempt < 32; ++attempt) {
        auto missing_child_rejected =
            make_platform_durable_file_adapter_in_fixed_namespace(
                missing_child_stage_parent.path(), child_name);
        auto existing_child_rejected =
            make_platform_durable_file_adapter_in_fixed_namespace(
                existing_child_stage_parent.path(), child_name);
        require(!missing_child_rejected->lock_authority().succeeded() &&
                    !existing_child_rejected->lock_authority().succeeded(),
                "Windows fixed namespace accepted a nonempty stage");
    }
    require(::GetProcessHandleCount(::GetCurrentProcess(), &handles_after) != 0 &&
                handles_after <= handles_before + 8,
            "Windows rejected stage directories leaked bound handles");
#endif
}

void require_native_bounded_read_boundaries() {
    const auto chain = make_chain();
    const auto committed = frame(chain.genesis);
    auto limits = generous_limits();
    limits.max_committed_bytes = committed.size();
    limits.max_crash_tail_bytes = 1;

    NativeDirectory exact("task020-native-read-exact-cap");
    static_cast<void>(create_native(exact.path(), chain));
    append_bytes(exact.path() / "journal.jsonl", "x");
    auto exact_journal = DurableJournal::native(exact.path(), limits);
    auto exact_result =
        exact_journal.recover_existing(TrustedReplayFloor::exact_root(
            std::string(chain.genesis_root.canonical_bytes())));
    require_result(exact_result, DurableJournalStatus::Published,
                   "native exact-cap journal read was rejected");
    require(read_bytes(exact.path() / "journal.jsonl") == committed &&
                read_bytes(exact.path() / "authority-root.json") ==
                    chain.genesis_root.canonical_bytes(),
            "native exact-cap recovery did not repair the exact tail");

    for (const auto &[label, tail] :
         {std::pair{std::string("task020-native-read-cap-plus-one"),
                    std::string("xy")},
          std::pair{std::string("task020-native-read-cap-plus-many"),
                    std::string(64, 'z')}}) {
        NativeDirectory over(label);
        static_cast<void>(create_native(over.path(), chain));
        append_bytes(over.path() / "journal.jsonl", tail);
        const auto before = snapshot_regular_files(over.path());
        auto journal = DurableJournal::native(over.path(), limits);
        auto result = journal.recover_existing(TrustedReplayFloor::exact_root(
            std::string(chain.genesis_root.canonical_bytes())));
        require_result(result, DurableJournalStatus::LimitExceeded,
                       "native over-cap journal read was not limited");
        require(snapshot_regular_files(over.path()) == before,
                "native over-cap read changed authority bytes");
    }
}

void require_native_literal_children() {
    const std::vector<std::string> journal_ids{
        "../escape",
        "/absolute",
        "C:\\root\\journal",
        "\\\\?\\C:\\device",
        "CON",
        "con",
        "A",
        "a",
        "%2e%2e%2fescape",
        "..\\escape",
        "aux.txt",
        "PRN",
        "COM1",
        "LPT1",
        "authority-root.json",
        "authority.lock",
        "JOURNAL.JSONL",
        "\\\\server\\share",
    };
    const std::set<std::string> expected_children{
        "authority-root.json", "authority.lock", "journal.jsonl"};
    NativeDirectory parent("task020-unicode-parent");
    write_bytes(parent.path() / "outside-sentinel.bin", "outside-authority");
    for (std::size_t index = 0; index < journal_ids.size(); ++index) {
        const auto authority_directory =
            parent.path() / ("authority-case-" + std::to_string(index));
        std::filesystem::create_directories(authority_directory);
        static_cast<void>(
            create_native(authority_directory, make_chain(journal_ids[index])));
        std::set<std::string> children;
        for (const auto &entry :
             std::filesystem::directory_iterator(authority_directory)) {
            children.insert(entry.path().filename().string());
        }
        require(children == expected_children,
                "native identity influenced the fixed child set");
    }
    const auto parent_snapshot = snapshot_regular_files(parent.path());
    require(parent_snapshot.at("outside-sentinel.bin") == "outside-authority" &&
                parent_snapshot.size() == journal_ids.size() * 3 + 1,
            "native identifier escaped or changed its selected directory");

    const auto missing_directory = parent.path() / "missing-authority";
    auto missing_journal =
        DurableJournal::native(missing_directory, generous_limits());
    auto missing_result = missing_journal.create_new(
        TrustedReplayFloor::uninitialized(), make_chain().genesis);
    require_result(missing_result, DurableJournalStatus::UnsupportedStorage,
                   "native persistence created a missing authority directory");
    require(!std::filesystem::exists(missing_directory) &&
                read_bytes(parent.path() / "outside-sentinel.bin") ==
                    "outside-authority",
            "native missing-directory refusal changed its parent");

    auto authority_directory = extended_length_path(
        parent.path() /
        std::filesystem::u8path(u8"authority-\u4F4F\u5B85-\u00E9-\U0001F680"));
    for (int index = 0; index < 7; ++index) {
        authority_directory /= std::string(48, static_cast<char>('a' + index));
    }
    std::filesystem::create_directories(authority_directory);
    const auto long_path_chain = make_chain("../C:\\PRN/authority-root.json");
    static_cast<void>(create_native(authority_directory, long_path_chain));
    std::set<std::string> children;
    for (const auto &entry :
         std::filesystem::directory_iterator(authority_directory)) {
        children.insert(entry.path().filename().string());
    }
    require(children == expected_children,
            "native long Unicode parent changed fixed child names");
    require(read_bytes(authority_directory / "journal.jsonl") ==
                frame(long_path_chain.genesis),
            "native path handling changed journal bytes");
}

std::set<std::string>
direct_child_names(const std::filesystem::path &directory) {
    std::set<std::string> names;
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        names.insert(entry.path().filename().generic_string());
    }
    return names;
}

void require_native_fresh_namespace_asymmetry() {
    const auto chain = make_chain();
    const std::vector<std::pair<std::string, std::string>> blockers{
        {"authority-root.json",
         std::string(chain.genesis_root.canonical_bytes())},
        {"journal.jsonl", frame(chain.genesis)},
        {".authority-root.json.stage", "root-stage-residue"},
        {".journal.jsonl.stage", "journal-stage-residue"},
        {"authority-root.json", ""},
        {"journal.jsonl", ""},
    };
    NativeDirectory parent("task020-native-create-asymmetry");
    write_bytes(parent.path() / "parent-sentinel.bin", "parent-sentinel");

    for (std::size_t index = 0; index < blockers.size(); ++index) {
        const auto authority_directory =
            parent.path() / ("incomplete-" + std::to_string(index));
        std::filesystem::create_directory(authority_directory);
        const auto &[name, bytes] = blockers[index];
        write_bytes(authority_directory / "authority.lock", "");
        write_bytes(authority_directory / name, bytes);
        const auto before = snapshot_regular_files(authority_directory);

        auto journal =
            DurableJournal::native(authority_directory, generous_limits());
        auto result = journal.create_new(TrustedReplayFloor::uninitialized(),
                                         chain.genesis);
        require_result(result, DurableJournalStatus::CorruptOrRollback,
                       "native create overwrote incomplete fixed namespace");
        const auto after = snapshot_regular_files(authority_directory);
        const auto preserved = after.find(name);
        require(preserved != after.end() && preserved->second == bytes &&
                    after == before,
                "native create changed incomplete fixed evidence");
        for (const auto &child : direct_child_names(authority_directory)) {
            require(child == name || child == "authority.lock",
                    "native create added authority files to an incomplete "
                    "namespace");
        }
        require(read_bytes(parent.path() / "parent-sentinel.bin") ==
                    "parent-sentinel",
                "native incomplete create changed its parent");
    }

    const auto duplicate_directory = parent.path() / "complete-duplicate";
    std::filesystem::create_directory(duplicate_directory);
    static_cast<void>(create_native(duplicate_directory, chain));
    const auto duplicate_before = snapshot_regular_files(duplicate_directory);
    auto duplicate_journal =
        DurableJournal::native(duplicate_directory, generous_limits());
    auto duplicate = duplicate_journal.create_new(
        TrustedReplayFloor::uninitialized(), chain.genesis);
    require_result(duplicate, DurableJournalStatus::ConflictBeforeWrite,
                   "native complete namespace was not a create conflict");
    require(snapshot_regular_files(duplicate_directory) == duplicate_before,
            "native duplicate create changed complete authority");

    for (const auto &[stage_name, stage_bytes] :
         {std::pair{std::string(".authority-root.json.stage"),
                    std::string("root-stage-with-complete-authority")},
          std::pair{std::string(".journal.jsonl.stage"),
                    std::string("journal-stage-with-complete-authority")}}) {
        const auto authority_directory =
            parent.path() / (stage_name == ".authority-root.json.stage"
                                 ? "complete-with-root-stage"
                                 : "complete-with-journal-stage");
        std::filesystem::create_directory(authority_directory);
        static_cast<void>(create_native(authority_directory, chain));
        write_bytes(authority_directory / stage_name, stage_bytes);
        const auto before = snapshot_regular_files(authority_directory);

        auto journal =
            DurableJournal::native(authority_directory, generous_limits());
        auto result = journal.create_new(TrustedReplayFloor::uninitialized(),
                                         chain.genesis);
        require_result(result, DurableJournalStatus::CorruptOrRollback,
                       "native stage did not override complete-create "
                       "conflict classification");
        require(snapshot_regular_files(authority_directory) == before &&
                    read_bytes(authority_directory / stage_name) == stage_bytes,
                "native create changed complete authority with stage "
                "residue");
        require(read_bytes(parent.path() / "parent-sentinel.bin") ==
                    "parent-sentinel",
                "native complete-with-stage create changed its parent");
    }

    const auto lock_only_directory = parent.path() / "lock-only";
    std::filesystem::create_directory(lock_only_directory);
    write_bytes(lock_only_directory / "authority.lock", "");
    static_cast<void>(create_native(lock_only_directory, chain));

    const auto sentinel_directory = parent.path() / "unrelated-sentinel";
    std::filesystem::create_directory(sentinel_directory);
    write_bytes(sentinel_directory / "sentinel.bin", "unrelated-sentinel");
    static_cast<void>(create_native(sentinel_directory, chain));
    require(read_bytes(sentinel_directory / "sentinel.bin") ==
                "unrelated-sentinel",
            "native create changed an unrelated sentinel");
}

bool is_explicit_symlink_fixture_unavailability(const std::error_code &error) {
#ifdef _WIN32
    if (error.category() == std::system_category() &&
        error.value() == ERROR_PRIVILEGE_NOT_HELD) {
        return true;
    }
#endif
    return error == std::errc::operation_not_permitted ||
           error == std::errc::permission_denied ||
           error == std::errc::function_not_supported ||
           error == std::errc::not_supported ||
           error == std::errc::read_only_file_system;
}

void require_symlink_fixture_unavailability_contract() {
#ifdef _WIN32
    require(is_explicit_symlink_fixture_unavailability(std::error_code(
                ERROR_PRIVILEGE_NOT_HELD, std::system_category())),
            "Windows symlink privilege failure was not treated as fixture "
            "unavailability");
    require(!is_explicit_symlink_fixture_unavailability(
                std::error_code(ERROR_INVALID_NAME, std::system_category())),
            "unrelated Windows symlink failure was treated as fixture "
            "unavailability");
#endif
}

void require_native_fixed_child_poisoning() {
    const std::vector<std::string> fixed_children{
        "journal.jsonl", "authority-root.json", "authority.lock",
        ".journal.jsonl.stage", ".authority-root.json.stage"};
    NativeDirectory parent("task020-fixed-child-poison");
    const auto outside_directory = parent.path() / "outside";
    std::filesystem::create_directory(outside_directory);
    write_bytes(parent.path() / "parent-sentinel.bin", "parent-sentinel");
    write_bytes(outside_directory / "target.bin", "outside-target");
    const auto outside_before = snapshot_regular_files(outside_directory);

    const auto require_unchanged_boundaries = [&] {
        require(read_bytes(parent.path() / "parent-sentinel.bin") ==
                        "parent-sentinel" &&
                    snapshot_regular_files(outside_directory) == outside_before,
                "fixed-child poison changed parent or outside bytes");
    };
    const auto require_only_poison_and_lock =
        [&](const std::filesystem::path &authority_directory,
            std::string_view poison_name) {
            const auto children = direct_child_names(authority_directory);
            for (const auto &child : children) {
                require(child == poison_name || child == "authority.lock",
                        "fixed-child refusal retained an unexpected child");
            }
            require(children.count(std::string(poison_name)) == 1,
                    "fixed-child refusal replaced the poison entry");
            if (poison_name != "authority.lock" &&
                children.count("authority.lock") != 0) {
                const auto lock_status = std::filesystem::symlink_status(
                    authority_directory / "authority.lock");
                require(std::filesystem::is_regular_file(lock_status) &&
                            !std::filesystem::is_symlink(lock_status),
                        "fixed-child refusal produced an unsafe lock entry");
            }
        };

    for (std::size_t index = 0; index < fixed_children.size(); ++index) {
        const auto authority_directory =
            parent.path() / ("directory-poison-" + std::to_string(index));
        std::filesystem::create_directory(authority_directory);
        const auto poison = authority_directory / fixed_children[index];
        std::filesystem::create_directory(poison);
        write_bytes(poison / "sentinel.bin", "poison-sentinel");

        auto journal =
            DurableJournal::native(authority_directory, generous_limits());
        auto result = journal.create_new(TrustedReplayFloor::uninitialized(),
                                         make_chain().genesis);
        require_result(result, DurableJournalStatus::UnsupportedStorage,
                       "native fixed directory was treated as a regular file");
        require(std::filesystem::is_directory(
                    std::filesystem::symlink_status(poison)) &&
                    direct_child_names(poison) ==
                        std::set<std::string>{"sentinel.bin"} &&
                    read_bytes(poison / "sentinel.bin") == "poison-sentinel",
                "native fixed directory was followed or replaced");
        require_only_poison_and_lock(authority_directory,
                                     fixed_children[index]);
        require_unchanged_boundaries();
    }

    bool symlink_fixture_unavailable = false;
    for (std::size_t index = 0; index < fixed_children.size(); ++index) {
        const auto authority_directory =
            parent.path() / ("symlink-poison-" + std::to_string(index));
        std::filesystem::create_directory(authority_directory);
        const auto poison = authority_directory / fixed_children[index];
        std::error_code symlink_error;
        std::filesystem::create_symlink(outside_directory / "target.bin",
                                        poison, symlink_error);
        if (symlink_error) {
            require(index == 0 && is_explicit_symlink_fixture_unavailability(
                                      symlink_error),
                    "native symlink poison fixture failed unexpectedly");
            symlink_fixture_unavailable = true;
            break;
        }

        auto journal =
            DurableJournal::native(authority_directory, generous_limits());
        auto result = journal.create_new(TrustedReplayFloor::uninitialized(),
                                         make_chain().genesis);
        require_result(result, DurableJournalStatus::UnsupportedStorage,
                       "native fixed symlink was followed as authority");
        require(std::filesystem::is_symlink(
                    std::filesystem::symlink_status(poison)),
                "native fixed symlink was replaced");
        require_only_poison_and_lock(authority_directory,
                                     fixed_children[index]);
        require_unchanged_boundaries();
    }
    if (symlink_fixture_unavailable) {
        require(snapshot_regular_files(outside_directory) == outside_before,
                "unavailable symlink fixture changed outside bytes");
    }
}

void require_native_storage_identity_binding() {
    const auto chain = make_chain();
    NativeDirectory directory_a("task020-identity-a");
    NativeDirectory directory_b("task020-identity-b");
    static_cast<void>(create_native(directory_a.path(), chain));
    static_cast<void>(create_native(directory_b.path(), chain));

    std::error_code hard_link_error;
    std::filesystem::remove(directory_b.path() / "authority.lock",
                            hard_link_error);
    require(!hard_link_error,
            "native alias fixture could not remove the closed lock entry");
    std::filesystem::create_hard_link(directory_a.path() / "authority.lock",
                                      directory_b.path() / "authority.lock",
                                      hard_link_error);
    require(!hard_link_error,
            "native alias fixture could not hard-link lock identities");

    const auto b_before = snapshot_regular_files(directory_b.path());
    auto append_authority = recover_native(
        directory_a.path(), chain.genesis_root.canonical_bytes());
    auto append_journal =
        DurableJournal::native(directory_b.path(), generous_limits());
    auto append_result = append_journal.append_and_publish(
        std::move(append_authority), chain.successor);
    require_result(append_result, DurableJournalStatus::CorruptOrRollback,
                   "cross-directory authority token appended by byte identity");
    require(!append_authority.available() &&
                snapshot_regular_files(directory_b.path()) == b_before,
            "cross-directory append changed authority storage");

    auto compact_authority = recover_native(
        directory_a.path(), chain.genesis_root.canonical_bytes());
    auto compact_journal =
        DurableJournal::native(directory_b.path(), generous_limits());
    auto compact_result =
        compact_journal.compact_physical(std::move(compact_authority));
    require_result(
        compact_result, DurableJournalStatus::CorruptOrRollback,
        "cross-directory authority token compacted by byte identity");
    require(!compact_authority.available() &&
                snapshot_regular_files(directory_b.path()) == b_before,
            "cross-directory compaction changed authority storage");

    auto export_authority = recover_native(
        directory_a.path(), chain.genesis_root.canonical_bytes());
    auto export_journal =
        DurableJournal::native(directory_b.path(), generous_limits());
    auto export_result = export_journal.export_exact_schema_candidate(
        export_authority, supported_journal_schema,
        JournalQuiescence::Confirmed);
    require(
        export_result.status == ExactSchemaExportStatus::CorruptOrRollback &&
            !export_result.exported() && !export_result.candidate.has_value() &&
            export_authority.available() &&
            snapshot_regular_files(directory_b.path()) == b_before,
        "cross-directory authority token exported by byte identity");

    auto same_storage_journal =
        DurableJournal::native(directory_a.path(), generous_limits());
    auto published = same_storage_journal.append_and_publish(
        std::move(export_authority), chain.successor);
    require_result(published, DurableJournalStatus::Published,
                   "fresh adapter for the same directory rejected authority");
}

void require_native_lock_recreation_invalidates_authority() {
    const auto chain = make_chain();
    const auto root = std::string(chain.genesis_root.canonical_bytes());
    const auto replace_lock = [](const std::filesystem::path &directory) {
        const auto lock = directory / "authority.lock";
        const auto retired = directory / "retired-authority-lock.bin";
        std::error_code error;
        std::filesystem::create_hard_link(lock, retired, error);
        require(!error,
                "native recreated-lock fixture could not retain old identity");
        std::filesystem::remove(lock, error);
        require(!error,
                "native recreated-lock fixture could not unlink old entry");
        write_bytes(lock, {});
    };
    const auto require_rebound = [&](const std::filesystem::path &directory) {
        auto rebound = recover_native(directory, root);
        require(rebound.available(),
                "fresh native recovery did not bind recreated stable lock");
    };

    NativeDirectory append_directory("task020-recreated-lock-append");
    auto append_token = create_native(append_directory.path(), chain);
    replace_lock(append_directory.path());
    const auto append_before = snapshot_regular_files(append_directory.path());
    auto append_journal =
        DurableJournal::native(append_directory.path(), generous_limits());
    auto append_result = append_journal.append_and_publish(
        std::move(append_token), chain.successor);
    require_result(append_result, DurableJournalStatus::UnsupportedStorage,
                   "recreated native lock accepted an old append token");
    require(!append_token.available() &&
                snapshot_regular_files(append_directory.path()) ==
                    append_before,
            "recreated-lock append changed authority bytes or retained token");
    require_rebound(append_directory.path());

    NativeDirectory compact_directory("task020-recreated-lock-compact");
    auto compact_token = create_native(compact_directory.path(), chain);
    replace_lock(compact_directory.path());
    const auto compact_before =
        snapshot_regular_files(compact_directory.path());
    auto compact_journal =
        DurableJournal::native(compact_directory.path(), generous_limits());
    auto compact_result =
        compact_journal.compact_physical(std::move(compact_token));
    require_result(compact_result, DurableJournalStatus::UnsupportedStorage,
                   "recreated native lock accepted an old compaction token");
    require(!compact_token.available() &&
                snapshot_regular_files(compact_directory.path()) ==
                    compact_before,
            "recreated-lock compaction changed bytes or retained token");
    require_rebound(compact_directory.path());

    NativeDirectory export_directory("task020-recreated-lock-export");
    auto export_token = create_native(export_directory.path(), chain);
    replace_lock(export_directory.path());
    const auto export_before = snapshot_regular_files(export_directory.path());
    auto export_journal =
        DurableJournal::native(export_directory.path(), generous_limits());
    auto export_result = export_journal.export_exact_schema_candidate(
        export_token, supported_journal_schema, JournalQuiescence::Confirmed);
    require(
        export_result.status == ExactSchemaExportStatus::UnsupportedStorage &&
            !export_result.exported() && !export_result.candidate.has_value() &&
            export_token.available() &&
            snapshot_regular_files(export_directory.path()) == export_before,
        "recreated native lock accepted, consumed, or changed export "
        "authority");
    require_rebound(export_directory.path());
}

void require_native_lock_path_revalidation() {
    NativeDirectory directory("task020-live-lock-path");
    const auto probe =
        probe_native_lock_path_revalidation(directory.path().string());
    require(probe.nonblocking_probe_rejected &&
                probe.first_unlock_succeeded &&
                probe.contender_succeeded,
            "native replacement lock was not serialized with the held "
            "authority lock");
#ifdef _WIN32
    require(probe.replacement_prevented_while_held &&
                !probe.contender_rebound &&
                probe.replacement_after_release_rebound,
            "Windows authority lock path was replaceable while held or did "
            "not rebind after release");
#else
    require(!probe.replacement_prevented_while_held &&
                probe.stale_identity_rejected && probe.contender_rebound,
            "POSIX authority identity did not reject and serialize a replaced "
            "live lock path");
#endif
}

void require_same_process_cas_race() {
    const auto chain = make_chain();
    NativeDirectory directory("task020-same-process");
    static_cast<void>(create_native(directory.path(), chain));
    auto left =
        recover_native(directory.path(), chain.genesis_root.canonical_bytes());
    auto right =
        recover_native(directory.path(), chain.genesis_root.canonical_bytes());

    std::mutex mutex;
    std::condition_variable condition;
    int ready = 0;
    bool go = false;
    std::optional<DurableJournalStatus> left_status;
    std::optional<DurableJournalStatus> right_status;
    const auto run = [&](PublishedJournal authority,
                         std::optional<DurableJournalStatus> &status) mutable {
        {
            std::unique_lock lock(mutex);
            ++ready;
            condition.notify_all();
            condition.wait(lock, [&] { return go; });
        }
        auto journal =
            DurableJournal::native(directory.path(), generous_limits());
        status =
            journal.append_and_publish(std::move(authority), chain.successor)
                .status;
    };
    std::thread left_thread(run, std::move(left), std::ref(left_status));
    std::thread right_thread(run, std::move(right), std::ref(right_status));
    {
        std::unique_lock lock(mutex);
        condition.wait(lock, [&] { return ready == 2; });
        go = true;
    }
    condition.notify_all();
    left_thread.join();
    right_thread.join();

    const std::multiset<DurableJournalStatus> observed{*left_status,
                                                       *right_status};
    const std::multiset<DurableJournalStatus> expected{
        DurableJournalStatus::Published,
        DurableJournalStatus::ConflictBeforeWrite};
    require(observed == expected,
            "same-process CAS race did not select exactly one writer");
    require(read_bytes(directory.path() / "journal.jsonl") ==
                    frame(chain.genesis) + frame(chain.successor) &&
                read_bytes(directory.path() / "authority-root.json") ==
                    chain.successor_root.canonical_bytes(),
            "losing same-process writer changed authority bytes");
}

std::string_view status_wire(DurableJournalStatus status) {
    switch (status) {
    case DurableJournalStatus::Published:
        return "published";
    case DurableJournalStatus::ConflictBeforeWrite:
        return "conflict_before_write";
    case DurableJournalStatus::UnsupportedStorage:
        return "unsupported_storage";
    case DurableJournalStatus::CorruptOrRollback:
        return "corrupt_or_rollback";
    case DurableJournalStatus::LimitExceeded:
        return "limit_exceeded";
    case DurableJournalStatus::RecoveryRequired:
        return "recovery_required";
    }
    return "unknown";
}

int race_init(const std::filesystem::path &directory,
              const std::filesystem::path &floor_file) {
    const auto chain = make_chain();
    const auto authority = create_native(directory, chain);
    write_bytes(floor_file, authority.root_bytes());
    return 0;
}

int race_worker(const std::filesystem::path &directory,
                const std::filesystem::path &floor_file,
                const std::filesystem::path &ready_file,
                const std::filesystem::path &go_file) {
    const auto chain = make_chain();
    auto authority = recover_native(directory, read_bytes(floor_file));
    write_bytes(ready_file, "ready");
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!std::filesystem::exists(go_file)) {
        require(std::chrono::steady_clock::now() < deadline,
                "cross-process race start timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    auto journal = DurableJournal::native(directory, generous_limits());
    const auto result =
        journal.append_and_publish(std::move(authority), chain.successor);
    std::cout << status_wire(result.status) << '\n';
    return 0;
}

int race_verify(const std::filesystem::path &directory,
                const std::filesystem::path &floor_file) {
    const auto chain = make_chain();
    const auto authority = recover_native(directory, read_bytes(floor_file));
    require(authority.root_bytes() == chain.successor_root.canonical_bytes(),
            "cross-process race recovered the wrong root");
    require(read_bytes(directory / "journal.jsonl") ==
                    frame(chain.genesis) + frame(chain.successor) &&
                read_bytes(directory / "authority-root.json") ==
                    chain.successor_root.canonical_bytes(),
            "cross-process CAS loser changed authority bytes");
    return 0;
}

int run_command(int argc, char **argv) {
    if (argc == 4 && std::string_view(argv[1]) == "--race-init") {
        return race_init(argv[2], argv[3]);
    }
    if (argc == 6 && std::string_view(argv[1]) == "--race-worker") {
        return race_worker(argv[2], argv[3], argv[4], argv[5]);
    }
    if (argc == 4 && std::string_view(argv[1]) == "--race-verify") {
        return race_verify(argv[2], argv[3]);
    }
    return -1;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 1) {
        const auto command_result = run_command(argc, argv);
        require(command_result >= 0,
                "unsupported durable-journal public-seam arguments");
        return command_result;
    }

    require_public_shape();
    require_shared_io_helpers();
    require_exact_initial_publication();
    require_durable_journal_moves();
    require_explicit_floor_states();
    require_fresh_namespace_asymmetry();
    require_unique_authority_moves();
    require_fixed_paths();
    require_identifier_boundaries_precede_persistence();
    require_replay_and_floors();
    require_surviving_stage_recovery();
    require_root_parser_is_task019();
    require_committed_origin_verification();
    require_origin_verifier_publication_and_lifetime();
    require_tail_classification_and_repair();
    require_explicit_limits();
    require_durability_ordering();
    require_stage_replacement_swap_is_fail_closed();
    require_paused_read_replacement_is_fail_closed();
    require_paused_read_holds_authority_lock();
    require_paused_write_observes_completed_effect();
    require_fake_adapter_destructor_closes_open_lock();
    require_fake_immutable_object_link_count_parity();
    require_final_under_lock_revalidation();
    require_preflight_and_bounded_read_trace();
    require_create_faults();
    require_append_and_fault_recovery();
    require_stale_cas_is_write_free();
    require_history_invalid_append_is_write_free();
    require_partial_io_and_interrupt_retries();
    require_stable_lock_identity();
    require_fence_epoch_cas_clear();
    require_fake_storage_identity_binding();
    require_fake_lock_recreation_invalidates_authority();
    require_directory_lineage_fence_cleanup();
    require_fenced_fresh_create_reconciles();
    require_create_post_failure_clearance_proof();
    require_retained_fresh_create_requires_live_lock();
    require_repair_faults();
    require_read_only_recovery_faults();
    require_physical_compaction();
    require_exact_schema_export();
    require_unsupported_preflight();
    require_native_preflight_identity_phase_residue();
    require_native_preflight_fence_blocks_nonfresh_authority();
    require_native_preflight_preserves_siblings();
    require_native_preflight_stage_collision_exhaustion_is_fenced();
    require_native_relative_directory_binding();
    require_native_unbound_adapter_preserves_working_directory();
    require_native_immutable_objects();
    require_fixed_namespace_publish_barrier_tracks_retries();
    require_native_fixed_namespace_factory();
    require_native_bounded_read_boundaries();
    require_native_literal_children();
    require_native_fresh_namespace_asymmetry();
    require_symlink_fixture_unavailability_contract();
    require_native_fixed_child_poisoning();
    require_native_storage_identity_binding();
    require_native_lock_recreation_invalidates_authority();
    require_native_lock_path_revalidation();
    require_same_process_cas_race();
    return 0;
}
