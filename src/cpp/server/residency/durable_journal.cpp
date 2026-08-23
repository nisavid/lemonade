#include "lemon/residency/durable_journal.h"

#include "authority_fence.h"
#include "platform/durable_file_adapter.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace lemon::residency {

namespace {

using detail::IdentityFenceToken;
using detail::clear_identity_fence;
using detail::fence_identity;
using detail::identity_fence;
using detail::identity_is_fenced;
using detail::same_directory_identity;

DurableJournalResult journal_result(DurableJournalStatus status) {
    return {status, std::nullopt};
}

ExactSchemaExportResult export_result(ExactSchemaExportStatus status) {
    return {status, std::nullopt};
}

std::string journal_frame(const ParsedJournalRecord &record) {
    std::string framed(record.canonical_bytes());
    framed.push_back('\n');
    return framed;
}

bool checked_add(std::size_t left, std::size_t right,
                 std::size_t &sum) noexcept {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        return false;
    }
    sum = left + right;
    return true;
}

bool limits_are_usable(const JournalLimits &limits,
                       std::size_t &journal_read_limit) noexcept {
    if (limits.max_committed_bytes == 0 ||
        limits.max_committed_records == 0 ||
        limits.max_resident_heads == 0 ||
        limits.max_crash_tail_bytes == 0) {
        return false;
    }
    return checked_add(limits.max_committed_bytes,
                       limits.max_crash_tail_bytes, journal_read_limit) &&
           journal_read_limit < std::numeric_limits<std::size_t>::max();
}

bool authority_root_matches(const AuthorityRootCandidate &root,
                            std::string_view bytes) noexcept {
    return root.canonical_bytes() == bytes;
}

} // namespace

struct DurableJournal::PersistenceState {
    std::string storage_identity;
    std::string committed_prefix_bytes;
    std::uint64_t committed_records = 0;
    std::unordered_set<std::string> resident_heads;
    bool journal_stage_present = false;
    bool root_stage_present = false;
    IdentityFenceToken fence_to_clear;
    DurableJournalStatus unlock_failure_status =
        DurableJournalStatus::RecoveryRequired;
};

class PublishedJournal::Impl {
public:
    Impl(JournalHistory &&selected_history, AuthorityRootCandidate selected_root,
         std::string selected_storage_identity,
         std::string selected_committed_prefix_bytes,
         std::uint64_t selected_committed_records,
         std::unordered_set<std::string> selected_resident_heads,
         bool selected_journal_stage_present,
         bool selected_root_stage_present)
        : history(std::move(selected_history)), root(std::move(selected_root)),
          storage_identity(std::move(selected_storage_identity)),
          committed_prefix_bytes(std::move(selected_committed_prefix_bytes)),
          committed_records(selected_committed_records),
          resident_heads(std::move(selected_resident_heads)),
          journal_stage_present(selected_journal_stage_present),
          root_stage_present(selected_root_stage_present) {}

    JournalHistory history;
    AuthorityRootCandidate root;
    std::string storage_identity;
    std::string committed_prefix_bytes;
    std::uint64_t committed_records;
    std::unordered_set<std::string> resident_heads;
    bool journal_stage_present;
    bool root_stage_present;
};

class DurableJournal::Impl {
public:
    struct ReplayResult {
        DurableJournalStatus status = DurableJournalStatus::CorruptOrRollback;
        std::optional<JournalHistory> history;
        std::optional<AuthorityRootCandidate> previous_root;
        PersistenceState persistence;
        std::size_t committed_bytes = 0;
    };

    Impl(std::unique_ptr<detail::DurableFileAdapter> selected_adapter,
         JournalLimits selected_limits)
        : adapter(std::move(selected_adapter)), limits(selected_limits) {}

    bool acquire_identity(std::string &identity) {
        if (!adapter) {
            return false;
        }
        const auto locked = adapter->lock_authority();
        if (!locked.succeeded()) {
            return false;
        }
        lock_held = true;
        auto observed = adapter->authority_identity();
        if (!observed.result.succeeded() || observed.identity.empty()) {
            static_cast<void>(release_lock());
            return false;
        }
        identity = std::move(observed.identity);
        return true;
    }

    detail::DurableFileResult
    preflight_and_inspect(detail::DurableFixedNamespaceResult &fixed) {
        const auto preflight = adapter->preflight_capabilities();
        if (!preflight.succeeded()) {
            return preflight;
        }
        fixed = adapter->inspect_fixed_namespace();
        return fixed.result;
    }

