#include "lemon/residency/local_overlay.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

using namespace lemon::residency;
using json = nlohmann::json;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::string digest(char value) { return std::string(64, value); }

std::vector<ClaimFamilyClosure> claims(std::uint64_t bytes) {
    std::vector<ClaimAmount> capacity;
    if (bytes != 0) {
        capacity.push_back(ClaimAmount{"gpu/gtt", ClaimUnit::Bytes, bytes});
    }
    return {
        ClaimFamilyClosure{
            ClaimFamily::ConsumableCapacity,
            bytes == 0 ? ClaimCompleteness::KnownZero
                       : ClaimCompleteness::Bounded,
            std::move(capacity),
        },
        ClaimFamilyClosure{
            ClaimFamily::SafetyFloor,
            ClaimCompleteness::KnownZero,
            {},
        },
        ClaimFamilyClosure{
            ClaimFamily::CardinalityPool,
            ClaimCompleteness::KnownZero,
            {},
        },
        ClaimFamilyClosure{
            ClaimFamily::CompatibilityExclusivity,
            ClaimCompleteness::NotApplicable,
            {},
        },
    };
}

std::vector<ClaimFamilyClosure> compatibility_claims(bool claimed) {
    std::vector<ClaimAmount> compatibility;
    if (claimed) {
        compatibility.push_back(
            ClaimAmount{"npu/exclusive", ClaimUnit::Count, 1});
    }
    return {
        ClaimFamilyClosure{ClaimFamily::ConsumableCapacity,
                           ClaimCompleteness::KnownZero, {}},
        ClaimFamilyClosure{ClaimFamily::SafetyFloor,
                           ClaimCompleteness::KnownZero, {}},
        ClaimFamilyClosure{ClaimFamily::CardinalityPool,
                           ClaimCompleteness::KnownZero, {}},
        ClaimFamilyClosure{
            ClaimFamily::CompatibilityExclusivity,
            claimed ? ClaimCompleteness::Bounded
                    : ClaimCompleteness::KnownZero,
            std::move(compatibility),
        },
    };
}

LocalOverlaySelectorIdentity selector() {
    RuntimeCatalogSelector catalog;
    catalog.source_support_baseline = std::string(40, 'a');
    catalog.base_variant = "llamacpp-rocm";
    catalog.platform = "linux-amd-rocm-llamacpp";
    catalog.backend_channel = "stable";
    catalog.model_type = "llm";
    catalog.operation_template = OperationTemplate::Adm;
    catalog.operation_kind = OperationKind::Admission;
    catalog.constraints = {
        ConstraintKind::Ownership,
        ConstraintKind::GpuSharedResidency,
        ConstraintKind::ModelTypePool,
        ConstraintKind::HostMemAvailableFloor,
    };
    catalog.recovery = "native_subprocess_tree";
    catalog.material_profiles = {
        {"configuration_profile",
         "profile-free-residency-estimation-v1-text-only"},
        {"hardware_profile", "hatchery-gfx1151-shared-gtt-v1"},
        {"workload_profile", "hatchery-text-generation-campaign-v1"},
    };

    return LocalOverlaySelectorIdentity{
        digest('1'), std::move(catalog), "model/alpha", digest('2'),
        digest('3'), digest('4'),        digest('5'),   digest('6'),
        digest('7'), digest('8'),        digest('9'),   digest('a'),
    };
}

ProfilingInputEnvelopeDraft profiling_draft() {
    ProfilingInputEnvelopeDraft draft;
    draft.schema = supported_profiling_input_schema;
    draft.deployment_id = digest('b');
    draft.sequence = 1;
    draft.profiling_transaction_id = "profiling/1";
    draft.selector = selector();
    draft.generations = OverlaySourceGenerations{1, 2, 3, 4, 5, 6, 7};
    DifferentialRetainedGttEvidenceDraft method_evidence;
    method_evidence.retained_gtt_claim =
        ClaimAmount{"gpu/gtt", ClaimUnit::Bytes, 4096};
    method_evidence.calibration_evidence_sha256 = digest('c');
    method_evidence.transient_envelope_sha256 = digest('d');
    method_evidence.owner_projection_coverage =
        ProfilingOwnerCoverage::Incomplete;
    draft.method_evidence = std::move(method_evidence);
    draft.completion.manifest_claims = claims(6144);
    draft.completion.ownership_recovery_evidence_sha256 = digest('e');
    draft.completion.action_lease_closure_sha256 = digest('f');
    draft.observation_contract_sha256 = digest('f');
    draft.predictor_contract_sha256 = digest('0');
    draft.observed_at = "2026-08-23T10:00:00Z";
    draft.fresh_until = "2026-08-23T10:05:00Z";
    draft.max_clock_skew_milliseconds = 1000;
    return draft;
}

