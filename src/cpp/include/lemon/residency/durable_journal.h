#pragma once

#include "lemon/residency/journal.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace lemon::residency {

inline constexpr std::string_view durable_journal_data_filename =
    "journal.jsonl";
inline constexpr std::string_view durable_journal_root_filename =
    "authority-root.json";
inline constexpr std::string_view durable_journal_lock_filename =
    "authority.lock";

struct JournalLimits {
    std::size_t max_committed_bytes;
    std::uint64_t max_committed_records;
    std::size_t max_resident_heads;
    std::size_t max_crash_tail_bytes;
};

enum class DurableJournalStatus {
    Published,
    ConflictBeforeWrite,
    UnsupportedStorage,
    CorruptOrRollback,
    LimitExceeded,
    RecoveryRequired,
};

enum class JournalQuiescence { Unconfirmed, Confirmed };

enum class ExactSchemaExportStatus {
    ExportedCandidate,
    QuiescenceRequired,
    ConflictBeforeRead,
    RecoveryRequired,
    UnsupportedStorage,
    CorruptOrRollback,
    SchemaMismatch,
    LimitExceeded,
};

class DurableJournal;

class TrustedReplayFloor {
public:
    TrustedReplayFloor() = delete;
    static TrustedReplayFloor uninitialized();
    static TrustedReplayFloor exact_root(std::string root);

private:
    friend class DurableJournal;
    explicit TrustedReplayFloor(std::optional<std::string> root);
    std::optional<std::string> root_;
};

namespace detail {
#ifdef LEMONADE_RESIDENCY_DURABLE_TESTING
class DurableJournalTestFactory;
#endif
} // namespace detail

class PublishedJournal {
public:
    PublishedJournal() = delete;
    PublishedJournal(const PublishedJournal &) = delete;
    PublishedJournal &operator=(const PublishedJournal &) = delete;
    PublishedJournal(PublishedJournal &&) noexcept;
    PublishedJournal &operator=(PublishedJournal &&) noexcept;
    ~PublishedJournal();

    bool available() const noexcept;
    std::string_view root_bytes() const noexcept;
    std::string_view journal_id() const noexcept;
    std::uint64_t tip_sequence() const noexcept;

private:
    friend class DurableJournal;
    class Impl;
    explicit PublishedJournal(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

struct DurableJournalResult {
    DurableJournalStatus status;
    std::optional<PublishedJournal> journal;
    bool published() const noexcept;
};

struct ExactSchemaExportCandidate {
    SchemaVersion schema;
    std::string journal_bytes;
    std::string authority_root_bytes;
};

struct ExactSchemaExportResult {
    ExactSchemaExportStatus status;
    std::optional<ExactSchemaExportCandidate> candidate;
    bool exported() const noexcept;
};

class DurableJournal {
public:
    DurableJournal() = delete;
    DurableJournal(const DurableJournal &) = delete;
    DurableJournal &operator=(const DurableJournal &) = delete;
    DurableJournal(DurableJournal &&) noexcept;
    DurableJournal &operator=(DurableJournal &&) noexcept;
    ~DurableJournal();

    static DurableJournal native(std::filesystem::path directory,
                                 JournalLimits limits);
    DurableJournalResult
    create_new(TrustedReplayFloor floor, const ParsedJournalRecord &genesis,
               const RecoveryOriginVerifier *verifier = nullptr);
    DurableJournalResult
    recover_existing(TrustedReplayFloor floor,
                     const RecoveryOriginVerifier *verifier = nullptr);
    DurableJournalResult
    append_and_publish(PublishedJournal &&authority,
                       const ParsedJournalRecord &candidate,
                       const RecoveryOriginVerifier *verifier = nullptr);
    DurableJournalResult compact_physical(PublishedJournal &&authority);
    ExactSchemaExportResult
    export_exact_schema_candidate(const PublishedJournal &authority,
                                  SchemaVersion target,
                                  JournalQuiescence quiescence);

private:
#ifdef LEMONADE_RESIDENCY_DURABLE_TESTING
    friend class detail::DurableJournalTestFactory;
#endif
    class Impl;
    struct PersistenceState;
    template <typename Adapter>
    explicit DurableJournal(std::unique_ptr<Adapter> adapter,
                            JournalLimits limits);
    DurableJournalResult mint_published(JournalHistory &&history,
                                        AuthorityRootCandidate root,
                                        PersistenceState &&persistence_state);
    std::unique_ptr<Impl> impl_;
};

} // namespace lemon::residency
