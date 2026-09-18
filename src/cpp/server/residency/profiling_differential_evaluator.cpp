#include "lemon/residency/profiling_differential_evaluator.h"

#include "local_overlay_codec.h"
#include "profiling_common.h"
#include "profiling_noise_codec.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <initializer_list>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace lemon::residency {
namespace {

using json = nlohmann::json;
using local_overlay_internal::selector_document;
using profiling_internal::BoundedSha256;
using profiling_internal::append_string;
using profiling_internal::append_u64;
using profiling_internal::bindings_document;
using profiling_internal::bounded_diagnostic;
using profiling_internal::digest_is_valid;
using profiling_internal::elapsed_between;
using profiling_internal::sha256_hex;

constexpr char component_evidence_domain[] =
    "lemonade.residency.profiling-differential-evidence/v1\0";
constexpr char repetition_provenance_domain[] =
    "lemonade.residency.profiling-differential-repetition/v2\0";
constexpr char noise_bindings_domain[] =
    "lemonade.residency.profiling-noise-bindings/v1\0";
constexpr char constraint_binding_domain[] =
    "lemonade.residency.profiling-differential-constraint/v1\0";

#ifndef LEMONADE_PROFILING_DIFFERENTIAL_METHOD_REVISION_SHA256
#error "The retained-GTT procedure revision must be supplied by CMake"
#endif

constexpr std::string_view supported_method_revision_sha256 =
    LEMONADE_PROFILING_DIFFERENTIAL_METHOD_REVISION_SHA256;

class ComponentParseFailure final : public std::runtime_error {
public:
    ComponentParseFailure(ProfilingDifferentialEvidenceParseStatus status,
                          std::string diagnostic)
        : std::runtime_error(std::move(diagnostic)), status_(status) {}

    ProfilingDifferentialEvidenceParseStatus status() const noexcept {
        return status_;
    }

private:
    ProfilingDifferentialEvidenceParseStatus status_;
};

[[noreturn]] void reject_parse(
    ProfilingDifferentialEvidenceParseStatus status,
    std::string diagnostic) {
    throw ComponentParseFailure(status, std::move(diagnostic));
}

bool identifier_is_valid(std::string_view value) noexcept {
    return !value.empty() &&
           value.size() <= max_local_overlay_identifier_bytes &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return character >= 0x21 && character <= 0x7e;
           });
}

bool bindings_are_valid(const ProfilingNoiseBindings &bindings) noexcept {
    return digest_is_valid(bindings.deployment_id) &&
           digest_is_valid(bindings.deployment_epoch_sha256) &&
           digest_is_valid(bindings.boot_id_sha256) &&
           digest_is_valid(bindings.device_identity_sha256) &&
           digest_is_valid(bindings.topology_sha256) &&
           digest_is_valid(bindings.kernel_identity_sha256) &&
           digest_is_valid(bindings.driver_identity_sha256) &&
           identifier_is_valid(bindings.counter_source_id) &&
           digest_is_valid(bindings.counter_source_revision_sha256) &&
           digest_is_valid(bindings.counter_continuity_epoch_sha256) &&
           digest_is_valid(bindings.campaign_contract_sha256) &&
           digest_is_valid(bindings.procedure_revision_sha256) &&
           digest_is_valid(bindings.background_inventory_sha256);
}

bool bindings_equal(const ProfilingNoiseBindings &left,
                    const ProfilingNoiseBindings &right) noexcept {
    return left.deployment_id == right.deployment_id &&
           left.deployment_epoch_sha256 == right.deployment_epoch_sha256 &&
           left.boot_id_sha256 == right.boot_id_sha256 &&
           left.device_identity_sha256 == right.device_identity_sha256 &&
           left.topology_sha256 == right.topology_sha256 &&
           left.kernel_identity_sha256 == right.kernel_identity_sha256 &&
           left.driver_identity_sha256 == right.driver_identity_sha256 &&
           left.counter_source_id == right.counter_source_id &&
           left.counter_source_revision_sha256 ==
               right.counter_source_revision_sha256 &&
           left.counter_continuity_epoch_sha256 ==
               right.counter_continuity_epoch_sha256 &&
           left.campaign_contract_sha256 == right.campaign_contract_sha256 &&
           left.procedure_revision_sha256 == right.procedure_revision_sha256 &&
           left.background_inventory_sha256 ==
               right.background_inventory_sha256;
}

bool method_bindings_equal(
    const ProfilingDifferentialMethodBinding &left,
    const ProfilingDifferentialMethodBinding &right) noexcept {
    return left.method_id == right.method_id &&
           left.method_revision_sha256 == right.method_revision_sha256 &&
           left.constraint_id == right.constraint_id &&
           left.constraint_revision_sha256 ==
               right.constraint_revision_sha256 &&
           left.covered_effect == right.covered_effect;
}

std::optional<ProfilingDifferentialMethodBinding> resolved_method_binding(
    std::string_view selector_sha256,
    std::string_view observation_contract_sha256,
    const std::vector<ConstraintKind> &constraints,
    std::string constraint_id) {
    if (!digest_is_valid(supported_method_revision_sha256) ||
        !digest_is_valid(selector_sha256) ||
        !digest_is_valid(observation_contract_sha256) ||
        !identifier_is_valid(constraint_id) ||
        std::find(constraints.begin(), constraints.end(),
                  ConstraintKind::GpuSharedResidency) == constraints.end()) {
        return std::nullopt;
    }

    std::string bytes(constraint_binding_domain,
                      sizeof(constraint_binding_domain) - 1);
    append_string(bytes, constraint_id);
    append_string(bytes, wire_name(ConstraintKind::GpuSharedResidency));
    append_string(bytes, "bytes");
    append_string(bytes, selector_sha256);
    append_string(bytes, observation_contract_sha256);
    const auto constraint_revision_sha256 = sha256_hex(bytes);
    if (!constraint_revision_sha256) return std::nullopt;

    return ProfilingDifferentialMethodBinding{
        std::string(profiling_differential_method_id),
        std::string(supported_method_revision_sha256),
        std::move(constraint_id),
        *constraint_revision_sha256,
        std::string(profiling_differential_covered_effect),
    };
}

std::string_view projection_coverage_wire(
    ProfilingDifferentialOwnerProjectionCoverage coverage) noexcept {
    switch (coverage) {
    case ProfilingDifferentialOwnerProjectionCoverage::Complete:
        return "complete";
    case ProfilingDifferentialOwnerProjectionCoverage::Incomplete:
        return "incomplete";
    case ProfilingDifferentialOwnerProjectionCoverage::Absent:
        return "absent";
    }
    return {};
}

ProfilingDifferentialOwnerProjectionCoverage weakest_coverage(
    ProfilingDifferentialOwnerProjectionCoverage left,
    ProfilingDifferentialOwnerProjectionCoverage right) noexcept {
    if (left == ProfilingDifferentialOwnerProjectionCoverage::Absent ||
        right == ProfilingDifferentialOwnerProjectionCoverage::Absent) {
        return ProfilingDifferentialOwnerProjectionCoverage::Absent;
    }
    if (left == ProfilingDifferentialOwnerProjectionCoverage::Incomplete ||
        right == ProfilingDifferentialOwnerProjectionCoverage::Incomplete) {
        return ProfilingDifferentialOwnerProjectionCoverage::Incomplete;
    }
    return ProfilingDifferentialOwnerProjectionCoverage::Complete;
}

enum class OwnerProjectionContext {
    Plateau,
    PostRelease,
};

struct OwnerProjectionResult {
    std::optional<ProfilingDifferentialOwnerProjectionCoverage> coverage;
    ProfilingDifferentialEvaluationStatus status =
        ProfilingDifferentialEvaluationStatus::Accepted;
    std::string_view diagnostic;
};

OwnerProjectionResult classify_owner_projection(
    const ProfilingDifferentialGttPoint &point,
    OwnerProjectionContext context) noexcept {
    const bool post_release = context == OwnerProjectionContext::PostRelease;
    switch (point.owner_projection_status) {
    case ProfilingDifferentialOwnerProjectionStatus::Absent:
        if (point.owner_gtt_used_bytes) {
            return {
                std::nullopt,
                ProfilingDifferentialEvaluationStatus::
                    ContradictoryOwnerProjection,
                post_release
                    ? "absent post-release owner projection carries a value"
                    : "absent owner projection carries a value"};
        }
        return {ProfilingDifferentialOwnerProjectionCoverage::Absent};
    case ProfilingDifferentialOwnerProjectionStatus::Incomplete:
        if (point.owner_gtt_used_bytes &&
            *point.owner_gtt_used_bytes > *point.global_gtt_used_bytes) {
            return {
                std::nullopt,
                ProfilingDifferentialEvaluationStatus::
                    ContradictoryOwnerProjection,
                post_release
                    ? "incomplete post-release owner projection exceeds the global point"
                    : "incomplete owner projection exceeds the global point"};
        }
        return {ProfilingDifferentialOwnerProjectionCoverage::Incomplete};
    case ProfilingDifferentialOwnerProjectionStatus::Complete:
        if (!point.owner_gtt_used_bytes ||
            *point.owner_gtt_used_bytes > *point.global_gtt_used_bytes) {
            return {
                std::nullopt,
                ProfilingDifferentialEvaluationStatus::
                    ContradictoryOwnerProjection,
                post_release
                    ? "complete post-release owner projection is invalid"
                    : "complete owner projection is invalid"};
        }
        return {ProfilingDifferentialOwnerProjectionCoverage::Complete};
    case ProfilingDifferentialOwnerProjectionStatus::Contradictory:
        return {
            std::nullopt,
            ProfilingDifferentialEvaluationStatus::ContradictoryOwnerProjection,
            post_release
                ? "post-release owner projection is contradictory"
                : "owner projection contradicts the global GTT point"};
    case ProfilingDifferentialOwnerProjectionStatus::SharedBuffer:
        return {
            std::nullopt,
            ProfilingDifferentialEvaluationStatus::SharedBufferEvidence,
            post_release
                ? "post-release owner evidence contains shared GTT"
                : "shared-buffer owner evidence is not disjoint"};
    default:
        return {
            std::nullopt,
            ProfilingDifferentialEvaluationStatus::ContradictoryOwnerProjection,
            post_release
                ? "post-release owner projection status is invalid"
                : "owner projection status is invalid"};
    }
}

constexpr std::uint64_t maximum_encoded_point_bytes = 1424;
constexpr std::uint64_t maximum_encoded_marker_bytes = 392;
constexpr std::uint64_t maximum_encoded_receipt_bytes = 400;
constexpr std::uint64_t maximum_encoded_plateau_bytes =
    maximum_encoded_marker_bytes + 8 +
    profiling_differential_maximum_plateau_points *
        maximum_encoded_point_bytes;
constexpr std::uint64_t maximum_repetition_provenance_bytes =
    sizeof(repetition_provenance_domain) - 1 + 16 +
    maximum_encoded_receipt_bytes + 3 * maximum_encoded_plateau_bytes;
constexpr std::uint64_t maximum_component_provenance_bytes =
    profiling_differential_maximum_repetitions *
    maximum_repetition_provenance_bytes;
static_assert(maximum_component_provenance_bytes /
                  profiling_differential_maximum_repetitions ==
              maximum_repetition_provenance_bytes);

bool append_time(BoundedSha256 &hash,
                 std::chrono::steady_clock::time_point value) noexcept {
    return hash.append_u64(static_cast<std::uint64_t>(
        value.time_since_epoch().count()));
}