ProfilingPhaseAttestationDraft phase_draft(
    ProfilingPhase phase, std::string selector_sha256) {
    ProfilingPhaseAttestationDraft draft;
    draft.schema = supported_local_overlay_schema;
    draft.phase = phase;
    draft.deployment_id = digest('b');
    draft.profiling_transaction_id = "profiling/1";
    draft.selector_sha256 = std::move(selector_sha256);
    draft.provider_id = "provider/system-residency";
    draft.provider_revision_sha256 = digest('1');
    draft.provenance_sha256 = digest('2');
    draft.observation_contract_sha256 = digest('f');
    draft.predictor_contract_sha256 = digest('0');
    draft.generations = OverlaySourceGenerations{1, 2, 3, 4, 5, 6, 7};
    draft.observation_generation =
        phase == ProfilingPhase::Baseline
            ? 1
            : phase == ProfilingPhase::Workload ? 2 : 3;
    draft.observed_at =
        phase == ProfilingPhase::Baseline
            ? "2026-08-23T10:00:00Z"
            : phase == ProfilingPhase::Workload
                ? "2026-08-23T10:01:00Z"
                : "2026-08-23T10:02:00Z";
    draft.fresh_until = "2026-08-23T10:05:00Z";
    draft.source_skew_milliseconds = 10;
    draft.max_source_skew_milliseconds = 1000;
    draft.health = ProfilingObservationHealth::Valid;
    draft.owner_coverage = ProfilingOwnerCoverage::Complete;
    draft.observed_claims = claims(4096);
    draft.attributed_claims = claims(4096);
    draft.external_change_claims = claims(0);
    draft.unattributed_claims = claims(0);
    draft.uncertainty_claims = claims(512);
    draft.safety_margin_claims = claims(256);
    draft.lifecycle_state =
        phase == ProfilingPhase::Baseline
            ? ProfilingLifecycleState::BaselineQuiescent
            : phase == ProfilingPhase::Workload
                ? ProfilingLifecycleState::WorkloadComplete
                : ProfilingLifecycleState::ReleaseVerified;
    return draft;
}

LocalOverlayMethodIdentity method() {
    return LocalOverlayMethodIdentity{
        "method/exact-profile",
        digest('1'),
        LocalOverlayMethodScope::DeploymentExact,
        OperationKind::Admission,
        std::nullopt,
        digest('2'),
    };
}

LocalOverlayObjectDraft
overlay_draft(const ParsedProfilingInputEnvelope &profile) {
    LocalOverlayObjectDraft draft;
    draft.schema = supported_local_overlay_schema;
    draft.deployment_id = std::string(profile.deployment_id());
    draft.sequence = profile.sequence();
    draft.profiling_input_sha256 = std::string(profile.checksum_sha256());
    draft.selector = profile.selector();
    draft.method = method();
    draft.bound_claims = claims(4096);
    draft.uncertainty_claims = claims(512);
    draft.safety_margin_claims = claims(256);
    draft.confidence_basis_points = 9900;
    draft.qualified_at = "2026-08-23T10:01:00Z";
    draft.expires_at = "2026-09-23T10:01:00Z";
    draft.status = LocalOverlayObjectStatus::Qualified;
    draft.decision_trace_sha256 = digest('3');
    return draft;
}

OverlayActivationRootDraft root_draft(const ParsedLocalOverlayObject &overlay) {
    OverlayActivationRootDraft draft;
    draft.schema = supported_local_overlay_schema;
    draft.deployment_id = std::string(overlay.deployment_id());
    draft.generation = 1;
    draft.transition = OverlayRootTransition::Qualification;
    draft.authority_status = OverlayAuthorityStatus::Active;
    draft.selected_overlay_sha256 = std::string(overlay.checksum_sha256());
    draft.selected_overlay_sequence = overlay.sequence();
    draft.sequence_high_water = overlay.sequence();
    draft.selected_selector_sha256 = std::string(overlay.selector_sha256());
    draft.selected_method_sha256 = std::string(overlay.method_sha256());
    draft.decision_trace_sha256 = digest('4');
    draft.activated_at = "2026-08-23T10:02:00Z";
    draft.expires_at = std::string(overlay.expires_at());
    return draft;
}

template <typename Result>
void require_rejected(const Result &result, OverlayContractStatus status,
                      std::string_view message) {
    require(!result.accepted(), message);
    require(!result.candidate.has_value(),
            "rejected codec result returned a candidate");
    require(result.status == status,
            "codec rejection returned the wrong status");
    require(result.diagnostic.size() <= max_local_overlay_diagnostic_bytes,
            "codec diagnostic exceeded its bound");
}

template <typename Result>
void require_rejected(const Result &result, std::string_view message) {
    require(!result.accepted(), message);
    require(!result.candidate.has_value(),
            "rejected codec result returned a candidate");
    require(result.diagnostic.size() <= max_local_overlay_diagnostic_bytes,
            "codec diagnostic exceeded its bound");
}

std::uint64_t amount(const std::vector<ClaimFamilyClosure> &closure,
                     ClaimFamily family, std::string_view constraint_id) {
    for (const auto &family_claims : closure) {
        if (family_claims.family != family) {
            continue;
        }
        for (const auto &entry : family_claims.entries) {
            if (entry.constraint_id == constraint_id) {
                return entry.amount;
            }
        }
    }
    return 0;
}

