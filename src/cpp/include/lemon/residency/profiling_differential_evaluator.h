#pragma once

#include "lemon/residency/profiling_differential_input.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lemon::residency {

inline constexpr std::chrono::seconds profiling_differential_window{5};
inline constexpr std::chrono::milliseconds
    profiling_differential_max_read_start_gap{100};
inline constexpr std::size_t profiling_differential_minimum_window_points = 50;
inline constexpr std::size_t profiling_differential_maximum_plateau_points =
    4096;
inline constexpr std::size_t profiling_differential_maximum_repetitions = 128;
// Canonical JSON with maximum-width scalar fields uses at most 327 bytes per
// repetition and 24,709 bytes for the envelope, array separators, selector,
// identities, method binding, accounting, claim, schema, and checksum.
inline constexpr std::size_t
    profiling_differential_maximum_repetition_evidence_bytes = 327;
inline constexpr std::size_t
    profiling_differential_maximum_canonical_fixed_bytes = 24709;
inline constexpr std::size_t
    profiling_differential_maximum_canonical_evidence_bytes =
        profiling_differential_maximum_canonical_fixed_bytes +
        profiling_differential_maximum_repetitions *
            profiling_differential_maximum_repetition_evidence_bytes;
static_assert(profiling_differential_maximum_canonical_evidence_bytes ==
              66565);
inline constexpr std::string_view profiling_differential_method_id =
    "differential_retained_gtt";
inline constexpr std::string_view profiling_differential_covered_effect =
    "retained_gtt";
inline constexpr std::string_view
    profiling_no_target_gtt_noise_procedure_revision_sha256 =
        "3c5a0b66d6317cc4dd96211cd68887a7cd0949d44cf748a61d9fdc35f4df6e3c";

struct ProfilingDifferentialMethodBinding {
    std::string method_id;
    std::string method_revision_sha256;
    std::string constraint_id;
    std::string constraint_revision_sha256;
    std::string covered_effect;
};

std::optional<ProfilingDifferentialMethodBinding>
resolve_retained_gtt_differential_method_binding(
    const ProfilingTransactionContext &transaction,
    std::string constraint_id);

enum class ProfilingDifferentialPreflightStatus {
    Accepted,
    EvidenceUnavailable,
    InvalidProcedureBinding,
    InvalidMethodBinding,
    InvalidRepetitionCount,
};

struct ProfilingDifferentialPreflightResult {
    ProfilingDifferentialPreflightStatus status =
        ProfilingDifferentialPreflightStatus::EvidenceUnavailable;
    std::string diagnostic;

    bool accepted() const noexcept;
};

ProfilingDifferentialPreflightResult
preflight_retained_gtt_differential(
    const ParsedProfilingNoiseResult &noise,
    const ProfilingDifferentialInputDraft &draft,
    const ProfilingDifferentialMethodBinding &method_binding);

enum class ProfilingDifferentialMarkerKind {
    BaselineReady,
    LoadedReady,
    ReleaseReady,
};

struct ProfilingDifferentialPhaseMarker {
    ProfilingDifferentialMarkerKind kind =
        ProfilingDifferentialMarkerKind::BaselineReady;
    std::chrono::steady_clock::time_point marked_at;
    bool ready = false;
    std::string frozen_input_sha256;
    std::string selector_sha256;
    std::string target_client_identity_sha256;
    std::string target_containment_identity_sha256;
    std::string provenance_sha256;
};

enum class ProfilingDifferentialOwnerProjectionStatus {
    Absent,
    Incomplete,
    Complete,
    Contradictory,
    SharedBuffer,
};

struct ProfilingDifferentialGttPoint {
    std::chrono::steady_clock::time_point scheduled_at;
    std::chrono::steady_clock::time_point read_started_at;
    std::chrono::steady_clock::time_point read_finished_at;
    std::optional<std::uint64_t> global_gtt_used_bytes;
    ProfilingNoiseBindings observed_bindings;
    std::string frozen_input_sha256;
    std::string selector_sha256;
    std::string target_client_identity_sha256;
    std::string target_containment_identity_sha256;
    std::string provenance_sha256;
    ProfilingDifferentialOwnerProjectionStatus owner_projection_status =
        ProfilingDifferentialOwnerProjectionStatus::Absent;
    std::optional<std::uint64_t> owner_gtt_used_bytes;
};

