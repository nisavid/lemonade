#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace lemon::residency {

class GeneratedContractRegistry;

class PromotionUnitId {
public:
    const std::string& token() const noexcept { return token_; }

    friend bool operator==(const PromotionUnitId& left, const PromotionUnitId& right) {
        return left.token_ == right.token_;
    }

    friend bool operator!=(const PromotionUnitId& left, const PromotionUnitId& right) {
        return !(left == right);
    }

private:
    explicit PromotionUnitId(std::string token) : token_(std::move(token)) {}

    std::string token_;

    friend class GeneratedContractRegistry;
};

class KnownReasonCode {
public:
    const std::string& token() const noexcept { return token_; }

    friend bool operator==(const KnownReasonCode& left, const KnownReasonCode& right) {
        return left.token_ == right.token_;
    }

    friend bool operator!=(const KnownReasonCode& left, const KnownReasonCode& right) {
        return !(left == right);
    }

private:
    explicit KnownReasonCode(std::string token) : token_(std::move(token)) {}

    std::string token_;

    friend class GeneratedContractRegistry;
};

class FallbackId {
public:
    const std::string& token() const noexcept { return token_; }

    friend bool operator==(const FallbackId& left, const FallbackId& right) {
        return left.token_ == right.token_;
    }

    friend bool operator!=(const FallbackId& left, const FallbackId& right) {
        return !(left == right);
    }

private:
    explicit FallbackId(std::string token) : token_(std::move(token)) {}

    std::string token_;

    friend class GeneratedContractRegistry;
};

class SchemaType {
public:
    const std::string& token() const noexcept { return token_; }

    friend bool operator==(const SchemaType& left, const SchemaType& right) {
        return left.token_ == right.token_;
    }

    friend bool operator!=(const SchemaType& left, const SchemaType& right) {
        return !(left == right);
    }

private:
    explicit SchemaType(std::string token) : token_(std::move(token)) {}

    std::string token_;

    friend class GeneratedContractRegistry;
};

struct UnknownWireValue {
    std::string token;
};

template <typename T> class DecodedValue {
public:
    static DecodedValue known(T value) { return DecodedValue(std::move(value)); }

    static DecodedValue unknown(std::string_view token) {
        return DecodedValue(UnknownWireValue{std::string(token)});
    }

    bool is_known() const noexcept { return std::holds_alternative<T>(value_); }

    const T* known_value() const noexcept { return std::get_if<T>(&value_); }

    const UnknownWireValue* unknown_value() const noexcept {
        return std::get_if<UnknownWireValue>(&value_);
    }

private:
    explicit DecodedValue(T value) : value_(std::move(value)) {}
    explicit DecodedValue(UnknownWireValue value) : value_(std::move(value)) {}

    std::variant<T, UnknownWireValue> value_;
};

using ReasonCode = DecodedValue<KnownReasonCode>;

enum class PromotionUnitKind {
    ExactCell,
    CompatibilityContract,
    LaterRuntime,
};

enum class OperationFamily {
    ResourceLifecycle,
    ResidentState,
};

enum class OperationTemplate {
    Adm,
    Lfr,
    Pre,
    Sta,
    Rec,
    Unl,
    Pin,
    Npc,
};

enum class OperationKind {
    Admission,
    ExplicitUnload,
    ForceUnload,
    PressureReclamation,
    StartupLoad,
    ServiceTermination,
    DeadBackendPruning,
    SameEpochRecoveryCleanup,
    PriorEpochOwnerCleanup,
    ArtifactScopeRecoveryCleanup,
    SavedPinMutation,
    RuntimePinMutation,
    LegacyPinBatch,
    ResidentStateRecoveryCleanup,
};

enum class ConstraintKind {
    GpuSharedResidency,
    GpuProviderResolvedCapacity,
    HostMemAvailableFloor,
    HostEffectsProviderResolved,
    ModelTypePool,
    Ownership,
    FlmTypeSlot,
    NpuCrossFamily,
    NpuExclusive,
};

enum class CapabilityLevel {
    Unsupported,
    FallbackOnly,
    Modeled,
    Validated,
};

enum class DeliveryState {
    Absent,
    ImplementedUnverified,
    ReleaseVerified,
};