std::string with_unknown_root_field(std::string bytes) {
    const auto end = bytes.rfind('}');
    require(end != std::string::npos, "canonical fixture has no root object");
    bytes.insert(end, ",\"future\":true");
    return bytes;
}

std::string tamper_once(std::string bytes, std::string_view needle,
                        std::string_view replacement) {
    const auto offset = bytes.find(needle);
    require(offset != std::string::npos, "tamper fixture field is absent");
    bytes.replace(offset, needle.size(), replacement);
    return bytes;
}

std::set<std::string> object_keys(const json &value) {
    require(value.is_object(), "schema compatibility value is not an object");
    std::set<std::string> result;
    for (auto member = value.begin(); member != value.end(); ++member) {
        result.insert(member.key());
    }
    return result;
}

std::set<std::string> string_set(const json &value) {
    require(value.is_array(), "schema required set is not an array");
    std::set<std::string> result;
    for (const auto &member : value) {
        require(member.is_string(), "schema required member is not a string");
        result.insert(member.get<std::string>());
    }
    return result;
}

json load_json(const std::filesystem::path &path) {
    std::ifstream input(path);
    require(input.good(), "generated schema could not be opened");
    json result;
    input >> result;
    return result;
}

void require_document_matches_schema(const json &document,
                                     const json &schema,
                                     std::string_view label,
                                     bool optional_fields_allowed = false) {
    const auto document_fields = object_keys(document);
    require(document_fields == string_set(schema.at("required")),
            std::string(label) +
                " codec fields differ from generated required fields");
    const auto schema_fields = object_keys(schema.at("properties"));
    require(optional_fields_allowed
                ? std::includes(schema_fields.begin(), schema_fields.end(),
                                document_fields.begin(),
                                document_fields.end())
                : document_fields == schema_fields,
            std::string(label) +
                " codec fields differ from generated schema properties");
}

void require_generated_schema_compatibility(
    const std::filesystem::path &repository_root) {
    auto profile = seal_profiling_input(profiling_draft());
    require(profile.accepted(), "schema fixture profile was rejected");
    auto phase = seal_profiling_phase_attestation(
        phase_draft(ProfilingPhase::Workload,
                    std::string(profile.candidate->selector_sha256())));
    require(phase.accepted(), "schema fixture phase was rejected");
    auto overlay = seal_local_overlay(overlay_draft(*profile.candidate));
    require(overlay.accepted(), "schema fixture overlay was rejected");
    auto root = seal_overlay_activation_root(root_draft(*overlay.candidate));
    require(root.accepted(), "schema fixture root was rejected");

    const auto schema_directory =
        repository_root / "docs/api/schemas/residency";
    const auto profile_schema = load_json(
        schema_directory / "profiling_input_envelope.schema.json");
    const auto phase_schema = load_json(
        schema_directory / "profiling_phase_attestation.schema.json");
    const auto overlay_schema = load_json(
        schema_directory / "deployment_local_overlay_object.schema.json");
    const auto root_schema = load_json(
        schema_directory / "overlay_activation_root.schema.json");
    const auto profile_document =
        json::parse(profile.candidate->canonical_bytes());
    const auto phase_document =
        json::parse(phase.candidate->canonical_bytes());
    const auto overlay_document =
        json::parse(overlay.candidate->canonical_bytes());
    const auto root_document = json::parse(root.candidate->canonical_bytes());

    require_document_matches_schema(profile_document, profile_schema,
                                    "profiling input");
    require_document_matches_schema(phase_document, phase_schema,
                                    "profiling phase attestation");
    require_document_matches_schema(overlay_document, overlay_schema,
                                    "local overlay");
    require_document_matches_schema(root_document, root_schema,
                                    "activation root");
    require_document_matches_schema(
        profile_document.at("selector"),
        profile_schema.at("$defs").at("selector_identity"),
        "profiling selector");
    require_document_matches_schema(
        profile_document.at("generations"),
        profile_schema.at("$defs").at("source_generations"),
        "profiling source generations");
    require_document_matches_schema(
        phase_document.at("generations"),
        phase_schema.at("$defs").at("source_generations"),
        "profiling phase source generations");
    require_document_matches_schema(
        overlay_document.at("selector"),
        overlay_schema.at("$defs").at("selector_identity"),
        "overlay selector");
    require_document_matches_schema(
        overlay_document.at("method"),
        overlay_schema.at("$defs").at("method_identity"),
        "overlay method", true);
}