    bool revalidate_authority(std::string_view expected_identity,
                              std::string_view expected_root,
                              std::string_view expected_journal,
                              bool expected_journal_stage,
                              bool expected_root_stage,
                              std::size_t journal_read_limit) {
        if (!lock_held) {
            return false;
        }
        const auto identity = adapter->authority_identity();
        if (!identity.result.succeeded() ||
            identity.identity != expected_identity) {
            return false;
        }
        const auto fixed = adapter->inspect_fixed_namespace();
        if (!fixed.result.succeeded() || !fixed.journal_present ||
            !fixed.root_present || !fixed.lock_present ||
            fixed.journal_stage_present != expected_journal_stage ||
            fixed.root_stage_present != expected_root_stage) {
            return false;
        }
        const auto root = adapter->read_root(max_journal_input_bytes);
        if (!root.result.succeeded() || root.truncated ||
            root.bytes != expected_root) {
            return false;
        }
        const auto journal = adapter->read_journal(journal_read_limit);
        return journal.result.succeeded() && !journal.truncated &&
               journal.bytes == expected_journal;
    }

    detail::DurableFileResult release_lock() {
        if (!lock_held) {
            return {detail::DurableFileStatus::Succeeded, {}};
        }
        auto released = adapter->unlock_authority();
        lock_held = false;
        return released;
    }

    ReplayResult replay_prefix(std::string_view root_bytes,
                               std::string_view journal_bytes,
                               const std::string &storage_identity,
                               const std::optional<std::string> &floor,
                               const RecoveryOriginVerifier *verifier,
                               bool journal_stage_present,
                               bool root_stage_present) const {
        ReplayResult replay;
        replay.persistence.storage_identity = storage_identity;
        replay.persistence.journal_stage_present = journal_stage_present;
        replay.persistence.root_stage_present = root_stage_present;
        std::size_t cursor = 0;
        bool floor_seen = false;
        while (cursor < journal_bytes.size()) {
            const auto newline = journal_bytes.find('\n', cursor);
            if (newline == std::string_view::npos) {
                return replay;
            }
            const auto frame_end = newline + 1;
            if (frame_end > limits.max_committed_bytes ||
                replay.persistence.committed_records ==
                    limits.max_committed_records) {
                replay.status = DurableJournalStatus::LimitExceeded;
                return replay;
            }
            const auto record_bytes =
                journal_bytes.substr(cursor, newline - cursor);
            const auto parsed_record = parse_record_candidate(record_bytes);
            if (!parsed_record.candidate.has_value()) {
                return replay;
            }
            replay.persistence.resident_heads.emplace(
                parsed_record.candidate->resident_id());
            if (replay.persistence.resident_heads.size() >
                limits.max_resident_heads) {
                replay.status = DurableJournalStatus::LimitExceeded;
                return replay;
            }
            ++replay.persistence.committed_records;
            if (!replay.history.has_value()) {
                auto history_result =
                    begin_history(*parsed_record.candidate, verifier);
                if (!history_result.history.has_value()) {
                    return replay;
                }
                replay.history.emplace(std::move(*history_result.history));
            } else {
                auto history_result = advance_history(
                    std::move(*replay.history), *parsed_record.candidate,
                    verifier);
                if (!history_result.history.has_value()) {
                    replay.history.reset();
                    return replay;
                }
                replay.history.emplace(std::move(*history_result.history));
            }
            const auto sealed = seal_authority_root_candidate(
                *replay.history,
                replay.previous_root.has_value() ? &*replay.previous_root
                                                 : nullptr);
            if (!sealed.candidate.has_value()) {
                replay.history.reset();
                return replay;
            }
            if (floor.has_value() &&
                sealed.candidate->canonical_bytes() == *floor) {
                floor_seen = true;
            }
            if (sealed.candidate->canonical_bytes() == root_bytes) {
                replay.committed_bytes = frame_end;
                replay.persistence.committed_prefix_bytes =
                    std::string(journal_bytes.substr(0, frame_end));
                if (!floor.has_value() || !floor_seen) {
                    replay.history.reset();
                    return replay;
                }
                replay.status = DurableJournalStatus::Published;
                return replay;
            }
            replay.previous_root.emplace(std::move(*sealed.candidate));
            cursor = frame_end;
        }
        replay.history.reset();
        return replay;
    }