enum class EffectiveMode {
    CapacityPlanned,
    BackendEnforced,
    CountLimited,
    Refused,
    AutomaticReclamation,
    ReportOnly,
    Disabled,
    AutomaticGrouped,
    Blocked,
    Ready,
    CleanupOnly,
};

enum class EffectiveModeDomain {
    Admission,
    Pressure,
    Startup,
    Recovery,
};

enum class FootprintConfidence {
    EnforcedComplete,
    ValidatedPredictor,
    CalibratedInstance,
    Incomplete,
    Unknown,
};

enum class SignalEvidenceState {
    Valid,
    Missing,
    Stale,
    Unhealthy,
    Incoherent,
    Superseded,
};

enum class OperationPhase {
    Evaluating,
    WaitingForEvidence,
    WaitingForInUse,
    Reserved,
    Executing,
    Closing,
    RecoveryRequired,
    Terminal,
};

enum class TerminalOutcome {
    Succeeded,
    Refused,
    Failed,
    Cancelled,
    Superseded,
    PartiallySucceeded,
    Quarantined,
};

enum class SchemaUse {
    Authority,
    Projection,
};

enum class SchemaCompatibility {
    Exact,
    ProjectionOnly,
    Unsupported,
};

struct SchemaVersion {
    std::uint32_t major;
    std::uint32_t minor;
};

struct SchemaRef {
    DecodedValue<SchemaType> type;
    SchemaVersion version;
};

