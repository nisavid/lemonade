#include "lemon/residency/profiling_differential_input.h"

#include "profiling_common.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace lemon::residency {
namespace {

using profiling_internal::append_string;
using profiling_internal::append_u64;
using profiling_internal::digest_is_valid;
using profiling_internal::sha256_hex;

bool identifier_is_valid(std::string_view value) noexcept {
    return !value.empty() &&
           value.size() <= max_local_overlay_identifier_bytes &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return character >= 0x21 && character <= 0x7e;
           });
}

bool bindings_equal_except_background(
    const ProfilingNoiseBindings &left,
    const ProfilingNoiseBindings &right) noexcept {
    return left.deployment_id == right.deployment_id &&
           left.deployment_epoch_sha256 ==
               right.deployment_epoch_sha256 &&
           left.boot_id_sha256 == right.boot_id_sha256 &&
           left.device_identity_sha256 ==
               right.device_identity_sha256 &&
           left.topology_sha256 == right.topology_sha256 &&
           left.kernel_identity_sha256 ==
               right.kernel_identity_sha256 &&
           left.driver_identity_sha256 ==
               right.driver_identity_sha256 &&
           left.counter_source_id == right.counter_source_id &&
           left.counter_source_revision_sha256 ==
               right.counter_source_revision_sha256 &&
           left.campaign_contract_sha256 ==
               right.campaign_contract_sha256 &&
           left.procedure_revision_sha256 ==
               right.procedure_revision_sha256;
}

bool bindings_equal(const ProfilingNoiseBindings &left,
                    const ProfilingNoiseBindings &right) noexcept {
    return bindings_equal_except_background(left, right) &&
           left.background_inventory_sha256 ==
               right.background_inventory_sha256;
}

bool generations_are_valid(
    const OverlaySourceGenerations &generations) noexcept {
    return generations.model != 0 && generations.backend != 0 &&
           generations.device != 0 && generations.topology != 0 &&
           generations.driver != 0 && generations.configuration != 0 &&
           generations.workload != 0;
}

bool transaction_identity_is_valid(
    const ProfilingTransactionContext &context) noexcept {
    return digest_is_valid(context.deployment_id) &&
           context.sequence != 0 &&
           identifier_is_valid(context.profiling_transaction_id) &&
           digest_is_valid(context.selector_sha256) &&
           generations_are_valid(context.generations) &&
           digest_is_valid(context.observation_contract_sha256) &&
           digest_is_valid(context.predictor_contract_sha256) &&
           digest_is_valid(
               context.ownership_recovery_evidence_sha256) &&
           digest_is_valid(context.action_lease_closure_sha256);
}

bool noise_result_is_valid(
    const ParsedProfilingNoiseResult &noise) {
    if (!digest_is_valid(noise.bindings_sha256()) ||
        !digest_is_valid(noise.checksum_sha256()) ||
        noise.canonical_bytes().empty()) {
        return false;
    }
    auto parsed = parse_profiling_noise_result(noise.canonical_bytes());
    return parsed.accepted() &&
           parsed.result->bindings_sha256() ==
               noise.bindings_sha256() &&
           parsed.result->n_gtt_bytes() == noise.n_gtt_bytes() &&
           parsed.result->checksum_sha256() ==
               noise.checksum_sha256() &&
           bindings_equal(parsed.result->bindings(), noise.bindings());
}

bool accounting_sum_is_valid(
    std::uint64_t n_gtt_bytes,
    const ProfilingDifferentialAccountingPartition &accounting) noexcept {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (accounting.x_gtt_bytes > maximum - n_gtt_bytes) return false;
    const auto with_uncertainty = n_gtt_bytes + accounting.x_gtt_bytes;
    return accounting.m_gtt_bytes <= maximum - with_uncertainty;
}

bool input_identity_is_valid(
    const ProfilingDifferentialInputIdentity &identity,
    const ProfilingNoiseBindings &bindings) noexcept {
    const auto &transaction = identity.transaction;
    return transaction_identity_is_valid(transaction) &&
           transaction.deployment_id == bindings.deployment_id &&
           transaction.selector.device_identity_sha256 ==
               bindings.device_identity_sha256 &&
           transaction.selector.topology_sha256 ==
               bindings.topology_sha256 &&
           transaction.selector.driver_identity_sha256 ==
               bindings.driver_identity_sha256 &&
           digest_is_valid(identity.target_client_identity_sha256) &&
           digest_is_valid(
               identity.target_containment_identity_sha256) &&
           digest_is_valid(identity.counter_continuity_epoch_sha256) &&
           digest_is_valid(identity.safety_contract_sha256);
}