    bool token_within_limits(const PublishedJournal::Impl &published) const {
        std::size_t ignored = 0;
        return limits_are_usable(limits, ignored) &&
               published.committed_prefix_bytes.size() <=
                   limits.max_committed_bytes &&
               published.committed_records <= limits.max_committed_records &&
               published.resident_heads.size() <= limits.max_resident_heads;
    }

    std::unique_ptr<detail::DurableFileAdapter> adapter;
    JournalLimits limits;
    bool lock_held = false;
};

TrustedReplayFloor::TrustedReplayFloor(std::optional<std::string> root)
    : root_(std::move(root)) {}

TrustedReplayFloor TrustedReplayFloor::uninitialized() {
    return TrustedReplayFloor(std::nullopt);
}

TrustedReplayFloor TrustedReplayFloor::exact_root(std::string root) {
    return TrustedReplayFloor(std::move(root));
}

PublishedJournal::PublishedJournal(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

PublishedJournal::PublishedJournal(PublishedJournal &&) noexcept = default;

PublishedJournal &
PublishedJournal::operator=(PublishedJournal &&) noexcept = default;

PublishedJournal::~PublishedJournal() = default;

bool PublishedJournal::available() const noexcept { return impl_ != nullptr; }

std::string_view PublishedJournal::root_bytes() const noexcept {
    return impl_ == nullptr ? std::string_view{} : impl_->root.canonical_bytes();
}

std::string_view PublishedJournal::journal_id() const noexcept {
    return impl_ == nullptr ? std::string_view{} : impl_->history.journal_id();
}

std::uint64_t PublishedJournal::tip_sequence() const noexcept {
    return impl_ == nullptr ? 0 : impl_->history.tip_sequence();
}

bool DurableJournalResult::published() const noexcept {
    return status == DurableJournalStatus::Published && journal.has_value() &&
           journal->available();
}

bool ExactSchemaExportResult::exported() const noexcept {
    return status == ExactSchemaExportStatus::ExportedCandidate &&
           candidate.has_value();
}

template <typename Adapter>
DurableJournal::DurableJournal(std::unique_ptr<Adapter> adapter,
                               JournalLimits limits)
    : impl_(std::make_unique<Impl>(std::move(adapter), limits)) {}

DurableJournal::DurableJournal(DurableJournal &&) noexcept = default;

DurableJournal &DurableJournal::operator=(DurableJournal &&) noexcept = default;

DurableJournal::~DurableJournal() = default;

DurableJournal DurableJournal::native(std::filesystem::path directory,
                                      JournalLimits limits) {
    return DurableJournal(
        detail::make_platform_durable_file_adapter(directory), limits);
}

DurableJournalResult
DurableJournal::mint_published(JournalHistory &&history,
                               AuthorityRootCandidate root,
    PersistenceState &&persistence_state) {
    const auto fence_to_clear = std::move(persistence_state.fence_to_clear);
    const auto unlock_failure_status =
        persistence_state.unlock_failure_status;
    auto published = PublishedJournal(std::make_unique<PublishedJournal::Impl>(
        std::move(history), std::move(root),
        std::move(persistence_state.storage_identity),
        std::move(persistence_state.committed_prefix_bytes),
        persistence_state.committed_records,
        std::move(persistence_state.resident_heads),
        persistence_state.journal_stage_present,
        persistence_state.root_stage_present));
    const auto unlocked = impl_->release_lock();
    if (!unlocked.succeeded()) {
        return journal_result(unlock_failure_status);
    }
    clear_identity_fence(published.impl_->storage_identity, fence_to_clear);
    return {DurableJournalStatus::Published,
            std::optional<PublishedJournal>(std::move(published))};
}

DurableJournalResult DurableJournal::create_new(
    TrustedReplayFloor floor, const ParsedJournalRecord &genesis,
    const RecoveryOriginVerifier *verifier) {
    std::size_t journal_read_limit = 0;
    if (!impl_ || floor.root_.has_value() ||
        !limits_are_usable(impl_->limits, journal_read_limit)) {
        return journal_result(floor.root_.has_value()
                                  ? DurableJournalStatus::CorruptOrRollback
                                  : DurableJournalStatus::LimitExceeded);
    }
    auto history_result = begin_history(genesis, verifier);
    if (!history_result.history) {
        return journal_result(DurableJournalStatus::CorruptOrRollback);
    }
    auto root_result =
        seal_authority_root_candidate(*history_result.history, nullptr);
    if (!root_result.candidate) {
        return journal_result(DurableJournalStatus::CorruptOrRollback);
    }
    const auto framed = journal_frame(genesis);
    if (framed.size() > impl_->limits.max_committed_bytes ||
        impl_->limits.max_committed_records < 1 ||
        impl_->limits.max_resident_heads < 1) {
        return journal_result(DurableJournalStatus::LimitExceeded);
    }

    std::string identity;
    if (!impl_->acquire_identity(identity)) {
        return journal_result(DurableJournalStatus::UnsupportedStorage);
    }
    const auto retained_fence = identity_fence(identity);
    detail::DurableFixedNamespaceResult fixed{};
    const auto preflight = impl_->preflight_and_inspect(fixed);
    if (!preflight.succeeded()) {
        if (preflight.effect_may_have_occurred() && !retained_fence) {
            fence_identity(identity);
        }
        static_cast<void>(impl_->release_lock());
        return journal_result(
            retained_fence || preflight.effect_may_have_occurred()
                ? DurableJournalStatus::RecoveryRequired
                : DurableJournalStatus::UnsupportedStorage);
    }
    if (retained_fence &&
        (!fixed.lock_present || fixed.journal_present || fixed.root_present ||
         fixed.journal_stage_present || fixed.root_stage_present)) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::RecoveryRequired);
    }
    if (fixed.journal_stage_present || fixed.root_stage_present ||
        fixed.journal_present != fixed.root_present) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::CorruptOrRollback);
    }
    if (fixed.journal_present && fixed.root_present) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::ConflictBeforeWrite);
    }
    const auto absent_root = impl_->adapter->read_root(max_journal_input_bytes);
    if (absent_root.result.status != detail::DurableFileStatus::NotFound) {
        static_cast<void>(impl_->release_lock());
        if (retained_fence) {
            return journal_result(DurableJournalStatus::RecoveryRequired);
        }
        return journal_result(absent_root.result.succeeded()
                                  ? DurableJournalStatus::CorruptOrRollback
                                  : DurableJournalStatus::UnsupportedStorage);
    }

    const auto provisional_fence =
        retained_fence ? retained_fence : fence_identity(identity);
    const auto created = impl_->adapter->create_journal(framed);
    if (!created.succeeded()) {
        const bool create_definitely_before_effect =
            !created.effect_may_have_occurred();
        const auto after_create =
            impl_->adapter->inspect_fixed_namespace();
        const auto live_identity = impl_->adapter->authority_identity();
        const bool fixed_namespace_provably_fresh =
            after_create.result.succeeded() &&
            !after_create.journal_present && !after_create.root_present &&
            !after_create.journal_stage_present &&
            !after_create.root_stage_present && after_create.lock_present;
        const bool authority_identity_still_matches =
            live_identity.result.succeeded() &&
            live_identity.identity == identity;
        const auto unlocked = impl_->release_lock();
        const bool safe_to_clear_fence =
            create_definitely_before_effect &&
            fixed_namespace_provably_fresh &&
            authority_identity_still_matches && unlocked.succeeded();
        if (safe_to_clear_fence) {
            clear_identity_fence(identity, provisional_fence);
            return journal_result(DurableJournalStatus::UnsupportedStorage);
        }
        return journal_result(DurableJournalStatus::RecoveryRequired);
    }
    const auto replaced = impl_->adapter->replace_root(
        root_result.candidate->canonical_bytes());
    if (!replaced.succeeded()) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::RecoveryRequired);
    }
    if (!impl_->revalidate_authority(
            identity, root_result.candidate->canonical_bytes(), framed, false,
            false, journal_read_limit)) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::RecoveryRequired);
    }
    PersistenceState persistence_state;
    persistence_state.storage_identity = identity;
    persistence_state.committed_prefix_bytes = framed;
    persistence_state.committed_records = 1;
    persistence_state.resident_heads.emplace(genesis.resident_id());
    persistence_state.fence_to_clear = provisional_fence;
    return mint_published(std::move(*history_result.history),
                          std::move(*root_result.candidate),
                          std::move(persistence_state));
}