bool append_bindings(BoundedSha256 &hash,
                     const ProfilingNoiseBindings &bindings) noexcept {
    return hash.append_string(bindings.deployment_id) &&
           hash.append_string(bindings.deployment_epoch_sha256) &&
           hash.append_string(bindings.boot_id_sha256) &&
           hash.append_string(bindings.device_identity_sha256) &&
           hash.append_string(bindings.topology_sha256) &&
           hash.append_string(bindings.kernel_identity_sha256) &&
           hash.append_string(bindings.driver_identity_sha256) &&
           hash.append_string(bindings.counter_source_id) &&
           hash.append_string(bindings.counter_source_revision_sha256) &&
           hash.append_string(bindings.counter_continuity_epoch_sha256) &&
           hash.append_string(bindings.campaign_contract_sha256) &&
           hash.append_string(bindings.procedure_revision_sha256) &&
           hash.append_string(bindings.background_inventory_sha256);
}

bool append_receipt(
    BoundedSha256 &hash,
    const ProfilingDifferentialRevalidationReceipt &receipt) noexcept {
    return hash.append_u64(static_cast<std::uint64_t>(receipt.phase())) &&
           hash.append_u64(receipt.ordinal()) &&
           append_time(hash, receipt.checked_at()) &&
           hash.append_string(receipt.frozen_input_sha256()) &&
           hash.append_string(receipt.noise_result_checksum_sha256()) &&
           hash.append_string(receipt.observation_sha256()) &&
           hash.append_string(receipt.previous_receipt_sha256()) &&
           hash.append_u64(static_cast<std::uint64_t>(receipt.status())) &&
           hash.append_u64(
               static_cast<std::uint64_t>(receipt.disposition())) &&
           hash.append_string(receipt.receipt_sha256());
}

bool append_marker(
    BoundedSha256 &hash,
    const std::optional<ProfilingDifferentialPhaseMarker> &marker) noexcept {
    if (!hash.append_u64(marker.has_value() ? 1 : 0)) return false;
    if (!marker) return true;
    return hash.append_u64(static_cast<std::uint64_t>(marker->kind)) &&
           append_time(hash, marker->marked_at) &&
           hash.append_u64(marker->ready ? 1 : 0) &&
           hash.append_string(marker->frozen_input_sha256) &&
           hash.append_string(marker->selector_sha256) &&
           hash.append_string(marker->target_client_identity_sha256) &&
           hash.append_string(marker->target_containment_identity_sha256) &&
           hash.append_string(marker->provenance_sha256);
}

bool append_point(BoundedSha256 &hash,
                  const ProfilingDifferentialGttPoint &point) noexcept {
    if (!append_time(hash, point.scheduled_at) ||
        !append_time(hash, point.read_started_at) ||
        !append_time(hash, point.read_finished_at) ||
        !hash.append_u64(point.global_gtt_used_bytes.has_value() ? 1 : 0)) {
        return false;
    }
    if (point.global_gtt_used_bytes &&
        !hash.append_u64(*point.global_gtt_used_bytes)) {
        return false;
    }
    if (!append_bindings(hash, point.observed_bindings) ||
        !hash.append_string(point.frozen_input_sha256) ||
        !hash.append_string(point.selector_sha256) ||
        !hash.append_string(point.target_client_identity_sha256) ||
        !hash.append_string(point.target_containment_identity_sha256) ||
        !hash.append_string(point.provenance_sha256) ||
        !hash.append_u64(
            static_cast<std::uint64_t>(point.owner_projection_status)) ||
        !hash.append_u64(point.owner_gtt_used_bytes.has_value() ? 1 : 0)) {
        return false;
    }
    return !point.owner_gtt_used_bytes ||
           hash.append_u64(*point.owner_gtt_used_bytes);
}

bool append_plateau(
    BoundedSha256 &hash,
    const ProfilingDifferentialPlateauObservation &plateau) noexcept {
    if (!append_marker(hash, plateau.marker) ||
        !hash.append_u64(static_cast<std::uint64_t>(plateau.points.size()))) {
        return false;
    }
    const auto retained_point_count = std::min(
        plateau.points.size(),
        profiling_differential_maximum_plateau_points);
    for (std::size_t index = 0; index < retained_point_count; ++index) {
        if (!append_point(hash, plateau.points[index])) return false;
    }
    return true;
}

struct RepetitionProvenance {
    std::string sha256;
    std::uint64_t encoded_bytes = 0;
};

std::optional<RepetitionProvenance> repetition_provenance(
    const ProfilingDifferentialRepetition &repetition,
    const ProfilingDifferentialRevalidationReceipt &receipt) {
    BoundedSha256 hash(maximum_repetition_provenance_bytes);
    if (!hash.append(std::string_view(
            repetition_provenance_domain,
            sizeof(repetition_provenance_domain) - 1)) ||
        !hash.append_u64(static_cast<std::uint64_t>(repetition.phase)) ||
        !hash.append_u64(repetition.ordinal) ||
        !append_receipt(hash, receipt) ||
        !append_plateau(hash, repetition.baseline) ||
        !append_plateau(hash, repetition.loaded) ||
        !append_plateau(hash, repetition.release)) {
        return std::nullopt;
    }
    const auto encoded_bytes = hash.bytes_hashed();
    auto sha256 = hash.finish();
    if (!sha256) return std::nullopt;
    return RepetitionProvenance{std::move(*sha256), encoded_bytes};
}

struct PlateauSummary {
    std::uint64_t minimum = 0;
    std::uint64_t maximum = 0;
    std::chrono::steady_clock::time_point window_end;
    std::chrono::steady_clock::time_point completed_at;
    ProfilingDifferentialOwnerProjectionCoverage projection_coverage =
        ProfilingDifferentialOwnerProjectionCoverage::Complete;
};

struct PlateauResult {
    ProfilingDifferentialEvaluationStatus status =
        ProfilingDifferentialEvaluationStatus::InvalidWindow;
    std::string diagnostic;
    std::optional<PlateauSummary> summary;
};

ProfilingDifferentialEvaluationResult reject(
    ProfilingDifferentialEvaluationStatus status,
    std::string diagnostic) {
    ProfilingDifferentialEvaluationResult result;
    result.status = status;
    result.diagnostic = bounded_diagnostic(std::move(diagnostic));
    return result;
}

ProfilingDifferentialEvaluationResult reject_observation(
    const FrozenProfilingDifferentialInput &input,
    ProfilingDifferentialEvaluationStatus status,
    std::string diagnostic,
    ProfilingDifferentialRevalidationDisposition disposition =
        ProfilingDifferentialRevalidationDisposition::RejectRevision) {
    auto result = reject(status, std::move(diagnostic));
    if (status == ProfilingDifferentialEvaluationStatus::SourceDrift) {
        disposition = ProfilingDifferentialRevalidationDisposition::
            InvalidateNoiseResult;
    }
    result.disposition = disposition;
    if (disposition == ProfilingDifferentialRevalidationDisposition::
                           InvalidateNoiseResult) {
        result.noise_result_checksum_sha256 =
            std::string(input.noise().checksum_sha256());
    }
    return result;
}

bool marker_matches(const ProfilingDifferentialPhaseMarker &marker,
                    ProfilingDifferentialMarkerKind expected_kind,
                    const FrozenProfilingDifferentialInput &input) noexcept {
    return marker.kind == expected_kind && marker.ready &&
           marker.frozen_input_sha256 == input.frozen_input_sha256() &&
           marker.selector_sha256 ==
               input.identity().transaction.selector_sha256 &&
           marker.target_client_identity_sha256 ==
               input.identity().target_client_identity_sha256 &&
           marker.target_containment_identity_sha256 ==
               input.identity().target_containment_identity_sha256 &&
           digest_is_valid(marker.provenance_sha256);
}

bool marker_defines_source_audit_scope(
    const ProfilingDifferentialPhaseMarker &marker,
    ProfilingDifferentialMarkerKind expected_kind) noexcept {
    return marker.kind == expected_kind && marker.ready &&
           digest_is_valid(marker.frozen_input_sha256) &&
           digest_is_valid(marker.selector_sha256) &&
           digest_is_valid(marker.target_client_identity_sha256) &&
           digest_is_valid(marker.target_containment_identity_sha256) &&
           digest_is_valid(marker.provenance_sha256);
}

bool point_has_usable_source_fact(
    const ProfilingDifferentialGttPoint &point) noexcept {
    return point.global_gtt_used_bytes.has_value() &&
           point.read_started_at >= point.scheduled_at &&
           point.read_finished_at >= point.read_started_at &&
           digest_is_valid(point.provenance_sha256) &&
           bindings_are_valid(point.observed_bindings);
}

bool retained_point_fields_are_bounded(
    const ProfilingDifferentialGttPoint &point) noexcept {
    return bindings_are_valid(point.observed_bindings) &&
           digest_is_valid(point.frozen_input_sha256) &&
           digest_is_valid(point.selector_sha256) &&
           digest_is_valid(point.target_client_identity_sha256) &&
           digest_is_valid(point.target_containment_identity_sha256) &&
           digest_is_valid(point.provenance_sha256);
}

enum class SourceAuditScope {
    FixedWindow,
    FromMarker,
};

enum class MonotoneNoiseValidity {
    Valid,
    Invalidated,
};

struct BoundedSourceFact {
    std::chrono::steady_clock::time_point scheduled_at;
    bool usable = false;
    bool mismatched = false;
};

struct BoundedPlateauIngestion {
    const ProfilingDifferentialPlateauObservation *plateau = nullptr;
    std::size_t retained_point_count = 0;
    bool overflowed = false;
    std::optional<BoundedSourceFact> overflow_source_fact;
};

struct BoundedRepetitionIngestion {
    BoundedPlateauIngestion baseline;
    BoundedPlateauIngestion loaded;
    BoundedPlateauIngestion release;
};

struct RevalidationAuthority {
    std::chrono::steady_clock::time_point checked_at;
    ProfilingDifferentialRevalidationStatus status =
        ProfilingDifferentialRevalidationStatus::RevisionRejected;
    ProfilingDifferentialRevalidationDisposition disposition =
        ProfilingDifferentialRevalidationDisposition::RejectRevision;
    std::string diagnostic;
    std::optional<ProfilingDifferentialRevalidationReceipt> receipt;
};

BoundedSourceFact bounded_source_fact(
    const ProfilingDifferentialGttPoint &point,
    const FrozenProfilingDifferentialInput &input) noexcept {
    const bool usable = point_has_usable_source_fact(point);
    return {
        point.scheduled_at,
        usable,
        usable &&
            !bindings_equal(point.observed_bindings,
                            input.noise().bindings()),
    };
}

BoundedPlateauIngestion ingest_plateau(
    const ProfilingDifferentialPlateauObservation &plateau,
    const FrozenProfilingDifferentialInput &input) noexcept {
    BoundedPlateauIngestion result;
    result.plateau = &plateau;
    result.retained_point_count = std::min(
        plateau.points.size(),
        profiling_differential_maximum_plateau_points);
    result.overflowed =
        plateau.points.size() >
        profiling_differential_maximum_plateau_points;
    if (result.overflowed) {
        result.overflow_source_fact = bounded_source_fact(
            plateau.points[profiling_differential_maximum_plateau_points],
            input);
    }
    return result;
}

