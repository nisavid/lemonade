#pragma once

#include "lemon/residency/profiling_noise.h"
#include "lemon/residency/profiling_transaction.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lemon::residency {

enum class ProfilingDifferentialRevisionState {
    Fresh,
    PreviouslyRejected,
};

struct ProfilingDifferentialRevisionBinding {
    std::string calibration_revision_sha256;
    std::string attempt_receipt_sha256;
    ProfilingDifferentialRevisionState state =
        ProfilingDifferentialRevisionState::PreviouslyRejected;
};

enum class ProfilingNoiseValidityState {
    Valid,
    Invalidated,
};

struct ProfilingNoiseValidityBinding {
    std::string noise_result_checksum_sha256;
    ProfilingNoiseValidityState state =
        ProfilingNoiseValidityState::Invalidated;
};

struct ProfilingDifferentialAccountingPartition {
    std::uint64_t x_gtt_bytes = 0;
    std::uint64_t m_gtt_bytes = 0;
    std::string x_gtt_evidence_sha256;
    std::string m_gtt_policy_sha256;
    std::string partition_contract_sha256;
};

struct ProfilingDifferentialInputIdentity {
    ProfilingTransactionContext transaction;
    std::string target_client_identity_sha256;
    std::string target_containment_identity_sha256;
    std::string counter_continuity_epoch_sha256;
    std::string safety_contract_sha256;
    std::string noise_trace_provenance_sha256;
};

struct ProfilingDifferentialInputDraft {
    ProfilingDifferentialInputIdentity identity;
    ProfilingNoiseValidityBinding noise_validity;
    ProfilingDifferentialRevisionBinding revision;
    ProfilingDifferentialAccountingPartition accounting;
    std::uint32_t calibration_repetitions = 0;
    std::uint32_t validation_repetitions = 0;
};

struct ProfilingDifferentialTargetActivity {
    std::string client_identity_sha256;
    std::string containment_identity_sha256;
};

enum class ProfilingDifferentialRepetitionPhase {
    Calibration,
    Validation,
};

struct ProfilingDifferentialRevalidationObservation {
    std::chrono::steady_clock::time_point checked_at;
    ProfilingNoiseBindings observed_bindings;
    std::string observed_noise_result_checksum_sha256;
    std::string observed_counter_continuity_epoch_sha256;
    std::uint64_t observed_non_target_gtt_range_bytes = 0;
    std::optional<ProfilingDifferentialTargetActivity> target_activity;
    bool counter_reset_detected = false;
    bool counter_discontinuity_detected = false;
    bool unexpected_non_target_client_detected = false;
};

enum class ProfilingDifferentialInputFreezeStatus {
    Accepted,
    EvidenceUnavailable,
    InvalidNoiseResult,
    InvalidIdentity,
    InvalidAccountingPartition,
    InvalidRepetitionCount,
    TraceProvenanceUnavailable,
    TraceProvenanceMismatch,
    NoiseValidityMismatch,
    NoiseResultInvalidated,
    RevisionAlreadyRejected,
    DigestUnavailable,
};

enum class ProfilingDifferentialRevalidationStatus {
    Accepted,
    RevisionRejected,
    NoiseResultMismatch,
    BindingMismatch,
    CounterReset,
    CounterDiscontinuity,
    BackgroundDrift,
    ExcessVariation,
    TargetMismatch,
    NonIncreasingObservation,
};

enum class ProfilingDifferentialRevalidationDisposition {
    Continue,
    RejectRevision,
    InvalidateNoiseResult,
};

struct ProfilingDifferentialInputFreezeResult;
struct ProfilingDifferentialRevalidationResult;
class ProfilingDifferentialAttemptState;
class ProfilingDifferentialRevalidationReceipt;

class FrozenProfilingDifferentialInput {
public:
    FrozenProfilingDifferentialInput() = delete;
    FrozenProfilingDifferentialInput(
        const FrozenProfilingDifferentialInput &) = delete;
    FrozenProfilingDifferentialInput &
    operator=(const FrozenProfilingDifferentialInput &) = delete;
    FrozenProfilingDifferentialInput(
        FrozenProfilingDifferentialInput &&) noexcept;
    FrozenProfilingDifferentialInput &
    operator=(FrozenProfilingDifferentialInput &&) noexcept;

    const ParsedProfilingNoiseResult &noise() const noexcept;
    const ProfilingDifferentialInputIdentity &identity() const noexcept;
    const ProfilingDifferentialRevisionBinding &revision() const noexcept;
    const ProfilingDifferentialAccountingPartition &accounting() const noexcept;
    std::uint32_t calibration_repetitions() const noexcept;
    std::uint32_t validation_repetitions() const noexcept;
    std::string_view frozen_input_sha256() const noexcept;

private:
    FrozenProfilingDifferentialInput(
        ParsedProfilingNoiseResult noise,
        ProfilingDifferentialInputDraft draft,
        std::string frozen_input_sha256);

    ParsedProfilingNoiseResult noise_;
    ProfilingDifferentialInputDraft draft_;
    std::string frozen_input_sha256_;

    friend ProfilingDifferentialInputFreezeResult
    freeze_profiling_differential_input(
        const ParsedProfilingNoiseResult &noise,
        ProfilingDifferentialInputDraft draft);
};

class ProfilingDifferentialRevalidationReceipt {
public:
    ProfilingDifferentialRevalidationReceipt() = delete;
    ProfilingDifferentialRevalidationReceipt(
        const ProfilingDifferentialRevalidationReceipt &) = default;
    ProfilingDifferentialRevalidationReceipt(
        ProfilingDifferentialRevalidationReceipt &&) noexcept = default;
    ProfilingDifferentialRevalidationReceipt &
    operator=(const ProfilingDifferentialRevalidationReceipt &) = default;
    ProfilingDifferentialRevalidationReceipt &
    operator=(ProfilingDifferentialRevalidationReceipt &&) noexcept = default;