DurableJournalResult DurableJournal::recover_existing(
    TrustedReplayFloor floor, const RecoveryOriginVerifier *verifier) {
    std::size_t journal_read_limit = 0;
    if (!impl_ || !floor.root_.has_value()) {
        return journal_result(DurableJournalStatus::CorruptOrRollback);
    }
    if (!limits_are_usable(impl_->limits, journal_read_limit)) {
        return journal_result(DurableJournalStatus::LimitExceeded);
    }
    std::string identity;
    if (!impl_->acquire_identity(identity)) {
        return journal_result(DurableJournalStatus::UnsupportedStorage);
    }
    detail::DurableFixedNamespaceResult fixed{};
    const auto preflight = impl_->preflight_and_inspect(fixed);
    if (!preflight.succeeded()) {
        if (preflight.effect_may_have_occurred()) {
            fence_identity(identity);
        }
        static_cast<void>(impl_->release_lock());
        return journal_result(
            preflight.effect_may_have_occurred()
                ? DurableJournalStatus::RecoveryRequired
                : DurableJournalStatus::UnsupportedStorage);
    }
    if (!fixed.journal_present || !fixed.root_present) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::CorruptOrRollback);
    }
    const auto root_read = impl_->adapter->read_root(max_journal_input_bytes);
    if (!root_read.result.succeeded()) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::UnsupportedStorage);
    }
    if (root_read.truncated) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::CorruptOrRollback);
    }
    const auto journal_read = impl_->adapter->read_journal(journal_read_limit);
    if (!journal_read.result.succeeded()) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::UnsupportedStorage);
    }
    if (journal_read.truncated) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::LimitExceeded);
    }
    auto replay = impl_->replay_prefix(
        root_read.bytes, journal_read.bytes, identity, floor.root_, verifier,
        fixed.journal_stage_present, fixed.root_stage_present);
    if (replay.status != DurableJournalStatus::Published ||
        !replay.history.has_value()) {
        static_cast<void>(impl_->release_lock());
        return journal_result(replay.status);
    }
    auto parsed = parse_authority_root_candidate(
        root_read.bytes, *replay.history,
        replay.previous_root.has_value() ? &*replay.previous_root : nullptr);
    if (!parsed.candidate) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::CorruptOrRollback);
    }
    const auto tail_bytes = journal_read.bytes.size() - replay.committed_bytes;
    if (tail_bytes > impl_->limits.max_crash_tail_bytes) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::LimitExceeded);
    }
    IdentityFenceToken provisional_fence;
    if (tail_bytes != 0) {
        provisional_fence = fence_identity(identity);
        const auto repaired =
            impl_->adapter->truncate_journal(replay.committed_bytes);
        if (!repaired.succeeded()) {
            const auto unlocked = impl_->release_lock();
            if (!repaired.effect_may_have_occurred() &&
                unlocked.succeeded()) {
                clear_identity_fence(identity, provisional_fence);
            }
            return journal_result(
                repaired.effect_may_have_occurred()
                    ? DurableJournalStatus::RecoveryRequired
                    : DurableJournalStatus::UnsupportedStorage);
        }
    }
    if (!impl_->revalidate_authority(
            identity, parsed.candidate->canonical_bytes(),
            replay.persistence.committed_prefix_bytes,
            fixed.journal_stage_present, fixed.root_stage_present,
            journal_read_limit)) {
        if (!provisional_fence) {
            fence_identity(identity);
        }
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::RecoveryRequired);
    }
    const auto unlock_fence =
        tail_bytes == 0 ? identity_fence(identity) : provisional_fence;
    auto persistence_state = std::move(replay.persistence);
    persistence_state.fence_to_clear = unlock_fence;
    persistence_state.unlock_failure_status =
        tail_bytes == 0 ? DurableJournalStatus::UnsupportedStorage
                        : DurableJournalStatus::RecoveryRequired;
    return mint_published(std::move(*replay.history),
                          std::move(*parsed.candidate),
                          std::move(persistence_state));
}

