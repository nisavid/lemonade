#include "lemon/residency/explanations.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace lemon::residency;

template <typename T, typename = void>
struct has_action_authority_method : std::false_type {};

template <typename T>
struct has_action_authority_method<
    T, std::void_t<decltype(std::declval<const T &>().authorizes_action())>>
    : std::true_type {};

template <typename T, typename = void>
struct has_publish_method : std::false_type {};

template <typename T>
struct has_publish_method<
    T, std::void_t<decltype(std::declval<const T &>().publish())>>
    : std::true_type {};

template <typename T, typename = void>
struct has_details_member : std::false_type {};

template <typename T>
struct has_details_member<
    T, std::void_t<decltype(std::declval<const T &>().details)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_reasons_method : std::false_type {};

template <typename T>
struct has_reasons_method<
    T, std::void_t<decltype(std::declval<const T &>().reasons())>>
    : std::true_type {};

template <typename T, typename = void>
struct has_family_method : std::false_type {};

template <typename T>
struct has_family_method<
    T, std::void_t<decltype(std::declval<const T &>().family())>>
    : std::true_type {};

template <typename T, typename = void>
struct has_operation_kind_method : std::false_type {};

template <typename T>
struct has_operation_kind_method<
    T, std::void_t<decltype(std::declval<const T &>().operation_kind())>>
    : std::true_type {};

template <typename T, typename = void>
struct has_plan_id_method : std::false_type {};

template <typename T>
struct has_plan_id_method<
    T, std::void_t<decltype(std::declval<const T &>().plan_id())>>
    : std::true_type {};

static_assert(
    !has_action_authority_method<OperationExplanationStoreSnapshot>::value);
static_assert(!has_publish_method<OperationExplanationStoreSnapshot>::value);
static_assert(
    !has_action_authority_method<OperationExplanationRevision>::value);
static_assert(!has_publish_method<OperationExplanationRevision>::value);
static_assert(
    !has_action_authority_method<OperationExplanationTombstone>::value);
static_assert(!has_publish_method<OperationExplanationTombstone>::value);
static_assert(
    !has_action_authority_method<OperationExplanationStoreUpdate>::value);
static_assert(!has_publish_method<OperationExplanationStoreUpdate>::value);
static_assert(
    !has_action_authority_method<OperationExplanationLookupResult>::value);
static_assert(!has_publish_method<OperationExplanationLookupResult>::value);
static_assert(!has_details_member<RenderedReason>::value);
static_assert(!has_reasons_method<OperationExplanationTombstone>::value);
static_assert(!has_family_method<OperationExplanationTombstone>::value);
static_assert(!has_operation_kind_method<OperationExplanationTombstone>::value);
static_assert(!has_plan_id_method<OperationExplanationTombstone>::value);
static_assert(!std::is_default_constructible_v<OperationExplanationRevision>);
static_assert(!std::is_default_constructible_v<OperationExplanationTombstone>);
static_assert(
    std::is_same_v<decltype(std::declval<WireReasonProjection>().code),
                   std::string>);
static_assert(
    std::is_same_v<decltype(std::declval<WireReasonProjection>().category_id),
                   std::string>);
static_assert(std::is_same_v<
              decltype(std::declval<WireReasonProjection>().presentation_id),
              std::string>);
static_assert(
    std::is_same_v<decltype(std::declval<WireReasonProjection>().severity),
                   std::string>);
static_assert(
    std::is_same_v<decltype(std::declval<WireReasonProjection>().title),
                   std::string>);
static_assert(std::is_same_v<
              decltype(std::declval<WireReasonProjection>().default_message),
              std::string>);
static_assert(
    std::is_same_v<decltype(std::declval<RenderedReason>().code), std::string>);
static_assert(
    std::is_same_v<decltype(std::declval<RenderedReason>().category_id),
                   std::string>);
static_assert(
    std::is_same_v<decltype(std::declval<RenderedReason>().presentation_id),
                   std::string>);
static_assert(std::is_same_v<decltype(std::declval<RenderedReason>().severity),
                             std::string>);
static_assert(std::is_same_v<decltype(std::declval<RenderedReason>().title),
                             std::string>);
static_assert(
    std::is_same_v<decltype(std::declval<RenderedReason>().default_message),
                   std::string>);
static_assert(
    std::is_same_v<decltype(std::declval<ReasonRenderResult>().status),
                   ReasonRenderStatus>);
static_assert(
    std::is_same_v<decltype(std::declval<ReasonRenderResult>().reason),
                   std::optional<RenderedReason>>);
static_assert(std::is_same_v<decltype(std::declval<OperationExplanationDraft>()
                                          .schema_version),
                             SchemaVersion>);
static_assert(std::is_same_v<
              decltype(std::declval<OperationExplanationDraft>().operation_id),
              std::string>);
static_assert(
    std::is_same_v<decltype(std::declval<OperationExplanationDraft>().plan_id),
                   std::optional<std::string>>);
static_assert(
    std::is_same_v<decltype(std::declval<OperationExplanationDraft>().family),
                   OperationFamily>);
static_assert(std::is_same_v<decltype(std::declval<OperationExplanationDraft>()
                                          .operation_kind),
                             OperationKind>);
static_assert(
    std::is_same_v<decltype(std::declval<OperationExplanationDraft>().phase),
                   OperationPhase>);