struct ProfilingDifferentialPlateauObservation {
    std::optional<ProfilingDifferentialPhaseMarker> marker;
    std::vector<ProfilingDifferentialGttPoint> points;
};

struct ProfilingDifferentialRepetition {
    ProfilingDifferentialRepetitionPhase phase =
        ProfilingDifferentialRepetitionPhase::Calibration;
    std::uint32_t ordinal = 0;
    std::optional<ProfilingDifferentialRevalidationReceipt>
        revalidation_receipt;
    ProfilingDifferentialPlateauObservation baseline;
    ProfilingDifferentialPlateauObservation loaded;
    ProfilingDifferentialPlateauObservation release;
};

enum class ProfilingDifferentialOwnerProjectionCoverage {
    Complete,
    Incomplete,
    Absent,
};

struct ProfilingDifferentialReleaseEvidence {
    std::uint64_t envelope_lower_bytes = 0;
    std::uint64_t envelope_upper_bytes = 0;
    std::uint64_t minimum_bytes = 0;
    std::uint64_t maximum_bytes = 0;
    bool verified = false;
};

struct ProfilingDifferentialRepetitionEvidence {
    std::uint32_t ordinal = 0;
    std::string provenance_sha256;
    std::uint64_t delta_bytes = 0;
    ProfilingDifferentialReleaseEvidence release;
};

enum class ProfilingDifferentialEvaluationStatus {
    Accepted,
    EvidenceUnavailable,
    InvalidMethodBinding,
    InvalidRepetitionCount,
    InvalidRepetitionOrder,
    RevalidationRejected,
    MissingMarker,
    InvalidMarker,
    InvalidPoint,
    InvalidWindow,
    UnstablePlateau,
    IdentityDrift,
    ActorDrift,
    SourceDrift,
    NegativeDelta,
    ArithmeticOverflow,
    ValidationExceeded,
    ReleaseEnvelopeBreach,
    ContradictoryOwnerProjection,
    SharedBufferEvidence,
    DigestUnavailable,
};

enum class ProfilingDifferentialEvidenceParseStatus {
    Accepted,
    InputTooLarge,
    MalformedJson,
    NonCanonical,
    UnsupportedSchema,
    InvalidIdentifier,
    UnknownField,
    InvalidValue,
    DigestMismatch,
    DigestUnavailable,
};

struct ProfilingDifferentialEvaluationResult;
struct ProfilingDifferentialEvidenceParseResult;

class ParsedProfilingDifferentialEvidence {
public:
    ParsedProfilingDifferentialEvidence() = delete;
    ParsedProfilingDifferentialEvidence(
        const ParsedProfilingDifferentialEvidence &) = default;
    ParsedProfilingDifferentialEvidence(
        ParsedProfilingDifferentialEvidence &&) noexcept = default;
    ParsedProfilingDifferentialEvidence &
    operator=(const ParsedProfilingDifferentialEvidence &) = default;
    ParsedProfilingDifferentialEvidence &
    operator=(ParsedProfilingDifferentialEvidence &&) noexcept = default;