DurableJournalResult DurableJournal::append_and_publish(
    PublishedJournal &&authority, const ParsedJournalRecord &candidate,
    const RecoveryOriginVerifier *verifier) {
    auto authority_state = std::move(authority.impl_);
    if (!impl_ || !authority_state) {
        return journal_result(DurableJournalStatus::CorruptOrRollback);
    }
    auto history_result = advance_history(std::move(authority_state->history),
                                          candidate, verifier);
    if (!history_result.history) {
        return journal_result(DurableJournalStatus::CorruptOrRollback);
    }
    std::size_t journal_read_limit = 0;
    if (!limits_are_usable(impl_->limits, journal_read_limit)) {
        return journal_result(DurableJournalStatus::LimitExceeded);
    }
    const auto framed = journal_frame(candidate);
    std::size_t next_committed_bytes = 0;
    if (!checked_add(
            authority_state->committed_prefix_bytes.size(),
            framed.size(), next_committed_bytes) ||
        next_committed_bytes > impl_->limits.max_committed_bytes ||
        authority_state->committed_records ==
            std::numeric_limits<std::uint64_t>::max() ||
        authority_state->committed_records + 1 >
            impl_->limits.max_committed_records) {
        return journal_result(DurableJournalStatus::LimitExceeded);
    }
    auto resident_heads = authority_state->resident_heads;
    resident_heads.emplace(candidate.resident_id());
    if (resident_heads.size() > impl_->limits.max_resident_heads) {
        return journal_result(DurableJournalStatus::LimitExceeded);
    }
    auto expected_journal = authority_state->committed_prefix_bytes;
    expected_journal.append(framed);

    if (identity_is_fenced(authority_state->storage_identity)) {
        return journal_result(DurableJournalStatus::RecoveryRequired);
    }
    std::string identity;
    if (!impl_->acquire_identity(identity)) {
        return journal_result(DurableJournalStatus::UnsupportedStorage);
    }
    if (identity != authority_state->storage_identity) {
        const auto status =
            same_directory_identity(identity,
                                    authority_state->storage_identity)
                ? DurableJournalStatus::UnsupportedStorage
                : DurableJournalStatus::CorruptOrRollback;
        static_cast<void>(impl_->release_lock());
        return journal_result(status);
    }
    if (identity_is_fenced(identity)) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::RecoveryRequired);
    }
    detail::DurableFixedNamespaceResult fixed{};
    const auto preflight = impl_->preflight_and_inspect(fixed);
    if (!preflight.succeeded()) {
        if (preflight.effect_may_have_occurred()) {
            fence_identity(identity);
        }
        static_cast<void>(impl_->release_lock());
        return journal_result(
            preflight.effect_may_have_occurred()
                ? DurableJournalStatus::RecoveryRequired
                : DurableJournalStatus::UnsupportedStorage);
    }
    if (!fixed.journal_present || !fixed.root_present) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::CorruptOrRollback);
    }
    if (fixed.journal_stage_present !=
            authority_state->journal_stage_present ||
        fixed.root_stage_present !=
            authority_state->root_stage_present) {
        fence_identity(identity);
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::RecoveryRequired);
    }
    const auto current_root =
        impl_->adapter->read_root(max_journal_input_bytes);
    if (!current_root.result.succeeded()) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::UnsupportedStorage);
    }
    if (current_root.truncated ||
        !authority_root_matches(authority_state->root, current_root.bytes)) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::ConflictBeforeWrite);
    }
    const auto current_journal =
        impl_->adapter->read_journal(journal_read_limit);
    if (!current_journal.result.succeeded()) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::UnsupportedStorage);
    }
    if (current_journal.truncated ||
        current_journal.bytes !=
            authority_state->committed_prefix_bytes) {
        fence_identity(identity);
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::RecoveryRequired);
    }

    const auto provisional_fence = fence_identity(identity);
    const auto appended = impl_->adapter->append_journal(framed);
    if (!appended.succeeded()) {
        const auto unlocked = impl_->release_lock();
        if (!appended.effect_may_have_occurred() && unlocked.succeeded()) {
            clear_identity_fence(identity, provisional_fence);
        }
        return journal_result(appended.effect_may_have_occurred()
                                  ? DurableJournalStatus::RecoveryRequired
                                  : DurableJournalStatus::UnsupportedStorage);
    }
    auto root_result = seal_authority_root_candidate(
        *history_result.history, &authority_state->root);
    if (!root_result.candidate) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::RecoveryRequired);
    }
    const auto replaced = impl_->adapter->replace_root(
        root_result.candidate->canonical_bytes());
    if (!replaced.succeeded()) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::RecoveryRequired);
    }
    if (!impl_->revalidate_authority(
            identity, root_result.candidate->canonical_bytes(),
            expected_journal, authority_state->journal_stage_present, false,
            journal_read_limit)) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::RecoveryRequired);
    }
    PersistenceState persistence_state;
    persistence_state.storage_identity = std::move(authority_state->storage_identity);
    persistence_state.committed_prefix_bytes = std::move(expected_journal);
    persistence_state.committed_records =
        authority_state->committed_records + 1;
    persistence_state.journal_stage_present = authority_state->journal_stage_present;
    persistence_state.resident_heads = std::move(resident_heads);
    persistence_state.root_stage_present = false;
    persistence_state.fence_to_clear = provisional_fence;
    return mint_published(std::move(*history_result.history),
                          std::move(*root_result.candidate),
                          std::move(persistence_state));
}

