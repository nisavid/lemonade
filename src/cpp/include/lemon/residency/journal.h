#pragma once

#include "lemon/residency/types.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lemon::residency {

inline constexpr SchemaVersion supported_journal_schema{1, 0};
inline constexpr std::size_t max_journal_input_bytes = 64 * 1024;
// Journal identifiers are opaque protocol values, not filesystem path components. Durable
// adapters must encode them or enforce an independent path-component validation contract.
inline constexpr std::size_t max_journal_identifier_bytes = 128;
inline constexpr std::size_t max_journal_array_entries = 256;
inline constexpr std::size_t max_journal_diagnostic_bytes = 256;

enum class ClaimFamily {
    ConsumableCapacity,
    SafetyFloor,
    CardinalityPool,
    CompatibilityExclusivity,
};

enum class ClaimCompleteness {
    NotApplicable,
    KnownZero,
    Bounded,
    Unknown,
};

enum class ClaimUnit {
    Bytes,
    Count,
};

enum class ResidentState {
    Prepared,
    Provisional,
    Active,
    Suspended,
    Quarantined,
    Released,
};

enum class RecoveryDisposition {
    VerifiedIntact,
    VerifiedReleased,
    Quarantined,
};

enum class RecoveryOriginKind {
    PreparedLaunch,
    RuntimeRealization,
    LifecycleEffect,
    JournalReplay,
    PriorEpochRecovery,
    CutoverReconciliation,
    ArtifactIdentityEffect,
};

enum class JournalStatus {
    Accepted,
    InputTooLarge,
    MalformedJson,
    NonCanonical,
    UnsupportedSchema,
    InvalidIdentifier,
    UnknownField,
    UnknownValue,
    InvalidValue,
    LimitExceeded,
    Duplicate,
    InvalidClaimClosure,
    InvalidLifecycle,
    InvalidHistory,
    UnverifiedRecoveryOrigin,
    ChecksumMismatch,
    DigestUnavailable,
};

struct OperationIdentity {
    std::string operation_id;
    std::optional<std::string> plan_id;
    OperationFamily family = OperationFamily::ResourceLifecycle;
    OperationKind kind = OperationKind::Admission;
};

struct ClaimAmount {
    std::string constraint_id;
    ClaimUnit unit = ClaimUnit::Bytes;
    std::uint64_t amount = 0;
};

struct ClaimFamilyClosure {
    ClaimFamily family = ClaimFamily::ConsumableCapacity;
    ClaimCompleteness completeness = ClaimCompleteness::Unknown;
    std::vector<ClaimAmount> entries;
};

struct RecoveryOrigin {
    RecoveryOriginKind kind = RecoveryOriginKind::PreparedLaunch;
    std::string evidence_sha256;
};

struct RecoveryOriginVerification {
    std::string_view journal_id;
    std::string_view resident_id;
    std::string_view daemon_epoch;
    const RecoveryOrigin &origin;
};

class RecoveryOriginVerifier {
public:
    virtual ~RecoveryOriginVerifier() = default;
    virtual bool verify(const RecoveryOriginVerification &request) const noexcept = 0;
};

struct JournalRecordDraft {
    SchemaVersion schema = supported_journal_schema;
    std::string journal_id;
    std::string resident_id;
    std::string daemon_epoch;
    OperationIdentity operation;
    std::vector<ClaimFamilyClosure> claim_closure;
    ResidentState resident_state = ResidentState::Prepared;
    std::optional<RecoveryDisposition> recovery_disposition;
    std::optional<RecoveryOrigin> quarantine_origin;
    std::vector<std::string> action_lease_claim_ids;
    std::vector<std::string> ownership_claim_ids;
    std::vector<std::string> recovery_claim_ids;
};

class ParsedJournalRecord;
class JournalHistory;
class AuthorityRootCandidate;
struct ParsedJournalRecordResult;
struct JournalHistoryResult;
struct AuthorityRootCandidateResult;

class ParsedJournalRecord {
public:
    ParsedJournalRecord() = delete;
    ParsedJournalRecord(const ParsedJournalRecord &) = default;
    ParsedJournalRecord &operator=(const ParsedJournalRecord &) = default;

    SchemaVersion schema() const noexcept;
    std::string_view journal_id() const noexcept;
    std::string_view resident_id() const noexcept;
    std::string_view daemon_epoch() const noexcept;
    std::uint64_t sequence() const noexcept;
    const std::optional<std::string> &predecessor_checksum_sha256() const noexcept;
    const OperationIdentity &operation() const noexcept;
    const std::vector<ClaimFamilyClosure> &claim_closure() const noexcept;
    ResidentState resident_state() const noexcept;
    const std::optional<RecoveryDisposition> &recovery_disposition() const noexcept;
    const std::optional<RecoveryOrigin> &quarantine_origin() const noexcept;
    const std::vector<std::string> &action_lease_claim_ids() const noexcept;
    const std::vector<std::string> &ownership_claim_ids() const noexcept;
    const std::vector<std::string> &recovery_claim_ids() const noexcept;
    std::string_view checksum_sha256() const noexcept;
    std::string_view canonical_bytes() const noexcept;

private:
    ParsedJournalRecord(JournalRecordDraft draft, std::uint64_t sequence,
                        std::optional<std::string> predecessor_checksum_sha256,
                        std::string checksum_sha256, std::string canonical_bytes);