void emit_schema_validation_corpus() {
    auto profile = seal_profiling_input(profiling_draft());
    require(profile.accepted(), "schema corpus profile was rejected");
    auto phase = seal_profiling_phase_attestation(
        phase_draft(ProfilingPhase::Workload,
                    std::string(profile.candidate->selector_sha256())));
    require(phase.accepted(), "schema corpus phase was rejected");

    auto maximum_confidence_draft = overlay_draft(*profile.candidate);
    maximum_confidence_draft.confidence_basis_points = 10000;
    auto overlay = seal_local_overlay(std::move(maximum_confidence_draft));
    require(overlay.accepted(), "maximum overlay confidence was rejected");

    auto excessive_confidence_draft = overlay_draft(*profile.candidate);
    excessive_confidence_draft.confidence_basis_points = 10001;
    require_rejected(
        seal_local_overlay(std::move(excessive_confidence_draft)),
        OverlayContractStatus::InvalidValue,
        "overlay accepted confidence above 10000 basis points");

    auto root = seal_overlay_activation_root(root_draft(*overlay.candidate));
    require(root.accepted(), "schema corpus activation root was rejected");

    const json corpus = {
        {"accepted_documents",
         {{"deployment_local_overlay_object",
           overlay.candidate->canonical_bytes()},
          {"overlay_activation_root", root.candidate->canonical_bytes()},
          {"profiling_phase_attestation",
           phase.candidate->canonical_bytes()},
          {"profiling_input_envelope",
           profile.candidate->canonical_bytes()}}},
        {"confidence_basis_points_boundary",
         {{"maximum_accepted", 10000}, {"first_rejected", 10001}}},
    };
    std::cout << corpus.dump() << '\n';
}

std::string without_positive_safety_margin(std::string bytes) {
    const auto field = bytes.find("\"safety_margin_claims\":[");
    require(field != std::string::npos,
            "canonical overlay has no safety-margin field");
    const std::string bounded =
        "{\"completeness\":\"bounded\",\"entries\":[{\"amount\":256,"
        "\"constraint_id\":\"gpu/gtt\",\"unit\":\"bytes\"}],"
        "\"family\":\"consumable_capacity\"}";
    const std::string known_zero =
        "{\"completeness\":\"known_zero\",\"entries\":[],"
        "\"family\":\"consumable_capacity\"}";
    const auto margin = bytes.find(bounded, field);
    require(margin != std::string::npos,
            "canonical overlay has no positive safety-margin entry");
    bytes.replace(margin, bounded.size(), known_zero);
    return bytes;
}

void require_public_shape() {
    static_assert(supported_profiling_input_schema.major == 2 &&
                  supported_profiling_input_schema.minor == 0);
    using ProfilingMethodEvidenceDraft =
        decltype(ProfilingInputEnvelopeDraft{}.method_evidence);
    static_assert(std::variant_size_v<ProfilingMethodEvidenceDraft> == 2);
    static_assert(std::is_same_v<
                  std::remove_reference_t<decltype(
                      ProfilingInputEnvelopeDraft{}.completion)>,
                  ProfilingCompletionDraft>);
    static_assert(
        std::is_assignable_v<ProfilingMethodEvidenceDraft &,
                             MutationCompleteIntervalEvidenceDraft>);
    static_assert(
        std::is_assignable_v<ProfilingMethodEvidenceDraft &,
                             DifferentialRetainedGttEvidenceDraft>);
    static_assert(
        !std::is_default_constructible_v<ParsedProfilingInputEnvelope>);
    static_assert(
        !std::is_default_constructible_v<ParsedProfilingPhaseAttestation>);
    static_assert(!std::is_default_constructible_v<ParsedLocalOverlayObject>);
    static_assert(
        !std::is_default_constructible_v<ParsedOverlayActivationRoot>);
    static_assert(std::is_copy_constructible_v<ParsedProfilingInputEnvelope>);
    static_assert(
        std::is_copy_constructible_v<ParsedProfilingPhaseAttestation>);
    static_assert(std::is_copy_constructible_v<ParsedLocalOverlayObject>);
    static_assert(std::is_copy_constructible_v<ParsedOverlayActivationRoot>);
}

