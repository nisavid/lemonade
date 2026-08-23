#pragma once

#include "lemon/residency/catalog.h"
#include "lemon/residency/claims.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lemon::residency {

inline constexpr SchemaVersion supported_local_overlay_schema{1, 0};
inline constexpr std::size_t max_local_overlay_input_bytes = 64 * 1024;
inline constexpr std::size_t max_local_overlay_identifier_bytes = 128;
inline constexpr std::size_t max_local_overlay_diagnostic_bytes = 256;

enum class OverlayContractStatus {
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
    IncompleteIdentity,
    IncompleteClaimClosure,
    InvalidClaimClosure,
    ClaimArithmeticOverflow,
    ReferenceMismatch,
    InvalidSequence,
    DigestMismatch,
    DigestUnavailable,
};

enum class LocalOverlayMethodScope {
    DeploymentExact,
    ArchitecturePredicate,
};

enum class LocalOverlayObjectStatus {
    Qualified,
};

enum class OverlayRootTransition {
    Qualification,
    Rollback,
};

enum class OverlayAuthorityStatus {
    Active,
};

struct LocalOverlaySelectorIdentity {
    std::string catalog_sha256;
    RuntimeCatalogSelector catalog_selector;
    std::string canonical_model_id;
    std::string model_artifact_sha256;
    std::string backend_build_sha256;
    std::string device_identity_sha256;
    std::string topology_sha256;
    std::string dependency_set_sha256;
    std::string driver_identity_sha256;
    std::string configuration_sha256;
    std::string workload_sha256;
    std::string operation_contract_sha256;
};

struct OverlaySourceGenerations {
    std::uint64_t model = 0;
    std::uint64_t backend = 0;
    std::uint64_t device = 0;
    std::uint64_t topology = 0;
    std::uint64_t driver = 0;
    std::uint64_t configuration = 0;
    std::uint64_t workload = 0;
};

struct ProfilingInputEnvelopeDraft {
    SchemaVersion schema = supported_local_overlay_schema;
    std::string deployment_id;
    std::uint64_t sequence = 0;
    std::string profiling_transaction_id;
    LocalOverlaySelectorIdentity selector;
    OverlaySourceGenerations generations;
    std::vector<ClaimFamilyClosure> attributed_claims;
    std::string baseline_observation_sha256;
    std::string workload_observation_sha256;
    std::string release_observation_sha256;
    std::string observation_contract_sha256;
    std::string predictor_contract_sha256;
    std::string observed_at;
    std::string fresh_until;
    std::uint64_t max_clock_skew_milliseconds = 0;
    bool attribution_complete = false;
    bool external_demand_absent = false;
    bool lifecycle_release_verified = false;
};

struct LocalOverlayMethodIdentity {
    std::string method_id;
    std::string method_revision_sha256;
    LocalOverlayMethodScope scope = LocalOverlayMethodScope::DeploymentExact;
    OperationKind operation_kind = OperationKind::Admission;
    std::optional<std::string> architecture_predicate_sha256;
    std::string calibration_revision_sha256;
};

struct LocalOverlayObjectDraft {
    SchemaVersion schema = supported_local_overlay_schema;
    std::string deployment_id;
    std::uint64_t sequence = 0;
    std::string profiling_input_sha256;
    LocalOverlaySelectorIdentity selector;
    LocalOverlayMethodIdentity method;
    std::vector<ClaimFamilyClosure> bound_claims;
    std::vector<ClaimFamilyClosure> uncertainty_claims;
    std::vector<ClaimFamilyClosure> safety_margin_claims;
    std::uint32_t confidence_basis_points = 0;
    std::string qualified_at;
    std::string expires_at;
    LocalOverlayObjectStatus status = LocalOverlayObjectStatus::Qualified;
    std::string decision_trace_sha256;
};