namespace detail {

template <typename Enum, std::size_t Size>
inline DecodedValue<Enum>
decode_enum(std::string_view wire,
            const std::array<std::pair<Enum, std::string_view>, Size>& values) {
    for (const auto& entry : values) {
        if (entry.second == wire) {
            return DecodedValue<Enum>::known(entry.first);
        }
    }
    return DecodedValue<Enum>::unknown(wire);
}

template <typename Enum, std::size_t Size>
constexpr std::string_view
encode_enum(Enum value,
            const std::array<std::pair<Enum, std::string_view>, Size>& values) noexcept {
    for (const auto& entry : values) {
        if (entry.first == value) {
            return entry.second;
        }
    }
    return {};
}

inline constexpr std::array<std::pair<PromotionUnitKind, std::string_view>, 3> promotion_unit_kinds{
    {
        {PromotionUnitKind::ExactCell, "exact_cell"},
        {PromotionUnitKind::CompatibilityContract, "compatibility_contract"},
        {PromotionUnitKind::LaterRuntime, "later_runtime"},
    }};

inline constexpr std::array<std::pair<OperationFamily, std::string_view>, 2> operation_families{{
    {OperationFamily::ResourceLifecycle, "resource_lifecycle"},
    {OperationFamily::ResidentState, "resident_state"},
}};

inline constexpr std::array<std::pair<OperationTemplate, std::string_view>, 8> operation_templates{{
    {OperationTemplate::Adm, "ADM"},
    {OperationTemplate::Lfr, "LFR"},
    {OperationTemplate::Pre, "PRE"},
    {OperationTemplate::Sta, "STA"},
    {OperationTemplate::Rec, "REC"},
    {OperationTemplate::Unl, "UNL"},
    {OperationTemplate::Pin, "PIN"},
    {OperationTemplate::Npc, "NPC"},
}};

inline constexpr std::array<std::pair<OperationKind, std::string_view>, 14> operation_kinds{{
    {OperationKind::Admission, "admission"},
    {OperationKind::ExplicitUnload, "explicit_unload"},
    {OperationKind::ForceUnload, "force_unload"},
    {OperationKind::PressureReclamation, "pressure_reclamation"},
    {OperationKind::StartupLoad, "startup_load"},
    {OperationKind::ServiceTermination, "service_termination"},
    {OperationKind::DeadBackendPruning, "dead_backend_pruning"},
    {OperationKind::SameEpochRecoveryCleanup, "same_epoch_recovery_cleanup"},
    {OperationKind::PriorEpochOwnerCleanup, "prior_epoch_owner_cleanup"},
    {OperationKind::ArtifactScopeRecoveryCleanup, "artifact_scope_recovery_cleanup"},
    {OperationKind::SavedPinMutation, "saved_pin_mutation"},
    {OperationKind::RuntimePinMutation, "runtime_pin_mutation"},
    {OperationKind::LegacyPinBatch, "legacy_pin_batch"},
    {OperationKind::ResidentStateRecoveryCleanup, "resident_state_recovery_cleanup"},
}};

inline constexpr std::array<std::pair<ConstraintKind, std::string_view>, 9> constraint_kinds{{
    {ConstraintKind::GpuSharedResidency, "gpu_shared_residency"},
    {ConstraintKind::GpuProviderResolvedCapacity, "gpu_provider_resolved_capacity"},
    {ConstraintKind::HostMemAvailableFloor, "host_memavailable_floor"},
    {ConstraintKind::HostEffectsProviderResolved, "host_effects_provider_resolved"},
    {ConstraintKind::ModelTypePool, "model_type_pool"},
    {ConstraintKind::Ownership, "ownership"},
    {ConstraintKind::FlmTypeSlot, "flm_type_slot"},
    {ConstraintKind::NpuCrossFamily, "npu_cross_family"},
    {ConstraintKind::NpuExclusive, "npu_exclusive"},
}};

inline constexpr std::array<std::pair<CapabilityLevel, std::string_view>, 4> capability_levels{{
    {CapabilityLevel::Unsupported, "unsupported"},
    {CapabilityLevel::FallbackOnly, "fallback_only"},
    {CapabilityLevel::Modeled, "modeled"},
    {CapabilityLevel::Validated, "validated"},
}};

inline constexpr std::array<std::pair<DeliveryState, std::string_view>, 3> delivery_states{{
    {DeliveryState::Absent, "absent"},
    {DeliveryState::ImplementedUnverified, "implemented_unverified"},
    {DeliveryState::ReleaseVerified, "release_verified"},
}};

inline constexpr std::array<std::pair<EffectiveMode, std::string_view>, 11> effective_modes{{
    {EffectiveMode::CapacityPlanned, "capacity_planned"},
    {EffectiveMode::BackendEnforced, "backend_enforced"},
    {EffectiveMode::CountLimited, "count_limited"},
    {EffectiveMode::Refused, "refused"},
    {EffectiveMode::AutomaticReclamation, "automatic_reclamation"},
    {EffectiveMode::ReportOnly, "report_only"},
    {EffectiveMode::Disabled, "disabled"},
    {EffectiveMode::AutomaticGrouped, "automatic_grouped"},
    {EffectiveMode::Blocked, "blocked"},
    {EffectiveMode::Ready, "ready"},
    {EffectiveMode::CleanupOnly, "cleanup_only"},
}};

inline constexpr std::array<std::pair<FootprintConfidence, std::string_view>, 5>
    footprint_confidences{{
        {FootprintConfidence::EnforcedComplete, "enforced_complete"},
        {FootprintConfidence::ValidatedPredictor, "validated_predictor"},
        {FootprintConfidence::CalibratedInstance, "calibrated_instance"},
        {FootprintConfidence::Incomplete, "incomplete"},
        {FootprintConfidence::Unknown, "unknown"},
    }};

inline constexpr std::array<std::pair<SignalEvidenceState, std::string_view>, 6>
    signal_evidence_states{{
        {SignalEvidenceState::Valid, "valid"},
        {SignalEvidenceState::Missing, "missing"},
        {SignalEvidenceState::Stale, "stale"},
        {SignalEvidenceState::Unhealthy, "unhealthy"},
        {SignalEvidenceState::Incoherent, "incoherent"},
        {SignalEvidenceState::Superseded, "superseded"},
    }};

inline constexpr std::array<std::pair<OperationPhase, std::string_view>, 8> operation_phases{{
    {OperationPhase::Evaluating, "evaluating"},
    {OperationPhase::WaitingForEvidence, "waiting_for_evidence"},
    {OperationPhase::WaitingForInUse, "waiting_for_in_use"},
    {OperationPhase::Reserved, "reserved"},
    {OperationPhase::Executing, "executing"},
    {OperationPhase::Closing, "closing"},
    {OperationPhase::RecoveryRequired, "recovery_required"},
    {OperationPhase::Terminal, "terminal"},
}};

inline constexpr std::array<std::pair<TerminalOutcome, std::string_view>, 7> terminal_outcomes{{
    {TerminalOutcome::Succeeded, "succeeded"},
    {TerminalOutcome::Refused, "refused"},
    {TerminalOutcome::Failed, "failed"},
    {TerminalOutcome::Cancelled, "cancelled"},
    {TerminalOutcome::Superseded, "superseded"},
    {TerminalOutcome::PartiallySucceeded, "partially_succeeded"},
    {TerminalOutcome::Quarantined, "quarantined"},
}};

}