BoundedRepetitionIngestion ingest_repetition(
    const ProfilingDifferentialRepetition &repetition,
    const FrozenProfilingDifferentialInput &input) noexcept {
    return {
        ingest_plateau(repetition.baseline, input),
        ingest_plateau(repetition.loaded, input),
        ingest_plateau(repetition.release, input),
    };
}

MonotoneNoiseValidity audit_source_facts(
    const BoundedPlateauIngestion &ingestion,
    ProfilingDifferentialMarkerKind expected_kind,
    SourceAuditScope scope,
    const FrozenProfilingDifferentialInput &input) noexcept {
    const auto &plateau = *ingestion.plateau;
    if (!plateau.marker ||
        !marker_defines_source_audit_scope(*plateau.marker, expected_kind) ||
        ingestion.retained_point_count == 0) {
        return MonotoneNoiseValidity::Valid;
    }

    auto scope_start = plateau.marker->marked_at;
    auto scope_end = std::chrono::steady_clock::time_point::max();
    bool scope_end_is_representable = true;
    if (scope == SourceAuditScope::FixedWindow) {
        std::optional<std::chrono::steady_clock::time_point> first;
        for (std::size_t index = 0;
             index < ingestion.retained_point_count; ++index) {
            const auto &point = plateau.points[index];
            if (point.scheduled_at >= plateau.marker->marked_at &&
                (!first || point.scheduled_at < *first)) {
                first = point.scheduled_at;
            }
        }
        if (ingestion.overflow_source_fact &&
            ingestion.overflow_source_fact->scheduled_at >=
                plateau.marker->marked_at &&
            (!first || ingestion.overflow_source_fact->scheduled_at <
                           *first)) {
            first = ingestion.overflow_source_fact->scheduled_at;
        }
        if (!first) return MonotoneNoiseValidity::Valid;

        const auto window_duration =
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                profiling_differential_window);
        if (*first <=
            std::chrono::steady_clock::time_point::max() - window_duration) {
            scope_end = *first + window_duration;
        } else {
            scope_end_is_representable = false;
        }
        scope_start = *first;
    }

    const auto in_scope = [&](const BoundedSourceFact &fact) {
        return fact.scheduled_at >= scope_start &&
               (scope == SourceAuditScope::FromMarker ||
                !scope_end_is_representable ||
                fact.scheduled_at < scope_end);
    };
    auto validity = MonotoneNoiseValidity::Valid;
    for (std::size_t index = 0;
         index < ingestion.retained_point_count; ++index) {
        const auto fact = bounded_source_fact(plateau.points[index], input);
        if (in_scope(fact) && fact.usable && fact.mismatched) {
            validity = MonotoneNoiseValidity::Invalidated;
        }
    }
    if (ingestion.overflow_source_fact &&
        in_scope(*ingestion.overflow_source_fact) &&
        ingestion.overflow_source_fact->usable &&
        ingestion.overflow_source_fact->mismatched) {
        validity = MonotoneNoiseValidity::Invalidated;
    }
    return validity;
}

MonotoneNoiseValidity audit_repetition_source_facts(
    const BoundedRepetitionIngestion &ingestion,
    const FrozenProfilingDifferentialInput &input) noexcept {
    auto validity = MonotoneNoiseValidity::Valid;
    const auto join = [&](MonotoneNoiseValidity observed) {
        if (observed == MonotoneNoiseValidity::Invalidated) {
            validity = MonotoneNoiseValidity::Invalidated;
        }
    };
    join(audit_source_facts(
        ingestion.baseline,
        ProfilingDifferentialMarkerKind::BaselineReady,
        SourceAuditScope::FixedWindow, input));
    join(audit_source_facts(
        ingestion.loaded,
        ProfilingDifferentialMarkerKind::LoadedReady,
        SourceAuditScope::FixedWindow, input));
    join(audit_source_facts(
        ingestion.release,
        ProfilingDifferentialMarkerKind::ReleaseReady,
        SourceAuditScope::FromMarker, input));
    return validity;
}

PlateauResult evaluate_plateau(
    const BoundedPlateauIngestion &ingestion,
    ProfilingDifferentialMarkerKind expected_kind,
    const FrozenProfilingDifferentialInput &input) {
    const auto &plateau = *ingestion.plateau;
    if (!plateau.marker) {
        return {ProfilingDifferentialEvaluationStatus::MissingMarker,
                "required phase-ready marker is missing", std::nullopt};
    }
    if (!marker_matches(*plateau.marker, expected_kind, input)) {
        return {ProfilingDifferentialEvaluationStatus::InvalidMarker,
                "phase-ready marker does not match the frozen input",
                std::nullopt};
    }
    if (ingestion.retained_point_count == 0 || ingestion.overflowed) {
        return {ProfilingDifferentialEvaluationStatus::InvalidWindow,
                "plateau point count is invalid", std::nullopt};
    }
    for (std::size_t index = 1;
         index < ingestion.retained_point_count; ++index) {
        if (plateau.points[index].scheduled_at <=
                plateau.points[index - 1].scheduled_at ||
            plateau.points[index].read_started_at <=
                plateau.points[index - 1].read_started_at) {
            return {ProfilingDifferentialEvaluationStatus::InvalidWindow,
                    "plateau point schedule is not strictly ordered",
                    std::nullopt};
        }
    }

    const auto retained_end =
        plateau.points.begin() + ingestion.retained_point_count;
    const auto first = std::find_if(
        plateau.points.begin(), retained_end,
        [&](const auto &point) {
            return point.scheduled_at >= plateau.marker->marked_at;
        });
    if (first == retained_end) {
        return {ProfilingDifferentialEvaluationStatus::InvalidWindow,
                "plateau has no acquisition on or after its marker",
                std::nullopt};
    }
    const auto window_duration =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            profiling_differential_window);
    if (first->scheduled_at >
        std::chrono::steady_clock::time_point::max() - window_duration) {
        return {ProfilingDifferentialEvaluationStatus::InvalidWindow,
                "plateau window boundary overflows", std::nullopt};
    }
    const auto window_end = first->scheduled_at + window_duration;
    const auto max_gap =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            profiling_differential_max_read_start_gap);
    const auto marker_gap =
        elapsed_between(plateau.marker->marked_at, first->scheduled_at);
    if (!marker_gap || *marker_gap > max_gap) {
        return {ProfilingDifferentialEvaluationStatus::InvalidWindow,
                "first plateau acquisition is late", std::nullopt};
    }
    PlateauSummary summary;
    summary.minimum = std::numeric_limits<std::uint64_t>::max();
    summary.window_end = window_end;
    summary.completed_at = window_end;
    bool observed = false;
    std::size_t point_count = 0;
    const ProfilingDifferentialGttPoint *previous = nullptr;
    const ProfilingDifferentialGttPoint *last = nullptr;
    for (auto point = first;
         point != retained_end && point->scheduled_at < window_end;
         ++point) {
        if (!point->global_gtt_used_bytes ||
            point->read_started_at < point->scheduled_at ||
            point->read_finished_at < point->read_started_at ||
            !digest_is_valid(point->provenance_sha256) ||
            !bindings_are_valid(point->observed_bindings)) {
            return {ProfilingDifferentialEvaluationStatus::InvalidPoint,
                    "plateau contains an incomplete point", std::nullopt};
        }
        if (point->read_started_at >= window_end) {
            return {ProfilingDifferentialEvaluationStatus::InvalidWindow,
                    "plateau acquisition starts outside the fixed window",
                    std::nullopt};
        }
        const auto schedule_delay =
            elapsed_between(point->scheduled_at, point->read_started_at);
        if (!schedule_delay || *schedule_delay > max_gap) {
            return {ProfilingDifferentialEvaluationStatus::InvalidWindow,
                    "plateau acquisition did not start on cadence",
                    std::nullopt};
        }
        if (previous != nullptr) {
            const auto scheduled_gap = elapsed_between(
                previous->scheduled_at, point->scheduled_at);
            const auto read_gap = elapsed_between(
                previous->read_started_at, point->read_started_at);
            if (!scheduled_gap ||
                *scheduled_gap <=
                    std::chrono::steady_clock::duration::zero() ||
                *scheduled_gap > max_gap || !read_gap ||
                *read_gap <= std::chrono::steady_clock::duration::zero() ||
                *read_gap > max_gap) {
                return {ProfilingDifferentialEvaluationStatus::InvalidWindow,
                        "plateau schedule or read-start gap is invalid",
                        std::nullopt};
            }
        }
        if (point->frozen_input_sha256 != input.frozen_input_sha256() ||
            point->selector_sha256 !=
                input.identity().transaction.selector_sha256) {
            return {ProfilingDifferentialEvaluationStatus::IdentityDrift,
                    "plateau point identity changed", std::nullopt};
        }
        if (point->target_client_identity_sha256 !=
                input.identity().target_client_identity_sha256 ||
            point->target_containment_identity_sha256 !=
                input.identity().target_containment_identity_sha256) {
            return {ProfilingDifferentialEvaluationStatus::ActorDrift,
                    "plateau actor or containment changed", std::nullopt};
        }
        if (!bindings_equal(point->observed_bindings,
                            input.noise().bindings())) {
            return {ProfilingDifferentialEvaluationStatus::SourceDrift,
                    "plateau source binding changed", std::nullopt};
        }
        const auto projection = classify_owner_projection(
            *point, OwnerProjectionContext::Plateau);
        if (!projection.coverage) {
            return {projection.status, std::string(projection.diagnostic),
                    std::nullopt};
        }
        summary.projection_coverage = weakest_coverage(
            summary.projection_coverage, *projection.coverage);
        observed = true;
        summary.minimum =
            std::min(summary.minimum, *point->global_gtt_used_bytes);
        summary.maximum =
            std::max(summary.maximum, *point->global_gtt_used_bytes);
        summary.completed_at =
            std::max(summary.completed_at, point->read_finished_at);
        previous = &*point;
        last = &*point;
        ++point_count;
    }
    if (!observed ||
        point_count < profiling_differential_minimum_window_points) {
        return {ProfilingDifferentialEvaluationStatus::InvalidWindow,
                "plateau fixed window has too few points", std::nullopt};
    }
    const auto final_scheduled_gap =
        elapsed_between(last->scheduled_at, window_end);
    const auto final_read_gap =
        elapsed_between(last->read_started_at, window_end);
    if (!final_scheduled_gap ||
        *final_scheduled_gap <=
            std::chrono::steady_clock::duration::zero() ||
        *final_scheduled_gap > max_gap || !final_read_gap ||
        *final_read_gap <= std::chrono::steady_clock::duration::zero() ||
        *final_read_gap > max_gap) {
        return {ProfilingDifferentialEvaluationStatus::InvalidWindow,
                "plateau does not reach the fixed window boundary",
                std::nullopt};
    }
    if (summary.maximum - summary.minimum > input.noise().n_gtt_bytes()) {
        return {ProfilingDifferentialEvaluationStatus::UnstablePlateau,
                "plateau variation exceeds N_gtt", std::nullopt};
    }
    return {ProfilingDifferentialEvaluationStatus::Accepted, {}, summary};
}

json release_document(const ProfilingDifferentialReleaseEvidence &release) {
    return json{
        {"envelope_lower_bytes", release.envelope_lower_bytes},
        {"envelope_upper_bytes", release.envelope_upper_bytes},
        {"maximum_bytes", release.maximum_bytes},
        {"minimum_bytes", release.minimum_bytes},
        {"verified", release.verified},
    };
}