static_assert(std::is_same_v<decltype(std::declval<OperationExplanationDraft>()
                                          .terminal_outcome),
                             std::optional<TerminalOutcome>>);
static_assert(std::is_same_v<decltype(std::declval<OperationExplanationDraft>()
                                          .explanation_revision),
                             std::uint64_t>);
static_assert(
    std::is_same_v<decltype(std::declval<OperationExplanationDraft>().reasons),
                   std::vector<WireReasonProjection>>);
static_assert(std::is_same_v<
              decltype(std::declval<OperationExplanationStoreUpdate>().status),
              ExplanationUpdateStatus>);
static_assert(
    std::is_same_v<
        decltype(std::declval<OperationExplanationStoreUpdate>().snapshot),
        OperationExplanationStoreSnapshot>);
static_assert(
    std::is_same_v<
        decltype(std::declval<OperationExplanationStoreUpdate>().revision),
        std::shared_ptr<const OperationExplanationRevision>>);
static_assert(
    std::is_same_v<
        decltype(std::declval<OperationExplanationStoreUpdate>().diagnostic),
        std::string>);
static_assert(std::is_same_v<
              decltype(std::declval<OperationExplanationLookupResult>().status),
              ExplanationLookupStatus>);
static_assert(
    std::is_same_v<
        decltype(std::declval<OperationExplanationLookupResult>().revision),
        std::shared_ptr<const OperationExplanationRevision>>);
static_assert(
    std::is_same_v<
        decltype(std::declval<OperationExplanationLookupResult>().tombstone),
        std::optional<OperationExplanationTombstone>>);