DurableJournalResult
DurableJournal::compact_physical(PublishedJournal &&authority) {
    auto authority_state = std::move(authority.impl_);
    if (!impl_ || !authority_state) {
        return journal_result(DurableJournalStatus::CorruptOrRollback);
    }
    if (!impl_->token_within_limits(*authority_state)) {
        return journal_result(DurableJournalStatus::LimitExceeded);
    }
    std::size_t journal_read_limit = 0;
    if (!limits_are_usable(impl_->limits, journal_read_limit)) {
        return journal_result(DurableJournalStatus::LimitExceeded);
    }
    if (identity_is_fenced(authority_state->storage_identity)) {
        return journal_result(DurableJournalStatus::RecoveryRequired);
    }
    std::string identity;
    if (!impl_->acquire_identity(identity)) {
        return journal_result(DurableJournalStatus::UnsupportedStorage);
    }
    if (identity != authority_state->storage_identity) {
        const auto status =
            same_directory_identity(identity,
                                    authority_state->storage_identity)
                ? DurableJournalStatus::UnsupportedStorage
                : DurableJournalStatus::CorruptOrRollback;
        static_cast<void>(impl_->release_lock());
        return journal_result(status);
    }
    if (identity_is_fenced(identity)) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::RecoveryRequired);
    }
    detail::DurableFixedNamespaceResult fixed{};
    const auto preflight = impl_->preflight_and_inspect(fixed);
    if (!preflight.succeeded()) {
        if (preflight.effect_may_have_occurred()) {
            fence_identity(identity);
        }
        static_cast<void>(impl_->release_lock());
        return journal_result(
            preflight.effect_may_have_occurred()
                ? DurableJournalStatus::RecoveryRequired
                : DurableJournalStatus::UnsupportedStorage);
    }
    if (!fixed.journal_present || !fixed.root_present) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::CorruptOrRollback);
    }
    if (fixed.journal_stage_present !=
            authority_state->journal_stage_present ||
        fixed.root_stage_present != authority_state->root_stage_present) {
        fence_identity(identity);
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::RecoveryRequired);
    }
    const auto current_root =
        impl_->adapter->read_root(max_journal_input_bytes);
    if (!current_root.result.succeeded()) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::UnsupportedStorage);
    }
    if (current_root.truncated ||
        !authority_root_matches(authority_state->root, current_root.bytes)) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::ConflictBeforeWrite);
    }
    const auto current_journal =
        impl_->adapter->read_journal(journal_read_limit);
    if (!current_journal.result.succeeded()) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::UnsupportedStorage);
    }
    if (current_journal.truncated ||
        current_journal.bytes !=
            authority_state->committed_prefix_bytes) {
        fence_identity(identity);
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::RecoveryRequired);
    }
    const auto provisional_fence = fence_identity(identity);
    const auto replaced = impl_->adapter->replace_journal(
        authority_state->committed_prefix_bytes);
    if (!replaced.succeeded()) {
        const auto unlocked = impl_->release_lock();
        if (!replaced.effect_may_have_occurred() && unlocked.succeeded()) {
            clear_identity_fence(identity, provisional_fence);
        }
        return journal_result(replaced.effect_may_have_occurred()
                                  ? DurableJournalStatus::RecoveryRequired
                                  : DurableJournalStatus::UnsupportedStorage);
    }
    if (!impl_->revalidate_authority(
            identity, authority_state->root.canonical_bytes(),
            authority_state->committed_prefix_bytes, false,
            authority_state->root_stage_present, journal_read_limit)) {
        static_cast<void>(impl_->release_lock());
        return journal_result(DurableJournalStatus::RecoveryRequired);
    }
    PersistenceState persistence_state;
    persistence_state.storage_identity = std::move(authority_state->storage_identity);
    persistence_state.committed_prefix_bytes =
        std::move(authority_state->committed_prefix_bytes);
    persistence_state.committed_records = authority_state->committed_records;
    persistence_state.resident_heads = std::move(authority_state->resident_heads);
    persistence_state.journal_stage_present = false;
    persistence_state.root_stage_present = authority_state->root_stage_present;
    persistence_state.fence_to_clear = provisional_fence;
    return mint_published(std::move(authority_state->history),
                          std::move(authority_state->root),
                          std::move(persistence_state));
}