json repetition_evidence_document(
    const ProfilingDifferentialRepetitionEvidence &repetition) {
    return json{
        {"delta_bytes", repetition.delta_bytes},
        {"ordinal", repetition.ordinal},
        {"provenance_sha256", repetition.provenance_sha256},
        {"release", release_document(repetition.release)},
    };
}

json repetitions_document(
    const std::vector<ProfilingDifferentialRepetitionEvidence> &repetitions) {
    json result = json::array();
    for (const auto &repetition : repetitions) {
        result.push_back(repetition_evidence_document(repetition));
    }
    return result;
}

std::optional<std::string> component_checksum(const json &payload) {
    std::string bytes(component_evidence_domain,
                      sizeof(component_evidence_domain) - 1);
    bytes += payload.dump();
    return sha256_hex(bytes);
}

void require_exact_keys(const json &object,
                        std::initializer_list<std::string_view> expected,
                        std::string_view label) {
    if (!object.is_object()) {
        reject_parse(ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                     std::string(label) + " must be an object");
    }
    std::set<std::string> expected_keys;
    for (const auto key : expected) {
        expected_keys.emplace(key);
        if (!object.contains(std::string(key))) {
            reject_parse(
                ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                std::string(label) + " is incomplete");
        }
    }
    for (const auto &[key, value] : object.items()) {
        static_cast<void>(value);
        if (expected_keys.find(key) == expected_keys.end()) {
            reject_parse(
                ProfilingDifferentialEvidenceParseStatus::UnknownField,
                std::string(label) + " has an unknown field");
        }
    }
}

const json &required(const json &object, std::string_view key) {
    const auto found = object.find(std::string(key));
    if (found == object.end()) {
        reject_parse(ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                     "required component evidence field is missing");
    }
    return *found;
}

std::string require_string(const json &value, std::string_view label) {
    if (!value.is_string()) {
        reject_parse(ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                     std::string(label) + " must be a string");
    }
    return value.get<std::string>();
}

std::uint64_t require_u64(const json &value, std::string_view label) {
    if (value.is_number_unsigned()) return value.get<std::uint64_t>();
    if (value.is_number_integer() && value.get<std::int64_t>() == 0) return 0;
    reject_parse(ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                 std::string(label) + " must be an unsigned integer");
}

bool require_bool(const json &value, std::string_view label) {
    if (!value.is_boolean()) {
        reject_parse(ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                     std::string(label) + " must be a Boolean");
    }
    return value.get<bool>();
}

void require_digest(std::string_view value, std::string_view label) {
    if (!digest_is_valid(value)) {
        reject_parse(ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                     std::string(label) + " is invalid");
    }
}

void require_identifier(std::string_view value, std::string_view label) {
    if (!identifier_is_valid(value)) {
        reject_parse(
            ProfilingDifferentialEvidenceParseStatus::InvalidIdentifier,
            std::string(label) + " is invalid");
    }
}

json parse_json(std::string_view bytes) {
    if (bytes.size() >
        profiling_differential_maximum_canonical_evidence_bytes) {
        reject_parse(ProfilingDifferentialEvidenceParseStatus::InputTooLarge,
                     "component evidence exceeds the input limit");
    }
    if (bytes.find('\0') != std::string_view::npos) {
        reject_parse(ProfilingDifferentialEvidenceParseStatus::MalformedJson,
                     "component evidence contains a NUL byte");
    }

    std::map<int, std::set<std::string>> object_keys;
    const auto callback = [&object_keys](int depth, json::parse_event_t event,
                                         json &parsed) {
        if (event == json::parse_event_t::object_start) {
            object_keys[depth + 1].clear();
        } else if (event == json::parse_event_t::key) {
            auto &keys = object_keys[depth];
            const auto key = parsed.get<std::string>();
            if (!keys.insert(key).second) {
                reject_parse(
                    ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                    "component evidence has a duplicate key");
            }
        } else if (event == json::parse_event_t::object_end) {
            object_keys.erase(depth + 1);
        }
        return true;
    };
    try {
        return json::parse(bytes.begin(), bytes.end(), callback, true, false);
    } catch (const ComponentParseFailure &) {
        throw;
    } catch (const json::exception &) {
        reject_parse(ProfilingDifferentialEvidenceParseStatus::MalformedJson,
                     "component evidence is malformed JSON");
    }
}

void parse_schema(const json &value) {
    require_exact_keys(value, {"major", "minor"}, "component schema");
    const auto major = require_u64(required(value, "major"), "schema major");
    const auto minor = require_u64(required(value, "minor"), "schema minor");
    if (major != 1 || minor != 0) {
        reject_parse(
            ProfilingDifferentialEvidenceParseStatus::UnsupportedSchema,
            "component evidence schema is unsupported");
    }
}

ProfilingNoiseBindings parse_bindings(const json &value) {
    require_exact_keys(
        value,
        {"background_inventory_sha256", "boot_id_sha256",
         "campaign_contract_sha256", "counter_continuity_epoch_sha256",
         "counter_source_id", "counter_source_revision_sha256",
         "deployment_epoch_sha256", "deployment_id",
         "device_identity_sha256", "driver_identity_sha256",
         "kernel_identity_sha256", "procedure_revision_sha256",
         "topology_sha256"},
        "noise bindings");
    ProfilingNoiseBindings bindings;
    bindings.background_inventory_sha256 = require_string(
        required(value, "background_inventory_sha256"),
        "background inventory digest");
    bindings.boot_id_sha256 =
        require_string(required(value, "boot_id_sha256"), "boot ID digest");
    bindings.campaign_contract_sha256 = require_string(
        required(value, "campaign_contract_sha256"),
        "campaign contract digest");
    bindings.counter_continuity_epoch_sha256 = require_string(
        required(value, "counter_continuity_epoch_sha256"),
        "counter continuity epoch digest");
    bindings.counter_source_id = require_string(
        required(value, "counter_source_id"), "counter source ID");
    bindings.counter_source_revision_sha256 = require_string(
        required(value, "counter_source_revision_sha256"),
        "counter source revision digest");
    bindings.deployment_epoch_sha256 = require_string(
        required(value, "deployment_epoch_sha256"),
        "deployment epoch digest");
    bindings.deployment_id =
        require_string(required(value, "deployment_id"), "deployment ID");
    bindings.device_identity_sha256 = require_string(
        required(value, "device_identity_sha256"), "device identity digest");
    bindings.driver_identity_sha256 = require_string(
        required(value, "driver_identity_sha256"), "driver identity digest");
    bindings.kernel_identity_sha256 = require_string(
        required(value, "kernel_identity_sha256"), "kernel identity digest");
    bindings.procedure_revision_sha256 = require_string(
        required(value, "procedure_revision_sha256"),
        "procedure revision digest");
    bindings.topology_sha256 =
        require_string(required(value, "topology_sha256"), "topology digest");

    require_digest(bindings.background_inventory_sha256,
                   "background inventory digest");
    require_digest(bindings.boot_id_sha256, "boot ID digest");
    require_digest(bindings.campaign_contract_sha256,
                   "campaign contract digest");
    require_digest(bindings.counter_continuity_epoch_sha256,
                   "counter continuity epoch digest");
    require_identifier(bindings.counter_source_id, "counter source ID");
    require_digest(bindings.counter_source_revision_sha256,
                   "counter source revision digest");
    require_digest(bindings.deployment_epoch_sha256,
                   "deployment epoch digest");
    require_digest(bindings.deployment_id, "deployment ID");
    require_digest(bindings.device_identity_sha256, "device identity digest");
    require_digest(bindings.driver_identity_sha256, "driver identity digest");
    require_digest(bindings.kernel_identity_sha256, "kernel identity digest");
    require_digest(bindings.procedure_revision_sha256,
                   "procedure revision digest");
    require_digest(bindings.topology_sha256, "topology digest");
    return bindings;
}

RuntimeCatalogSelector parse_catalog_selector(const json &value) {
    require_exact_keys(
        value,
        {"backend_channel", "base_variant", "constraints",
         "material_profiles", "model_type", "operation_kind",
         "operation_template", "platform", "recovery",
         "source_support_baseline"},
        "catalog selector");
    const auto &constraint_values = required(value, "constraints");
    if (!constraint_values.is_array() || constraint_values.empty() ||
        constraint_values.size() > 9) {
        reject_parse(ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                     "catalog constraints are invalid");
    }
    std::vector<ConstraintKind> constraints;
    constraints.reserve(constraint_values.size());
    for (const auto &constraint_value : constraint_values) {
        const auto wire =
            require_string(constraint_value, "catalog constraint");
        const auto decoded = decode_constraint_kind(wire);
        if (!decoded.is_known()) {
            reject_parse(
                ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                "catalog constraint is unknown");
        }
        constraints.push_back(*decoded.known_value());
    }

    const auto &profile_values = required(value, "material_profiles");
    if (!profile_values.is_object() || profile_values.empty() ||
        profile_values.size() > 32) {
        reject_parse(ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                     "material profiles are invalid");
    }
    std::map<std::string, std::string> material_profiles;
    for (const auto &[key, profile] : profile_values.items()) {
        material_profiles.emplace(
            key, require_string(profile, "material profile value"));
    }

    const auto operation_template_wire = require_string(
        required(value, "operation_template"), "operation template");
    const auto operation_template =
        decode_operation_template(operation_template_wire);
    const auto operation_kind_wire = require_string(
        required(value, "operation_kind"), "operation kind");
    const auto operation_kind = decode_operation_kind(operation_kind_wire);
    if (!operation_template.is_known() || !operation_kind.is_known()) {
        reject_parse(ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                     "catalog operation is unknown");
    }
    return RuntimeCatalogSelector{
        require_string(required(value, "source_support_baseline"),
                       "source support baseline"),
        require_string(required(value, "base_variant"), "base variant"),
        require_string(required(value, "platform"), "platform"),
        require_string(required(value, "backend_channel"), "backend channel"),
        require_string(required(value, "model_type"), "model type"),
        *operation_template.known_value(), *operation_kind.known_value(),
        std::move(constraints),
        require_string(required(value, "recovery"), "recovery"),
        std::move(material_profiles),
    };
}

LocalOverlaySelectorIdentity parse_selector(const json &value) {
    require_exact_keys(
        value,
        {"backend_build_sha256", "canonical_model_id", "catalog",
         "catalog_sha256", "configuration_sha256",
         "dependency_set_sha256", "device_identity_sha256",
         "driver_identity_sha256", "model_artifact_sha256",
         "operation_contract_sha256", "topology_sha256",
         "workload_sha256"},
        "exact fingerprint");
    LocalOverlaySelectorIdentity selector{
        require_string(required(value, "catalog_sha256"), "catalog digest"),
        parse_catalog_selector(required(value, "catalog")),
        require_string(required(value, "canonical_model_id"),
                       "canonical model ID"),
        require_string(required(value, "model_artifact_sha256"),
                       "model artifact digest"),
        require_string(required(value, "backend_build_sha256"),
                       "backend build digest"),
        require_string(required(value, "device_identity_sha256"),
                       "device identity digest"),
        require_string(required(value, "topology_sha256"), "topology digest"),
        require_string(required(value, "dependency_set_sha256"),
                       "dependency set digest"),
        require_string(required(value, "driver_identity_sha256"),
                       "driver identity digest"),
        require_string(required(value, "configuration_sha256"),
                       "configuration digest"),
        require_string(required(value, "workload_sha256"), "workload digest"),
        require_string(required(value, "operation_contract_sha256"),
                       "operation contract digest"),
    };
    auto canonical = canonicalize_local_overlay_selector(std::move(selector));
    if (!canonical.accepted() ||
        selector_document(*canonical.selector) != value) {
        reject_parse(ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                     "exact fingerprint is not canonical");
    }
    return std::move(*canonical.selector);
}

