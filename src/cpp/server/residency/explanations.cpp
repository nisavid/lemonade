#include "lemon/residency/explanations.h"

#include <limits>
#include <unordered_map>
#include <utility>

namespace lemon::residency {
namespace {

constexpr std::size_t kMaxIdentityBytes = 128;
constexpr std::size_t kMaxReasonCodeBytes = 128;

bool same_schema_version(SchemaVersion left, SchemaVersion right) noexcept {
    return left.major == right.major && left.minor == right.minor;
}

bool valid_ascii_token(const std::string &value, std::size_t max_bytes) {
    if (value.empty() || value.size() > max_bytes) {
        return false;
    }
    for (const unsigned char character : value) {
        if (character < 0x21 || character > 0x7e) {
            return false;
        }
    }
    return true;
}

bool valid_identity(const std::string &value) {
    return valid_ascii_token(value, kMaxIdentityBytes);
}

bool valid_reason_code(const std::string &value) {
    return valid_ascii_token(value, kMaxReasonCodeBytes);
}

RenderedReason render_known_reason(std::string code,
                                   const ReasonMetadata &metadata) {
    return {std::move(code),
            std::string(metadata.category_id),
            std::string(metadata.presentation_id),
            std::string(metadata.severity),
            std::string(metadata.title),
            std::string(metadata.default_message)};
}

RenderedReason render_presentation(std::string code,
                                   const ReasonPresentationMetadata &metadata) {
    return {std::move(code),
            std::string(metadata.category_id),
            std::string(metadata.id),
            std::string(metadata.severity),
            std::string(metadata.title),
            std::string(metadata.default_message)};
}

RenderedReason render_unknown_reason(std::string code) {
    return {std::move(code),
            {},
            {},
            "warning",
            "Residency condition",
            "A residency condition is not recognized by this version."};
}

bool same_reason(const RenderedReason &left, const RenderedReason &right) {
    return left.code == right.code && left.category_id == right.category_id &&
           left.presentation_id == right.presentation_id &&
           left.severity == right.severity && left.title == right.title &&
           left.default_message == right.default_message;
}

bool same_reasons(const std::vector<RenderedReason> &left,
                  const std::vector<RenderedReason> &right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!same_reason(left[index], right[index])) {
            return false;
        }
    }
    return true;
}

bool same_revision_content(const OperationExplanationRevision &revision,
                           const OperationExplanationDraft &draft,
                           const std::vector<RenderedReason> &reasons) {
    return same_schema_version(revision.schema_version(),
                               draft.schema_version) &&
           revision.operation_id() == draft.operation_id &&
           revision.plan_id() == draft.plan_id &&
           revision.family() == draft.family &&
           revision.operation_kind() == draft.operation_kind &&
           revision.phase() == draft.phase &&
           revision.terminal_outcome() == draft.terminal_outcome &&
           revision.explanation_revision() == draft.explanation_revision &&
           same_reasons(revision.reasons(), reasons);
}

bool checked_add_seconds(ExplanationTimePoint value, std::uint64_t seconds,
                         ExplanationTimePoint &result) {
    using Duration = ExplanationTimePoint::duration;
    using Rep = Duration::rep;

    if (seconds >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return false;
    }
    const auto delta =
        std::chrono::duration_cast<Duration>(std::chrono::seconds{
            static_cast<std::int64_t>(seconds)});
    const auto elapsed = value.time_since_epoch();
    if constexpr (std::numeric_limits<Rep>::is_signed) {
        if (delta.count() > 0 &&
            elapsed.count() >
                std::numeric_limits<Rep>::max() - delta.count()) {
            return false;
        }
    } else {
        if (elapsed.count() >
            std::numeric_limits<Rep>::max() - delta.count()) {
            return false;
        }
    }
    result = value + delta;
    return true;
}

struct ValidatedDraft {
    ExplanationUpdateStatus status{ExplanationUpdateStatus::Accepted};
    std::vector<RenderedReason> reasons;
    std::string diagnostic;
};