void append_selector(std::string &bytes,
                     const LocalOverlaySelectorIdentity &selector) {
    append_string(bytes, selector.catalog_sha256);
    const auto &catalog = selector.catalog_selector;
    append_string(bytes, catalog.source_support_baseline);
    append_string(bytes, catalog.base_variant);
    append_string(bytes, catalog.platform);
    append_string(bytes, catalog.backend_channel);
    append_string(bytes, catalog.model_type);
    append_u64(bytes,
               static_cast<std::uint64_t>(catalog.operation_template));
    append_u64(bytes,
               static_cast<std::uint64_t>(catalog.operation_kind));
    append_u64(bytes,
               static_cast<std::uint64_t>(catalog.constraints.size()));
    for (const auto constraint : catalog.constraints) {
        append_u64(bytes, static_cast<std::uint64_t>(constraint));
    }
    append_string(bytes, catalog.recovery);
    append_u64(bytes,
               static_cast<std::uint64_t>(
                   catalog.material_profiles.size()));
    for (const auto &[key, value] : catalog.material_profiles) {
        append_string(bytes, key);
        append_string(bytes, value);
    }
    append_string(bytes, selector.canonical_model_id);
    append_string(bytes, selector.model_artifact_sha256);
    append_string(bytes, selector.backend_build_sha256);
    append_string(bytes, selector.device_identity_sha256);
    append_string(bytes, selector.topology_sha256);
    append_string(bytes, selector.dependency_set_sha256);
    append_string(bytes, selector.driver_identity_sha256);
    append_string(bytes, selector.configuration_sha256);
    append_string(bytes, selector.workload_sha256);
    append_string(bytes, selector.operation_contract_sha256);
}

void append_transaction(std::string &bytes,
                        const ProfilingTransactionContext &context) {
    append_string(bytes, context.deployment_id);
    append_u64(bytes, context.sequence);
    append_string(bytes, context.profiling_transaction_id);
    append_selector(bytes, context.selector);
    append_string(bytes, context.selector_sha256);
    append_u64(bytes, context.generations.model);
    append_u64(bytes, context.generations.backend);
    append_u64(bytes, context.generations.device);
    append_u64(bytes, context.generations.topology);
    append_u64(bytes, context.generations.driver);
    append_u64(bytes, context.generations.configuration);
    append_u64(bytes, context.generations.workload);
    append_string(bytes, context.observation_contract_sha256);
    append_string(bytes, context.predictor_contract_sha256);
    append_string(bytes,
                  context.ownership_recovery_evidence_sha256);
    append_string(bytes, context.action_lease_closure_sha256);
}

std::optional<std::string> frozen_input_digest(
    const ParsedProfilingNoiseResult &noise,
    const ProfilingDifferentialInputDraft &draft) {
    std::string bytes =
        "lemonade/profiling-differential-input/v1";
    append_string(bytes, noise.canonical_bytes());
    append_transaction(bytes, draft.identity.transaction);
    append_string(bytes,
                  draft.identity.target_client_identity_sha256);
    append_string(
        bytes, draft.identity.target_containment_identity_sha256);
    append_string(bytes,
                  draft.identity.counter_continuity_epoch_sha256);
    append_string(bytes, draft.identity.safety_contract_sha256);
    append_string(bytes,
                  draft.identity.noise_trace_provenance_sha256);
    append_string(bytes, draft.revision.calibration_revision_sha256);
    append_string(bytes, draft.revision.attempt_receipt_sha256);
    append_u64(bytes,
               static_cast<std::uint64_t>(draft.revision.state));
    append_u64(bytes, draft.accounting.x_gtt_bytes);
    append_u64(bytes, draft.accounting.m_gtt_bytes);
    append_string(bytes, draft.accounting.x_gtt_evidence_sha256);
    append_string(bytes, draft.accounting.m_gtt_policy_sha256);
    append_string(bytes, draft.accounting.partition_contract_sha256);
    append_u64(bytes, draft.calibration_repetitions);
    append_u64(bytes, draft.validation_repetitions);
    return sha256_hex(bytes);
}

ProfilingDifferentialInputFreezeResult freeze_failure(
    ProfilingDifferentialInputFreezeStatus status,
    std::string diagnostic) {
    ProfilingDifferentialInputFreezeResult result;
    result.status = status;
    result.diagnostic = std::move(diagnostic);
    return result;
}

} // namespace

FrozenProfilingDifferentialInput::FrozenProfilingDifferentialInput(
    ParsedProfilingNoiseResult noise,
    ProfilingDifferentialInputDraft draft,
    std::string frozen_input_sha256)
    : noise_(std::move(noise)),
      draft_(std::move(draft)),
      frozen_input_sha256_(std::move(frozen_input_sha256)) {}

FrozenProfilingDifferentialInput::FrozenProfilingDifferentialInput(
    FrozenProfilingDifferentialInput &&) noexcept = default;