struct OverlayActivationRootDraft {
    SchemaVersion schema = supported_local_overlay_schema;
    std::string deployment_id;
    std::uint64_t generation = 0;
    std::optional<std::string> previous_root_sha256;
    OverlayRootTransition transition = OverlayRootTransition::Qualification;
    OverlayAuthorityStatus authority_status = OverlayAuthorityStatus::Active;
    std::string selected_overlay_sha256;
    std::uint64_t selected_overlay_sequence = 0;
    std::uint64_t sequence_high_water = 0;
    std::string selected_selector_sha256;
    std::string selected_method_sha256;
    std::string decision_trace_sha256;
    std::string activated_at;
    std::string expires_at;
};

class ParsedProfilingInputEnvelope;
class ParsedLocalOverlayObject;
class ParsedOverlayActivationRoot;
struct ParsedProfilingInputEnvelopeResult;
struct ParsedLocalOverlayObjectResult;
struct ParsedOverlayActivationRootResult;

class ParsedProfilingInputEnvelope {
public:
    ParsedProfilingInputEnvelope() = delete;
    ParsedProfilingInputEnvelope(const ParsedProfilingInputEnvelope &) =
        default;
    ParsedProfilingInputEnvelope &
    operator=(const ParsedProfilingInputEnvelope &) = default;

    SchemaVersion schema() const noexcept;
    std::string_view deployment_id() const noexcept;
    std::uint64_t sequence() const noexcept;
    std::string_view profiling_transaction_id() const noexcept;
    const LocalOverlaySelectorIdentity &selector() const noexcept;
    std::string_view selector_sha256() const noexcept;
    const OverlaySourceGenerations &generations() const noexcept;
    const std::vector<ClaimFamilyClosure> &attributed_claims() const noexcept;
    std::string_view baseline_observation_sha256() const noexcept;
    std::string_view workload_observation_sha256() const noexcept;
    std::string_view release_observation_sha256() const noexcept;
    std::string_view observation_contract_sha256() const noexcept;
    std::string_view predictor_contract_sha256() const noexcept;
    std::string_view observed_at() const noexcept;
    std::string_view fresh_until() const noexcept;
    std::uint64_t max_clock_skew_milliseconds() const noexcept;
    bool attribution_complete() const noexcept;
    bool external_demand_absent() const noexcept;
    bool lifecycle_release_verified() const noexcept;
    std::string_view checksum_sha256() const noexcept;
    std::string_view canonical_bytes() const noexcept;

private:
    ParsedProfilingInputEnvelope(ProfilingInputEnvelopeDraft draft,
                                 std::string selector_sha256,
                                 std::string checksum_sha256,
                                 std::string canonical_bytes);

    ProfilingInputEnvelopeDraft draft_;
    std::string selector_sha256_;
    std::string checksum_sha256_;
    std::string canonical_bytes_;

    friend ParsedProfilingInputEnvelopeResult
    seal_profiling_input(ProfilingInputEnvelopeDraft draft);
    friend ParsedProfilingInputEnvelopeResult
    parse_profiling_input(std::string_view bytes);
};

class ParsedLocalOverlayObject {
public:
    ParsedLocalOverlayObject() = delete;
    ParsedLocalOverlayObject(const ParsedLocalOverlayObject &) = default;
    ParsedLocalOverlayObject &
    operator=(const ParsedLocalOverlayObject &) = default;

    SchemaVersion schema() const noexcept;
    std::string_view deployment_id() const noexcept;
    std::uint64_t sequence() const noexcept;
    std::string_view profiling_input_sha256() const noexcept;
    const LocalOverlaySelectorIdentity &selector() const noexcept;
    std::string_view selector_sha256() const noexcept;
    const LocalOverlayMethodIdentity &method() const noexcept;
    std::string_view method_sha256() const noexcept;
    const std::vector<ClaimFamilyClosure> &bound_claims() const noexcept;
    const std::vector<ClaimFamilyClosure> &uncertainty_claims() const noexcept;
    const std::vector<ClaimFamilyClosure> &
    safety_margin_claims() const noexcept;
    const std::vector<ClaimFamilyClosure> &conservative_claims() const noexcept;
    std::uint32_t confidence_basis_points() const noexcept;
    LocalOverlayObjectStatus status() const noexcept;
    std::string_view qualified_at() const noexcept;
    std::string_view expires_at() const noexcept;
    std::string_view decision_trace_sha256() const noexcept;
    std::string_view checksum_sha256() const noexcept;
    std::string_view canonical_bytes() const noexcept;

private:
    ParsedLocalOverlayObject(
        LocalOverlayObjectDraft draft,
        std::vector<ClaimFamilyClosure> conservative_claims,
        std::string selector_sha256, std::string method_sha256,
        std::string checksum_sha256, std::string canonical_bytes);