void require_phase_attestation_codec() {
    auto profile = seal_profiling_input(profiling_draft());
    require(profile.accepted(), "phase fixture profile was rejected");
    auto sealed = seal_profiling_phase_attestation(phase_draft(
        ProfilingPhase::Workload,
        std::string(profile.candidate->selector_sha256())));
    require(sealed.accepted(), sealed.diagnostic);
    require(sealed.candidate->phase() == ProfilingPhase::Workload &&
                sealed.candidate->lifecycle_state() ==
                    ProfilingLifecycleState::WorkloadComplete &&
                sealed.candidate->selector_sha256() ==
                    profile.candidate->selector_sha256() &&
                sealed.candidate->provider_id() ==
                    "provider/system-residency" &&
                sealed.candidate->provenance_sha256() == digest('2') &&
                sealed.candidate->observation_generation() == 2 &&
                amount(sealed.candidate->uncertainty_claims(),
                       ClaimFamily::ConsumableCapacity, "gpu/gtt") == 512 &&
                amount(sealed.candidate->safety_margin_claims(),
                       ClaimFamily::ConsumableCapacity, "gpu/gtt") == 256,
            "phase attestation lost serialized evidence");

    const auto canonical = std::string(sealed.candidate->canonical_bytes());
    auto reparsed = parse_profiling_phase_attestation(canonical);
    require(reparsed.accepted(), reparsed.diagnostic);
    require(reparsed.candidate->canonical_bytes() == canonical &&
                reparsed.candidate->checksum_sha256() ==
                    sealed.candidate->checksum_sha256(),
            "phase attestation round-trip changed canonical identity");
    require_rejected(
        parse_profiling_phase_attestation(with_unknown_root_field(canonical)),
        OverlayContractStatus::UnknownField,
        "phase attestation accepted an unknown field");

    auto unhealthy = phase_draft(
        ProfilingPhase::Workload,
        std::string(profile.candidate->selector_sha256()));
    unhealthy.health = ProfilingObservationHealth::Unhealthy;
    require_rejected(seal_profiling_phase_attestation(std::move(unhealthy)),
                     OverlayContractStatus::InvalidValue,
                     "phase attestation accepted unhealthy evidence");

    auto contaminated = phase_draft(
        ProfilingPhase::Workload,
        std::string(profile.candidate->selector_sha256()));
    contaminated.external_change_claims = claims(1);
    require_rejected(
        seal_profiling_phase_attestation(std::move(contaminated)),
        OverlayContractStatus::InvalidClaimClosure,
        "phase attestation accepted external-demand contamination");

    auto incomplete_owner = phase_draft(
        ProfilingPhase::Workload,
        std::string(profile.candidate->selector_sha256()));
    incomplete_owner.owner_coverage = ProfilingOwnerCoverage::Incomplete;
    require_rejected(
        seal_profiling_phase_attestation(std::move(incomplete_owner)),
        OverlayContractStatus::IncompleteIdentity,
        "phase attestation accepted incomplete owner coverage");

    auto wrong_lifecycle = phase_draft(
        ProfilingPhase::Workload,
        std::string(profile.candidate->selector_sha256()));
    wrong_lifecycle.lifecycle_state = ProfilingLifecycleState::ReleaseVerified;
    require_rejected(
        seal_profiling_phase_attestation(std::move(wrong_lifecycle)),
        OverlayContractStatus::InvalidValue,
        "phase attestation accepted a mismatched lifecycle state");

    auto excessive_skew = phase_draft(
        ProfilingPhase::Workload,
        std::string(profile.candidate->selector_sha256()));
    excessive_skew.source_skew_milliseconds = 1001;
    require_rejected(
        seal_profiling_phase_attestation(std::move(excessive_skew)),
        OverlayContractStatus::InvalidValue,
        "phase attestation accepted excessive source skew");

    auto unattributed = phase_draft(
        ProfilingPhase::Workload,
        std::string(profile.candidate->selector_sha256()));
    unattributed.unattributed_claims = claims(1);
    require_rejected(
        seal_profiling_phase_attestation(std::move(unattributed)),
        OverlayContractStatus::InvalidClaimClosure,
        "phase attestation accepted unattributed demand");

    auto no_margin = phase_draft(
        ProfilingPhase::Workload,
        std::string(profile.candidate->selector_sha256()));
    no_margin.safety_margin_claims = claims(0);
    require_rejected(
        seal_profiling_phase_attestation(std::move(no_margin)),
        OverlayContractStatus::InvalidClaimClosure,
        "phase attestation accepted a zero safety margin");
}

