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
    ProfilingDifferentialRevisionBinding revision;
    ProfilingDifferentialAccountingPartition accounting;
    std::uint32_t calibration_repetitions = 0;
    std::uint32_t validation_repetitions = 0;
};

struct ProfilingDifferentialTargetActivity {
    std::string client_identity_sha256;
    std::string containment_identity_sha256;
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
};

struct ProfilingDifferentialInputFreezeResult;
struct ProfilingDifferentialRevalidationResult;

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
    bool revision_rejected() const noexcept;

private:
    FrozenProfilingDifferentialInput(
        ParsedProfilingNoiseResult noise,
        ProfilingDifferentialInputDraft draft,
        std::string frozen_input_sha256);

    ParsedProfilingNoiseResult noise_;
    ProfilingDifferentialInputDraft draft_;
    std::string frozen_input_sha256_;
    bool revision_rejected_ = false;

    friend ProfilingDifferentialInputFreezeResult
    freeze_profiling_differential_input(
        const ParsedProfilingNoiseResult &noise,
        ProfilingDifferentialInputDraft draft);
    friend ProfilingDifferentialRevalidationResult
    revalidate_profiling_differential_input(
        FrozenProfilingDifferentialInput &input,
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
    std::string diagnostic;

    bool accepted() const noexcept;
};

ProfilingDifferentialInputFreezeResult
freeze_profiling_differential_input(
    const ParsedProfilingNoiseResult &noise,
    ProfilingDifferentialInputDraft draft);

ProfilingDifferentialRevalidationResult
revalidate_profiling_differential_input(
    FrozenProfilingDifferentialInput &input,
    const ProfilingDifferentialRevalidationObservation &observation);

} // namespace lemon::residency