    LocalOverlayObjectDraft draft_;
    std::vector<ClaimFamilyClosure> conservative_claims_;
    std::string selector_sha256_;
    std::string method_sha256_;
    std::string checksum_sha256_;
    std::string canonical_bytes_;

    friend ParsedLocalOverlayObjectResult
    seal_local_overlay(LocalOverlayObjectDraft draft);
    friend ParsedLocalOverlayObjectResult
    parse_local_overlay(std::string_view bytes);
};

class ParsedOverlayActivationRoot {
public:
    ParsedOverlayActivationRoot() = delete;
    ParsedOverlayActivationRoot(const ParsedOverlayActivationRoot &) = default;
    ParsedOverlayActivationRoot &
    operator=(const ParsedOverlayActivationRoot &) = default;

    SchemaVersion schema() const noexcept;
    std::string_view deployment_id() const noexcept;
    std::uint64_t generation() const noexcept;
    const std::optional<std::string> &previous_root_sha256() const noexcept;
    OverlayRootTransition transition() const noexcept;
    OverlayAuthorityStatus authority_status() const noexcept;
    std::string_view selected_overlay_sha256() const noexcept;
    std::uint64_t selected_overlay_sequence() const noexcept;
    std::uint64_t sequence_high_water() const noexcept;
    std::string_view selected_selector_sha256() const noexcept;
    std::string_view selected_method_sha256() const noexcept;
    std::string_view decision_trace_sha256() const noexcept;
    std::string_view activated_at() const noexcept;
    std::string_view expires_at() const noexcept;
    std::string_view checksum_sha256() const noexcept;
    std::string_view canonical_bytes() const noexcept;

private:
    ParsedOverlayActivationRoot(OverlayActivationRootDraft draft,
                                std::string checksum_sha256,
                                std::string canonical_bytes);

    OverlayActivationRootDraft draft_;
    std::string checksum_sha256_;
    std::string canonical_bytes_;

    friend ParsedOverlayActivationRootResult
    seal_overlay_activation_root(OverlayActivationRootDraft draft);
    friend ParsedOverlayActivationRootResult
    parse_overlay_activation_root(std::string_view bytes);
};

struct ParsedProfilingInputEnvelopeResult {
    OverlayContractStatus status = OverlayContractStatus::InvalidValue;
    std::string diagnostic;
    std::optional<ParsedProfilingInputEnvelope> candidate;

    bool accepted() const noexcept;
};

struct ParsedLocalOverlayObjectResult {
    OverlayContractStatus status = OverlayContractStatus::InvalidValue;
    std::string diagnostic;
    std::optional<ParsedLocalOverlayObject> candidate;

    bool accepted() const noexcept;
};

struct ParsedOverlayActivationRootResult {
    OverlayContractStatus status = OverlayContractStatus::InvalidValue;
    std::string diagnostic;
    std::optional<ParsedOverlayActivationRoot> candidate;

    bool accepted() const noexcept;
};

ParsedProfilingInputEnvelopeResult
seal_profiling_input(ProfilingInputEnvelopeDraft draft);
ParsedProfilingInputEnvelopeResult
parse_profiling_input(std::string_view bytes);
ParsedLocalOverlayObjectResult
seal_local_overlay(LocalOverlayObjectDraft draft);
ParsedLocalOverlayObjectResult parse_local_overlay(std::string_view bytes);
ParsedOverlayActivationRootResult
seal_overlay_activation_root(OverlayActivationRootDraft draft);
ParsedOverlayActivationRootResult
parse_overlay_activation_root(std::string_view bytes);

} // namespace lemon::residency