void require_profiling_input_codec() {
    auto sealed = seal_profiling_input(profiling_draft());
    require(sealed.accepted(), sealed.diagnostic);
    require(sealed.candidate->sequence() == 1 &&
                sealed.candidate->deployment_id() == digest('b') &&
                sealed.candidate->checksum_sha256().size() == 64 &&
                sealed.candidate->selector_sha256().size() == 64,
            "sealed profiling input lost its closed identity");

    const auto canonical = std::string(sealed.candidate->canonical_bytes());
    const auto document = json::parse(canonical);
    const auto &method_evidence = document.at("method_evidence");
    require(object_keys(method_evidence) ==
                std::set<std::string>{"calibration_evidence_sha256",
                                      "covered_effect", "method",
                                      "owner_projection_coverage",
                                      "retained_gtt_claim",
                                      "transient_envelope_sha256"} &&
                method_evidence.at("method") ==
                    "differential_retained_gtt" &&
                method_evidence.at("covered_effect") == "retained_gtt" &&
                method_evidence.at("owner_projection_coverage") ==
                    "incomplete",
            "differential profiling input lost its method, covered effect, "
            "or incomplete owner-projection state");
    require(object_keys(document.at("completion")) ==
                std::set<std::string>{"action_lease_closure_sha256",
                                      "manifest_claims",
                                      "ownership_recovery_evidence_sha256"},
            "profiling completion serialized an open or incomplete shape");
    require(document.at("confidence") == "calibrated_instance",
            "complete profiling evidence did not derive calibrated-instance "
            "confidence");
    require(!document.contains("attribution_complete") &&
                !document.contains("external_demand_absent") &&
                !method_evidence.contains("owner_projection_claim") &&
                canonical.find("\"selector_sha256\"") == std::string::npos,
            "profiling input serialized a legacy proof flag, a fabricated "
            "owner projection, or a derived selector digest");
    auto reparsed = parse_profiling_input(canonical);
    require(reparsed.accepted(), reparsed.diagnostic);
    require(reparsed.candidate->canonical_bytes() == canonical &&
                reparsed.candidate->checksum_sha256() ==
                    sealed.candidate->checksum_sha256() &&
                reparsed.candidate->selector_sha256() ==
                    sealed.candidate->selector_sha256(),
            "profiling input round-trip changed canonical identity");

    require_rejected(parse_profiling_input(" " + canonical),
                     OverlayContractStatus::NonCanonical,
                     "profiling input accepted noncanonical whitespace");
    require_rejected(parse_profiling_input("{"),
                     OverlayContractStatus::MalformedJson,
                     "profiling input accepted malformed JSON");
    require_rejected(parse_profiling_input(with_unknown_root_field(canonical)),
                     OverlayContractStatus::UnknownField,
                     "profiling input accepted an unknown field");
    require_rejected(
        parse_profiling_input(tamper_once(
            canonical,
            "\"calibration_evidence_sha256\":\"" + digest('c') + "\"",
            "\"baseline_observation_sha256\":\"" + digest('7') +
                "\",\"calibration_evidence_sha256\":\"" + digest('c') +
                "\"")),
        OverlayContractStatus::UnknownField,
        "differential profiling input accepted a mixed method arm");
    require_rejected(
        parse_profiling_input(tamper_once(
            canonical, "\"method\":\"differential_retained_gtt\"",
            "\"method\":\"future_method\"")),
        OverlayContractStatus::UnknownValue,
        "profiling input accepted an unknown evidence method");
    require_rejected(parse_profiling_input(tamper_once(
                         canonical, "\"deployment_id\":\"" + digest('b') + "\"",
                         "\"deployment_id\":\"" + digest('c') + "\"")),
                     OverlayContractStatus::DigestMismatch,
                     "profiling input accepted a stale checksum");
    require_rejected(parse_profiling_input(tamper_once(
                         canonical, "\"schema\":{",
                         "\"schema\":{\"major\":1,\"minor\":0},\"schema\":{")),
                     OverlayContractStatus::Duplicate,
                     "profiling input accepted a duplicate key");

    auto v1 = profiling_draft();
    v1.schema = supported_local_overlay_schema;
    require_rejected(seal_profiling_input(std::move(v1)),
                     OverlayContractStatus::UnsupportedSchema,
                     "profiling input accepted the legacy shared schema");

    auto missing_transient = profiling_draft();
    std::get<DifferentialRetainedGttEvidenceDraft>(
        missing_transient.method_evidence)
        .transient_envelope_sha256.clear();
    require_rejected(seal_profiling_input(std::move(missing_transient)),
                     "profiling input accepted missing transient evidence");

    auto missing_ownership = profiling_draft();
    missing_ownership.completion.ownership_recovery_evidence_sha256.clear();
    require_rejected(seal_profiling_input(std::move(missing_ownership)),
                     "profiling input accepted missing ownership and recovery "
                     "evidence");

    auto missing_action_lease = profiling_draft();
    missing_action_lease.completion.action_lease_closure_sha256.clear();
    require_rejected(seal_profiling_input(std::move(missing_action_lease)),
                     "profiling input accepted missing action-lease closure");

    auto unknown_claims = profiling_draft();
    unknown_claims.completion.manifest_claims.front().completeness =
        ClaimCompleteness::Unknown;
    unknown_claims.completion.manifest_claims.front().entries.clear();
    require_rejected(seal_profiling_input(std::move(unknown_claims)),
                     "profiling input accepted an unknown claim family");

    auto incomplete_host_floor = profiling_draft();
    for (auto &family : incomplete_host_floor.completion.manifest_claims) {
        if (family.family == ClaimFamily::SafetyFloor) {
            family.completeness = ClaimCompleteness::Unknown;
            family.entries.clear();
        }
    }
    require_rejected(seal_profiling_input(std::move(incomplete_host_floor)),
                     "profiling input accepted an incomplete host-memory "
                     "safety floor");

    auto incomplete_cardinality = profiling_draft();
    for (auto &family : incomplete_cardinality.completion.manifest_claims) {
        if (family.family == ClaimFamily::CardinalityPool) {
            family.completeness = ClaimCompleteness::Unknown;
            family.entries.clear();
        }
    }
    require_rejected(seal_profiling_input(std::move(incomplete_cardinality)),
                     "profiling input accepted an incomplete cardinality "
                     "closure");

    auto insufficient_manifest = profiling_draft();
    insufficient_manifest.completion.manifest_claims = claims(2048);
    require_rejected(seal_profiling_input(std::move(insufficient_manifest)),
                     "profiling input accepted a manifest GTT bound below its "
                     "retained-GTT evidence");
}

