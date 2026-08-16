#pragma once

#include "lemon/residency/generated_contract.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lemon::residency {

using ExplanationClock = std::chrono::system_clock;
using ExplanationTimePoint = ExplanationClock::time_point;

struct WireReasonProjection {
    std::string code;
    std::string category_id;
    std::string presentation_id;
    std::string severity;
    std::string title;
    std::string default_message;
};

struct RenderedReason {
    std::string code;
    std::string category_id;
    std::string presentation_id;
    std::string severity;
    std::string title;
    std::string default_message;
};

enum class ReasonRenderStatus {
    Known,
    UnknownCategoryFallback,
    UnknownFallback,
    UnsupportedSchema,
};

struct ReasonRenderResult {
    ReasonRenderStatus status;
    std::optional<RenderedReason> reason;
};

ReasonRenderResult
render_reason_projection(SchemaVersion supported_version,
                         SchemaVersion projection_version,
                         const WireReasonProjection &projection);

struct OperationExplanationDraft {
    SchemaVersion schema_version{1, 0};
    std::string operation_id;
    std::optional<std::string> plan_id;
    OperationFamily family{OperationFamily::ResourceLifecycle};
    OperationKind operation_kind{OperationKind::Admission};
    OperationPhase phase{OperationPhase::Evaluating};
    std::optional<TerminalOutcome> terminal_outcome;
    std::uint64_t explanation_revision{0};
    std::vector<WireReasonProjection> reasons;
};

class OperationExplanationRevision {
public:
    OperationExplanationRevision() = delete;

    const SchemaVersion &schema_version() const noexcept;
    const std::string &operation_id() const noexcept;
    const std::optional<std::string> &plan_id() const noexcept;
    OperationFamily family() const noexcept;
    OperationKind operation_kind() const noexcept;
    OperationPhase phase() const noexcept;
    const std::optional<TerminalOutcome> &terminal_outcome() const noexcept;
    std::uint64_t explanation_revision() const noexcept;
    const std::vector<RenderedReason> &reasons() const noexcept;
    const std::optional<std::string> &primary_reason_code() const noexcept;
    ExplanationTimePoint created_at() const noexcept;
    ExplanationTimePoint updated_at() const noexcept;
    const std::optional<ExplanationTimePoint> &expires_at() const noexcept;
    const std::optional<ExplanationTimePoint> &forgotten_at() const noexcept;

private:
    OperationExplanationRevision(
        OperationExplanationDraft draft, std::vector<RenderedReason> reasons,
        ExplanationTimePoint created_at, ExplanationTimePoint updated_at,
        std::optional<ExplanationTimePoint> expires_at,
        std::optional<ExplanationTimePoint> forgotten_at);

    OperationExplanationDraft draft_;
    std::vector<RenderedReason> reasons_;
    std::optional<std::string> primary_reason_code_;
    ExplanationTimePoint created_at_;
    ExplanationTimePoint updated_at_;
    std::optional<ExplanationTimePoint> expires_at_;
    std::optional<ExplanationTimePoint> forgotten_at_;

    friend class OperationExplanationStoreSnapshot;
};

class OperationExplanationTombstone {
public:
    OperationExplanationTombstone() = delete;

    const std::string &operation_id() const noexcept;
    std::uint64_t last_explanation_revision() const noexcept;
    ExplanationTimePoint detail_expired_at() const noexcept;
    ExplanationTimePoint forgotten_at() const noexcept;

private:
    OperationExplanationTombstone(std::string operation_id,
                                  std::uint64_t last_explanation_revision,
                                  ExplanationTimePoint detail_expired_at,
                                  ExplanationTimePoint forgotten_at);

    std::string operation_id_;
    std::uint64_t last_explanation_revision_;
    ExplanationTimePoint detail_expired_at_;
    ExplanationTimePoint forgotten_at_;

    friend class OperationExplanationStoreSnapshot;
};

enum class ExplanationUpdateStatus {
    Accepted,
    Idempotent,
    Invalid,
    UnsupportedSchema,
    RevisionConflict,
    TerminalSealed,
};

enum class ExplanationLookupStatus {
    Detail,
    Tombstone,
    Missing,
};

struct OperationExplanationStoreUpdate;
struct OperationExplanationLookupResult;

class OperationExplanationStoreSnapshot {
public:
    static OperationExplanationStoreSnapshot empty();

    OperationExplanationStoreUpdate
    with_revision(const OperationExplanationDraft &draft,
                  ExplanationTimePoint committed_at) const;
    OperationExplanationLookupResult
    lookup(const std::string &operation_id, ExplanationTimePoint now) const;
    OperationExplanationStoreSnapshot compacted(ExplanationTimePoint now) const;

private:
    struct State;

    explicit OperationExplanationStoreSnapshot(
        std::shared_ptr<const State> state);

    std::shared_ptr<const State> state_;
};

struct OperationExplanationStoreUpdate {
    ExplanationUpdateStatus status;
    OperationExplanationStoreSnapshot snapshot;
    std::shared_ptr<const OperationExplanationRevision> revision;
    std::string diagnostic;
};

struct OperationExplanationLookupResult {
    ExplanationLookupStatus status;
    std::shared_ptr<const OperationExplanationRevision> revision;
    std::optional<OperationExplanationTombstone> tombstone;
};

}