    std::string_view frozen_input_sha256() const noexcept;
    const ProfilingDifferentialMethodBinding &method_binding() const noexcept;
    const LocalOverlaySelectorIdentity &exact_fingerprint() const noexcept;
    std::string_view selector_sha256() const noexcept;
    std::string_view calibration_revision_sha256() const noexcept;
    const ProfilingDifferentialAccountingPartition &accounting() const noexcept;
    std::uint64_t n_gtt_bytes() const noexcept;
    std::uint64_t retained_gtt_bound_bytes() const noexcept;
    const std::vector<ProfilingDifferentialRepetitionEvidence> &
    calibration_repetitions() const noexcept;
    const std::vector<ProfilingDifferentialRepetitionEvidence> &
    validation_repetitions() const noexcept;
    ProfilingDifferentialOwnerProjectionCoverage
    owner_projection_coverage() const noexcept;
    std::string_view checksum_sha256() const noexcept;
    std::string_view canonical_bytes() const noexcept;

private:
    ParsedProfilingDifferentialEvidence(
        std::string frozen_input_sha256,
        ProfilingDifferentialMethodBinding method_binding,
        LocalOverlaySelectorIdentity exact_fingerprint,
        std::string selector_sha256,
        std::string calibration_revision_sha256,
        ProfilingDifferentialAccountingPartition accounting,
        std::uint64_t n_gtt_bytes,
        std::uint64_t retained_gtt_bound_bytes,
        std::vector<ProfilingDifferentialRepetitionEvidence>
            calibration_repetitions,
        std::vector<ProfilingDifferentialRepetitionEvidence>
            validation_repetitions,
        ProfilingDifferentialOwnerProjectionCoverage owner_projection_coverage,
        std::string checksum_sha256,
        std::string canonical_bytes);

    std::string frozen_input_sha256_;
    ProfilingDifferentialMethodBinding method_binding_;
    LocalOverlaySelectorIdentity exact_fingerprint_;
    std::string selector_sha256_;
    std::string calibration_revision_sha256_;
    ProfilingDifferentialAccountingPartition accounting_;
    std::uint64_t n_gtt_bytes_ = 0;
    std::uint64_t retained_gtt_bound_bytes_ = 0;
    std::vector<ProfilingDifferentialRepetitionEvidence>
        calibration_repetitions_;
    std::vector<ProfilingDifferentialRepetitionEvidence>
        validation_repetitions_;
    ProfilingDifferentialOwnerProjectionCoverage owner_projection_coverage_ =
        ProfilingDifferentialOwnerProjectionCoverage::Absent;
    std::string checksum_sha256_;
    std::string canonical_bytes_;

    friend struct ProfilingDifferentialEvaluationResult;
    friend struct ProfilingDifferentialEvidenceParseResult;
    friend ProfilingDifferentialEvaluationResult
    evaluate_retained_gtt_differential(
        const FrozenProfilingDifferentialInput &input,
        ProfilingDifferentialMethodBinding method_binding,
        const std::vector<ProfilingDifferentialRepetition> &repetitions);
    friend ProfilingDifferentialEvidenceParseResult
    parse_profiling_differential_evidence(std::string_view bytes);
};

std::optional<DifferentialRetainedGttEvidenceDraft>
compose_retained_gtt_profiling_input_evidence(
    const ParsedProfilingDifferentialEvidence &evidence,
    std::string transient_envelope_sha256);

struct ProfilingDifferentialEvaluationResult {
    ProfilingDifferentialEvaluationStatus status =
        ProfilingDifferentialEvaluationStatus::EvidenceUnavailable;
    ProfilingDifferentialRevalidationDisposition disposition =
        ProfilingDifferentialRevalidationDisposition::RejectRevision;
    std::optional<ProfilingDifferentialRevalidationStatus>
        revalidation_status;
    std::optional<std::string> noise_result_checksum_sha256;
    std::string diagnostic;
    std::optional<ParsedProfilingDifferentialEvidence> evidence;

    bool accepted() const noexcept;
};

struct ProfilingDifferentialEvidenceParseResult {
    ProfilingDifferentialEvidenceParseStatus status =
        ProfilingDifferentialEvidenceParseStatus::InvalidValue;
    std::string diagnostic;
    std::optional<ParsedProfilingDifferentialEvidence> evidence;

    bool accepted() const noexcept;
};

ProfilingDifferentialEvaluationResult
evaluate_retained_gtt_differential(
    const FrozenProfilingDifferentialInput &input,
    ProfilingDifferentialMethodBinding method_binding,
    const std::vector<ProfilingDifferentialRepetition> &repetitions);

ProfilingDifferentialEvidenceParseResult
parse_profiling_differential_evidence(std::string_view bytes);

} // namespace lemon::residency