void require_overlay_object_codec() {
    auto profile = seal_profiling_input(profiling_draft());
    require(profile.accepted(), "overlay fixture profile was rejected");

    auto sealed = seal_local_overlay(overlay_draft(*profile.candidate));
    require(sealed.accepted(), sealed.diagnostic);
    require(amount(sealed.candidate->conservative_claims(),
                   ClaimFamily::ConsumableCapacity, "gpu/gtt") == 4864,
            "overlay did not use checked claim addition for its conservative "
            "bound");
    require(amount(sealed.candidate->bound_claims(),
                   ClaimFamily::ConsumableCapacity, "gpu/gtt") == 4096 &&
                amount(sealed.candidate->uncertainty_claims(),
                       ClaimFamily::ConsumableCapacity, "gpu/gtt") == 512 &&
                amount(sealed.candidate->safety_margin_claims(),
                       ClaimFamily::ConsumableCapacity, "gpu/gtt") == 256,
            "overlay did not retain its reconstructible bound components");
    require(sealed.candidate->status() == LocalOverlayObjectStatus::Qualified &&
                sealed.candidate->selector_sha256() ==
                    profile.candidate->selector_sha256() &&
                sealed.candidate->method_sha256().size() == 64,
            "overlay lost status, selector, or method identity");

    const auto canonical = std::string(sealed.candidate->canonical_bytes());
    require(canonical.find("\"selector_sha256\"") == std::string::npos &&
                canonical.find("\"method_sha256\"") == std::string::npos &&
                canonical.find("\"conservative_claims\"") == std::string::npos,
            "overlay serialized derived fields outside the public schema");
    auto reparsed = parse_local_overlay(canonical);
    require(reparsed.accepted(), reparsed.diagnostic);
    require(reparsed.candidate->canonical_bytes() == canonical &&
                amount(reparsed.candidate->conservative_claims(),
                       ClaimFamily::ConsumableCapacity, "gpu/gtt") == 4864,
            "overlay round-trip changed canonical content");

    require_rejected(parse_local_overlay("\n" + canonical),
                     OverlayContractStatus::NonCanonical,
                     "overlay accepted noncanonical whitespace");
    require_rejected(parse_local_overlay(with_unknown_root_field(canonical)),
                     OverlayContractStatus::UnknownField,
                     "overlay accepted an unknown field");
    require_rejected(parse_local_overlay(tamper_once(
                         canonical, "\"confidence_basis_points\":9900",
                         "\"confidence_basis_points\":9800")),
                     OverlayContractStatus::DigestMismatch,
                     "overlay accepted a stale checksum");
    require_rejected(
        parse_local_overlay(without_positive_safety_margin(canonical)),
        OverlayContractStatus::InvalidClaimClosure,
        "overlay parser accepted a zero safety margin");

    auto wrong_operation = overlay_draft(*profile.candidate);
    wrong_operation.method.operation_kind = OperationKind::ExplicitUnload;
    require_rejected(seal_local_overlay(std::move(wrong_operation)),
                     OverlayContractStatus::ReferenceMismatch,
                     "overlay accepted a method for another operation");

    auto invalid_expiry = overlay_draft(*profile.candidate);
    invalid_expiry.expires_at = invalid_expiry.qualified_at;
    require_rejected(seal_local_overlay(std::move(invalid_expiry)),
                     OverlayContractStatus::InvalidValue,
                     "overlay accepted a non-advancing expiry");

    auto incomplete_method = overlay_draft(*profile.candidate);
    incomplete_method.method.scope =
        LocalOverlayMethodScope::ArchitecturePredicate;
    require_rejected(
        seal_local_overlay(std::move(incomplete_method)),
        OverlayContractStatus::IncompleteIdentity,
        "overlay accepted an architecture method without its predicate");

    auto zero_slack = overlay_draft(*profile.candidate);
    zero_slack.safety_margin_claims = claims(0);
    require_rejected(seal_local_overlay(std::move(zero_slack)),
                     OverlayContractStatus::InvalidClaimClosure,
                     "overlay seal accepted a zero safety margin");

    auto unreclosable_conservative = overlay_draft(*profile.candidate);
    unreclosable_conservative.bound_claims = compatibility_claims(true);
    unreclosable_conservative.uncertainty_claims =
        compatibility_claims(false);
    unreclosable_conservative.safety_margin_claims =
        compatibility_claims(true);
    require_rejected(
        seal_local_overlay(std::move(unreclosable_conservative)),
        OverlayContractStatus::InvalidClaimClosure,
        "overlay sealed a conservative compatibility claim that cannot be "
        "reclosed");

    auto overflowing_claims = overlay_draft(*profile.candidate);
    overflowing_claims.bound_claims =
        claims(std::numeric_limits<std::uint64_t>::max());
    overflowing_claims.uncertainty_claims = claims(1);
    overflowing_claims.safety_margin_claims = claims(1);
    require_rejected(seal_local_overlay(std::move(overflowing_claims)),
                     OverlayContractStatus::ClaimArithmeticOverflow,
                     "overlay accepted overflowing conservative claims");
}