ProfilingDifferentialMethodBinding parse_method_binding(const json &value) {
    require_exact_keys(value,
                       {"constraint_id", "constraint_revision_sha256",
                        "method_id", "method_revision_sha256"},
                       "method binding");
    ProfilingDifferentialMethodBinding binding;
    binding.constraint_id = require_string(required(value, "constraint_id"),
                                           "constraint ID");
    binding.constraint_revision_sha256 = require_string(
        required(value, "constraint_revision_sha256"),
        "constraint revision digest");
    binding.method_id =
        require_string(required(value, "method_id"), "method ID");
    binding.method_revision_sha256 = require_string(
        required(value, "method_revision_sha256"), "method revision digest");
    binding.covered_effect = std::string(profiling_differential_covered_effect);
    require_identifier(binding.constraint_id, "constraint ID");
    require_digest(binding.constraint_revision_sha256,
                   "constraint revision digest");
    require_digest(binding.method_revision_sha256, "method revision digest");
    if (binding.method_id != profiling_differential_method_id) {
        reject_parse(ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                     "component method is unsupported");
    }
    return binding;
}

ProfilingDifferentialAccountingPartition parse_accounting(
    const json &value,
    std::uint64_t &n_gtt_bytes) {
    require_exact_keys(
        value,
        {"m_gtt_bytes", "m_gtt_policy_sha256", "n_gtt_bytes",
         "partition_contract_sha256", "x_gtt_bytes",
         "x_gtt_evidence_sha256"},
        "accounting terms");
    ProfilingDifferentialAccountingPartition accounting;
    accounting.m_gtt_bytes =
        require_u64(required(value, "m_gtt_bytes"), "M_gtt");
    accounting.m_gtt_policy_sha256 = require_string(
        required(value, "m_gtt_policy_sha256"), "M_gtt policy digest");
    n_gtt_bytes = require_u64(required(value, "n_gtt_bytes"), "N_gtt");
    accounting.partition_contract_sha256 = require_string(
        required(value, "partition_contract_sha256"),
        "accounting partition digest");
    accounting.x_gtt_bytes =
        require_u64(required(value, "x_gtt_bytes"), "X_gtt");
    accounting.x_gtt_evidence_sha256 = require_string(
        required(value, "x_gtt_evidence_sha256"), "X_gtt evidence digest");
    require_digest(accounting.m_gtt_policy_sha256, "M_gtt policy digest");
    require_digest(accounting.partition_contract_sha256,
                   "accounting partition digest");
    require_digest(accounting.x_gtt_evidence_sha256,
                   "X_gtt evidence digest");
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (accounting.x_gtt_bytes > maximum - n_gtt_bytes ||
        accounting.m_gtt_bytes >
            maximum - (n_gtt_bytes + accounting.x_gtt_bytes)) {
        reject_parse(ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                     "accounting terms overflow");
    }
    return accounting;
}

ProfilingDifferentialReleaseEvidence parse_release(const json &value) {
    require_exact_keys(value,
                       {"envelope_lower_bytes", "envelope_upper_bytes",
                        "maximum_bytes", "minimum_bytes", "verified"},
                       "release result");
    ProfilingDifferentialReleaseEvidence release;
    release.envelope_lower_bytes = require_u64(
        required(value, "envelope_lower_bytes"), "release lower bound");
    release.envelope_upper_bytes = require_u64(
        required(value, "envelope_upper_bytes"), "release upper bound");
    release.maximum_bytes = require_u64(required(value, "maximum_bytes"),
                                        "release maximum");
    release.minimum_bytes = require_u64(required(value, "minimum_bytes"),
                                        "release minimum");
    release.verified =
        require_bool(required(value, "verified"), "release verification");
    if (!release.verified ||
        release.envelope_lower_bytes > release.minimum_bytes ||
        release.minimum_bytes > release.maximum_bytes ||
        release.maximum_bytes > release.envelope_upper_bytes) {
        reject_parse(ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                     "release result is not verified inside its envelope");
    }
    return release;
}

std::vector<ProfilingDifferentialRepetitionEvidence>
parse_repetition_evidence(const json &value, std::string_view label) {
    if (!value.is_array() || value.empty() ||
        value.size() > profiling_differential_maximum_repetitions) {
        reject_parse(ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                     std::string(label) + " count is invalid");
    }
    std::vector<ProfilingDifferentialRepetitionEvidence> repetitions;
    repetitions.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto &record = value.at(index);
        require_exact_keys(
            record, {"delta_bytes", "ordinal", "provenance_sha256", "release"},
            "repetition evidence");
        const auto ordinal =
            require_u64(required(record, "ordinal"), "repetition ordinal");
        const auto provenance = require_string(
            required(record, "provenance_sha256"),
            "repetition provenance digest");
        require_digest(provenance, "repetition provenance digest");
        const auto delta =
            require_u64(required(record, "delta_bytes"), "repetition delta");
        if (ordinal != index ||
            delta > static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max())) {
            reject_parse(ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                         "repetition evidence is out of order or overflows");
        }
        repetitions.push_back(ProfilingDifferentialRepetitionEvidence{
            static_cast<std::uint32_t>(ordinal), provenance, delta,
            parse_release(required(record, "release")),
        });
    }
    return repetitions;
}

} // namespace

std::optional<ProfilingDifferentialMethodBinding>
resolve_retained_gtt_differential_method_binding(
    const ProfilingTransactionContext &transaction,
    std::string constraint_id) {
    try {
        const auto canonical =
            canonicalize_local_overlay_selector(transaction.selector);
        if (!canonical.accepted() ||
            canonical.selector_sha256 != transaction.selector_sha256) {
            return std::nullopt;
        }
        return resolved_method_binding(
            transaction.selector_sha256,
            transaction.observation_contract_sha256,
            transaction.selector.catalog_selector.constraints,
            std::move(constraint_id));
    } catch (...) {
        return std::nullopt;
    }
}

bool ProfilingDifferentialPreflightResult::accepted() const noexcept {
    return status == ProfilingDifferentialPreflightStatus::Accepted;
}

ProfilingDifferentialPreflightResult
preflight_retained_gtt_differential(
    const ParsedProfilingNoiseResult &noise,
    const ProfilingDifferentialInputDraft &draft,
    const ProfilingDifferentialMethodBinding &method_binding) {
    try {
        if (noise.bindings().procedure_revision_sha256 !=
            profiling_no_target_gtt_noise_procedure_revision_sha256) {
            return {
                ProfilingDifferentialPreflightStatus::InvalidProcedureBinding,
                "no-target noise procedure revision is unsupported",
            };
        }
        const auto supported_method =
            resolve_retained_gtt_differential_method_binding(
                draft.identity.transaction,
                draft.method_binding.constraint_id);
        if (!supported_method ||
            !method_bindings_equal(draft.method_binding, method_binding) ||
            !method_bindings_equal(draft.method_binding,
                                   *supported_method)) {
            return {
                ProfilingDifferentialPreflightStatus::InvalidMethodBinding,
                "retained-GTT method binding is invalid",
            };
        }
        const auto calibration_count =
            static_cast<std::uint64_t>(draft.calibration_repetitions);
        const auto validation_count =
            static_cast<std::uint64_t>(draft.validation_repetitions);
        if (calibration_count == 0 || validation_count == 0 ||
            calibration_count > profiling_differential_maximum_repetitions ||
            validation_count > profiling_differential_maximum_repetitions ||
            calibration_count + validation_count >
                profiling_differential_maximum_repetitions) {
            return {
                ProfilingDifferentialPreflightStatus::InvalidRepetitionCount,
                "repetition count exceeds the evaluator limit",
            };
        }
        return {
            ProfilingDifferentialPreflightStatus::Accepted,
            "retained-GTT evaluator preflight accepted",
        };
    } catch (...) {
        return {
            ProfilingDifferentialPreflightStatus::EvidenceUnavailable,
            "retained-GTT evaluator preflight failed closed",
        };
    }
}

ParsedProfilingDifferentialEvidence::ParsedProfilingDifferentialEvidence(
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
    std::string canonical_bytes)
    : frozen_input_sha256_(std::move(frozen_input_sha256)),
      method_binding_(std::move(method_binding)),
      exact_fingerprint_(std::move(exact_fingerprint)),
      selector_sha256_(std::move(selector_sha256)),
      calibration_revision_sha256_(
          std::move(calibration_revision_sha256)),
      accounting_(std::move(accounting)),
      n_gtt_bytes_(n_gtt_bytes),
      retained_gtt_bound_bytes_(retained_gtt_bound_bytes),
      calibration_repetitions_(std::move(calibration_repetitions)),
      validation_repetitions_(std::move(validation_repetitions)),
      owner_projection_coverage_(owner_projection_coverage),
      checksum_sha256_(std::move(checksum_sha256)),
      canonical_bytes_(std::move(canonical_bytes)) {}

std::string_view
ParsedProfilingDifferentialEvidence::frozen_input_sha256() const noexcept {
    return frozen_input_sha256_;
}

const ProfilingDifferentialMethodBinding &
ParsedProfilingDifferentialEvidence::method_binding() const noexcept {
    return method_binding_;
}

const LocalOverlaySelectorIdentity &
ParsedProfilingDifferentialEvidence::exact_fingerprint() const noexcept {
    return exact_fingerprint_;
}

std::string_view
ParsedProfilingDifferentialEvidence::selector_sha256() const noexcept {
    return selector_sha256_;
}

std::string_view ParsedProfilingDifferentialEvidence::
calibration_revision_sha256() const noexcept {
    return calibration_revision_sha256_;
}

const ProfilingDifferentialAccountingPartition &
ParsedProfilingDifferentialEvidence::accounting() const noexcept {
    return accounting_;
}

std::uint64_t
ParsedProfilingDifferentialEvidence::n_gtt_bytes() const noexcept {
    return n_gtt_bytes_;
}

std::uint64_t ParsedProfilingDifferentialEvidence::
retained_gtt_bound_bytes() const noexcept {
    return retained_gtt_bound_bytes_;
}

const std::vector<ProfilingDifferentialRepetitionEvidence> &
ParsedProfilingDifferentialEvidence::calibration_repetitions() const noexcept {
    return calibration_repetitions_;
}

const std::vector<ProfilingDifferentialRepetitionEvidence> &
ParsedProfilingDifferentialEvidence::validation_repetitions() const noexcept {
    return validation_repetitions_;
}

ProfilingDifferentialOwnerProjectionCoverage
ParsedProfilingDifferentialEvidence::owner_projection_coverage()
    const noexcept {
    return owner_projection_coverage_;
}

std::string_view
ParsedProfilingDifferentialEvidence::checksum_sha256() const noexcept {
    return checksum_sha256_;
}

std::string_view
ParsedProfilingDifferentialEvidence::canonical_bytes() const noexcept {
    return canonical_bytes_;
}