FrozenProfilingDifferentialInput &
FrozenProfilingDifferentialInput::operator=(
    FrozenProfilingDifferentialInput &&) noexcept = default;

const ParsedProfilingNoiseResult &
FrozenProfilingDifferentialInput::noise() const noexcept {
    return noise_;
}

const ProfilingDifferentialInputIdentity &
FrozenProfilingDifferentialInput::identity() const noexcept {
    return draft_.identity;
}

const ProfilingDifferentialRevisionBinding &
FrozenProfilingDifferentialInput::revision() const noexcept {
    return draft_.revision;
}

const ProfilingDifferentialAccountingPartition &
FrozenProfilingDifferentialInput::accounting() const noexcept {
    return draft_.accounting;
}

std::uint32_t
FrozenProfilingDifferentialInput::calibration_repetitions() const noexcept {
    return draft_.calibration_repetitions;
}

std::uint32_t
FrozenProfilingDifferentialInput::validation_repetitions() const noexcept {
    return draft_.validation_repetitions;
}

std::string_view
FrozenProfilingDifferentialInput::frozen_input_sha256() const noexcept {
    return frozen_input_sha256_;
}

bool FrozenProfilingDifferentialInput::revision_rejected() const noexcept {
    return revision_rejected_;
}

bool ProfilingDifferentialInputFreezeResult::accepted() const noexcept {
    return status == ProfilingDifferentialInputFreezeStatus::Accepted &&
           input.has_value();
}

bool ProfilingDifferentialRevalidationResult::accepted() const noexcept {
    return status == ProfilingDifferentialRevalidationStatus::Accepted;
}

ProfilingDifferentialInputFreezeResult
freeze_profiling_differential_input(
    const ParsedProfilingNoiseResult &noise,
    ProfilingDifferentialInputDraft draft) {
    try {
        if (!noise_result_is_valid(noise)) {
            return freeze_failure(
                ProfilingDifferentialInputFreezeStatus::
                    InvalidNoiseResult,
                "canonical no-target noise result is invalid");
        }

        auto canonical_selector =
            canonicalize_local_overlay_selector(
                draft.identity.transaction.selector);
        if (!canonical_selector.accepted() ||
            canonical_selector.selector_sha256 !=
                draft.identity.transaction.selector_sha256) {
            return freeze_failure(
                ProfilingDifferentialInputFreezeStatus::InvalidIdentity,
                "profiling selector identity or digest is invalid");
        }
        draft.identity.transaction.selector =
            std::move(*canonical_selector.selector);

        if (!digest_is_valid(
                draft.identity.noise_trace_provenance_sha256)) {
            return freeze_failure(
                ProfilingDifferentialInputFreezeStatus::
                    TraceProvenanceUnavailable,
                "no-target trace provenance binding is unavailable");
        }
        if (std::string_view(
                draft.identity.noise_trace_provenance_sha256) !=
            noise.trace_provenance_sha256()) {
            return freeze_failure(
                ProfilingDifferentialInputFreezeStatus::
                    TraceProvenanceMismatch,
                "no-target trace provenance does not match the "
                "immutable noise result");
        }
        if (draft.revision.state !=
            ProfilingDifferentialRevisionState::Fresh) {
            return freeze_failure(
                ProfilingDifferentialInputFreezeStatus::
                    RevisionAlreadyRejected,
                "calibration revision was already rejected");
        }
        if (!digest_is_valid(
                draft.revision.calibration_revision_sha256) ||
            !digest_is_valid(draft.revision.attempt_receipt_sha256)) {
            return freeze_failure(
                ProfilingDifferentialInputFreezeStatus::InvalidIdentity,
                "calibration revision binding is invalid");
        }
        if (draft.calibration_repetitions == 0 ||
            draft.validation_repetitions == 0) {
            return freeze_failure(
                ProfilingDifferentialInputFreezeStatus::
                    InvalidRepetitionCount,
                "calibration and validation counts must be positive");
        }
        if (!digest_is_valid(
                draft.accounting.x_gtt_evidence_sha256) ||
            !digest_is_valid(draft.accounting.m_gtt_policy_sha256) ||
            !digest_is_valid(
                draft.accounting.partition_contract_sha256) ||
            !accounting_sum_is_valid(noise.n_gtt_bytes(),
                                     draft.accounting)) {
            return freeze_failure(
                ProfilingDifferentialInputFreezeStatus::
                    InvalidAccountingPartition,
                "differential GTT accounting partition is invalid");
        }
        if (!input_identity_is_valid(draft.identity,
                                     noise.bindings())) {
            return freeze_failure(
                ProfilingDifferentialInputFreezeStatus::InvalidIdentity,
                "differential profiling identity is invalid");
        }

        const auto digest = frozen_input_digest(noise, draft);
        if (!digest.has_value()) {
            return freeze_failure(
                ProfilingDifferentialInputFreezeStatus::
                    DigestUnavailable,
                "frozen differential input digest is unavailable");
        }

        FrozenProfilingDifferentialInput frozen(
            noise, std::move(draft), *digest);
        ProfilingDifferentialInputFreezeResult result;
        result.status =
            ProfilingDifferentialInputFreezeStatus::Accepted;
        result.diagnostic =
            "differential profiling input frozen";
        result.input.emplace(std::move(frozen));
        return result;
    } catch (...) {
        return freeze_failure(
            ProfilingDifferentialInputFreezeStatus::
                EvidenceUnavailable,
            "differential profiling input freezing failed");
    }
}