ValidatedDraft validate_draft(const OperationExplanationDraft &draft) {
    if (!same_schema_version(draft.schema_version,
                             kExplanationSchemaVersion)) {
        return {ExplanationUpdateStatus::UnsupportedSchema, {},
                "the explanation schema is not authoritative"};
    }
    if (!valid_identity(draft.operation_id)) {
        return {ExplanationUpdateStatus::Invalid, {},
                "the operation identity is invalid"};
    }
    if (draft.plan_id.has_value() && !valid_identity(*draft.plan_id)) {
        return {ExplanationUpdateStatus::Invalid, {},
                "the plan identity is invalid"};
    }
    if (operation_family(draft.operation_kind) != draft.family) {
        return {ExplanationUpdateStatus::Invalid, {},
                "the operation family does not match its kind"};
    }
    const bool terminal = draft.phase == OperationPhase::Terminal;
    if (terminal != draft.terminal_outcome.has_value()) {
        return {ExplanationUpdateStatus::Invalid, {},
                "the terminal outcome does not match the operation phase"};
    }
    if (terminal && draft.reasons.empty()) {
        return {ExplanationUpdateStatus::Invalid, {},
                "a terminal explanation requires a primary reason"};
    }
    if (draft.reasons.size() > kMaxExplanationReasons) {
        return {ExplanationUpdateStatus::Invalid, {},
                "the explanation has too many reasons"};
    }

    std::vector<RenderedReason> rendered;
    rendered.reserve(draft.reasons.size());
    const OperationReasonRuleMetadata *previous_rule = nullptr;
    std::string previous_code;
    for (std::size_t index = 0; index < draft.reasons.size(); ++index) {
        const auto &reason = draft.reasons[index];
        if (!valid_reason_code(reason.code)) {
            return {ExplanationUpdateStatus::Invalid, {},
                    "an explanation reason code is invalid"};
        }
        const auto *rule = operation_reason_rule_metadata(reason.code);
        const auto *metadata = reason_metadata(reason.code);
        if (rule == nullptr || metadata == nullptr) {
            return {ExplanationUpdateStatus::Invalid, {},
                    "an explanation reason is not authoritative"};
        }
        const bool secondary = index != 0;
        if ((index == 0 && rule->secondary_only) ||
            !operation_reason_is_legal(reason.code, draft.operation_kind,
                                       draft.phase, draft.terminal_outcome,
                                       secondary)) {
            return {ExplanationUpdateStatus::Invalid, {},
                    "an explanation reason is outside its operation envelope"};
        }
        if (previous_rule != nullptr && previous_code != reason.code &&
            previous_rule->canonical_rank > rule->canonical_rank) {
            return {ExplanationUpdateStatus::Invalid, {},
                    "the explanation reasons are not in canonical order"};
        }
        rendered.push_back(render_known_reason(reason.code, *metadata));
        previous_rule = rule;
        previous_code = reason.code;
    }
    return {ExplanationUpdateStatus::Accepted, std::move(rendered), {}};
}

OperationExplanationStoreUpdate
rejected_update(ExplanationUpdateStatus status,
                const OperationExplanationStoreSnapshot &snapshot,
                std::string diagnostic) {
    return {status, snapshot, nullptr, std::move(diagnostic)};
}

}

struct OperationExplanationStoreSnapshot::State {
    struct Entry {
        std::shared_ptr<const OperationExplanationRevision> revision;
        std::optional<OperationExplanationTombstone> tombstone;
    };

    std::unordered_map<std::string, Entry> entries;
};

ReasonRenderResult
render_reason_projection(SchemaVersion supported_version,
                         SchemaVersion projection_version,
                         const WireReasonProjection &projection) {
    if (projection_version.major != supported_version.major ||
        projection_version.minor < supported_version.minor) {
        return {ReasonRenderStatus::UnsupportedSchema, std::nullopt};
    }
    if (!valid_reason_code(projection.code)) {
        return {ReasonRenderStatus::UnknownFallback,
                render_unknown_reason("unknown_reason")};
    }
    if (const auto *metadata = reason_metadata(projection.code)) {
        return {ReasonRenderStatus::Known,
                render_known_reason(projection.code, *metadata)};
    }

    const ReasonPresentationMetadata *presentation =
        matching_reason_presentation_for_category(
            projection.category_id, projection.presentation_id);
    if (presentation == nullptr) {
        presentation =
            unique_reason_presentation_for_category(projection.category_id);
    }
    if (presentation != nullptr) {
        return {ReasonRenderStatus::UnknownCategoryFallback,
                render_presentation(projection.code, *presentation)};
    }
    return {ReasonRenderStatus::UnknownFallback,
            render_unknown_reason(projection.code)};
}

OperationExplanationRevision::OperationExplanationRevision(
    OperationExplanationDraft draft, std::vector<RenderedReason> reasons,
    ExplanationTimePoint created_at, ExplanationTimePoint updated_at,
    std::optional<ExplanationTimePoint> expires_at,
    std::optional<ExplanationTimePoint> forgotten_at)
    : draft_(std::move(draft)), reasons_(std::move(reasons)),
      created_at_(created_at), updated_at_(updated_at), expires_at_(expires_at),
      forgotten_at_(forgotten_at) {
    if (!reasons_.empty()) {
        primary_reason_code_ = reasons_.front().code;
    }
}

const SchemaVersion &
OperationExplanationRevision::schema_version() const noexcept {
    return draft_.schema_version;
}