std::optional<DifferentialRetainedGttEvidenceDraft>
compose_retained_gtt_profiling_input_evidence(
    const ParsedProfilingDifferentialEvidence &evidence,
    std::string transient_envelope_sha256) {
    try {
        if (!digest_is_valid(transient_envelope_sha256)) {
            return std::nullopt;
        }

        ProfilingOwnerCoverage owner_projection_coverage;
        switch (evidence.owner_projection_coverage()) {
        case ProfilingDifferentialOwnerProjectionCoverage::Complete:
            owner_projection_coverage = ProfilingOwnerCoverage::Complete;
            break;
        case ProfilingDifferentialOwnerProjectionCoverage::Incomplete:
            owner_projection_coverage = ProfilingOwnerCoverage::Incomplete;
            break;
        case ProfilingDifferentialOwnerProjectionCoverage::Absent:
            owner_projection_coverage = ProfilingOwnerCoverage::Unknown;
            break;
        default:
            return std::nullopt;
        }

        return DifferentialRetainedGttEvidenceDraft{
            ClaimAmount{
                evidence.method_binding().constraint_id,
                ClaimUnit::Bytes,
                evidence.retained_gtt_bound_bytes(),
            },
            std::string(evidence.checksum_sha256()),
            std::move(transient_envelope_sha256),
            owner_projection_coverage,
        };
    } catch (...) {
        return std::nullopt;
    }
}

bool ProfilingDifferentialEvaluationResult::accepted() const noexcept {
    return status == ProfilingDifferentialEvaluationStatus::Accepted &&
           disposition ==
               ProfilingDifferentialRevalidationDisposition::Continue &&
           evidence.has_value();
}

bool ProfilingDifferentialEvidenceParseResult::accepted() const noexcept {
    return status == ProfilingDifferentialEvidenceParseStatus::Accepted &&
           evidence.has_value();
}