[[noreturn]] void fail(std::string_view message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void require(bool condition, std::string_view message) {
    if (!condition) {
        fail(message);
    }
}

ExplanationTimePoint at(std::int64_t epoch_seconds) {
    return ExplanationTimePoint{std::chrono::seconds{epoch_seconds}};
}

WireReasonProjection reason(std::string code, std::string category_id,
                            std::string presentation_id, std::string severity,
                            std::string title, std::string default_message) {
    WireReasonProjection projection;
    projection.code = std::move(code);
    projection.category_id = std::move(category_id);
    projection.presentation_id = std::move(presentation_id);
    projection.severity = std::move(severity);
    projection.title = std::move(title);
    projection.default_message = std::move(default_message);
    return projection;
}

WireReasonProjection cancelled_reason() {
    return reason(
        "residency_cancelled", "lifecycle", "p_lifecycle", "warning",
        "Lifecycle changed",
        "The residency lifecycle operation was interrupted or superseded.");
}

WireReasonProjection capability_reason() {
    return reason("residency_capability_unsupported", "capability",
                  "p_capability", "error", "Capability unavailable",
                  "The required residency capability is unavailable.");
}

WireReasonProjection recovery_reason() {
    return reason("residency_recovery_required", "recovery", "p_recovery",
                  "error", "Recovery required",
                  "Residency recovery is required before normal lifecycle work "
                  "can continue.");
}

WireReasonProjection success_reason() {
    return reason("residency_operation_succeeded", "success", "p_success",
                  "info", "Operation succeeded",
                  "The residency operation succeeded.");
}

WireReasonProjection deprecated_pin_reason() {
    return reason("residency_unconditional_pin_write_deprecated",
                  "compatibility", "p_compatibility", "warning",
                  "Compatibility constraint",
                  "A compatibility rule affects the requested operation.");
}

OperationExplanationDraft
resource_draft(std::string operation_id, std::uint64_t revision,
               OperationPhase phase = OperationPhase::Evaluating,
               std::optional<TerminalOutcome> outcome = std::nullopt,
               std::vector<WireReasonProjection> reasons = {}) {
    OperationExplanationDraft draft{};
    draft.operation_id = std::move(operation_id);
    draft.plan_id = std::string("plan-1");
    draft.family = OperationFamily::ResourceLifecycle;
    draft.operation_kind = OperationKind::Admission;
    draft.phase = phase;
    draft.terminal_outcome = outcome;
    draft.explanation_revision = revision;
    draft.reasons = std::move(reasons);
    return draft;
}

void require_local_reason(const RenderedReason &rendered, std::string_view code,
                          std::string_view category_id,
                          std::string_view presentation_id,
                          std::string_view severity, std::string_view title,
                          std::string_view default_message) {
    require(rendered.code == code, "rendered reason changed the wire code");
    require(rendered.category_id == category_id,
            "rendered reason has the wrong category");
    require(rendered.presentation_id == presentation_id,
            "rendered reason has the wrong presentation");
    require(rendered.severity == severity,
            "rendered reason has the wrong severity");
    require(rendered.title == title, "rendered reason has the wrong title");
    require(rendered.default_message == default_message,
            "rendered reason has the wrong default message");
}

void require_rejection(const OperationExplanationStoreUpdate &update,
                       ExplanationUpdateStatus expected,
                       std::string_view label) {
    require(update.status == expected, label);
    require(update.revision == nullptr, "rejected update exposed a revision");
    require(!update.diagnostic.empty(),
            "rejected update omitted its diagnostic");
    require(update.diagnostic.size() <= 1000,
            "rejected update diagnostic is unbounded");
}

void require_detail(const OperationExplanationLookupResult &lookup,
                    std::uint64_t expected_revision) {
    require(lookup.status == ExplanationLookupStatus::Detail,
            "lookup did not return detail");
    require(lookup.revision != nullptr, "detail lookup omitted its revision");
    require(!lookup.tombstone.has_value(), "detail lookup exposed a tombstone");
    require(lookup.revision->explanation_revision() == expected_revision,
            "detail lookup returned the wrong revision");
}

void test_reason_rendering() {
    const SchemaVersion supported{1, 0};

    const auto known = render_reason_projection(
        supported, {1, 0},
        reason("residency_capability_unsupported", "sender-category",
               "sender-presentation", "critical",
               u8"Sender \u202e title must not render \U0001f4a5",
               u8"Sender \u2066 message must not render \U0001f512"));
    require(known.status == ReasonRenderStatus::Known,
            "known reason was not recognized");
    require(known.reason.has_value(),
            "known reason omitted its local rendering");
    require_local_reason(*known.reason, "residency_capability_unsupported",
                         "capability", "p_capability", "error",
                         "Capability unavailable",
                         "The required residency capability is unavailable.");

    const auto exact_unknown = render_reason_projection(
        supported, {1, 0},
        reason("future_capacity_condition", "capacity", "p_capacity",
               "critical", "Injected title", "Injected message"));
    require(exact_unknown.status == ReasonRenderStatus::UnknownCategoryFallback,
            "exact-schema unknown reason was not rendered conservatively");
    require(exact_unknown.reason.has_value(),
            "exact-schema unknown reason was hidden");
    require_local_reason(*exact_unknown.reason, "future_capacity_condition",
                         "capacity", "p_capacity", "warning",
                         "Capacity unavailable",
                         "The requested residency capacity is unavailable.");

    const auto newer_minor = render_reason_projection(
        supported, {1, 1},
        reason("future_capacity_minor", "capacity", "p_capacity", "critical",
               "Injected newer-minor title", "Injected newer-minor message"));
    require(newer_minor.status == ReasonRenderStatus::UnknownCategoryFallback,
            "newer-minor projection was not bounded to local metadata");
    require(newer_minor.reason.has_value(),
            "newer-minor projection was hidden");
    require_local_reason(*newer_minor.reason, "future_capacity_minor",
                         "capacity", "p_capacity", "warning",
                         "Capacity unavailable",
                         "The requested residency capacity is unavailable.");

    const auto unique_category = render_reason_projection(
        supported, {1, 1},
        reason("future_capacity_unique", "capacity", "p_action", "critical",
               "Injected title", "Injected message"));
    require(unique_category.status ==
                ReasonRenderStatus::UnknownCategoryFallback,
            "unique local category did not select its local presentation");
    require(unique_category.reason.has_value(),
            "unique category fallback was hidden");
    require_local_reason(*unique_category.reason, "future_capacity_unique",
                         "capacity", "p_capacity", "warning",
                         "Capacity unavailable",
                         "The requested residency capacity is unavailable.");

    const auto matching_ambiguous_category =
        render_reason_projection(supported, {1, 1},
                                 reason("future_status_auth", "authentication",
                                        "p_status_authentication", "critical",
                                        "Injected title", "Injected message"));
    require(matching_ambiguous_category.status ==
                ReasonRenderStatus::UnknownCategoryFallback,
            "matching presentation/category pair was not honored locally");
    require(matching_ambiguous_category.reason.has_value(),
            "matching presentation/category fallback was hidden");
    require_local_reason(*matching_ambiguous_category.reason,
                         "future_status_auth", "authentication",
                         "p_status_authentication", "warning",
                         "Status authorization required",
                         "A valid residency status capability or admin "
                         "authorization is required.");

    const auto ambiguous_category = render_reason_projection(
        supported, {1, 1},
        reason("future_auth_condition", "authentication",
               "unknown-presentation", "critical", "Injected title",
               "Injected message"));
    require(ambiguous_category.status == ReasonRenderStatus::UnknownFallback,
            "ambiguous category did not use the fixed fallback");
    require(ambiguous_category.reason.has_value(),
            "fixed unknown fallback was hidden");
    require(ambiguous_category.reason->code == "future_auth_condition",
            "fixed fallback did not preserve the unknown code");
    require(ambiguous_category.reason->severity == "warning",
            "fixed fallback is not a warning");
    require(ambiguous_category.reason->title == "Residency condition",
            "fixed fallback title drifted");
    require(ambiguous_category.reason->default_message ==
                "A residency condition is not recognized by this version.",
            "fixed fallback message drifted");
    require(ambiguous_category.reason->title != "Injected title" &&
                ambiguous_category.reason->default_message !=
                    "Injected message",
            "fixed fallback trusted sender display text");

    const auto unknown_category = render_reason_projection(
        supported, {1, 1},
        reason("future_unknown_category", "sender-category",
               "sender-presentation", "critical", "Injected title",
               "Injected message"));
    require(unknown_category.status == ReasonRenderStatus::UnknownFallback,
            "unknown category did not use the fixed fallback");
    require(unknown_category.reason.has_value(),
            "unknown-category fallback was hidden");
    require(unknown_category.reason->code == "future_unknown_category",
            "unknown-category fallback changed the code");
    require(unknown_category.reason->severity == "warning",
            "unknown-category fallback is not a warning");
    require(unknown_category.reason->title == "Residency condition",
            "unknown-category fallback title drifted");
    require(unknown_category.reason->default_message ==
                "A residency condition is not recognized by this version.",
            "unknown-category fallback message drifted");

    const auto known_newer_minor = render_reason_projection(
        supported, {1, 1},
        reason("residency_cancelled", "sender-category", "sender-presentation",
               "critical", u8"Sender \u202e newer-minor title",
               u8"Sender \u2066 newer-minor message"));
    require(known_newer_minor.status == ReasonRenderStatus::Known,
            "known newer-minor reason did not use local metadata");
    require(known_newer_minor.reason.has_value(),
            "known newer-minor reason was hidden");
    require_local_reason(
        *known_newer_minor.reason, "residency_cancelled", "lifecycle",
        "p_lifecycle", "warning", "Lifecycle changed",
        "The residency lifecycle operation was interrupted or superseded.");

    const auto older_minor = render_reason_projection(
        {1, 1}, {1, 0},
        reason("residency_cancelled", "lifecycle", "p_lifecycle", "warning",
               "Lifecycle changed",
               "The residency lifecycle operation was interrupted or superseded."));
    require(older_minor.status == ReasonRenderStatus::UnsupportedSchema,
            "older-minor projection was rendered");
    require(!older_minor.reason.has_value(),
            "older-minor projection exposed a reason");

    const auto unknown_major = render_reason_projection(
        supported, {2, 0},
        reason("future_major_condition", "capacity", "p_capacity", "warning",
               "Capacity unavailable",
               "The requested residency capacity is unavailable."));
    require(unknown_major.status == ReasonRenderStatus::UnsupportedSchema,
            "unknown schema major was rendered");
    require(!unknown_major.reason.has_value(),
            "unknown schema major exposed a reason");
}

void test_update_validation() {
    const auto empty = OperationExplanationStoreSnapshot::empty();

    const OperationExplanationDraft default_draft{};
    require(default_draft.schema_version.major == 1 &&
                default_draft.schema_version.minor == 0,
            "operation draft did not default to explanation schema 1.0");

    auto accepted_empty =
        empty.with_revision(resource_draft("empty-reasons", 0), at(10));
    require(accepted_empty.status == ExplanationUpdateStatus::Accepted,
            "valid nonterminal empty-reason revision was rejected");
    require(accepted_empty.diagnostic.empty(),
            "accepted update has a diagnostic");
    require(accepted_empty.revision != nullptr,
            "accepted update omitted its revision");
    require(!accepted_empty.revision->primary_reason_code().has_value(),
            "empty reason list produced a primary reason");

    auto invalid_id = resource_draft("", 0);
    require_rejection(empty.with_revision(invalid_id, at(10)),
                      ExplanationUpdateStatus::Invalid,
                      "empty operation identity was accepted");

    auto oversized_id = resource_draft(std::string(129, 'x'), 0);
    require_rejection(empty.with_revision(oversized_id, at(10)),
                      ExplanationUpdateStatus::Invalid,
                      "oversized operation identity was accepted");

    auto whitespace_id = resource_draft("invalid operation", 0);
    require_rejection(empty.with_revision(whitespace_id, at(10)),
                      ExplanationUpdateStatus::Invalid,
                      "operation identity containing whitespace was accepted");

    auto non_ascii_id = resource_draft(u8"invalid-\u00e9", 0);
    require_rejection(empty.with_revision(non_ascii_id, at(10)),
                      ExplanationUpdateStatus::Invalid,
                      "non-ASCII operation identity was accepted");

    auto maximum_ids = resource_draft(std::string(128, 'o'), 0);
    maximum_ids.plan_id = std::string(128, 'p');
    require(empty.with_revision(maximum_ids, at(10)).status ==
                ExplanationUpdateStatus::Accepted,
            "maximum-width printable identities were rejected");

    auto null_plan_id = resource_draft("null-plan-id", 0);
    null_plan_id.plan_id = std::nullopt;
    require(empty.with_revision(null_plan_id, at(10)).status ==
                ExplanationUpdateStatus::Accepted,
            "nullable plan identity was rejected");

    auto empty_plan_id = resource_draft("empty-plan-id", 0);
    empty_plan_id.plan_id = std::string();
    require_rejection(empty.with_revision(empty_plan_id, at(10)),
                      ExplanationUpdateStatus::Invalid,
                      "empty nonnull plan identity was accepted");

    auto oversized_plan_id = resource_draft("oversized-plan-id", 0);
    oversized_plan_id.plan_id = std::string(129, 'p');
    require_rejection(empty.with_revision(oversized_plan_id, at(10)),
                      ExplanationUpdateStatus::Invalid,
                      "oversized plan identity was accepted");

    auto whitespace_plan_id = resource_draft("whitespace-plan-id", 0);
    whitespace_plan_id.plan_id = std::string("invalid plan");
    require_rejection(empty.with_revision(whitespace_plan_id, at(10)),
                      ExplanationUpdateStatus::Invalid,
                      "plan identity containing whitespace was accepted");

    const std::vector<std::pair<OperationKind, OperationFamily>>
        operation_families{
            {OperationKind::Admission, OperationFamily::ResourceLifecycle},
            {OperationKind::ExplicitUnload, OperationFamily::ResourceLifecycle},
            {OperationKind::ForceUnload, OperationFamily::ResourceLifecycle},
            {OperationKind::PressureReclamation,
             OperationFamily::ResourceLifecycle},
            {OperationKind::StartupLoad, OperationFamily::ResourceLifecycle},
            {OperationKind::ServiceTermination,
             OperationFamily::ResourceLifecycle},
            {OperationKind::DeadBackendPruning,
             OperationFamily::ResourceLifecycle},
            {OperationKind::SameEpochRecoveryCleanup,
             OperationFamily::ResourceLifecycle},
            {OperationKind::PriorEpochOwnerCleanup,
             OperationFamily::ResourceLifecycle},
            {OperationKind::ArtifactScopeRecoveryCleanup,
             OperationFamily::ResourceLifecycle},
            {OperationKind::SavedPinMutation, OperationFamily::ResidentState},
            {OperationKind::RuntimePinMutation, OperationFamily::ResidentState},
            {OperationKind::LegacyPinBatch, OperationFamily::ResidentState},
            {OperationKind::ResidentStateRecoveryCleanup,
             OperationFamily::ResidentState},
        };
    for (std::size_t index = 0; index < operation_families.size(); ++index) {
        const auto [kind, family] = operation_families[index];
        auto valid =
            resource_draft("valid-family-kind-" + std::to_string(index), 0);
        valid.operation_kind = kind;
        valid.family = family;
        require(empty.with_revision(valid, at(10)).status ==
                    ExplanationUpdateStatus::Accepted,
                "generated operation family mapping was rejected");

        auto invalid = valid;
        invalid.operation_id = "invalid-family-kind-" + std::to_string(index);
        invalid.family = family == OperationFamily::ResourceLifecycle
                             ? OperationFamily::ResidentState
                             : OperationFamily::ResourceLifecycle;
        require_rejection(empty.with_revision(invalid, at(10)),
                          ExplanationUpdateStatus::Invalid,
                          "operation kind was accepted in the wrong family");
    }

    auto forged_kind = resource_draft("forged-operation-kind", 0);
    forged_kind.operation_kind = static_cast<OperationKind>(0x7fff);
    require_rejection(empty.with_revision(forged_kind, at(10)),
                      ExplanationUpdateStatus::Invalid,
                      "forged operation kind was accepted");

    auto forged_family = resource_draft("forged-operation-family", 0);
    forged_family.family = static_cast<OperationFamily>(0x7fff);
    require_rejection(empty.with_revision(forged_family, at(10)),
                      ExplanationUpdateStatus::Invalid,
                      "forged operation family was accepted");

    auto forged_phase = resource_draft("forged-operation-phase", 0);
    forged_phase.phase = static_cast<OperationPhase>(0x7fff);
    require_rejection(empty.with_revision(forged_phase, at(10)),
                      ExplanationUpdateStatus::Invalid,
                      "forged operation phase was accepted");

    auto forged_outcome = resource_draft(
        "forged-terminal-outcome", 0, OperationPhase::Terminal,
        static_cast<TerminalOutcome>(0x7fff), {success_reason()});
    require_rejection(empty.with_revision(forged_outcome, at(10)),
                      ExplanationUpdateStatus::Invalid,
                      "forged terminal outcome was accepted");

    auto outcome_without_terminal =
        resource_draft("outcome-without-terminal", 0);
    outcome_without_terminal.terminal_outcome = TerminalOutcome::Cancelled;
    require_rejection(empty.with_revision(outcome_without_terminal, at(10)),
                      ExplanationUpdateStatus::Invalid,
                      "nonterminal phase accepted a terminal outcome");

    auto terminal_without_outcome =
        resource_draft("terminal-without-outcome", 0, OperationPhase::Terminal,
                       std::nullopt, {cancelled_reason()});
    require_rejection(empty.with_revision(terminal_without_outcome, at(10)),
                      ExplanationUpdateStatus::Invalid,
                      "terminal phase accepted no outcome");

    auto terminal_without_reason =
        resource_draft("terminal-without-reason", 0, OperationPhase::Terminal,
                       TerminalOutcome::Succeeded);
    require_rejection(empty.with_revision(terminal_without_reason, at(10)),
                      ExplanationUpdateStatus::Invalid,
                      "terminal revision accepted no primary reason");

    auto illegal_envelope =
        resource_draft("illegal-envelope", 0, OperationPhase::Evaluating,
                       std::nullopt, {success_reason()});
    require_rejection(empty.with_revision(illegal_envelope, at(10)),
                      ExplanationUpdateStatus::Invalid,
                      "reason was accepted outside its generated envelope");

    auto unknown_reason = resource_draft(
        "unknown-reason", 0, OperationPhase::Evaluating, std::nullopt,
        {reason("future_reason", "capacity", "p_capacity", "warning",
                "Capacity unavailable",
                "The requested residency capacity is unavailable.")});
    require_rejection(
        empty.with_revision(unknown_reason, at(10)),
        ExplanationUpdateStatus::Invalid,
        "unknown authoritative reason was accepted without an envelope");

    auto secondary_primary =
        resource_draft("secondary-primary", 0, OperationPhase::Evaluating,
                       std::nullopt, {deprecated_pin_reason()});
    secondary_primary.family = OperationFamily::ResidentState;
    secondary_primary.operation_kind = OperationKind::SavedPinMutation;
    require_rejection(empty.with_revision(secondary_primary, at(10)),
                      ExplanationUpdateStatus::Invalid,
                      "secondary-only reason was accepted as primary");

    auto secondary_valid = resource_draft(
        "secondary-valid", 0, OperationPhase::Evaluating, std::nullopt,
        {cancelled_reason(), deprecated_pin_reason()});
    secondary_valid.family = OperationFamily::ResidentState;
    secondary_valid.operation_kind = OperationKind::SavedPinMutation;
    const auto secondary_update = empty.with_revision(secondary_valid, at(10));
    require(secondary_update.status == ExplanationUpdateStatus::Accepted,
            "secondary-only reason was rejected in secondary position");
    require(secondary_update.revision->primary_reason_code() ==
                std::optional<std::string>{"residency_cancelled"},
            "primary reason was not derived from reasons[0]");

    auto canonical_order =
        resource_draft("canonical-order", 0, OperationPhase::Evaluating,
                       std::nullopt, {cancelled_reason(), capability_reason()});
    const auto canonical_order_update =
        empty.with_revision(canonical_order, at(10));
    require(canonical_order_update.status == ExplanationUpdateStatus::Accepted,
            "canonical distinct-code reason order was rejected");
    require(canonical_order_update.revision->reasons().size() == 2,
            "canonical-order revision lost a reason");
    require(canonical_order_update.revision->reasons()[0].code ==
                    "residency_cancelled" &&
                canonical_order_update.revision->reasons()[1].code ==
                    "residency_capability_unsupported",
            "projection store did not preserve canonical caller order");
    require(canonical_order_update.revision->primary_reason_code() ==
                std::optional<std::string>{"residency_cancelled"},
            "canonical first reason did not become primary");

    auto reversed_order =
        resource_draft("reversed-order", 0, OperationPhase::Evaluating,
                       std::nullopt, {capability_reason(), cancelled_reason()});
    require_rejection(empty.with_revision(reversed_order, at(10)),
                      ExplanationUpdateStatus::Invalid,
                      "reversed distinct-code reason order was accepted");

    auto adjacent_duplicate = resource_draft(
        "adjacent-duplicate", 0, OperationPhase::Evaluating, std::nullopt,
        {cancelled_reason(), cancelled_reason()});
    const auto adjacent_duplicate_update =
        empty.with_revision(adjacent_duplicate, at(10));
    require_rejection(adjacent_duplicate_update, ExplanationUpdateStatus::Invalid,
                      "adjacent duplicate reason code was accepted");
    require(adjacent_duplicate_update.diagnostic ==
                "the explanation repeats a reason code",
            "adjacent duplicate reason used the wrong diagnostic");

    auto nonadjacent_duplicate = resource_draft(
        "nonadjacent-duplicate", 0, OperationPhase::Evaluating, std::nullopt,
        {cancelled_reason(), capability_reason(), cancelled_reason()});
    const auto nonadjacent_duplicate_update =
        empty.with_revision(nonadjacent_duplicate, at(10));
    require_rejection(nonadjacent_duplicate_update,
                      ExplanationUpdateStatus::Invalid,
                      "nonadjacent duplicate reason code was accepted");
    require(nonadjacent_duplicate_update.diagnostic ==
                "the explanation repeats a reason code",
            "nonadjacent duplicate reason used the wrong diagnostic");

    auto max_reasons = resource_draft("max-reasons", 0);
    const std::vector<std::string> max_reason_codes{
        "residency_recovery_not_ready",
        "residency_cancelled",
        "residency_plan_invalidated",
        "residency_footprint_confidence_insufficient",
        "residency_capability_unsupported",
        "residency_evidence_missing",
        "residency_evidence_stale",
        "residency_evidence_unhealthy",
        "residency_evidence_incoherent",
        "residency_evidence_superseded",
        "slots_pinned_error",
        "router_residency_conflict",
        "residency_protected_pinned",
        "residency_protected_in_use",
        "residency_capacity_insufficient",
        "residency_plan_infeasible",
    };
    for (const auto &code : max_reason_codes) {
        max_reasons.reasons.push_back(reason(code, {}, {}, {}, {}, {}));
    }
    require(empty.with_revision(max_reasons, at(10)).status ==
                ExplanationUpdateStatus::Accepted,
            "the 16-reason boundary was rejected");
    auto too_many_reasons = resource_draft("too-many-reasons", 0);
    too_many_reasons.reasons.assign(17, cancelled_reason());
    require_rejection(empty.with_revision(too_many_reasons, at(10)),
                      ExplanationUpdateStatus::Invalid,
                      "an unbounded reason list was accepted");

    auto newer_minor = resource_draft("newer-minor-authority", 0);
    newer_minor.schema_version = {1, 1};
    require_rejection(empty.with_revision(newer_minor, at(10)),
                      ExplanationUpdateStatus::UnsupportedSchema,
                      "newer-minor projection schema gained store authority");

    auto unknown_major = resource_draft("unknown-major-authority", 0);
    unknown_major.schema_version = {2, 0};
    require_rejection(empty.with_revision(unknown_major, at(10)),
                      ExplanationUpdateStatus::UnsupportedSchema,
                      "unknown schema major gained store authority");
}

void test_revision_and_snapshot_semantics() {
    const auto empty = OperationExplanationStoreSnapshot::empty();
    auto initial_draft =
        resource_draft("owned-operation", 0, OperationPhase::Closing,
                       std::nullopt, {cancelled_reason()});
    initial_draft.plan_id = std::string("owned-plan");
    initial_draft.reasons.front().severity = "critical";
    initial_draft.reasons.front().title = u8"Borrowed \u202e title";
    initial_draft.reasons.front().default_message =
        u8"Borrowed \u2066 message \U0001f512";
    const auto committed_draft = initial_draft;
    const auto first = empty.with_revision(initial_draft, at(100));
    require(first.status == ExplanationUpdateStatus::Accepted,
            "initial revision was rejected");
    require(first.revision != nullptr, "initial revision was not returned");
    require(first.revision->schema_version().major == 1,
            "revision schema major drifted");
    require(first.revision->schema_version().minor == 0,
            "revision schema minor drifted");
    require(first.revision->operation_id() == "owned-operation",
            "revision identity drifted");
    require(first.revision->plan_id() ==
                std::optional<std::string>{"owned-plan"},
            "plan id drifted");
    require(first.revision->family() == OperationFamily::ResourceLifecycle,
            "family drifted");
    require(first.revision->operation_kind() == OperationKind::Admission,
            "operation kind drifted");
    require(first.revision->phase() == OperationPhase::Closing,
            "phase drifted");
    require(!first.revision->terminal_outcome().has_value(),
            "nonterminal revision has an outcome");
    require(first.revision->created_at() == at(100), "created time drifted");
    require(first.revision->updated_at() == at(100), "updated time drifted");
    require(!first.revision->expires_at().has_value(),
            "active revision received an expiry");
    require(!first.revision->forgotten_at().has_value(),
            "active revision received a forget time");
    require(first.revision->reasons().size() == 1, "revision lost its reason");
    require_local_reason(
        first.revision->reasons().front(), "residency_cancelled", "lifecycle",
        "p_lifecycle", "warning", "Lifecycle changed",
        "The residency lifecycle operation was interrupted or superseded.");

    initial_draft.operation_id = "mutated-operation";
    initial_draft.plan_id = std::string("mutated-plan");
    initial_draft.reasons.front().code = "mutated-code";
    initial_draft.reasons.front().title = "Mutated title";
    require(first.revision->operation_id() == "owned-operation" &&
                first.revision->plan_id() ==
                    std::optional<std::string>{"owned-plan"} &&
                first.revision->reasons().front().code ==
                    "residency_cancelled" &&
                first.revision->reasons().front().title == "Lifecycle changed",
            "revision retained borrowed input storage");

    auto exact_retry = committed_draft;
    const auto retry = first.snapshot.with_revision(exact_retry, at(999));
    require(retry.status == ExplanationUpdateStatus::Idempotent,
            "identical retry was not idempotent");
    require(retry.diagnostic.empty(), "idempotent retry has a diagnostic");
    require(retry.revision != nullptr,
            "idempotent retry omitted the existing revision");
    require(retry.revision->updated_at() == at(100),
            "idempotent retry changed commit time");

    auto changed_same_revision = exact_retry;
    changed_same_revision.phase = OperationPhase::Evaluating;
    const auto conflict =
        first.snapshot.with_revision(changed_same_revision, at(101));
    require_rejection(conflict, ExplanationUpdateStatus::RevisionConflict,
                      "changed same-number revision was accepted");
    require_detail(conflict.snapshot.lookup("owned-operation", at(101)), 0);

    auto missing_initial = resource_draft("missing-initial", 1);
    require_rejection(empty.with_revision(missing_initial, at(101)),
                      ExplanationUpdateStatus::RevisionConflict,
                      "new operation did not start at revision zero");

    auto gap = resource_draft("owned-operation", 2);
    gap.plan_id = std::string("owned-plan");
    require_rejection(first.snapshot.with_revision(gap, at(101)),
                      ExplanationUpdateStatus::RevisionConflict,
                      "revision gap was accepted");

    auto reverse_transition =
        resource_draft("owned-operation", 1, OperationPhase::Evaluating,
                       std::nullopt, {cancelled_reason()});
    reverse_transition.plan_id = std::string("owned-plan");
    const auto second =
        first.snapshot.with_revision(reverse_transition, at(200));
    require(second.status == ExplanationUpdateStatus::Accepted,
            "projection store reconstructed predecessor transition policy");
    require(second.revision->created_at() == at(100),
            "later revision changed creation time");
    require(second.revision->updated_at() == at(200),
            "later revision lost commit time");
    require_detail(first.snapshot.lookup("owned-operation", at(200)), 0);
    require_detail(second.snapshot.lookup("owned-operation", at(200)), 1);

    auto terminal =
        resource_draft("owned-operation", 2, OperationPhase::Terminal,
                       TerminalOutcome::Succeeded, {success_reason()});
    terminal.plan_id = std::string("owned-plan");
    const auto terminal_update =
        second.snapshot.with_revision(terminal, at(300));
    require(terminal_update.status == ExplanationUpdateStatus::Accepted,
            "terminal revision was rejected");
    require(terminal_update.revision->expires_at() ==
                std::optional<ExplanationTimePoint>{at(86700)},
            "terminal detail expiry did not use the generated 24-hour bound");
    require(terminal_update.revision->forgotten_at() ==
                std::optional<ExplanationTimePoint>{at(605100)},
            "terminal forget time did not use the generated seven-day bound");

    const auto terminal_retry =
        terminal_update.snapshot.with_revision(terminal, at(400));
    require(terminal_retry.status == ExplanationUpdateStatus::Idempotent,
            "identical terminal retry was not idempotent");
    require(terminal_retry.revision->updated_at() == at(300),
            "terminal retry changed commit time");

    auto after_terminal = resource_draft("owned-operation", 3);
    after_terminal.plan_id = std::string("owned-plan");
    require_rejection(
        terminal_update.snapshot.with_revision(after_terminal, at(400)),
        ExplanationUpdateStatus::TerminalSealed,
        "terminal operation accepted another revision");
}

void test_retention_and_compaction() {
    const auto empty = OperationExplanationStoreSnapshot::empty();

    const auto active = empty.with_revision(
        resource_draft("active-operation", 0, OperationPhase::Evaluating),
        at(1000));
    require(active.status == ExplanationUpdateStatus::Accepted,
            "active revision was rejected");
    require_detail(active.snapshot.lookup("active-operation", at(100000000)),
                   0);
    require_detail(active.snapshot.compacted(at(100000000))
                       .lookup("active-operation", at(100000000)),
                   0);

    const auto recovery =
        empty.with_revision(resource_draft("recovery-operation", 0,
                                           OperationPhase::RecoveryRequired,
                                           std::nullopt, {recovery_reason()}),
                            at(1000));
    require(recovery.status == ExplanationUpdateStatus::Accepted,
            "recovery-required revision was rejected");
    require_detail(
        recovery.snapshot.lookup("recovery-operation", at(100000000)), 0);
    require_detail(recovery.snapshot.compacted(at(100000000))
                       .lookup("recovery-operation", at(100000000)),
                   0);

    const auto terminal = empty.with_revision(
        resource_draft("terminal-operation", 0, OperationPhase::Terminal,
                       TerminalOutcome::Succeeded, {success_reason()}),
        at(1000));
    require(terminal.status == ExplanationUpdateStatus::Accepted,
            "retained terminal revision was rejected");

    require_detail(terminal.snapshot.lookup("terminal-operation", at(87399)),
                   0);

    const auto detail_boundary =
        terminal.snapshot.lookup("terminal-operation", at(87400));
    require(detail_boundary.status == ExplanationLookupStatus::Tombstone,
            "detail remained visible at the 24-hour boundary");
    require(detail_boundary.revision == nullptr,
            "tombstone lookup exposed detail");
    require(detail_boundary.tombstone.has_value(),
            "tombstone lookup omitted its marker");
    require(detail_boundary.tombstone->operation_id() == "terminal-operation",
            "tombstone changed the operation identity");
    require(detail_boundary.tombstone->last_explanation_revision() == 0,
            "tombstone changed the last revision");
    require(detail_boundary.tombstone->detail_expired_at() == at(87400),
            "tombstone changed the detail expiry");
    require(detail_boundary.tombstone->forgotten_at() == at(605800),
            "tombstone changed the forget time");

    const auto last_tombstone =
        terminal.snapshot.lookup("terminal-operation", at(605799));
    require(last_tombstone.status == ExplanationLookupStatus::Tombstone,
            "tombstone expired before the seven-day boundary");
    require(last_tombstone.revision == nullptr,
            "late tombstone exposed detail");

    const auto forgotten =
        terminal.snapshot.lookup("terminal-operation", at(605800));
    require(forgotten.status == ExplanationLookupStatus::Missing,
            "forgotten record remained addressable");
    require(forgotten.revision == nullptr, "missing lookup exposed detail");
    require(!forgotten.tombstone.has_value(),
            "missing lookup exposed a tombstone");

    const auto compacted_tombstone = terminal.snapshot.compacted(at(87400));
    require(
        compacted_tombstone.lookup("terminal-operation", at(87400)).status ==
            ExplanationLookupStatus::Tombstone,
        "compaction did not retain the tombstone interval");
    require(compacted_tombstone.compacted(at(605800))
                    .lookup("terminal-operation", at(605800))
                    .status == ExplanationLookupStatus::Missing,
            "compaction did not forget the expired tombstone");

    const auto never_present =
        terminal.snapshot.lookup("never-present", at(1000));
    require(never_present.status == ExplanationLookupStatus::Missing,
            "missing identity was not missing");
    require(never_present.revision == nullptr,
            "missing identity exposed detail");
    require(!never_present.tombstone.has_value(),
            "missing identity exposed a tombstone");
}

}

int run_residency_explanations_public_seam() {
    test_reason_rendering();
    test_update_validation();
    test_revision_and_snapshot_semantics();
    test_retention_and_compaction();
    return 0;
}

#ifndef RESIDENCY_EXPLANATIONS_SEAM_NO_MAIN
int main() {
    return run_residency_explanations_public_seam();
}
#endif