const std::string &OperationExplanationRevision::operation_id() const noexcept {
    return draft_.operation_id;
}

const std::optional<std::string> &
OperationExplanationRevision::plan_id() const noexcept {
    return draft_.plan_id;
}

OperationFamily OperationExplanationRevision::family() const noexcept {
    return draft_.family;
}

OperationKind OperationExplanationRevision::operation_kind() const noexcept {
    return draft_.operation_kind;
}

OperationPhase OperationExplanationRevision::phase() const noexcept {
    return draft_.phase;
}

const std::optional<TerminalOutcome> &
OperationExplanationRevision::terminal_outcome() const noexcept {
    return draft_.terminal_outcome;
}

std::uint64_t
OperationExplanationRevision::explanation_revision() const noexcept {
    return draft_.explanation_revision;
}

const std::vector<RenderedReason> &
OperationExplanationRevision::reasons() const noexcept {
    return reasons_;
}

const std::optional<std::string> &
OperationExplanationRevision::primary_reason_code() const noexcept {
    return primary_reason_code_;
}

ExplanationTimePoint
OperationExplanationRevision::created_at() const noexcept {
    return created_at_;
}

ExplanationTimePoint
OperationExplanationRevision::updated_at() const noexcept {
    return updated_at_;
}

const std::optional<ExplanationTimePoint> &
OperationExplanationRevision::expires_at() const noexcept {
    return expires_at_;
}

const std::optional<ExplanationTimePoint> &
OperationExplanationRevision::forgotten_at() const noexcept {
    return forgotten_at_;
}

OperationExplanationTombstone::OperationExplanationTombstone(
    std::string operation_id, std::uint64_t last_explanation_revision,
    ExplanationTimePoint detail_expired_at,
    ExplanationTimePoint forgotten_at)
    : operation_id_(std::move(operation_id)),
      last_explanation_revision_(last_explanation_revision),
      detail_expired_at_(detail_expired_at), forgotten_at_(forgotten_at) {}

const std::string &
OperationExplanationTombstone::operation_id() const noexcept {
    return operation_id_;
}

std::uint64_t
OperationExplanationTombstone::last_explanation_revision() const noexcept {
    return last_explanation_revision_;
}

ExplanationTimePoint
OperationExplanationTombstone::detail_expired_at() const noexcept {
    return detail_expired_at_;
}

ExplanationTimePoint
OperationExplanationTombstone::forgotten_at() const noexcept {
    return forgotten_at_;
}

OperationExplanationStoreSnapshot::OperationExplanationStoreSnapshot(
    std::shared_ptr<const State> state)
    : state_(std::move(state)) {}

OperationExplanationStoreSnapshot OperationExplanationStoreSnapshot::empty() {
    return OperationExplanationStoreSnapshot(std::make_shared<const State>());
}