inline constexpr std::string_view wire_name(PromotionUnitKind value) noexcept {
    return detail::encode_enum(value, detail::promotion_unit_kinds);
}

inline constexpr std::string_view wire_name(OperationFamily value) noexcept {
    return detail::encode_enum(value, detail::operation_families);
}

inline constexpr std::string_view wire_name(OperationTemplate value) noexcept {
    return detail::encode_enum(value, detail::operation_templates);
}

inline constexpr std::string_view wire_name(OperationKind value) noexcept {
    return detail::encode_enum(value, detail::operation_kinds);
}

inline constexpr std::string_view wire_name(ConstraintKind value) noexcept {
    return detail::encode_enum(value, detail::constraint_kinds);
}

inline constexpr std::string_view wire_name(CapabilityLevel value) noexcept {
    return detail::encode_enum(value, detail::capability_levels);
}

inline constexpr std::string_view wire_name(DeliveryState value) noexcept {
    return detail::encode_enum(value, detail::delivery_states);
}

inline constexpr std::string_view wire_name(EffectiveMode value) noexcept {
    return detail::encode_enum(value, detail::effective_modes);
}

inline constexpr std::string_view wire_name(FootprintConfidence value) noexcept {
    return detail::encode_enum(value, detail::footprint_confidences);
}

inline constexpr std::string_view wire_name(SignalEvidenceState value) noexcept {
    return detail::encode_enum(value, detail::signal_evidence_states);
}

inline constexpr std::string_view wire_name(OperationPhase value) noexcept {
    return detail::encode_enum(value, detail::operation_phases);
}

inline constexpr std::string_view wire_name(TerminalOutcome value) noexcept {
    return detail::encode_enum(value, detail::terminal_outcomes);
}

inline DecodedValue<PromotionUnitKind> decode_promotion_unit_kind(std::string_view wire) {
    return detail::decode_enum(wire, detail::promotion_unit_kinds);
}

inline DecodedValue<OperationFamily> decode_operation_family(std::string_view wire) {
    return detail::decode_enum(wire, detail::operation_families);
}

inline DecodedValue<OperationTemplate> decode_operation_template(std::string_view wire) {
    return detail::decode_enum(wire, detail::operation_templates);
}

inline DecodedValue<OperationKind> decode_operation_kind(std::string_view wire) {
    return detail::decode_enum(wire, detail::operation_kinds);
}

inline DecodedValue<ConstraintKind> decode_constraint_kind(std::string_view wire) {
    return detail::decode_enum(wire, detail::constraint_kinds);
}

inline DecodedValue<CapabilityLevel> decode_capability_level(std::string_view wire) {
    return detail::decode_enum(wire, detail::capability_levels);
}

inline DecodedValue<DeliveryState> decode_delivery_state(std::string_view wire) {
    return detail::decode_enum(wire, detail::delivery_states);
}

inline DecodedValue<EffectiveMode> decode_effective_mode(std::string_view wire) {
    return detail::decode_enum(wire, detail::effective_modes);
}

inline DecodedValue<FootprintConfidence> decode_footprint_confidence(std::string_view wire) {
    return detail::decode_enum(wire, detail::footprint_confidences);
}

inline DecodedValue<SignalEvidenceState> decode_signal_evidence_state(std::string_view wire) {
    return detail::decode_enum(wire, detail::signal_evidence_states);
}

inline DecodedValue<OperationPhase> decode_operation_phase(std::string_view wire) {
    return detail::decode_enum(wire, detail::operation_phases);
}