ProfilingDifferentialEvaluationResult
evaluate_retained_gtt_differential(
    const FrozenProfilingDifferentialInput &input,
    ProfilingDifferentialMethodBinding method_binding,
    const std::vector<ProfilingDifferentialRepetition> &repetitions) {
    try {
        if (input.noise().bindings().procedure_revision_sha256 !=
            profiling_no_target_gtt_noise_procedure_revision_sha256) {
            return reject(
                ProfilingDifferentialEvaluationStatus::InvalidMethodBinding,
                "no-target noise procedure revision is unsupported");
        }
        const auto &frozen_method_binding = input.method_binding();
        const auto supported_method =
            resolve_retained_gtt_differential_method_binding(
                input.identity().transaction,
                frozen_method_binding.constraint_id);
        if (!supported_method ||
            !method_bindings_equal(method_binding, frozen_method_binding) ||
            !method_bindings_equal(frozen_method_binding,
                                   *supported_method)) {
            return reject(
                ProfilingDifferentialEvaluationStatus::InvalidMethodBinding,
                "retained-GTT method binding is invalid");
        }
        const auto calibration_count = input.calibration_repetitions();
        const auto validation_count = input.validation_repetitions();
        if (calibration_count >
                profiling_differential_maximum_repetitions ||
            validation_count >
                profiling_differential_maximum_repetitions ||
            repetitions.size() >
                profiling_differential_maximum_repetitions ||
            repetitions.size() !=
                static_cast<std::size_t>(calibration_count) +
                    static_cast<std::size_t>(validation_count)) {
            return reject(
                ProfilingDifferentialEvaluationStatus::InvalidRepetitionCount,
                "repetition records do not match the frozen counts");
        }

        std::vector<RevalidationAuthority> authorities;
        authorities.reserve(repetitions.size());
        std::string previous_receipt_sha256 =
            input.revision().attempt_receipt_sha256;
        std::optional<ProfilingDifferentialRevalidationResult>
            terminal_revalidation;
        for (std::size_t index = 0; index < repetitions.size(); ++index) {
            if (terminal_revalidation) break;
            const auto &repetition = repetitions[index];
            const auto expected_phase =
                index < calibration_count
                    ? ProfilingDifferentialRepetitionPhase::Calibration
                    : ProfilingDifferentialRepetitionPhase::Validation;
            const auto expected_ordinal = static_cast<std::uint32_t>(
                index < calibration_count ? index
                                          : index - calibration_count);

            ProfilingDifferentialRevalidationResult revalidation;
            auto checked_at = std::chrono::steady_clock::time_point{};
            if (repetition.revalidation_receipt) {
                const auto &receipt = *repetition.revalidation_receipt;
                if (!validate_profiling_differential_revalidation_receipt(
                        input, receipt, expected_phase, expected_ordinal,
                        previous_receipt_sha256)) {
                    revalidation.status =
                        ProfilingDifferentialRevalidationStatus::
                            RevisionRejected;
                    revalidation.disposition =
                        ProfilingDifferentialRevalidationDisposition::
                            RejectRevision;
                    revalidation.diagnostic =
                        "revalidation receipt chain is invalid";
                } else {
                    revalidation.status = receipt.status();
                    revalidation.disposition = receipt.disposition();
                    revalidation.diagnostic =
                        "retained revalidation receipt was consumed";
                    revalidation.receipt = receipt;
                    checked_at = receipt.checked_at();
                    previous_receipt_sha256 = receipt.receipt_sha256();
                }
            } else {
                revalidation.status =
                    ProfilingDifferentialRevalidationStatus::RevisionRejected;
                revalidation.disposition =
                    ProfilingDifferentialRevalidationDisposition::
                        RejectRevision;
                revalidation.diagnostic =
                    "revalidation receipt is missing";
            }
            authorities.push_back({
                checked_at, revalidation.status, revalidation.disposition,
                revalidation.diagnostic, revalidation.receipt});
            if (!revalidation.accepted()) {
                terminal_revalidation = std::move(revalidation);
            }
        }

        if (terminal_revalidation &&
            terminal_revalidation->disposition ==
                ProfilingDifferentialRevalidationDisposition::
                    InvalidateNoiseResult) {
            auto result = reject_observation(
                input,
                ProfilingDifferentialEvaluationStatus::RevalidationRejected,
                terminal_revalidation->diagnostic,
                terminal_revalidation->disposition);
            result.revalidation_status = terminal_revalidation->status;
            return result;
        }

        std::vector<BoundedRepetitionIngestion> ingestions;
        ingestions.reserve(authorities.size());
        for (std::size_t index = 0; index < authorities.size(); ++index) {
            if (authorities[index].disposition !=
                ProfilingDifferentialRevalidationDisposition::Continue) {
                break;
            }
            ingestions.push_back(ingest_repetition(repetitions[index], input));
        }
        for (const auto &ingestion : ingestions) {
            if (audit_repetition_source_facts(ingestion, input) ==
                MonotoneNoiseValidity::Invalidated) {
                return reject_observation(
                    input,
                    ProfilingDifferentialEvaluationStatus::SourceDrift,
                    "authenticated repetition source binding changed");
            }
        }
        if (terminal_revalidation) {
            auto result = reject_observation(
                input,
                ProfilingDifferentialEvaluationStatus::RevalidationRejected,
                terminal_revalidation->diagnostic,
                terminal_revalidation->disposition);
            result.revalidation_status = terminal_revalidation->status;
            return result;
        }

        for (const auto &ingestion : ingestions) {
            for (const auto *plateau : {&ingestion.baseline,
                                        &ingestion.loaded,
                                        &ingestion.release}) {
                for (std::size_t index = 0;
                     index < plateau->retained_point_count; ++index) {
                    if (!retained_point_fields_are_bounded(
                            plateau->plateau->points[index])) {
                        return reject_observation(
                            input,
                            ProfilingDifferentialEvaluationStatus::InvalidPoint,
                            "retained point fields exceed their bounds");
                    }
                }
            }
        }

        std::vector<ProfilingDifferentialRepetitionEvidence>
            calibration_evidence;
        std::vector<ProfilingDifferentialRepetitionEvidence>
            validation_evidence;
        calibration_evidence.reserve(calibration_count);
        validation_evidence.reserve(validation_count);
        ProfilingDifferentialOwnerProjectionCoverage projection_coverage =
            ProfilingDifferentialOwnerProjectionCoverage::Complete;
        std::uint64_t maximum_calibration_delta = 0;
        std::uint64_t retained_bound = 0;
        std::uint64_t component_provenance_bytes = 0;
        std::optional<std::chrono::steady_clock::time_point>
            previous_release_completed_at;

        for (std::size_t index = 0; index < repetitions.size(); ++index) {
            const auto &repetition = repetitions[index];
            const auto expected_phase =
                index < calibration_count
                    ? ProfilingDifferentialRepetitionPhase::Calibration
                    : ProfilingDifferentialRepetitionPhase::Validation;
            const auto expected_ordinal = static_cast<std::uint32_t>(
                expected_phase ==
                        ProfilingDifferentialRepetitionPhase::Calibration
                    ? index
                    : index - calibration_count);
            const auto revalidation_checked_at = authorities[index].checked_at;
            const auto &ingestion = ingestions[index];
            if (repetition.phase != expected_phase ||
                repetition.ordinal != expected_ordinal) {
                return reject(
                    ProfilingDifferentialEvaluationStatus::
                        InvalidRepetitionOrder,
                    "calibration and validation records are not disjoint and ordered");
            }
            if (previous_release_completed_at &&
                (revalidation_checked_at <
                     *previous_release_completed_at ||
                 !repetition.baseline.marker ||
                 repetition.baseline.marker->marked_at <
                     *previous_release_completed_at)) {
                return reject(
                    ProfilingDifferentialEvaluationStatus::
                        InvalidRepetitionOrder,
                    "profiling repetitions overlap");
            }

            const auto baseline = evaluate_plateau(
                ingestion.baseline,
                ProfilingDifferentialMarkerKind::BaselineReady, input);
            if (!baseline.summary) {
                return reject_observation(
                    input, baseline.status, baseline.diagnostic);
            }
            const auto loaded = evaluate_plateau(
                ingestion.loaded,
                ProfilingDifferentialMarkerKind::LoadedReady, input);
            if (!loaded.summary) {
                return reject_observation(
                    input, loaded.status, loaded.diagnostic);
            }
            const auto release = evaluate_plateau(
                ingestion.release,
                ProfilingDifferentialMarkerKind::ReleaseReady, input);
            if (!release.summary) {
                return reject_observation(
                    input, release.status, release.diagnostic);
            }
            if (revalidation_checked_at >
                    repetition.baseline.marker->marked_at ||
                baseline.summary->completed_at >
                    repetition.loaded.marker->marked_at ||
                loaded.summary->completed_at >
                    repetition.release.marker->marked_at) {
                return reject(
                    ProfilingDifferentialEvaluationStatus::InvalidWindow,
                    "revalidation and plateau windows are not ordered");
            }
            auto release_completed_at = release.summary->completed_at;

            if (loaded.summary->maximum < baseline.summary->minimum) {
                return reject(
                    ProfilingDifferentialEvaluationStatus::NegativeDelta,
                    "loaded plateau is below its baseline");
            }
            const auto delta =
                loaded.summary->maximum - baseline.summary->minimum;
            if (delta > static_cast<std::uint64_t>(
                            std::numeric_limits<std::int64_t>::max())) {
                return reject(
                    ProfilingDifferentialEvaluationStatus::
                        ArithmeticOverflow,
                    "retained-GTT signed delta overflows");
            }

            const auto n_gtt = input.noise().n_gtt_bytes();
            const auto release_lower = baseline.summary->minimum > n_gtt
                                           ? baseline.summary->minimum - n_gtt
                                           : 0;
            if (baseline.summary->maximum >
                std::numeric_limits<std::uint64_t>::max() - n_gtt) {
                return reject(
                    ProfilingDifferentialEvaluationStatus::
                        ArithmeticOverflow,
                    "release-envelope upper bound overflows");
            }
            const auto release_upper = baseline.summary->maximum + n_gtt;
            for (std::size_t point_index = 0;
                 point_index < ingestion.release.retained_point_count;
                 ++point_index) {
                const auto &point =
                    repetition.release.points[point_index];
                if (point.scheduled_at <
                    repetition.release.marker->marked_at) {
                    continue;
                }
                if (!point.global_gtt_used_bytes ||
                    point.read_started_at < point.scheduled_at ||
                    point.read_finished_at < point.read_started_at ||
                    !digest_is_valid(point.provenance_sha256) ||
                    !bindings_are_valid(point.observed_bindings)) {
                    return reject(
                        ProfilingDifferentialEvaluationStatus::InvalidPoint,
                        "post-release point is incomplete");
                }
                release_completed_at =
                    std::max(release_completed_at, point.read_finished_at);
                if (point.frozen_input_sha256 !=
                        input.frozen_input_sha256() ||
                    point.selector_sha256 !=
                        input.identity().transaction.selector_sha256) {
                    return reject(
                        ProfilingDifferentialEvaluationStatus::IdentityDrift,
                        "post-release point identity changed");
                }
                if (point.target_client_identity_sha256 !=
                        input.identity().target_client_identity_sha256 ||
                    point.target_containment_identity_sha256 !=
                        input.identity()
                            .target_containment_identity_sha256) {
                    return reject(
                        ProfilingDifferentialEvaluationStatus::ActorDrift,
                        "post-release actor or containment changed");
                }
                if (!bindings_equal(point.observed_bindings,
                                    input.noise().bindings())) {
                    return reject_observation(
                        input,
                        ProfilingDifferentialEvaluationStatus::SourceDrift,
                        "post-release source binding changed");
                }
                const auto projection = classify_owner_projection(
                    point, OwnerProjectionContext::PostRelease);
                if (!projection.coverage) {
                    return reject(projection.status,
                                  std::string(projection.diagnostic));
                }
                projection_coverage = weakest_coverage(
                    projection_coverage, *projection.coverage);
                if (*point.global_gtt_used_bytes < release_lower ||
                    *point.global_gtt_used_bytes > release_upper) {
                    return reject(
                        ProfilingDifferentialEvaluationStatus::
                            ReleaseEnvelopeBreach,
                        "post-release point exceeds the baseline envelope");
                }
            }
            if (release.summary->minimum < release_lower ||
                release.summary->maximum > release_upper) {
                return reject(
                    ProfilingDifferentialEvaluationStatus::
                        ReleaseEnvelopeBreach,
                    "post-release points exceed the baseline envelope");
            }
            previous_release_completed_at = release_completed_at;

            if (!authorities[index].receipt) {
                return reject(
                    ProfilingDifferentialEvaluationStatus::
                        RevalidationRejected,
                    "accepted repetition has no revalidation receipt");
            }
            const auto provenance = repetition_provenance(
                repetition, *authorities[index].receipt);
            if (!provenance ||
                provenance->encoded_bytes >
                    maximum_component_provenance_bytes -
                        component_provenance_bytes) {
                return reject(
                    ProfilingDifferentialEvaluationStatus::DigestUnavailable,
                    "repetition provenance SHA-256 is unavailable");
            }
            component_provenance_bytes += provenance->encoded_bytes;
            ProfilingDifferentialRepetitionEvidence evidence;
            evidence.ordinal = repetition.ordinal;
            evidence.provenance_sha256 = std::move(provenance->sha256);
            evidence.delta_bytes = delta;
            evidence.release = {
                release_lower,
                release_upper,
                release.summary->minimum,
                release.summary->maximum,
                true,
            };

            projection_coverage = weakest_coverage(
                projection_coverage,
                weakest_coverage(
                    baseline.summary->projection_coverage,
                    weakest_coverage(
                        loaded.summary->projection_coverage,
                        release.summary->projection_coverage)));

            if (index < calibration_count) {
                maximum_calibration_delta =
                    std::max(maximum_calibration_delta, delta);
                calibration_evidence.push_back(std::move(evidence));
                if (calibration_evidence.size() == calibration_count) {
                    const auto maximum =
                        std::numeric_limits<std::uint64_t>::max();
                    if (input.accounting().x_gtt_bytes > maximum - n_gtt) {
                        return reject(
                            ProfilingDifferentialEvaluationStatus::
                                ArithmeticOverflow,
                            "retained allowance overflows");
                    }
                    auto allowance = n_gtt + input.accounting().x_gtt_bytes;
                    if (input.accounting().m_gtt_bytes >
                        maximum - allowance) {
                        return reject(
                            ProfilingDifferentialEvaluationStatus::
                                ArithmeticOverflow,
                            "retained allowance overflows");
                    }
                    allowance += input.accounting().m_gtt_bytes;
                    if (maximum_calibration_delta > maximum - allowance) {
                        return reject(
                            ProfilingDifferentialEvaluationStatus::
                                ArithmeticOverflow,
                            "retained-GTT bound overflows");
                    }
                    retained_bound = maximum_calibration_delta + allowance;
                }
            } else {
                if (delta > retained_bound) {
                    return reject(
                        ProfilingDifferentialEvaluationStatus::
                            ValidationExceeded,
                        "validation delta exceeds the frozen retained bound");
                }
                validation_evidence.push_back(std::move(evidence));
            }
        }

        const auto &identity = input.identity();
        json payload{
            {"accounting_terms",
             json{{"m_gtt_bytes", input.accounting().m_gtt_bytes},
                  {"m_gtt_policy_sha256",
                   input.accounting().m_gtt_policy_sha256},
                  {"n_gtt_bytes", input.noise().n_gtt_bytes()},
                  {"partition_contract_sha256",
                   input.accounting().partition_contract_sha256},
                  {"x_gtt_bytes", input.accounting().x_gtt_bytes},
                  {"x_gtt_evidence_sha256",
                   input.accounting().x_gtt_evidence_sha256}}},
            {"calibration_repetitions",
             repetitions_document(calibration_evidence)},
            {"calibration_revision_sha256",
             input.revision().calibration_revision_sha256},
            {"covered_effect", frozen_method_binding.covered_effect},
            {"exact_fingerprint",
             selector_document(identity.transaction.selector)},
            {"frozen_identities",
             json{{"action_lease_closure_sha256",
                   identity.transaction.action_lease_closure_sha256},
                  {"attempt_receipt_sha256",
                   input.revision().attempt_receipt_sha256},
                  {"counter_continuity_epoch_sha256",
                   identity.counter_continuity_epoch_sha256},
                  {"deployment_id", identity.transaction.deployment_id},
                  {"noise_bindings", bindings_document(input.noise().bindings())},
                  {"noise_bindings_sha256", input.noise().bindings_sha256()},
                  {"noise_result_checksum_sha256",
                   input.noise().checksum_sha256()},
                  {"noise_trace_provenance_sha256",
                   input.noise().trace_provenance_sha256()},
                  {"observation_contract_sha256",
                   identity.transaction.observation_contract_sha256},
                  {"ownership_recovery_evidence_sha256",
                   identity.transaction
                       .ownership_recovery_evidence_sha256},
                  {"predictor_contract_sha256",
                   identity.transaction.predictor_contract_sha256},
                  {"profiling_sequence", identity.transaction.sequence},
                  {"profiling_transaction_id",
                   identity.transaction.profiling_transaction_id},
                  {"safety_contract_sha256", identity.safety_contract_sha256},
                  {"selector_sha256", identity.transaction.selector_sha256},
                  {"source_generations",
                   json{{"backend", identity.transaction.generations.backend},
                        {"configuration",
                         identity.transaction.generations.configuration},
                        {"device", identity.transaction.generations.device},
                        {"driver", identity.transaction.generations.driver},
                        {"model", identity.transaction.generations.model},
                        {"topology", identity.transaction.generations.topology},
                        {"workload",
                         identity.transaction.generations.workload}}},
                  {"target_client_identity_sha256",
                   identity.target_client_identity_sha256},
                  {"target_containment_identity_sha256",
                   identity.target_containment_identity_sha256}}},
            {"frozen_input_sha256", input.frozen_input_sha256()},
            {"method_binding",
             json{{"constraint_id", frozen_method_binding.constraint_id},
                  {"constraint_revision_sha256",
                   frozen_method_binding.constraint_revision_sha256},
                  {"method_id", frozen_method_binding.method_id},
                  {"method_revision_sha256",
                   frozen_method_binding.method_revision_sha256}}},
            {"owner_projection_coverage",
             projection_coverage_wire(projection_coverage)},
            {"retained_gtt_bound_bytes", retained_bound},
            {"retained_gtt_claim",
             json{{"amount", retained_bound},
                  {"constraint_id", frozen_method_binding.constraint_id},
                  {"unit", "bytes"}}},
            {"schema", json{{"major", 1}, {"minor", 0}}},
            {"validation_repetitions",
             repetitions_document(validation_evidence)},
        };
        const auto checksum = component_checksum(payload);
        if (!checksum) {
            return reject(
                ProfilingDifferentialEvaluationStatus::DigestUnavailable,
                "component evidence SHA-256 is unavailable");
        }
        payload["checksum_sha256"] = *checksum;
        auto canonical_bytes = payload.dump();

        if (canonical_bytes.size() >
            profiling_differential_maximum_canonical_evidence_bytes) {
            return reject(
                ProfilingDifferentialEvaluationStatus::EvidenceUnavailable,
                "canonical component evidence exceeds its proven ceiling");
        }
        auto parsed = parse_profiling_differential_evidence(canonical_bytes);
        if (!parsed.accepted() ||
            parsed.evidence->canonical_bytes() != canonical_bytes) {
            return reject(
                ProfilingDifferentialEvaluationStatus::EvidenceUnavailable,
                "canonical component evidence did not round-trip");
        }

        ProfilingDifferentialEvaluationResult result;
        result.status = ProfilingDifferentialEvaluationStatus::Accepted;
        result.disposition =
            ProfilingDifferentialRevalidationDisposition::Continue;
        result.diagnostic = "retained-GTT differential evidence accepted";
        result.evidence = std::move(parsed.evidence);
        return result;
    } catch (...) {
        return reject(
            ProfilingDifferentialEvaluationStatus::EvidenceUnavailable,
            "retained-GTT differential evaluation failed closed");
    }
}