void require_activation_root_codec() {
    auto profile = seal_profiling_input(profiling_draft());
    require(profile.accepted(), "root fixture profile was rejected");
    auto overlay = seal_local_overlay(overlay_draft(*profile.candidate));
    require(overlay.accepted(), "root fixture overlay was rejected");

    auto sealed = seal_overlay_activation_root(root_draft(*overlay.candidate));
    require(sealed.accepted(), sealed.diagnostic);
    require(sealed.candidate->generation() == 1 &&
                sealed.candidate->selected_overlay_sequence() == 1 &&
                sealed.candidate->sequence_high_water() == 1 &&
                sealed.candidate->authority_status() ==
                    OverlayAuthorityStatus::Active &&
                sealed.candidate->selected_overlay_sha256() ==
                    overlay.candidate->checksum_sha256(),
            "activation root lost generation, sequence, status, or content "
            "address");

    const auto canonical = std::string(sealed.candidate->canonical_bytes());
    auto reparsed = parse_overlay_activation_root(canonical);
    require(reparsed.accepted(), reparsed.diagnostic);
    require(reparsed.candidate->canonical_bytes() == canonical &&
                reparsed.candidate->checksum_sha256() ==
                    sealed.candidate->checksum_sha256(),
            "activation-root round-trip changed canonical identity");

    require_rejected(parse_overlay_activation_root(" " + canonical),
                     OverlayContractStatus::NonCanonical,
                     "activation root accepted noncanonical whitespace");
    require_rejected(
        parse_overlay_activation_root(with_unknown_root_field(canonical)),
        OverlayContractStatus::UnknownField,
        "activation root accepted an unknown field");
    require_rejected(
        parse_overlay_activation_root(
            tamper_once(canonical, "\"generation\":1", "\"generation\":2")),
        OverlayContractStatus::InvalidSequence,
        "activation root accepted a generation without a predecessor");

    auto skipped_genesis_sequence = root_draft(*overlay.candidate);
    skipped_genesis_sequence.selected_overlay_sequence = 2;
    skipped_genesis_sequence.sequence_high_water = 2;
    require_rejected(
        seal_overlay_activation_root(std::move(skipped_genesis_sequence)),
        OverlayContractStatus::InvalidSequence,
        "activation-root genesis accepted a skipped overlay sequence");

    require_rejected(parse_overlay_activation_root(tamper_once(
                         canonical, "\"authority_status\":\"active\",", "")),
                     OverlayContractStatus::InvalidValue,
                     "activation root accepted a missing required field");
    require_rejected(
        parse_overlay_activation_root(tamper_once(
            canonical,
            "\"selected_overlay_sha256\":\"" +
                std::string(overlay.candidate->checksum_sha256()) + "\"",
            "\"selected_overlay_sha256\":\"" + digest('7') + "\"")),
        OverlayContractStatus::DigestMismatch,
        "activation root accepted a stale checksum");

    auto rollback = root_draft(*overlay.candidate);
    rollback.generation = 2;
    rollback.previous_root_sha256 =
        std::string(sealed.candidate->checksum_sha256());
    rollback.transition = OverlayRootTransition::Rollback;
    rollback.sequence_high_water = 2;
    rollback.selected_overlay_sequence = 1;
    auto rolled_back = seal_overlay_activation_root(std::move(rollback));
    require(rolled_back.accepted(), rolled_back.diagnostic);
    require(rolled_back.candidate->generation() == 2 &&
                rolled_back.candidate->selected_overlay_sequence() == 1 &&
                rolled_back.candidate->sequence_high_water() == 2,
            "rollback rewrote generation or sequence high-water");

    auto invalid_rollback = root_draft(*overlay.candidate);
    invalid_rollback.generation = 2;
    invalid_rollback.previous_root_sha256 =
        std::string(sealed.candidate->checksum_sha256());
    invalid_rollback.transition = OverlayRootTransition::Rollback;
    invalid_rollback.sequence_high_water = 1;
    require_rejected(seal_overlay_activation_root(std::move(invalid_rollback)),
                     OverlayContractStatus::InvalidSequence,
                     "rollback accepted the current high-water sequence");
}

} // namespace

int main(int argc, char **argv) {
    if (argc == 2 &&
        std::string_view(argv[1]) == "--emit-schema-validation-corpus") {
        emit_schema_validation_corpus();
        return 0;
    }
    require(argc <= 2, "unexpected local-overlay test argument");
    const auto repository_root =
        argc == 2 ? std::filesystem::path(argv[1])
                  : std::filesystem::current_path();
    require_public_shape();
    require_phase_attestation_codec();
    require_profiling_input_codec();
    require_overlay_object_codec();
    require_activation_root_codec();
    require_generated_schema_compatibility(repository_root);
    std::cout << "residency local-overlay contract passed\n";
    return 0;
}