inline DecodedValue<TerminalOutcome> decode_terminal_outcome(std::string_view wire) {
    return detail::decode_enum(wire, detail::terminal_outcomes);
}

inline constexpr bool effective_mode_is_in_domain(EffectiveModeDomain domain,
                                                  EffectiveMode mode) noexcept {
    switch (domain) {
        case EffectiveModeDomain::Admission:
            return mode == EffectiveMode::CapacityPlanned ||
                   mode == EffectiveMode::BackendEnforced || mode == EffectiveMode::CountLimited ||
                   mode == EffectiveMode::Refused;
        case EffectiveModeDomain::Pressure:
            return mode == EffectiveMode::AutomaticReclamation ||
                   mode == EffectiveMode::ReportOnly || mode == EffectiveMode::Disabled;
        case EffectiveModeDomain::Startup:
            return mode == EffectiveMode::AutomaticGrouped || mode == EffectiveMode::Disabled ||
                   mode == EffectiveMode::Blocked;
        case EffectiveModeDomain::Recovery:
            return mode == EffectiveMode::Ready || mode == EffectiveMode::CleanupOnly ||
                   mode == EffectiveMode::Blocked;
    }
    return false;
}

struct EffectiveModeSelection {
    EffectiveModeDomain domain;
    DecodedValue<EffectiveMode> mode;
    std::optional<DecodedValue<FallbackId>> fallback_id;

    bool fallback_used() const noexcept { return fallback_id.has_value(); }

    bool is_structurally_valid() const noexcept {
        const auto* known_mode = mode.known_value();
        if (known_mode == nullptr || !effective_mode_is_in_domain(domain, *known_mode)) {
            return false;
        }
        return !fallback_id.has_value() || fallback_id->is_known();
    }
};

inline constexpr bool is_known_operation_family(OperationFamily family) noexcept {
    switch (family) {
        case OperationFamily::ResourceLifecycle:
        case OperationFamily::ResidentState:
            return true;
    }
    return false;
}

inline constexpr bool is_known_operation_phase(OperationPhase phase) noexcept {
    switch (phase) {
        case OperationPhase::Evaluating:
        case OperationPhase::WaitingForEvidence:
        case OperationPhase::WaitingForInUse:
        case OperationPhase::Reserved:
        case OperationPhase::Executing:
        case OperationPhase::Closing:
        case OperationPhase::RecoveryRequired:
        case OperationPhase::Terminal:
            return true;
    }
    return false;
}

inline constexpr bool is_known_terminal_outcome(TerminalOutcome outcome) noexcept {
    switch (outcome) {
        case TerminalOutcome::Succeeded:
        case TerminalOutcome::Refused:
        case TerminalOutcome::Failed:
        case TerminalOutcome::Cancelled:
        case TerminalOutcome::Superseded:
        case TerminalOutcome::PartiallySucceeded:
        case TerminalOutcome::Quarantined:
            return true;
    }
    return false;
}

inline bool operation_state_is_valid(OperationFamily family, OperationPhase phase,
                                     std::optional<TerminalOutcome> outcome) noexcept {
    if (!is_known_operation_family(family) || !is_known_operation_phase(phase)) {
        return false;
    }
    if ((phase == OperationPhase::Terminal) != outcome.has_value()) {
        return false;
    }
    if (!outcome.has_value()) {
        return true;
    }
    if (!is_known_terminal_outcome(*outcome)) {
        return false;
    }
    return *outcome != TerminalOutcome::Quarantined || family == OperationFamily::ResourceLifecycle;
}

inline constexpr SchemaCompatibility
classify_schema_version(SchemaVersion supported, SchemaVersion received, SchemaUse use) noexcept {
    if (use != SchemaUse::Authority && use != SchemaUse::Projection) {
        return SchemaCompatibility::Unsupported;
    }
    if (supported.major != received.major) {
        return SchemaCompatibility::Unsupported;
    }
    if (supported.minor == received.minor) {
        return SchemaCompatibility::Exact;
    }
    if (use == SchemaUse::Projection && received.minor > supported.minor) {
        return SchemaCompatibility::ProjectionOnly;
    }
    return SchemaCompatibility::Unsupported;
}

}