ExactSchemaExportResult DurableJournal::export_exact_schema_candidate(
    const PublishedJournal &authority, SchemaVersion target,
    JournalQuiescence quiescence) {
    if (!impl_ || !authority.impl_) {
        return export_result(ExactSchemaExportStatus::CorruptOrRollback);
    }
    if (quiescence != JournalQuiescence::Confirmed) {
        return export_result(ExactSchemaExportStatus::QuiescenceRequired);
    }
    if (target.major != supported_journal_schema.major ||
        target.minor != supported_journal_schema.minor) {
        return export_result(ExactSchemaExportStatus::SchemaMismatch);
    }
    if (!impl_->token_within_limits(*authority.impl_)) {
        return export_result(ExactSchemaExportStatus::LimitExceeded);
    }
    std::size_t journal_read_limit = 0;
    if (!limits_are_usable(impl_->limits, journal_read_limit)) {
        return export_result(ExactSchemaExportStatus::LimitExceeded);
    }
    if (identity_is_fenced(authority.impl_->storage_identity)) {
        return export_result(ExactSchemaExportStatus::RecoveryRequired);
    }
    std::string identity;
    if (!impl_->acquire_identity(identity)) {
        return export_result(ExactSchemaExportStatus::UnsupportedStorage);
    }
    if (identity != authority.impl_->storage_identity) {
        const auto status =
            same_directory_identity(identity,
                                    authority.impl_->storage_identity)
                ? ExactSchemaExportStatus::UnsupportedStorage
                : ExactSchemaExportStatus::CorruptOrRollback;
        static_cast<void>(impl_->release_lock());
        return export_result(status);
    }
    if (identity_is_fenced(identity)) {
        static_cast<void>(impl_->release_lock());
        return export_result(ExactSchemaExportStatus::RecoveryRequired);
    }
    detail::DurableFixedNamespaceResult fixed{};
    const auto preflight = impl_->preflight_and_inspect(fixed);
    if (!preflight.succeeded()) {
        if (preflight.effect_may_have_occurred()) {
            fence_identity(identity);
        }
        static_cast<void>(impl_->release_lock());
        return export_result(
            preflight.effect_may_have_occurred()
                ? ExactSchemaExportStatus::RecoveryRequired
                : ExactSchemaExportStatus::UnsupportedStorage);
    }
    if (!fixed.journal_present || !fixed.root_present) {
        static_cast<void>(impl_->release_lock());
        return export_result(ExactSchemaExportStatus::CorruptOrRollback);
    }
    const auto current_root =
        impl_->adapter->read_root(max_journal_input_bytes);
    if (!current_root.result.succeeded()) {
        static_cast<void>(impl_->release_lock());
        return export_result(ExactSchemaExportStatus::UnsupportedStorage);
    }
    if (current_root.truncated) {
        static_cast<void>(impl_->release_lock());
        return export_result(ExactSchemaExportStatus::CorruptOrRollback);
    }
    if (current_root.bytes != authority.impl_->root.canonical_bytes()) {
        const auto parsed = parse_authority_root_candidate(
            current_root.bytes, authority.impl_->history);
        const auto status = parsed.status == JournalStatus::InvalidHistory
                                ? ExactSchemaExportStatus::ConflictBeforeRead
                                : ExactSchemaExportStatus::CorruptOrRollback;
        static_cast<void>(impl_->release_lock());
        return export_result(status);
    }
    const auto current_journal =
        impl_->adapter->read_journal(journal_read_limit);
    if (!current_journal.result.succeeded()) {
        static_cast<void>(impl_->release_lock());
        return export_result(ExactSchemaExportStatus::UnsupportedStorage);
    }
    if (current_journal.truncated ||
        current_journal.bytes !=
            authority.impl_->committed_prefix_bytes) {
        static_cast<void>(impl_->release_lock());
        return export_result(ExactSchemaExportStatus::CorruptOrRollback);
    }
    const auto unlocked = impl_->release_lock();
    if (!unlocked.succeeded()) {
        return export_result(ExactSchemaExportStatus::UnsupportedStorage);
    }
    return {ExactSchemaExportStatus::ExportedCandidate,
            ExactSchemaExportCandidate{
                supported_journal_schema,
                authority.impl_->committed_prefix_bytes,
                std::string(authority.impl_->root.canonical_bytes())}};
}

namespace detail {

#ifdef LEMONADE_RESIDENCY_DURABLE_TESTING
DurableJournal DurableJournalTestFactory::make(
    std::unique_ptr<DurableFileAdapter> adapter, JournalLimits limits) {
    return DurableJournal(std::move(adapter), limits);
}

DurableJournal make_durable_journal_for_test(
    std::unique_ptr<DurableFileAdapter> adapter, JournalLimits limits) {
    return DurableJournalTestFactory::make(std::move(adapter), limits);
}
#endif

} // namespace detail

} // namespace lemon::residency
