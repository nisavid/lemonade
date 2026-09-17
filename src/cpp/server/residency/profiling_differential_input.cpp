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
           left.counter_continuity_epoch_sha256 ==
               right.counter_continuity_epoch_sha256 &&
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
    const auto &constraints =
        transaction.selector.catalog_selector.constraints;
    return transaction_identity_is_valid(transaction) &&
           std::find(constraints.begin(), constraints.end(),
                     ConstraintKind::GpuSharedResidency) !=
               constraints.end() &&
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
           identity.counter_continuity_epoch_sha256 ==
               bindings.counter_continuity_epoch_sha256 &&
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
    append_string(
        bytes, draft.noise_validity.noise_result_checksum_sha256);
    append_u64(bytes,
               static_cast<std::uint64_t>(draft.noise_validity.state));
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

void append_bindings(std::string &bytes,
                     const ProfilingNoiseBindings &bindings) {
    append_string(bytes, bindings.deployment_id);
    append_string(bytes, bindings.deployment_epoch_sha256);
    append_string(bytes, bindings.boot_id_sha256);
    append_string(bytes, bindings.device_identity_sha256);
    append_string(bytes, bindings.topology_sha256);
    append_string(bytes, bindings.kernel_identity_sha256);
    append_string(bytes, bindings.driver_identity_sha256);
    append_string(bytes, bindings.counter_source_id);
    append_string(bytes, bindings.counter_source_revision_sha256);
    append_string(bytes, bindings.counter_continuity_epoch_sha256);
    append_string(bytes, bindings.campaign_contract_sha256);
    append_string(bytes, bindings.procedure_revision_sha256);
    append_string(bytes, bindings.background_inventory_sha256);
}

bool revalidation_observation_is_structurally_valid(
    const ProfilingDifferentialRevalidationObservation &observation) noexcept {
    return bindings_are_valid(observation.observed_bindings) &&
           digest_is_valid(
               observation.observed_noise_result_checksum_sha256) &&
           digest_is_valid(
               observation.observed_counter_continuity_epoch_sha256) &&
           (!observation.target_activity ||
            (digest_is_valid(
                 observation.target_activity->client_identity_sha256) &&
             digest_is_valid(
                 observation.target_activity
                     ->containment_identity_sha256)));
}

std::optional<std::string> revalidation_observation_digest(
    const ProfilingDifferentialRevalidationObservation &observation) {
    if (!revalidation_observation_is_structurally_valid(observation)) {
        return std::nullopt;
    }

    std::string bytes =
        "lemonade/profiling-differential-revalidation-observation/v1";
    append_u64(bytes, static_cast<std::uint64_t>(
                          observation.checked_at.time_since_epoch().count()));
    append_bindings(bytes, observation.observed_bindings);
    append_string(bytes,
                  observation.observed_noise_result_checksum_sha256);
    append_string(bytes,
                  observation.observed_counter_continuity_epoch_sha256);
    append_u64(bytes, observation.observed_non_target_gtt_range_bytes);
    append_u64(bytes, observation.target_activity.has_value() ? 1 : 0);
    if (observation.target_activity) {
        append_string(bytes,
                      observation.target_activity->client_identity_sha256);
        append_string(
            bytes,
            observation.target_activity->containment_identity_sha256);
    }
    append_u64(bytes, observation.counter_reset_detected ? 1 : 0);
    append_u64(bytes, observation.counter_discontinuity_detected ? 1 : 0);
    append_u64(bytes,
               observation.unexpected_non_target_client_detected ? 1 : 0);
    return sha256_hex(bytes);
}