OperationExplanationStoreUpdate
OperationExplanationStoreSnapshot::with_revision(
    const OperationExplanationDraft &draft,
    ExplanationTimePoint committed_at) const {
    const auto validated = validate_draft(draft);
    if (validated.status != ExplanationUpdateStatus::Accepted) {
        return rejected_update(validated.status, *this, validated.diagnostic);
    }

    const auto existing = state_->entries.find(draft.operation_id);
    ExplanationTimePoint created_at = committed_at;
    if (existing == state_->entries.end()) {
        if (draft.explanation_revision != 0) {
            return rejected_update(
                ExplanationUpdateStatus::RevisionConflict, *this,
                "the first explanation revision must be zero");
        }
    } else {
        if (existing->second.revision == nullptr) {
            return rejected_update(ExplanationUpdateStatus::TerminalSealed,
                                   *this,
                                   "the retained operation is terminal");
        }
        const auto &current = *existing->second.revision;
        if (draft.explanation_revision == current.explanation_revision()) {
            if (same_revision_content(current, draft, validated.reasons)) {
                return {ExplanationUpdateStatus::Idempotent, *this,
                        existing->second.revision, {}};
            }
            return rejected_update(
                ExplanationUpdateStatus::RevisionConflict, *this,
                "the explanation revision already has different content");
        }
        if (current.phase() == OperationPhase::Terminal) {
            return rejected_update(ExplanationUpdateStatus::TerminalSealed,
                                   *this,
                                   "the operation explanation is terminal");
        }
        if (current.explanation_revision() ==
                std::numeric_limits<std::uint64_t>::max() ||
            draft.explanation_revision !=
                current.explanation_revision() + 1) {
            return rejected_update(
                ExplanationUpdateStatus::RevisionConflict, *this,
                "the explanation revision is not the next revision");
        }
        if (current.family() != draft.family ||
            current.operation_kind() != draft.operation_kind) {
            return rejected_update(
                ExplanationUpdateStatus::Invalid, *this,
                "the operation identity envelope changed across revisions");
        }
        if (current.plan_id().has_value() &&
            current.plan_id() != draft.plan_id) {
            return rejected_update(
                ExplanationUpdateStatus::Invalid, *this,
                "the operation plan identity changed across revisions");
        }
        if (committed_at < current.updated_at()) {
            return rejected_update(
                ExplanationUpdateStatus::Invalid, *this,
                "the explanation commit time moved backward");
        }
        created_at = current.created_at();
    }

    std::optional<ExplanationTimePoint> expires_at;
    std::optional<ExplanationTimePoint> forgotten_at;
    if (draft.phase == OperationPhase::Terminal) {
        ExplanationTimePoint detail_expiry;
        ExplanationTimePoint forget_time;
        if (!checked_add_seconds(
                committed_at,
                kOperationRetentionPolicy.terminal_detail_seconds,
                detail_expiry) ||
            !checked_add_seconds(
                committed_at,
                kOperationRetentionPolicy.forgotten_after_terminal_seconds,
                forget_time)) {
            return rejected_update(ExplanationUpdateStatus::Invalid, *this,
                                   "the explanation retention deadline overflows");
        }
        expires_at = detail_expiry;
        forgotten_at = forget_time;
    }

    auto stored_draft = draft;
    stored_draft.reasons.clear();
    stored_draft.reasons.reserve(validated.reasons.size());
    for (const auto &reason : validated.reasons) {
        stored_draft.reasons.push_back(
            {reason.code, reason.category_id, reason.presentation_id,
             reason.severity, reason.title, reason.default_message});
    }
    auto revision = std::shared_ptr<const OperationExplanationRevision>(
        new OperationExplanationRevision(
            std::move(stored_draft), validated.reasons, created_at,
            committed_at, expires_at, forgotten_at));
    auto next_state = std::make_shared<State>(*state_);
    next_state->entries[draft.operation_id] = {revision, std::nullopt};
    auto next = OperationExplanationStoreSnapshot(std::move(next_state));
    return {ExplanationUpdateStatus::Accepted, std::move(next),
            std::move(revision), {}};
}

OperationExplanationLookupResult OperationExplanationStoreSnapshot::lookup(
    const std::string &operation_id, ExplanationTimePoint now) const {
    const auto found = state_->entries.find(operation_id);
    if (found == state_->entries.end()) {
        return {ExplanationLookupStatus::Missing, nullptr, std::nullopt};
    }
    const auto &entry = found->second;
    if (entry.tombstone.has_value()) {
        if (now >= entry.tombstone->forgotten_at()) {
            return {ExplanationLookupStatus::Missing, nullptr, std::nullopt};
        }
        return {ExplanationLookupStatus::Tombstone, nullptr, entry.tombstone};
    }
    if (entry.revision == nullptr) {
        return {ExplanationLookupStatus::Missing, nullptr, std::nullopt};
    }
    if (entry.revision->forgotten_at().has_value() &&
        now >= *entry.revision->forgotten_at()) {
        return {ExplanationLookupStatus::Missing, nullptr, std::nullopt};
    }
    if (entry.revision->expires_at().has_value() &&
        now >= *entry.revision->expires_at()) {
        return {ExplanationLookupStatus::Tombstone,
                nullptr,
                OperationExplanationTombstone(
                    entry.revision->operation_id(),
                    entry.revision->explanation_revision(),
                    *entry.revision->expires_at(),
                    *entry.revision->forgotten_at())};
    }
    return {ExplanationLookupStatus::Detail, entry.revision, std::nullopt};
}

OperationExplanationStoreSnapshot
OperationExplanationStoreSnapshot::compacted(ExplanationTimePoint now) const {
    auto next_state = std::make_shared<State>(*state_);
    for (auto entry = next_state->entries.begin();
         entry != next_state->entries.end();) {
        auto &record = entry->second;
        if (record.tombstone.has_value()) {
            if (now >= record.tombstone->forgotten_at()) {
                entry = next_state->entries.erase(entry);
                continue;
            }
        } else if (record.revision != nullptr &&
                   record.revision->forgotten_at().has_value()) {
            if (now >= *record.revision->forgotten_at()) {
                entry = next_state->entries.erase(entry);
                continue;
            }
            if (now >= *record.revision->expires_at()) {
                record.tombstone = OperationExplanationTombstone(
                    record.revision->operation_id(),
                    record.revision->explanation_revision(),
                    *record.revision->expires_at(),
                    *record.revision->forgotten_at());
                record.revision.reset();
            }
        }
        ++entry;
    }
    return OperationExplanationStoreSnapshot(std::move(next_state));
}

}