    ProfilingDifferentialRepetitionPhase phase() const noexcept;
    std::uint32_t ordinal() const noexcept;
    std::chrono::steady_clock::time_point checked_at() const noexcept;
    std::string_view frozen_input_sha256() const noexcept;
    std::string_view noise_result_checksum_sha256() const noexcept;
    std::string_view observation_sha256() const noexcept;
    std::string_view previous_receipt_sha256() const noexcept;
    ProfilingDifferentialRevalidationStatus status() const noexcept;
    ProfilingDifferentialRevalidationDisposition disposition() const noexcept;
    std::string_view receipt_sha256() const noexcept;
    bool accepted() const noexcept;

private:
    ProfilingDifferentialRevalidationReceipt(
        ProfilingDifferentialRepetitionPhase phase,
        std::uint32_t ordinal,
        std::chrono::steady_clock::time_point checked_at,
        std::string frozen_input_sha256,
        std::string noise_result_checksum_sha256,
        std::string observation_sha256,
        std::string previous_receipt_sha256,
        ProfilingDifferentialRevalidationStatus status,
        ProfilingDifferentialRevalidationDisposition disposition,
        std::string receipt_sha256);

    ProfilingDifferentialRepetitionPhase phase_ =
        ProfilingDifferentialRepetitionPhase::Calibration;
    std::uint32_t ordinal_ = 0;
    std::chrono::steady_clock::time_point checked_at_;
    std::string frozen_input_sha256_;
    std::string noise_result_checksum_sha256_;
    std::string observation_sha256_;
    std::string previous_receipt_sha256_;
    ProfilingDifferentialRevalidationStatus status_ =
        ProfilingDifferentialRevalidationStatus::RevisionRejected;
    ProfilingDifferentialRevalidationDisposition disposition_ =
        ProfilingDifferentialRevalidationDisposition::RejectRevision;
    std::string receipt_sha256_;

    friend ProfilingDifferentialRevalidationResult
    revalidate_profiling_differential_input(
        const FrozenProfilingDifferentialInput &input,
        ProfilingDifferentialAttemptState &attempt,
        ProfilingDifferentialRepetitionPhase phase,
        std::uint32_t ordinal,
        const ProfilingDifferentialRevalidationObservation &observation);
};

class ProfilingDifferentialAttemptState {
public:
    ProfilingDifferentialAttemptState() = delete;
    explicit ProfilingDifferentialAttemptState(
        const FrozenProfilingDifferentialInput &input);
    ProfilingDifferentialAttemptState(
        const ProfilingDifferentialAttemptState &) = delete;
    ProfilingDifferentialAttemptState &
    operator=(const ProfilingDifferentialAttemptState &) = delete;
    ProfilingDifferentialAttemptState(
        ProfilingDifferentialAttemptState &&) noexcept = default;
    ProfilingDifferentialAttemptState &
    operator=(ProfilingDifferentialAttemptState &&) noexcept = default;

    std::uint32_t receipts_issued() const noexcept;
    bool revision_rejected() const noexcept;
    bool noise_result_invalidated() const noexcept;

private:
    std::string frozen_input_sha256_;
    std::string noise_result_checksum_sha256_;
    std::string last_receipt_sha256_;
    std::uint32_t calibration_repetitions_ = 0;
    std::uint32_t validation_repetitions_ = 0;
    std::uint32_t receipts_issued_ = 0;
    bool revision_rejected_ = false;
    bool noise_result_invalidated_ = false;
    std::optional<std::chrono::steady_clock::time_point>
        last_accepted_revalidation_at_;

    friend ProfilingDifferentialRevalidationResult
    revalidate_profiling_differential_input(
        const FrozenProfilingDifferentialInput &input,
        ProfilingDifferentialAttemptState &attempt,
        ProfilingDifferentialRepetitionPhase phase,
        std::uint32_t ordinal,
        const ProfilingDifferentialRevalidationObservation &observation);
};

struct ProfilingDifferentialInputFreezeResult {
    ProfilingDifferentialInputFreezeStatus status =
        ProfilingDifferentialInputFreezeStatus::EvidenceUnavailable;
    std::string diagnostic;
    std::optional<FrozenProfilingDifferentialInput> input;

    bool accepted() const noexcept;
};

struct ProfilingDifferentialRevalidationResult {
    ProfilingDifferentialRevalidationStatus status =
        ProfilingDifferentialRevalidationStatus::RevisionRejected;
    ProfilingDifferentialRevalidationDisposition disposition =
        ProfilingDifferentialRevalidationDisposition::RejectRevision;
    std::string diagnostic;
    std::optional<ProfilingDifferentialRevalidationReceipt> receipt;

    bool accepted() const noexcept;
};

ProfilingDifferentialInputFreezeResult
freeze_profiling_differential_input(
    const ParsedProfilingNoiseResult &noise,
    ProfilingDifferentialInputDraft draft);

ProfilingDifferentialRevalidationResult
revalidate_profiling_differential_input(
    const FrozenProfilingDifferentialInput &input,
    ProfilingDifferentialAttemptState &attempt,
    ProfilingDifferentialRepetitionPhase phase,
    std::uint32_t ordinal,
    const ProfilingDifferentialRevalidationObservation &observation);

bool validate_profiling_differential_revalidation_receipt(
    const FrozenProfilingDifferentialInput &input,
    const ProfilingDifferentialRevalidationReceipt &receipt,
    ProfilingDifferentialRepetitionPhase expected_phase,
    std::uint32_t expected_ordinal,
    std::string_view expected_previous_receipt_sha256) noexcept;

} // namespace lemon::residency