std::optional<std::string> revalidation_receipt_digest(
    const FrozenProfilingDifferentialInput &input,
    ProfilingDifferentialRepetitionPhase phase,
    std::uint32_t ordinal,
    std::chrono::steady_clock::time_point checked_at,
    std::string_view observation_sha256,
    std::string_view previous_receipt_sha256,
    ProfilingDifferentialRevalidationStatus status,
    ProfilingDifferentialRevalidationDisposition disposition) {
    std::string bytes =
        "lemonade/profiling-differential-revalidation-receipt/v1";
    append_string(bytes, input.frozen_input_sha256());
    append_string(bytes, input.noise().checksum_sha256());
    append_u64(bytes, static_cast<std::uint64_t>(phase));
    append_u64(bytes, ordinal);
    append_u64(bytes, static_cast<std::uint64_t>(
                          checked_at.time_since_epoch().count()));
    append_string(bytes, observation_sha256);
    append_string(bytes, previous_receipt_sha256);
    append_u64(bytes, static_cast<std::uint64_t>(status));
    append_u64(bytes, static_cast<std::uint64_t>(disposition));
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

ProfilingDifferentialRevalidationReceipt::
    ProfilingDifferentialRevalidationReceipt(
        ProfilingDifferentialRepetitionPhase phase,
        std::uint32_t ordinal,
        std::chrono::steady_clock::time_point checked_at,
        std::string frozen_input_sha256,
        std::string noise_result_checksum_sha256,
        std::string observation_sha256,
        std::string previous_receipt_sha256,
        ProfilingDifferentialRevalidationStatus status,
        ProfilingDifferentialRevalidationDisposition disposition,
        std::string receipt_sha256)
    : phase_(phase), ordinal_(ordinal), checked_at_(checked_at),
      frozen_input_sha256_(std::move(frozen_input_sha256)),
      noise_result_checksum_sha256_(
          std::move(noise_result_checksum_sha256)),
      observation_sha256_(std::move(observation_sha256)),
      previous_receipt_sha256_(std::move(previous_receipt_sha256)),
      status_(status), disposition_(disposition),
      receipt_sha256_(std::move(receipt_sha256)) {}

ProfilingDifferentialRepetitionPhase
ProfilingDifferentialRevalidationReceipt::phase() const noexcept {
    return phase_;
}

std::uint32_t
ProfilingDifferentialRevalidationReceipt::ordinal() const noexcept {
    return ordinal_;
}

std::chrono::steady_clock::time_point
ProfilingDifferentialRevalidationReceipt::checked_at() const noexcept {
    return checked_at_;
}

std::string_view ProfilingDifferentialRevalidationReceipt::
frozen_input_sha256() const noexcept {
    return frozen_input_sha256_;
}

std::string_view ProfilingDifferentialRevalidationReceipt::
noise_result_checksum_sha256() const noexcept {
    return noise_result_checksum_sha256_;
}

std::string_view ProfilingDifferentialRevalidationReceipt::
observation_sha256() const noexcept {
    return observation_sha256_;
}

std::string_view ProfilingDifferentialRevalidationReceipt::
previous_receipt_sha256() const noexcept {
    return previous_receipt_sha256_;
}

ProfilingDifferentialRevalidationStatus
ProfilingDifferentialRevalidationReceipt::status() const noexcept {
    return status_;
}

ProfilingDifferentialRevalidationDisposition
ProfilingDifferentialRevalidationReceipt::disposition() const noexcept {
    return disposition_;
}

std::string_view ProfilingDifferentialRevalidationReceipt::
receipt_sha256() const noexcept {
    return receipt_sha256_;
}

bool ProfilingDifferentialRevalidationReceipt::accepted() const noexcept {
    return status_ == ProfilingDifferentialRevalidationStatus::Accepted &&
           disposition_ ==
               ProfilingDifferentialRevalidationDisposition::Continue;
}

ProfilingDifferentialAttemptState::ProfilingDifferentialAttemptState(
    const FrozenProfilingDifferentialInput &input)
    : frozen_input_sha256_(input.frozen_input_sha256()),
      noise_result_checksum_sha256_(input.noise().checksum_sha256()),
      last_receipt_sha256_(input.revision().attempt_receipt_sha256),
      calibration_repetitions_(input.calibration_repetitions()),
      validation_repetitions_(input.validation_repetitions()) {}

std::uint32_t
ProfilingDifferentialAttemptState::receipts_issued() const noexcept {
    return receipts_issued_;
}

bool ProfilingDifferentialAttemptState::revision_rejected() const noexcept {
    return revision_rejected_;
}

bool
ProfilingDifferentialAttemptState::noise_result_invalidated() const noexcept {
    return noise_result_invalidated_;
}

bool ProfilingDifferentialInputFreezeResult::accepted() const noexcept {
    return status == ProfilingDifferentialInputFreezeStatus::Accepted &&
           input.has_value();
}

bool ProfilingDifferentialRevalidationResult::accepted() const noexcept {
    return status == ProfilingDifferentialRevalidationStatus::Accepted &&
           disposition ==
               ProfilingDifferentialRevalidationDisposition::Continue &&
           receipt.has_value() && receipt->accepted();
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
        if (!digest_is_valid(
                draft.noise_validity.noise_result_checksum_sha256) ||
            draft.noise_validity.noise_result_checksum_sha256 !=
                noise.checksum_sha256()) {
            return freeze_failure(
                ProfilingDifferentialInputFreezeStatus::
                    NoiseValidityMismatch,
                "noise validity binding does not match the immutable "
                "noise result");
        }
        if (draft.noise_validity.state !=
            ProfilingNoiseValidityState::Valid) {
            return freeze_failure(
                ProfilingDifferentialInputFreezeStatus::
                    NoiseResultInvalidated,
                "no-target noise result is invalidated");
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
    const FrozenProfilingDifferentialInput &input,
    ProfilingDifferentialAttemptState &attempt,
    ProfilingDifferentialRepetitionPhase phase,
    std::uint32_t ordinal,
    const ProfilingDifferentialRevalidationObservation &observation) {
    const auto without_receipt =
        [&](ProfilingDifferentialRevalidationStatus status,
            ProfilingDifferentialRevalidationDisposition disposition,
            std::string diagnostic) {
            ProfilingDifferentialRevalidationResult result;
            result.status = status;
            result.disposition = disposition;
            result.diagnostic = std::move(diagnostic);
            return result;
        };

    try {
        if (attempt.frozen_input_sha256_ != input.frozen_input_sha256() ||
            attempt.noise_result_checksum_sha256_ !=
                input.noise().checksum_sha256()) {
            attempt.revision_rejected_ = true;
            return without_receipt(
                ProfilingDifferentialRevalidationStatus::RevisionRejected,
                ProfilingDifferentialRevalidationDisposition::RejectRevision,
                "attempt state does not match the frozen input");
        }
        if (attempt.revision_rejected_) {
            return without_receipt(
                ProfilingDifferentialRevalidationStatus::RevisionRejected,
                attempt.noise_result_invalidated_
                    ? ProfilingDifferentialRevalidationDisposition::
                          InvalidateNoiseResult
                    : ProfilingDifferentialRevalidationDisposition::
                          RejectRevision,
                "calibration revision is terminally rejected");
        }

        const auto receipt_index =
            static_cast<std::uint64_t>(attempt.receipts_issued_);
        const auto calibration_count =
            static_cast<std::uint64_t>(attempt.calibration_repetitions_);
        const auto validation_count =
            static_cast<std::uint64_t>(attempt.validation_repetitions_);
        if (receipt_index >= calibration_count + validation_count) {
            attempt.revision_rejected_ = true;
            return without_receipt(
                ProfilingDifferentialRevalidationStatus::RevisionRejected,
                ProfilingDifferentialRevalidationDisposition::RejectRevision,
                "all frozen repetition receipts were already issued");
        }
        const auto expected_phase =
            receipt_index < calibration_count
                ? ProfilingDifferentialRepetitionPhase::Calibration
                : ProfilingDifferentialRepetitionPhase::Validation;
        const auto expected_ordinal = static_cast<std::uint32_t>(
            receipt_index < calibration_count
                ? receipt_index
                : receipt_index - calibration_count);
        if (phase != expected_phase || ordinal != expected_ordinal) {
            attempt.revision_rejected_ = true;
            return without_receipt(
                ProfilingDifferentialRevalidationStatus::RevisionRejected,
                ProfilingDifferentialRevalidationDisposition::RejectRevision,
                "revalidation repetition is not the next frozen repetition");
        }

        auto status = ProfilingDifferentialRevalidationStatus::Accepted;
        auto disposition =
            ProfilingDifferentialRevalidationDisposition::Continue;
        std::string_view diagnostic =
            "frozen differential input remains valid";
        const auto invalidate =
            [&](ProfilingDifferentialRevalidationStatus observed_status,
                std::string_view observed_diagnostic) {
                attempt.noise_result_invalidated_ = true;
                status = observed_status;
                disposition = ProfilingDifferentialRevalidationDisposition::
                    InvalidateNoiseResult;
                diagnostic = observed_diagnostic;
            };
        if (observation.counter_reset_detected) {
            invalidate(ProfilingDifferentialRevalidationStatus::CounterReset,
                       "global GTT counter reset was detected");
        } else if (observation.counter_discontinuity_detected) {
            invalidate(
                ProfilingDifferentialRevalidationStatus::CounterDiscontinuity,
                "global GTT counter discontinuity was detected");
        } else if (!digest_is_valid(
                       observation.observed_noise_result_checksum_sha256) ||
                   observation.observed_noise_result_checksum_sha256 !=
                       input.noise().checksum_sha256()) {
            invalidate(
                ProfilingDifferentialRevalidationStatus::NoiseResultMismatch,
                "no-target noise result binding changed");
        } else if (!digest_is_valid(
                       observation
                           .observed_counter_continuity_epoch_sha256) ||
                   observation.observed_counter_continuity_epoch_sha256 !=
                       input.identity().counter_continuity_epoch_sha256) {
            invalidate(
                ProfilingDifferentialRevalidationStatus::CounterDiscontinuity,
                "global GTT counter continuity binding changed");
        } else if (!bindings_equal_except_background(
                       observation.observed_bindings,
                       input.noise().bindings())) {
            invalidate(ProfilingDifferentialRevalidationStatus::BindingMismatch,
                       "boot-scoped no-target binding changed");
        } else if (observation.observed_bindings
                           .background_inventory_sha256 !=
                       input.noise().bindings()
                           .background_inventory_sha256 ||
                   observation.unexpected_non_target_client_detected) {
            invalidate(ProfilingDifferentialRevalidationStatus::BackgroundDrift,
                       "non-target GTT client inventory changed");
        } else if (observation.observed_non_target_gtt_range_bytes >
                   input.noise().n_gtt_bytes()) {
            invalidate(ProfilingDifferentialRevalidationStatus::ExcessVariation,
                       "non-target GTT variation exceeds the frozen bound");
        } else if (observation.target_activity &&
                   (!digest_is_valid(
                        observation.target_activity
                            ->client_identity_sha256) ||
                    !digest_is_valid(
                        observation.target_activity
                            ->containment_identity_sha256) ||
                    observation.target_activity->client_identity_sha256 !=
                        input.identity().target_client_identity_sha256 ||
                    observation.target_activity
                            ->containment_identity_sha256 !=
                        input.identity()
                            .target_containment_identity_sha256)) {
            status = ProfilingDifferentialRevalidationStatus::TargetMismatch;
            disposition =
                ProfilingDifferentialRevalidationDisposition::RejectRevision;
            diagnostic =
                "observed target activity is not the frozen contained target";
        } else if (attempt.last_accepted_revalidation_at_ &&
                   observation.checked_at <=
                       *attempt.last_accepted_revalidation_at_) {
            status = ProfilingDifferentialRevalidationStatus::
                NonIncreasingObservation;
            disposition =
                ProfilingDifferentialRevalidationDisposition::RejectRevision;
            diagnostic =
                "revalidation observation is not newer than the last accepted observation";
        }

        const auto observation_sha256 =
            revalidation_observation_digest(observation);
        if (!observation_sha256) {
            attempt.revision_rejected_ = true;
            if (!revalidation_observation_is_structurally_valid(
                    observation)) {
                return without_receipt(status, disposition,
                                       std::string(diagnostic));
            }
            attempt.noise_result_invalidated_ =
                attempt.noise_result_invalidated_ ||
                disposition == ProfilingDifferentialRevalidationDisposition::
                                   InvalidateNoiseResult;
            return without_receipt(
                ProfilingDifferentialRevalidationStatus::DigestUnavailable,
                attempt.noise_result_invalidated_
                    ? ProfilingDifferentialRevalidationDisposition::
                          InvalidateNoiseResult
                    : ProfilingDifferentialRevalidationDisposition::
                          RejectRevision,
                "revalidation observation digest is unavailable");
        }
        const auto receipt_sha256 = revalidation_receipt_digest(
            input, phase, ordinal, observation.checked_at,
            *observation_sha256, attempt.last_receipt_sha256_, status,
            disposition);
        if (!receipt_sha256) {
            attempt.revision_rejected_ = true;
            attempt.noise_result_invalidated_ =
                attempt.noise_result_invalidated_ ||
                disposition == ProfilingDifferentialRevalidationDisposition::
                                   InvalidateNoiseResult;
            return without_receipt(
                ProfilingDifferentialRevalidationStatus::DigestUnavailable,
                attempt.noise_result_invalidated_
                    ? ProfilingDifferentialRevalidationDisposition::
                          InvalidateNoiseResult
                    : ProfilingDifferentialRevalidationDisposition::
                          RejectRevision,
                "revalidation receipt digest is unavailable");
        }

        ProfilingDifferentialRevalidationResult result;
        result.status = status;
        result.disposition = disposition;
        result.diagnostic = diagnostic;
        result.receipt = ProfilingDifferentialRevalidationReceipt(
            phase, ordinal, observation.checked_at,
            std::string(input.frozen_input_sha256()),
            std::string(input.noise().checksum_sha256()),
            *observation_sha256, attempt.last_receipt_sha256_, status,
            disposition, *receipt_sha256);
        attempt.last_receipt_sha256_ = *receipt_sha256;
        ++attempt.receipts_issued_;
        if (disposition ==
            ProfilingDifferentialRevalidationDisposition::Continue) {
            attempt.last_accepted_revalidation_at_ = observation.checked_at;
        } else {
            attempt.revision_rejected_ = true;
            attempt.noise_result_invalidated_ =
                attempt.noise_result_invalidated_ ||
                disposition == ProfilingDifferentialRevalidationDisposition::
                                   InvalidateNoiseResult;
        }
        return result;
    } catch (...) {
        attempt.revision_rejected_ = true;
        return without_receipt(
            ProfilingDifferentialRevalidationStatus::RevisionRejected,
            attempt.noise_result_invalidated_
                ? ProfilingDifferentialRevalidationDisposition::
                      InvalidateNoiseResult
                : ProfilingDifferentialRevalidationDisposition::
                      RejectRevision,
            "differential profiling input revalidation failed");
    }
}

bool validate_profiling_differential_revalidation_receipt(
    const FrozenProfilingDifferentialInput &input,
    const ProfilingDifferentialRevalidationReceipt &receipt,
    ProfilingDifferentialRepetitionPhase expected_phase,
    std::uint32_t expected_ordinal,
    std::string_view expected_previous_receipt_sha256) noexcept {
    try {
        if (receipt.phase() != expected_phase ||
            receipt.ordinal() != expected_ordinal ||
            receipt.frozen_input_sha256() != input.frozen_input_sha256() ||
            receipt.noise_result_checksum_sha256() !=
                input.noise().checksum_sha256() ||
            !digest_is_valid(receipt.observation_sha256()) ||
            receipt.previous_receipt_sha256() !=
                expected_previous_receipt_sha256 ||
            !digest_is_valid(receipt.receipt_sha256())) {
            return false;
        }
        const auto expected = revalidation_receipt_digest(
            input, receipt.phase(), receipt.ordinal(), receipt.checked_at(),
            receipt.observation_sha256(), receipt.previous_receipt_sha256(),
            receipt.status(), receipt.disposition());
        return expected && *expected == receipt.receipt_sha256();
    } catch (...) {
        return false;
    }
}

} // namespace lemon::residency