    JournalRecordDraft draft_;
    std::uint64_t sequence_;
    std::optional<std::string> predecessor_checksum_sha256_;
    std::string checksum_sha256_;
    std::string canonical_bytes_;

    friend ParsedJournalRecordResult seal_genesis(JournalRecordDraft draft,
                                                  const RecoveryOriginVerifier *verifier);
    friend ParsedJournalRecordResult seal_successor(const JournalHistory &history,
                                                    JournalRecordDraft draft,
                                                    const RecoveryOriginVerifier *verifier);
    friend ParsedJournalRecordResult parse_record_candidate(std::string_view bytes);
};

struct ParsedJournalRecordResult {
    JournalStatus status = JournalStatus::InvalidValue;
    std::string diagnostic;
    std::optional<ParsedJournalRecord> candidate;

    bool accepted() const noexcept;
};

class JournalHistory {
public:
    JournalHistory() = delete;
    ~JournalHistory();
    JournalHistory(JournalHistory &&) noexcept;
    JournalHistory &operator=(JournalHistory &&) noexcept;
    JournalHistory(const JournalHistory &) = delete;
    JournalHistory &operator=(const JournalHistory &) = delete;

    bool available() const noexcept;
    std::string_view journal_id() const noexcept;
    std::string_view daemon_epoch() const noexcept;
    std::uint64_t tip_sequence() const noexcept;
    std::string_view tip_checksum_sha256() const noexcept;

private:
    struct State;

    explicit JournalHistory(std::unique_ptr<State> state);

    std::unique_ptr<State> state_;

    friend JournalHistoryResult begin_history(const ParsedJournalRecord &genesis,
                                              const RecoveryOriginVerifier *verifier);
    friend JournalHistoryResult advance_history(JournalHistory &&history,
                                                const ParsedJournalRecord &candidate,
                                                const RecoveryOriginVerifier *verifier);
    friend ParsedJournalRecordResult seal_successor(const JournalHistory &history,
                                                    JournalRecordDraft draft,
                                                    const RecoveryOriginVerifier *verifier);
    friend AuthorityRootCandidateResult
    seal_authority_root_candidate(const JournalHistory &history,
                                  const AuthorityRootCandidate *previous_root);
    friend AuthorityRootCandidateResult
    parse_authority_root_candidate(std::string_view bytes, const JournalHistory &history,
                                   const AuthorityRootCandidate *previous_root);
};

struct JournalHistoryResult {
    JournalStatus status = JournalStatus::InvalidHistory;
    std::string diagnostic;
    std::optional<JournalHistory> history;

    bool accepted() const noexcept;
};

class AuthorityRootCandidate {
public:
    AuthorityRootCandidate() = delete;
    AuthorityRootCandidate(const AuthorityRootCandidate &) = default;
    AuthorityRootCandidate &operator=(const AuthorityRootCandidate &) = default;

    SchemaVersion schema() const noexcept;
    std::string_view journal_id() const noexcept;
    std::string_view daemon_epoch() const noexcept;
    std::uint64_t generation() const noexcept;
    std::uint64_t tip_sequence() const noexcept;
    std::string_view tip_checksum_sha256() const noexcept;
    std::string_view checksum_sha256() const noexcept;
    std::string_view canonical_bytes() const noexcept;

private:
    AuthorityRootCandidate(SchemaVersion schema, std::string journal_id, std::string daemon_epoch,
                           std::uint64_t generation, std::uint64_t tip_sequence,
                           std::string tip_checksum_sha256, std::string checksum_sha256,
                           std::string canonical_bytes);

    SchemaVersion schema_;
    std::string journal_id_;
    std::string daemon_epoch_;
    std::uint64_t generation_;
    std::uint64_t tip_sequence_;
    std::string tip_checksum_sha256_;
    std::string checksum_sha256_;
    std::string canonical_bytes_;

    friend AuthorityRootCandidateResult
    seal_authority_root_candidate(const JournalHistory &history,
                                  const AuthorityRootCandidate *previous_root);
    friend AuthorityRootCandidateResult
    parse_authority_root_candidate(std::string_view bytes, const JournalHistory &history,
                                   const AuthorityRootCandidate *previous_root);
};

struct AuthorityRootCandidateResult {
    JournalStatus status = JournalStatus::InvalidValue;
    std::string diagnostic;
    std::optional<AuthorityRootCandidate> candidate;

    bool accepted() const noexcept;
};

ParsedJournalRecordResult seal_genesis(JournalRecordDraft draft,
                                       const RecoveryOriginVerifier *verifier = nullptr);

ParsedJournalRecordResult seal_successor(const JournalHistory &history, JournalRecordDraft draft,
                                         const RecoveryOriginVerifier *verifier = nullptr);

ParsedJournalRecordResult parse_record_candidate(std::string_view bytes);

JournalHistoryResult begin_history(const ParsedJournalRecord &genesis,
                                   const RecoveryOriginVerifier *verifier = nullptr);

JournalHistoryResult advance_history(JournalHistory &&history, const ParsedJournalRecord &candidate,
                                     const RecoveryOriginVerifier *verifier = nullptr);

AuthorityRootCandidateResult
seal_authority_root_candidate(const JournalHistory &history,
                              const AuthorityRootCandidate *previous_root = nullptr);

AuthorityRootCandidateResult
parse_authority_root_candidate(std::string_view bytes, const JournalHistory &history,
                               const AuthorityRootCandidate *previous_root = nullptr);

} // namespace lemon::residency