ProfilingDifferentialEvidenceParseResult
parse_profiling_differential_evidence(std::string_view bytes) {
    try {
        const auto document = parse_json(bytes);
        require_exact_keys(
            document,
            {"accounting_terms", "calibration_repetitions",
             "calibration_revision_sha256", "checksum_sha256",
             "covered_effect", "exact_fingerprint", "frozen_identities",
             "frozen_input_sha256", "method_binding",
             "owner_projection_coverage", "retained_gtt_bound_bytes",
             "retained_gtt_claim", "schema", "validation_repetitions"},
            "component evidence");
        parse_schema(required(document, "schema"));

        const auto checksum = require_string(
            required(document, "checksum_sha256"), "component checksum");
        require_digest(checksum, "component checksum");
        const auto covered_effect = require_string(
            required(document, "covered_effect"), "covered effect");
        if (covered_effect != profiling_differential_covered_effect) {
            reject_parse(ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                         "component covered effect is unsupported");
        }

        auto method_binding =
            parse_method_binding(required(document, "method_binding"));
        auto exact_fingerprint =
            parse_selector(required(document, "exact_fingerprint"));
        auto canonical_fingerprint =
            canonicalize_local_overlay_selector(exact_fingerprint);
        if (!canonical_fingerprint.accepted()) {
            reject_parse(ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                         "exact fingerprint is invalid");
        }

        const auto &frozen_identities =
            required(document, "frozen_identities");
        require_exact_keys(
            frozen_identities,
            {"action_lease_closure_sha256", "attempt_receipt_sha256",
             "counter_continuity_epoch_sha256", "deployment_id",
             "noise_bindings", "noise_bindings_sha256",
             "noise_result_checksum_sha256", "noise_trace_provenance_sha256",
             "observation_contract_sha256",
             "ownership_recovery_evidence_sha256",
             "predictor_contract_sha256", "profiling_sequence",
             "profiling_transaction_id", "safety_contract_sha256",
             "selector_sha256", "source_generations",
             "target_client_identity_sha256",
             "target_containment_identity_sha256"},
            "frozen identities");
        const auto action_lease_closure = require_string(
            required(frozen_identities, "action_lease_closure_sha256"),
            "action lease closure digest");
        const auto attempt_receipt = require_string(
            required(frozen_identities, "attempt_receipt_sha256"),
            "attempt receipt digest");
        const auto counter_continuity_epoch = require_string(
            required(frozen_identities,
                     "counter_continuity_epoch_sha256"),
            "counter continuity epoch digest");
        const auto deployment_id = require_string(
            required(frozen_identities, "deployment_id"), "deployment ID");
        const auto noise_bindings =
            parse_bindings(required(frozen_identities, "noise_bindings"));
        const auto noise_bindings_sha256 = require_string(
            required(frozen_identities, "noise_bindings_sha256"),
            "noise bindings digest");
        const auto noise_result_checksum = require_string(
            required(frozen_identities, "noise_result_checksum_sha256"),
            "noise result checksum");
        const auto noise_trace_provenance = require_string(
            required(frozen_identities, "noise_trace_provenance_sha256"),
            "noise trace provenance digest");
        const auto observation_contract = require_string(
            required(frozen_identities, "observation_contract_sha256"),
            "observation contract digest");
        const auto ownership_recovery_evidence = require_string(
            required(frozen_identities,
                     "ownership_recovery_evidence_sha256"),
            "ownership recovery evidence digest");
        const auto predictor_contract = require_string(
            required(frozen_identities, "predictor_contract_sha256"),
            "predictor contract digest");
        const auto profiling_sequence = require_u64(
            required(frozen_identities, "profiling_sequence"),
            "profiling sequence");
        const auto profiling_transaction_id = require_string(
            required(frozen_identities, "profiling_transaction_id"),
            "profiling transaction ID");
        const auto safety_contract = require_string(
            required(frozen_identities, "safety_contract_sha256"),
            "safety contract digest");
        const auto selector_sha256 = require_string(
            required(frozen_identities, "selector_sha256"),
            "selector digest");
        const auto target_client = require_string(
            required(frozen_identities, "target_client_identity_sha256"),
            "target client identity digest");
        const auto target_containment = require_string(
            required(frozen_identities,
                     "target_containment_identity_sha256"),
            "target containment identity digest");
        const auto &source_generations =
            required(frozen_identities, "source_generations");
        require_exact_keys(source_generations,
                           {"backend", "configuration", "device", "driver",
                            "model", "topology", "workload"},
                           "source generations");
        bool generations_valid = true;
        for (const auto key : {"backend", "configuration", "device", "driver",
                               "model", "topology", "workload"}) {
            generations_valid =
                generations_valid &&
                require_u64(required(source_generations, key),
                            "source generation") != 0;
        }
        require_digest(action_lease_closure,
                       "action lease closure digest");
        require_digest(attempt_receipt, "attempt receipt digest");
        require_digest(counter_continuity_epoch,
                       "counter continuity epoch digest");
        require_digest(deployment_id, "deployment ID");
        require_digest(noise_bindings_sha256, "noise bindings digest");
        require_digest(noise_result_checksum, "noise result checksum");
        require_digest(noise_trace_provenance,
                       "noise trace provenance digest");
        require_digest(observation_contract, "observation contract digest");
        require_digest(ownership_recovery_evidence,
                       "ownership recovery evidence digest");
        require_digest(predictor_contract, "predictor contract digest");
        require_identifier(profiling_transaction_id,
                           "profiling transaction ID");
        require_digest(safety_contract, "safety contract digest");
        require_digest(selector_sha256, "selector digest");
        require_digest(target_client, "target client identity digest");
        require_digest(target_containment,
                       "target containment identity digest");
        if (profiling_sequence == 0 || !generations_valid ||
            deployment_id != noise_bindings.deployment_id ||
            counter_continuity_epoch !=
                noise_bindings.counter_continuity_epoch_sha256) {
            reject_parse(ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                         "frozen transaction identity is inconsistent");
        }

        std::string noise_binding_bytes(noise_bindings_domain,
                                        sizeof(noise_bindings_domain) - 1);
        noise_binding_bytes += bindings_document(noise_bindings).dump();
        const auto expected_noise_bindings =
            sha256_hex(noise_binding_bytes);
        if (!expected_noise_bindings) {
            return {ProfilingDifferentialEvidenceParseStatus::
                        DigestUnavailable,
                    "noise bindings SHA-256 is unavailable", std::nullopt};
        }
        if (noise_bindings_sha256 != *expected_noise_bindings ||
            selector_sha256 !=
                canonical_fingerprint.selector_sha256) {
            reject_parse(
                ProfilingDifferentialEvidenceParseStatus::DigestMismatch,
                "frozen identity digest does not match its content");
        }
        if (exact_fingerprint.device_identity_sha256 !=
                noise_bindings.device_identity_sha256 ||
            exact_fingerprint.topology_sha256 !=
                noise_bindings.topology_sha256 ||
            exact_fingerprint.driver_identity_sha256 !=
                noise_bindings.driver_identity_sha256 ||
            std::find(exact_fingerprint.catalog_selector.constraints.begin(),
                      exact_fingerprint.catalog_selector.constraints.end(),
                      ConstraintKind::GpuSharedResidency) ==
                exact_fingerprint.catalog_selector.constraints.end()) {
            reject_parse(ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                         "frozen fingerprint and noise identities disagree");
        }
        if (noise_bindings.procedure_revision_sha256 !=
            profiling_no_target_gtt_noise_procedure_revision_sha256) {
            reject_parse(ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                         "no-target noise procedure revision is unsupported");
        }
        const auto supported_method = resolved_method_binding(
            selector_sha256, observation_contract,
            exact_fingerprint.catalog_selector.constraints,
            method_binding.constraint_id);
        if (!supported_method ||
            !method_bindings_equal(method_binding, *supported_method)) {
            reject_parse(ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                         "component method binding is unsupported");
        }

        const auto frozen_input_sha256 = require_string(
            required(document, "frozen_input_sha256"),
            "frozen input digest");
        const auto calibration_revision_sha256 = require_string(
            required(document, "calibration_revision_sha256"),
            "calibration revision digest");
        require_digest(frozen_input_sha256, "frozen input digest");
        require_digest(calibration_revision_sha256,
                       "calibration revision digest");

        std::uint64_t n_gtt_bytes = 0;
        auto accounting = parse_accounting(
            required(document, "accounting_terms"), n_gtt_bytes);
        auto calibration_repetitions = parse_repetition_evidence(
            required(document, "calibration_repetitions"),
            "calibration repetition");
        auto validation_repetitions = parse_repetition_evidence(
            required(document, "validation_repetitions"),
            "validation repetition");
        if (calibration_repetitions.size() +
                validation_repetitions.size() >
            profiling_differential_maximum_repetitions) {
            reject_parse(ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                         "component evidence exceeds the repetition limit");
        }
        const auto retained_bound = require_u64(
            required(document, "retained_gtt_bound_bytes"),
            "retained-GTT bound");
        const auto &retained_claim = required(document, "retained_gtt_claim");
        require_exact_keys(retained_claim,
                           {"amount", "constraint_id", "unit"},
                           "retained-GTT claim");
        const auto retained_claim_amount = require_u64(
            required(retained_claim, "amount"), "retained claim amount");
        const auto retained_claim_constraint = require_string(
            required(retained_claim, "constraint_id"),
            "retained claim constraint ID");
        const auto retained_claim_unit = require_string(
            required(retained_claim, "unit"), "retained claim unit");
        if (retained_claim_amount != retained_bound ||
            retained_claim_constraint != method_binding.constraint_id ||
            retained_claim_unit != "bytes") {
            reject_parse(ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                         "retained-GTT claim does not match its method and bound");
        }
        const auto maximum_calibration = std::max_element(
            calibration_repetitions.begin(), calibration_repetitions.end(),
            [](const auto &left, const auto &right) {
                return left.delta_bytes < right.delta_bytes;
            })->delta_bytes;
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        auto allowance = n_gtt_bytes + accounting.x_gtt_bytes;
        allowance += accounting.m_gtt_bytes;
        if (maximum_calibration > maximum - allowance ||
            retained_bound != maximum_calibration + allowance ||
            std::any_of(validation_repetitions.begin(),
                        validation_repetitions.end(),
                        [&](const auto &repetition) {
                            return repetition.delta_bytes > retained_bound;
                        })) {
            reject_parse(ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                         "retained-GTT bound or validation result is invalid");
        }

        const auto coverage_wire = require_string(
            required(document, "owner_projection_coverage"),
            "owner projection coverage");
        ProfilingDifferentialOwnerProjectionCoverage coverage;
        if (coverage_wire == "complete") {
            coverage =
                ProfilingDifferentialOwnerProjectionCoverage::Complete;
        } else if (coverage_wire == "incomplete") {
            coverage =
                ProfilingDifferentialOwnerProjectionCoverage::Incomplete;
        } else if (coverage_wire == "absent") {
            coverage = ProfilingDifferentialOwnerProjectionCoverage::Absent;
        } else {
            reject_parse(ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                         "owner projection coverage is unknown");
        }

        auto payload = document;
        payload.erase("checksum_sha256");
        const auto expected_checksum = component_checksum(payload);
        if (!expected_checksum) {
            return {ProfilingDifferentialEvidenceParseStatus::
                        DigestUnavailable,
                    "component evidence SHA-256 is unavailable", std::nullopt};
        }
        if (checksum != *expected_checksum) {
            reject_parse(
                ProfilingDifferentialEvidenceParseStatus::DigestMismatch,
                "component evidence checksum does not match");
        }
        payload["checksum_sha256"] = checksum;
        auto canonical_bytes = payload.dump();
        if (bytes != canonical_bytes) {
            reject_parse(ProfilingDifferentialEvidenceParseStatus::NonCanonical,
                         "component evidence is not canonical JSON");
        }

        ProfilingDifferentialEvidenceParseResult result;
        result.status = ProfilingDifferentialEvidenceParseStatus::Accepted;
        result.evidence = ParsedProfilingDifferentialEvidence(
            frozen_input_sha256, std::move(method_binding),
            std::move(exact_fingerprint), selector_sha256,
            calibration_revision_sha256, std::move(accounting), n_gtt_bytes,
            retained_bound, std::move(calibration_repetitions),
            std::move(validation_repetitions), coverage, checksum,
            std::move(canonical_bytes));
        return result;
    } catch (const ComponentParseFailure &failure) {
        return {failure.status(), bounded_diagnostic(failure.what()),
                std::nullopt};
    } catch (...) {
        return {ProfilingDifferentialEvidenceParseStatus::InvalidValue,
                "component evidence validation failed closed", std::nullopt};
    }
}

} // namespace lemon::residency