ProfilingDifferentialRevalidationResult
revalidate_profiling_differential_input(
    FrozenProfilingDifferentialInput &input,
    const ProfilingDifferentialRevalidationObservation &observation) {
    auto reject =
        [&](ProfilingDifferentialRevalidationStatus status,
            std::string diagnostic) {
            input.revision_rejected_ = true;
            ProfilingDifferentialRevalidationResult result;
            result.status = status;
            result.diagnostic = std::move(diagnostic);
            return result;
        };

    try {
        if (input.revision_rejected_) {
            return reject(
                ProfilingDifferentialRevalidationStatus::
                    RevisionRejected,
                "calibration revision is terminally rejected");
        }
        if (observation.counter_reset_detected) {
            return reject(
                ProfilingDifferentialRevalidationStatus::CounterReset,
                "global GTT counter reset was detected");
        }
        if (observation.counter_discontinuity_detected) {
            return reject(
                ProfilingDifferentialRevalidationStatus::
                    CounterDiscontinuity,
                "global GTT counter discontinuity was detected");
        }
        if (!digest_is_valid(
                observation.observed_noise_result_checksum_sha256) ||
            observation.observed_noise_result_checksum_sha256 !=
                input.noise_.checksum_sha256()) {
            return reject(
                ProfilingDifferentialRevalidationStatus::
                    NoiseResultMismatch,
                "no-target noise result binding changed");
        }
        if (!digest_is_valid(
                observation
                    .observed_counter_continuity_epoch_sha256) ||
            observation.observed_counter_continuity_epoch_sha256 !=
                input.draft_.identity.counter_continuity_epoch_sha256) {
            return reject(
                ProfilingDifferentialRevalidationStatus::
                    CounterDiscontinuity,
                "global GTT counter continuity binding changed");
        }

        const auto &expected_bindings = input.noise_.bindings();
        if (!bindings_equal_except_background(
                observation.observed_bindings, expected_bindings)) {
            return reject(
                ProfilingDifferentialRevalidationStatus::
                    BindingMismatch,
                "boot-scoped no-target binding changed");
        }
        if (observation.observed_bindings
                    .background_inventory_sha256 !=
                expected_bindings.background_inventory_sha256 ||
            observation.unexpected_non_target_client_detected) {
            return reject(
                ProfilingDifferentialRevalidationStatus::
                    BackgroundDrift,
                "non-target GTT client inventory changed");
        }
        if (observation.observed_non_target_gtt_range_bytes >
            input.noise_.n_gtt_bytes()) {
            return reject(
                ProfilingDifferentialRevalidationStatus::
                    ExcessVariation,
                "non-target GTT variation exceeds the frozen bound");
        }
        if (observation.target_activity.has_value()) {
            const auto &target = *observation.target_activity;
            if (!digest_is_valid(target.client_identity_sha256) ||
                !digest_is_valid(
                    target.containment_identity_sha256) ||
                target.client_identity_sha256 !=
                    input.draft_.identity
                        .target_client_identity_sha256 ||
                target.containment_identity_sha256 !=
                    input.draft_.identity
                        .target_containment_identity_sha256) {
                return reject(
                    ProfilingDifferentialRevalidationStatus::
                        TargetMismatch,
                    "observed target activity is not the frozen "
                    "contained target");
            }
        }

        if (input.last_accepted_revalidation_at_.has_value() &&
            observation.checked_at <=
                *input.last_accepted_revalidation_at_) {
            return reject(
                ProfilingDifferentialRevalidationStatus::
                    NonIncreasingObservation,
                "revalidation observation is not newer than the "
                "last accepted observation");
        }
        input.last_accepted_revalidation_at_ = observation.checked_at;

        ProfilingDifferentialRevalidationResult result;
        result.status =
            ProfilingDifferentialRevalidationStatus::Accepted;
        result.diagnostic =
            "frozen differential input remains valid";
        return result;
    } catch (...) {
        return reject(
            ProfilingDifferentialRevalidationStatus::
                RevisionRejected,
            "differential profiling input revalidation failed");
    }
}

} // namespace lemon::residency
