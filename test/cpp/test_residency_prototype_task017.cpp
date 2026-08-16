#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>

namespace lemon::residency::prototype {

enum class Mechanism : std::uint8_t {
    absent,
    successful_noop,
    llamacpp_slot_cache_erase_v1,
};

enum class Evidence : std::uint8_t {
    verified,
    verified_intact,
    unknown,
    unsupported,
};

enum class Disposition : std::uint8_t {
    reconciled,
    verified_intact,
    quarantine,
    fallback,
};

enum class LeaseState : std::uint8_t {
    idle_soft_reclaiming,
    idle,
};

enum class Operation : std::uint8_t {
    pressure_soft_release,
    pressure_hard_release,
};

enum class ReportingFault : std::uint8_t {
    none,
    missing,
    stale,
    skew,
    unhealthy,
    incomplete,
    incoherent_gtt_sample_missing,
    incoherent_host_sample_missing,
    incoherent_generation,
    incoherent_identity,
    incoherent_generation_zero,
    incoherent_generation_overflow,
    incoherent_equal_invalid_identity,
    incoherent_affected_only_wrong_target,
    incoherent_samples_only_wrong_target,
    incoherent_wrong_target,
};

enum class UnsupportedPreObservationFault : std::uint8_t {
    none,
    missing,
    stale,
    skew,
    unhealthy,
    incomplete,
    cache_gtt_missing,
    cache_host_missing,
    generation_zero,
    global_gtt_headroom_missing,
    global_host_headroom_missing,
    generation_overflow_to_zero,
    equal_invalid_device_identity,
    equal_invalid_backend_artifact_digest,
    equal_invalid_source_build_dependency_closure,
    equal_invalid_driver_runtime_closure,
    equal_invalid_model_manifest_digest,
    equal_invalid_normalized_configuration_digest,
    equal_invalid_evidence_index_digest,
    equal_invalid_evidence_liveness_lease,
    equal_invalid_evidence_liveness_present,
    equal_invalid_evidence_liveness_valid,
    equal_invalid_resident_id,
    equal_invalid_resident_generation,
    equal_invalid_backend_instance_birth_token,
    equal_invalid_topology_generation,
    equal_invalid_allocation_group_id,
    equal_invalid_observation_contract_digest,
    resident_before_device_identity_mismatch,
    resident_before_backend_artifact_digest_mismatch,
    resident_before_source_build_dependency_closure_mismatch,
    resident_before_driver_runtime_closure_mismatch,
    resident_before_model_manifest_digest_mismatch,
    resident_before_normalized_configuration_digest_mismatch,
    resident_before_evidence_index_digest_mismatch,
    resident_before_evidence_liveness_lease_mismatch,
    resident_before_evidence_liveness_present_mismatch,
    resident_before_evidence_liveness_valid_mismatch,
    resident_before_resident_id_mismatch,
    resident_before_resident_generation_mismatch,
    resident_before_backend_instance_birth_token_mismatch,
    resident_before_topology_generation_mismatch,
    resident_before_allocation_group_id_mismatch,
    resident_before_observation_contract_digest_mismatch,
    process_identity_missing,
    process_identity_zero,
    weights_identity_missing,
    weights_identity_zero,
    model_residency_identity_missing,
    model_residency_identity_zero,
    pin_unknown,
};

enum class IdentityAxis : std::uint8_t {
    device_identity,
    backend_artifact_digest,
    source_build_dependency_closure,
    driver_runtime_closure,
    model_manifest_digest,
    normalized_configuration_digest,
    evidence_index_digest,
    evidence_liveness_lease,
    evidence_liveness_present,
    evidence_liveness_valid,
    resident_id,
    resident_generation,
    backend_instance_birth_token,
    topology_generation,
    allocation_group_id,
    observation_contract_digest,
};

enum class ReportingTopology : std::uint8_t {
    all_equal_invalid,
    sample_to_sample_mismatch,
    affected_only_wrong_target,
    coherent_samples_only_wrong_target,
    all_three_wrong_target,
};

enum class PressureMode : std::uint8_t {
    report_only,
    disabled_invalid_evidence,
};

enum class FallbackProfile : std::uint8_t {
    rocm,
    vulkan,
};

enum class ChangedPostAxis : std::uint8_t {
    runtime_identity,
    cache_gtt,
    cache_host,
    global_gtt,
    global_host,
    observation_generation_replay,
    observation_generation_zero,
    observation_generation_overflow_to_zero,
};

enum class NoEffectAxis : std::uint8_t {
    owned_gtt,
    owned_host,
    global_gtt,
    global_host,
    unrelated_gtt,
    unrelated_host,
};

enum class NoEffectContextAxis : std::uint8_t {
    runtime_identity,
    process_identity,
    weights_identity,
    model_residency_identity,
    pin,
};

enum class ClaimId : std::uint8_t {
    model_weights,
    slot_cache,
    invalid,
};

enum class Effect : std::uint8_t {
    persistent_weights,
    reconstructible_state,
};

enum class UnknownReason : std::uint8_t {
    none,
    acknowledgement,
    action_lease,
    active_use,
    arithmetic,
    claim_group,
    dispatch,
    effect,
    identity,
    ledger_commit,
    ledger_generation,
    observation,
    observation_generation,
    slot_selection,
    unrelated_demand,
};

enum class IdentityToken : std::uint8_t {
    device_identity,
    backend_artifact_digest,
    source_build_dependency_closure,
    driver_runtime_closure,
    model_manifest_digest,
    normalized_configuration_digest,
    evidence_index_digest,
    evidence_liveness_lease,
    resident_id,
    resident_generation,
    backend_instance_birth_token,
    topology_generation,
    allocation_group_id,
    observation_contract_digest,
    action_lease,
    action_lease_claim_generation,
    pre_observation_generation,
    ledger_generation,
};

enum class Fault : std::uint8_t {
    slot_set_missing,
    slot_set_empty,
    slot_id_duplicate,
    slot_count_oversized,
    ack_missing,
    ack_count_mismatch,
    ack_id_mismatch,
    ack_partial,
    dispatch_failure,
    dispatch_completed_without_attempt,
    invalid_precondition_after_dispatch_attempt,
    valid_precondition_without_dispatch,
    valid_precondition_without_dispatch_changed_post,
    unsupported_mechanism_after_dispatch_attempt,
    unsupported_mechanism_without_dispatch_changed_post,
    ack_without_physical_delta,
    ack_without_physical_delta_each_axis_changed,
    pre_identity_mismatch,
    post_identity_mismatch,
    backend_instance_birth_token_mismatch,
    process_identity_missing,
    process_identity_zero,
    process_identity_changed,
    weights_identity_missing,
    weights_identity_zero,
    weights_identity_changed,
    model_residency_identity_missing,
    model_residency_identity_zero,
    model_residency_identity_changed,
    pin_missing,
    pin_changed,
    device_identity_mismatch,
    backend_artifact_mismatch,
    source_build_dependency_mismatch,
    driver_runtime_mismatch,
    model_manifest_mismatch,
    normalized_configuration_mismatch,
    evidence_index_mismatch,
    evidence_liveness_missing,
    evidence_liveness_expired,
    observation_contract_mismatch,
    topology_generation_mismatch,
    allocation_group_mismatch,
    action_lease_missing,
    action_lease_mismatch,
    action_lease_identity_mismatch,
    action_lease_evidence_liveness_lease_mismatch,
    action_lease_wrong_state,
    action_lease_wrong_operation,
    action_lease_wrong_slot_set,
    action_lease_wrong_claim_generation,
    action_lease_wrong_pre_observation_generation,
    action_lease_wrong_observation_contract,
    resident_generation_mismatch,
    observation_generation_mismatch,
    each_required_identity_token_zero,
    observation_before_missing,
    observation_before_stale,
    observation_before_skew,
    observation_before_unhealthy,
    observation_before_incomplete,
    observation_before_gtt_effect_missing,
    observation_before_host_effect_missing,
    observation_after_missing,
    observation_after_stale,
    observation_after_skew,
    observation_after_unhealthy,
    observation_after_incomplete,
    release_partial,
    gtt_release_partial,
    host_release_partial,
    cache_nonzero_after,
    gtt_cache_nonzero_after,
    host_cache_nonzero_after,
    gtt_effect_missing,
    host_effect_missing,
    global_gtt_headroom_missing,
    global_host_headroom_missing,
    ledger_claim_group_missing,
    ledger_claim_group_duplicate,
    ledger_claim_group_wrong_id,
    ledger_weights_group_wrong_id,
    ledger_claim_group_wrong_effect,
    ledger_weights_group_wrong_effect,
    ledger_claim_group_wrong_allocation,
    ledger_weights_group_wrong_allocation,
    ledger_weights_gtt_measurement_missing,
    ledger_weights_host_measurement_missing,
    ledger_cache_gtt_measurement_missing,
    ledger_cache_host_measurement_missing,
    ledger_weights_bytes_mismatch,
    ledger_weights_gtt_bytes_mismatch,
    ledger_weights_host_bytes_mismatch,
    ledger_cache_bytes_mismatch,
    ledger_cache_gtt_bytes_mismatch,
    ledger_cache_host_bytes_mismatch,
    effect_out_of_envelope,
    gtt_release_exceeds_maximum,
    host_release_exceeds_maximum,
    unrelated_demand_miscredit,
    gtt_causal_mismatch,
    host_causal_mismatch,
    unrelated_gtt_growth_missing,
    unrelated_host_growth_missing,
    observation_generation_increment_overflow,
    arithmetic_overflow,
    gtt_causal_checked_add_overflow,
    host_causal_checked_add_overflow,
    ledger_generation_increment_overflow,
    global_headroom_subtraction_underflow,
    gtt_global_headroom_subtraction_underflow,
    host_global_headroom_subtraction_underflow,
    cache_release_subtraction_underflow,
    gtt_cache_release_subtraction_underflow,
    host_cache_release_subtraction_underflow,
    ledger_generation_stale,
    ledger_commit_failure,
    active_use,
};

struct MeasuredBytes {
    bool present;
    std::uint64_t value;
};

struct PhysicalIdentity {
    bool present;
    std::uint64_t value;
};

struct KnownBool {
    bool known;
    bool value;
};

struct RuntimeBindings {
    std::uint64_t device_identity;
    std::uint64_t backend_artifact_digest;
    std::uint64_t source_build_dependency_closure;
    std::uint64_t driver_runtime_closure;
    std::uint64_t model_manifest_digest;
    std::uint64_t normalized_configuration_digest;
    std::uint64_t evidence_index_digest;
    std::uint64_t evidence_liveness_lease;
    bool evidence_liveness_present;
    bool evidence_liveness_valid;
};

struct RuntimeIdentity {
    RuntimeBindings bindings;
    std::uint64_t resident_id;
    std::uint64_t resident_generation;
    std::uint64_t backend_instance_birth_token;
    std::uint64_t topology_generation;
    std::uint64_t allocation_group_id;
    std::uint64_t observation_contract_digest;
};

struct ReportingSample {
    bool present;
    std::uint64_t generation;
    RuntimeIdentity identity;
};

struct ReportingEvidence {
    bool present;
    bool fresh;
    bool within_skew;
    bool healthy;
    bool complete;
    RuntimeIdentity affected_target;
    ReportingSample gtt_sample;
    ReportingSample host_sample;
};

struct ReportingFixture {
    RuntimeIdentity expected_target;
    ReportingEvidence evidence;
};

struct SlotSet {
    bool present;
    std::array<std::uint32_t, 2> ids;
    std::size_t count;
};

struct ActionLease {
    bool present;
    bool active_use;
    std::uint64_t action_lease;
    LeaseState state;
    Operation operation;
    SlotSet slot_set;
    std::uint64_t claim_generation;
    std::uint64_t pre_observation_generation;
    std::uint64_t observation_contract_digest;
    RuntimeIdentity identity;
};

struct Acknowledgement {
    bool present;
    bool complete;
    std::array<std::uint32_t, 2> ids;
    std::size_t count;
};

struct PhysicalObservation {
    bool present;
    bool fresh;
    bool within_skew;
    bool healthy;
    bool complete;
    std::uint64_t generation;
    RuntimeIdentity identity;
    MeasuredBytes cache_gtt;
    MeasuredBytes cache_host;
    MeasuredBytes global_gtt_headroom;
    MeasuredBytes global_host_headroom;
    PhysicalIdentity process_identity;
    PhysicalIdentity weights_identity;
    PhysicalIdentity model_residency_identity;
    KnownBool pin;
};

struct ClaimGroup {
    ClaimId id;
    Effect effect;
    std::uint64_t allocation_group_id;
    MeasuredBytes gtt;
    MeasuredBytes host;
};

struct LedgerSnapshot {
    std::array<ClaimGroup, 2> groups;
    std::size_t count;
    std::uint64_t generation;
};

struct ReleaseInput {
    Mechanism mechanism;
    RuntimeIdentity resident;
    std::uint64_t requested_action_lease;
    ActionLease lease;
    SlotSet request;
    bool dispatch_attempted;
    bool dispatch_completed;
    Acknowledgement acknowledgement;
    PhysicalObservation before;
    PhysicalObservation after;
    MeasuredBytes unrelated_gtt_growth;
    MeasuredBytes unrelated_host_growth;
    std::uint64_t maximum_gtt_release_bytes;
    std::uint64_t maximum_host_release_bytes;
    LedgerSnapshot ledger_before;
    std::uint64_t ledger_commit_generation;
    bool ledger_commit_succeeds;
};

struct ArithmeticResult {
    bool known;
    std::uint64_t value;
};

struct Verification {
    Evidence evidence;
    Disposition disposition;
    UnknownReason reason;
    bool dispatch_called;
    bool ledger_changed;
    bool residency_preserved;
    bool claims_maximized;
    bool post_observation_available;
    bool post_observation_unchanged_fresh;
    std::uint64_t owned_gtt_release_bytes;
    std::uint64_t owned_host_release_bytes;
    std::uint64_t global_gtt_improvement_bytes;
    std::uint64_t global_host_improvement_bytes;
    std::uint64_t credit_before_ack_bytes;
    std::uint64_t credit_before_physical_bytes;
    std::uint64_t credited_gtt_bytes;
    std::uint64_t credited_host_bytes;
    std::size_t removed_group_count;
    LedgerSnapshot ledger_after;
};

struct NegativeCase {
    Fault fault;
    const char* key;
};

struct UnsupportedObservation {
    PressureMode mode;
    Evidence evidence;
    Disposition disposition;
    UnknownReason reason;
    bool claims_maximized;
    bool detected;
    bool no_dispatch;
    bool post_observation_available;
    bool post_observation_unchanged_fresh;
    bool ledger_unchanged;
    bool residency_preserved;
    bool neutral_fields;
};

constexpr std::array<IdentityToken, 18> required_identity_tokens = {
    IdentityToken::device_identity,
    IdentityToken::backend_artifact_digest,
    IdentityToken::source_build_dependency_closure,
    IdentityToken::driver_runtime_closure,
    IdentityToken::model_manifest_digest,
    IdentityToken::normalized_configuration_digest,
    IdentityToken::evidence_index_digest,
    IdentityToken::evidence_liveness_lease,
    IdentityToken::resident_id,
    IdentityToken::resident_generation,
    IdentityToken::backend_instance_birth_token,
    IdentityToken::topology_generation,
    IdentityToken::allocation_group_id,
    IdentityToken::observation_contract_digest,
    IdentityToken::action_lease,
    IdentityToken::action_lease_claim_generation,
    IdentityToken::pre_observation_generation,
    IdentityToken::ledger_generation,
};

ArithmeticResult checked_add(std::uint64_t left, std::uint64_t right) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return ArithmeticResult{false, 0};
    }
    return ArithmeticResult{true, left + right};
}

ArithmeticResult checked_subtract(std::uint64_t larger, std::uint64_t smaller) {
    if (smaller > larger) {
        return ArithmeticResult{false, 0};
    }
    return ArithmeticResult{true, larger - smaller};
}

ArithmeticResult checked_increment(std::uint64_t value) {
    return checked_add(value, 1);
}

bool same_bindings(
    const RuntimeBindings& left, const RuntimeBindings& right) {
    return left.device_identity == right.device_identity &&
           left.backend_artifact_digest == right.backend_artifact_digest &&
           left.source_build_dependency_closure ==
               right.source_build_dependency_closure &&
           left.driver_runtime_closure == right.driver_runtime_closure &&
           left.model_manifest_digest == right.model_manifest_digest &&
           left.normalized_configuration_digest ==
               right.normalized_configuration_digest &&
           left.evidence_index_digest == right.evidence_index_digest &&
           left.evidence_liveness_lease ==
               right.evidence_liveness_lease &&
           left.evidence_liveness_present ==
               right.evidence_liveness_present &&
           left.evidence_liveness_valid == right.evidence_liveness_valid;
}

bool valid_bindings(const RuntimeBindings& bindings) {
    return bindings.device_identity != 0 &&
           bindings.backend_artifact_digest != 0 &&
           bindings.source_build_dependency_closure != 0 &&
           bindings.driver_runtime_closure != 0 &&
           bindings.model_manifest_digest != 0 &&
           bindings.normalized_configuration_digest != 0 &&
           bindings.evidence_index_digest != 0 &&
           bindings.evidence_liveness_lease != 0 &&
           bindings.evidence_liveness_present &&
           bindings.evidence_liveness_valid;
}

bool same_identity(
    const RuntimeIdentity& left, const RuntimeIdentity& right) {
    return same_bindings(left.bindings, right.bindings) &&
           left.resident_id == right.resident_id &&
           left.resident_generation == right.resident_generation &&
           left.backend_instance_birth_token ==
               right.backend_instance_birth_token &&
           left.topology_generation == right.topology_generation &&
           left.allocation_group_id == right.allocation_group_id &&
           left.observation_contract_digest ==
               right.observation_contract_digest;
}

bool valid_identity(const RuntimeIdentity& identity) {
    return valid_bindings(identity.bindings) && identity.resident_id != 0 &&
           identity.resident_generation != 0 &&
           identity.backend_instance_birth_token != 0 &&
           identity.topology_generation != 0 &&
           identity.allocation_group_id != 0 &&
           identity.observation_contract_digest != 0;
}

void invalidate_identity_axis(
    RuntimeIdentity& identity, IdentityAxis axis) {
    switch (axis) {
    case IdentityAxis::device_identity:
        identity.bindings.device_identity = 0;
        break;
    case IdentityAxis::backend_artifact_digest:
        identity.bindings.backend_artifact_digest = 0;
        break;
    case IdentityAxis::source_build_dependency_closure:
        identity.bindings.source_build_dependency_closure = 0;
        break;
    case IdentityAxis::driver_runtime_closure:
        identity.bindings.driver_runtime_closure = 0;
        break;
    case IdentityAxis::model_manifest_digest:
        identity.bindings.model_manifest_digest = 0;
        break;
    case IdentityAxis::normalized_configuration_digest:
        identity.bindings.normalized_configuration_digest = 0;
        break;
    case IdentityAxis::evidence_index_digest:
        identity.bindings.evidence_index_digest = 0;
        break;
    case IdentityAxis::evidence_liveness_lease:
        identity.bindings.evidence_liveness_lease = 0;
        break;
    case IdentityAxis::evidence_liveness_present:
        identity.bindings.evidence_liveness_present = false;
        break;
    case IdentityAxis::evidence_liveness_valid:
        identity.bindings.evidence_liveness_valid = false;
        break;
    case IdentityAxis::resident_id:
        identity.resident_id = 0;
        break;
    case IdentityAxis::resident_generation:
        identity.resident_generation = 0;
        break;
    case IdentityAxis::backend_instance_birth_token:
        identity.backend_instance_birth_token = 0;
        break;
    case IdentityAxis::topology_generation:
        identity.topology_generation = 0;
        break;
    case IdentityAxis::allocation_group_id:
        identity.allocation_group_id = 0;
        break;
    case IdentityAxis::observation_contract_digest:
        identity.observation_contract_digest = 0;
        break;
    }
}

void change_identity_axis(RuntimeIdentity& identity, IdentityAxis axis) {
    switch (axis) {
    case IdentityAxis::device_identity:
        identity.bindings.device_identity += 1;
        break;
    case IdentityAxis::backend_artifact_digest:
        identity.bindings.backend_artifact_digest += 1;
        break;
    case IdentityAxis::source_build_dependency_closure:
        identity.bindings.source_build_dependency_closure += 1;
        break;
    case IdentityAxis::driver_runtime_closure:
        identity.bindings.driver_runtime_closure += 1;
        break;
    case IdentityAxis::model_manifest_digest:
        identity.bindings.model_manifest_digest += 1;
        break;
    case IdentityAxis::normalized_configuration_digest:
        identity.bindings.normalized_configuration_digest += 1;
        break;
    case IdentityAxis::evidence_index_digest:
        identity.bindings.evidence_index_digest += 1;
        break;
    case IdentityAxis::evidence_liveness_lease:
        identity.bindings.evidence_liveness_lease += 1;
        break;
    case IdentityAxis::evidence_liveness_present:
        identity.bindings.evidence_liveness_present = false;
        break;
    case IdentityAxis::evidence_liveness_valid:
        identity.bindings.evidence_liveness_valid = false;
        break;
    case IdentityAxis::resident_id:
        identity.resident_id += 1;
        break;
    case IdentityAxis::resident_generation:
        identity.resident_generation += 1;
        break;
    case IdentityAxis::backend_instance_birth_token:
        identity.backend_instance_birth_token += 1;
        break;
    case IdentityAxis::topology_generation:
        identity.topology_generation += 1;
        break;
    case IdentityAxis::allocation_group_id:
        identity.allocation_group_id += 1;
        break;
    case IdentityAxis::observation_contract_digest:
        identity.observation_contract_digest += 1;
        break;
    }
}

bool required_identity_tokens_nonzero(const ReleaseInput& input) {
    return valid_identity(input.resident) &&
           input.requested_action_lease != 0 &&
           input.lease.action_lease != 0 &&
           input.lease.claim_generation != 0 &&
           input.lease.pre_observation_generation != 0 &&
           input.before.generation != 0 &&
           input.ledger_before.generation != 0;
}

bool same_physical_identity(
    const PhysicalIdentity& left, const PhysicalIdentity& right) {
    return left.present == right.present && left.value == right.value;
}

bool preserved_physical_identity(
    const PhysicalIdentity& before, const PhysicalIdentity& after) {
    return before.present && after.present && before.value == after.value;
}

bool changed_physical_identity(
    const PhysicalIdentity& before, const PhysicalIdentity& after) {
    return before.present && after.present && before.value != after.value;
}

bool same_residency(
    const PhysicalObservation& before,
    const PhysicalObservation& after) {
    return preserved_physical_identity(
               before.process_identity, after.process_identity) &&
           preserved_physical_identity(
               before.weights_identity, after.weights_identity) &&
           preserved_physical_identity(
               before.model_residency_identity,
               after.model_residency_identity) &&
           before.pin.known && after.pin.known &&
           before.pin.value == after.pin.value;
}

bool valid_physical_identity(const PhysicalObservation& observation) {
    return observation.process_identity.present &&
           observation.process_identity.value != 0 &&
           observation.weights_identity.present &&
           observation.weights_identity.value != 0 &&
           observation.model_residency_identity.present &&
           observation.model_residency_identity.value != 0 &&
           observation.pin.known;
}

bool valid_slot_set(const SlotSet& slots) {
    if (!slots.present || slots.count == 0 || slots.count > slots.ids.size()) {
        return false;
    }
    for (std::size_t left = 0; left < slots.count; ++left) {
        for (std::size_t right = left + 1; right < slots.count; ++right) {
            if (slots.ids[left] == slots.ids[right]) {
                return false;
            }
        }
    }
    return true;
}

bool same_slot_set(const SlotSet& left, const SlotSet& right) {
    if (!valid_slot_set(left) || !valid_slot_set(right) ||
        left.count != right.count) {
        return false;
    }
    for (std::size_t left_index = 0; left_index < left.count; ++left_index) {
        bool matched = false;
        for (std::size_t right_index = 0; right_index < right.count;
             ++right_index) {
            if (left.ids[left_index] == right.ids[right_index]) {
                matched = true;
            }
        }
        if (!matched) {
            return false;
        }
    }
    return true;
}

bool exact_acknowledgement(
    const SlotSet& request, const Acknowledgement& acknowledgement) {
    const SlotSet acknowledged{
        acknowledgement.present,
        acknowledgement.ids,
        acknowledgement.count,
    };
    return acknowledgement.complete && same_slot_set(request, acknowledged);
}

bool valid_observation_envelope(const PhysicalObservation& observation) {
    return observation.present && observation.fresh &&
           observation.within_skew && observation.healthy &&
           observation.complete &&
           observation.cache_gtt.present && observation.cache_host.present &&
           observation.global_gtt_headroom.present &&
           observation.global_host_headroom.present;
}

bool valid_observation(const PhysicalObservation& observation) {
    return valid_observation_envelope(observation) &&
           valid_identity(observation.identity) &&
           valid_physical_identity(observation);
}

bool same_measurement(const MeasuredBytes& left, const MeasuredBytes& right) {
    return left.present == right.present && left.value == right.value;
}

bool same_claim(const ClaimGroup& left, const ClaimGroup& right) {
    return left.id == right.id && left.effect == right.effect &&
           left.allocation_group_id == right.allocation_group_id &&
           left.gtt.present == right.gtt.present &&
           left.gtt.value == right.gtt.value &&
           left.host.present == right.host.present &&
           left.host.value == right.host.value;
}

bool same_ledger(
    const LedgerSnapshot& left, const LedgerSnapshot& right) {
    if (left.generation != right.generation || left.count != right.count ||
        left.count > left.groups.size() || right.count > right.groups.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.count; ++index) {
        if (!same_claim(left.groups[index], right.groups[index])) {
            return false;
        }
    }
    return true;
}

bool valid_claim_groups(
    const LedgerSnapshot& ledger,
    const RuntimeIdentity& resident,
    const PhysicalObservation& before) {
    if (ledger.count != 2 || ledger.generation == 0 ||
        !before.cache_gtt.present ||
        !before.cache_host.present) {
        return false;
    }
    const ClaimGroup& weights = ledger.groups[0];
    const ClaimGroup& cache = ledger.groups[1];
    return weights.id == ClaimId::model_weights &&
           weights.effect == Effect::persistent_weights &&
           weights.allocation_group_id == 30 && weights.gtt.present &&
           weights.gtt.value == 8192 && weights.host.present &&
           weights.host.value == 4096 &&
           cache.id == ClaimId::slot_cache &&
           cache.effect == Effect::reconstructible_state &&
           cache.allocation_group_id == resident.allocation_group_id &&
           cache.gtt.present &&
           cache.gtt.value == before.cache_gtt.value && cache.host.present &&
           cache.host.value == before.cache_host.value;
}

bool contains_claim(const LedgerSnapshot& ledger, ClaimId id) {
    for (std::size_t index = 0; index < ledger.count; ++index) {
        if (ledger.groups[index].id == id) {
            return true;
        }
    }
    return false;
}

bool action_lease_matches(const ReleaseInput& input) {
    return required_identity_tokens_nonzero(input) && input.lease.present &&
           input.lease.action_lease == input.requested_action_lease &&
           input.lease.state == LeaseState::idle_soft_reclaiming &&
           input.lease.operation == Operation::pressure_soft_release &&
           same_slot_set(input.lease.slot_set, input.request) &&
           input.lease.claim_generation == input.ledger_before.generation &&
           input.lease.pre_observation_generation == input.before.generation &&
           input.lease.observation_contract_digest ==
               input.resident.observation_contract_digest &&
           same_identity(input.lease.identity, input.resident);
}

UnknownReason precondition_failure_reason(const ReleaseInput& input) {
    if (!valid_slot_set(input.request)) {
        return UnknownReason::slot_selection;
    }
    if (!required_identity_tokens_nonzero(input) ||
        !valid_identity(input.resident) ||
        !valid_identity(input.lease.identity) ||
        !valid_identity(input.before.identity) ||
        !same_identity(input.resident, input.before.identity) ||
        !valid_physical_identity(input.before)) {
        return UnknownReason::identity;
    }
    if (!valid_observation_envelope(input.before)) {
        return UnknownReason::observation;
    }
    if (!action_lease_matches(input)) {
        return UnknownReason::action_lease;
    }
    if (!valid_claim_groups(
            input.ledger_before, input.resident, input.before)) {
        return UnknownReason::claim_group;
    }
    if (input.lease.active_use) {
        return UnknownReason::active_use;
    }
    return UnknownReason::none;
}

UnknownReason unsupported_safety_failure_reason(const ReleaseInput& input) {
    if (!input.before.global_gtt_headroom.present ||
        !input.before.global_host_headroom.present) {
        return UnknownReason::observation;
    }
    if (!checked_increment(input.before.generation).known) {
        return UnknownReason::observation_generation;
    }
    if (!valid_identity(input.resident) ||
        !valid_identity(input.before.identity) ||
        !same_identity(input.resident, input.before.identity) ||
        !valid_physical_identity(input.before)) {
        return UnknownReason::identity;
    }
    return UnknownReason::none;
}

bool unchanged_fresh_observation(const ReleaseInput& input) {
    const ArithmeticResult next_generation =
        checked_increment(input.before.generation);
    return valid_observation(input.after) && next_generation.known &&
           input.after.generation == next_generation.value &&
           same_identity(input.before.identity, input.after.identity) &&
           same_residency(input.before, input.after) &&
           input.before.cache_gtt.value == input.after.cache_gtt.value &&
           input.before.cache_host.value == input.after.cache_host.value &&
           input.before.global_gtt_headroom.present ==
               input.after.global_gtt_headroom.present &&
           input.before.global_gtt_headroom.value ==
               input.after.global_gtt_headroom.value &&
           input.before.global_host_headroom.present ==
               input.after.global_host_headroom.present &&
           input.before.global_host_headroom.value ==
               input.after.global_host_headroom.value;
}

bool valid_no_effect_context(const ReleaseInput& input) {
    const ArithmeticResult next_generation =
        checked_increment(input.before.generation);
    return valid_observation(input.before) && valid_observation(input.after) &&
           same_identity(input.resident, input.before.identity) &&
           same_identity(input.resident, input.after.identity) &&
           same_residency(input.before, input.after) &&
           next_generation.known &&
           input.after.generation == next_generation.value;
}

bool post_residency_preserved(const ReleaseInput& input) {
    const ArithmeticResult next_generation =
        checked_increment(input.before.generation);
    return valid_observation(input.after) &&
           same_identity(input.resident, input.after.identity) &&
           next_generation.known &&
           input.after.generation == next_generation.value &&
           same_residency(input.before, input.after);
}

Verification base_verification(
    const ReleaseInput& input,
    Evidence evidence,
    Disposition disposition,
    UnknownReason reason,
    bool dispatch_called,
    bool claims_maximized) {
    return Verification{
        evidence,
        disposition,
        reason,
        dispatch_called,
        false,
        post_residency_preserved(input),
        claims_maximized,
        input.after.present,
        unchanged_fresh_observation(input),
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        input.ledger_before,
    };
}

Verification verify_soft_release(const ReleaseInput& input) {
    const bool unchanged_post = unchanged_fresh_observation(input);
    if (!input.dispatch_attempted && input.dispatch_completed) {
        return base_verification(
            input,
            Evidence::unknown,
            Disposition::quarantine,
            UnknownReason::dispatch,
            false,
            true);
    }
    if (input.mechanism != Mechanism::llamacpp_slot_cache_erase_v1) {
        if (input.dispatch_attempted) {
            return base_verification(
                input,
                Evidence::unknown,
                Disposition::quarantine,
                UnknownReason::dispatch,
                input.dispatch_attempted,
                true);
        }
        const UnknownReason safety_reason =
            unsupported_safety_failure_reason(input);
        if (safety_reason != UnknownReason::none) {
            return base_verification(
                input,
                Evidence::unknown,
                Disposition::quarantine,
                safety_reason,
                false,
                true);
        }
        if (!unchanged_post) {
            return base_verification(
                input,
                Evidence::unknown,
                Disposition::quarantine,
                UnknownReason::dispatch,
                false,
                true);
        }
        return base_verification(
            input,
            Evidence::unsupported,
            Disposition::fallback,
            UnknownReason::none,
            false,
            false);
    }
    const UnknownReason precondition_reason =
        precondition_failure_reason(input);
    if (precondition_reason != UnknownReason::none) {
        if (input.dispatch_attempted || !unchanged_post) {
            return base_verification(
                input,
                Evidence::unknown,
                Disposition::quarantine,
                precondition_reason,
                input.dispatch_attempted,
                true);
        }
        return base_verification(
            input,
            Evidence::unknown,
            Disposition::verified_intact,
            precondition_reason,
            false,
            false);
    }
    if (!input.dispatch_attempted) {
        return base_verification(
            input,
            unchanged_post ? Evidence::verified_intact : Evidence::unknown,
            unchanged_post ? Disposition::verified_intact
                           : Disposition::quarantine,
            unchanged_post ? UnknownReason::none : UnknownReason::observation,
            false,
            !unchanged_post);
    }
    if (!input.dispatch_completed) {
        return base_verification(
            input,
            Evidence::unknown,
            Disposition::quarantine,
            UnknownReason::dispatch,
            input.dispatch_attempted,
            true);
    }
    if (!exact_acknowledgement(input.request, input.acknowledgement)) {
        return base_verification(
            input,
            Evidence::unknown,
            Disposition::quarantine,
            UnknownReason::acknowledgement,
            true,
            true);
    }
    if (!valid_observation_envelope(input.after)) {
        return base_verification(
            input,
            Evidence::unknown,
            Disposition::quarantine,
            UnknownReason::observation,
            true,
            true);
    }
    if (!valid_identity(input.after.identity) ||
        !same_identity(input.resident, input.after.identity) ||
        !valid_physical_identity(input.after)) {
        return base_verification(
            input,
            Evidence::unknown,
            Disposition::quarantine,
            UnknownReason::identity,
            true,
            true);
    }

    const ArithmeticResult next_observation_generation =
        checked_increment(input.before.generation);
    if (!next_observation_generation.known ||
        input.after.generation != next_observation_generation.value) {
        return base_verification(
            input,
            Evidence::unknown,
            Disposition::quarantine,
            UnknownReason::observation_generation,
            true,
            true);
    }

    const ArithmeticResult gtt_release = checked_subtract(
        input.before.cache_gtt.value, input.after.cache_gtt.value);
    const ArithmeticResult host_release = checked_subtract(
        input.before.cache_host.value, input.after.cache_host.value);
    const ArithmeticResult global_gtt_improvement = checked_subtract(
        input.after.global_gtt_headroom.value,
        input.before.global_gtt_headroom.value);
    const ArithmeticResult global_host_improvement = checked_subtract(
        input.after.global_host_headroom.value,
        input.before.global_host_headroom.value);
    if (!gtt_release.known || !host_release.known ||
        !global_gtt_improvement.known || !global_host_improvement.known) {
        return base_verification(
            input,
            Evidence::unknown,
            Disposition::quarantine,
            UnknownReason::arithmetic,
            true,
            true);
    }
    if (!input.unrelated_gtt_growth.present ||
        !input.unrelated_host_growth.present) {
        return base_verification(
            input,
            Evidence::unknown,
            Disposition::quarantine,
            UnknownReason::unrelated_demand,
            true,
            true);
    }

    const bool no_effect =
        gtt_release.value == 0 && host_release.value == 0 &&
        global_gtt_improvement.value == 0 &&
        global_host_improvement.value == 0 &&
        input.unrelated_gtt_growth.value == 0 &&
        input.unrelated_host_growth.value == 0;
    if (no_effect) {
        if (!valid_no_effect_context(input)) {
            return base_verification(
                input,
                Evidence::unknown,
                Disposition::quarantine,
                UnknownReason::identity,
                true,
                true);
        }
        return base_verification(
            input,
            Evidence::verified_intact,
            Disposition::verified_intact,
            UnknownReason::effect,
            true,
            false);
    }

    Verification ambiguous = base_verification(
        input,
        Evidence::unknown,
        Disposition::quarantine,
        UnknownReason::effect,
        true,
        true);
    if (input.after.cache_gtt.value != 0 ||
        input.after.cache_host.value != 0 || gtt_release.value == 0 ||
        host_release.value == 0 ||
        gtt_release.value > input.maximum_gtt_release_bytes ||
        host_release.value > input.maximum_host_release_bytes) {
        return ambiguous;
    }

    const ArithmeticResult causal_gtt = checked_add(
        global_gtt_improvement.value, input.unrelated_gtt_growth.value);
    const ArithmeticResult causal_host = checked_add(
        global_host_improvement.value, input.unrelated_host_growth.value);
    if (!causal_gtt.known || !causal_host.known) {
        ambiguous.reason = UnknownReason::arithmetic;
        return ambiguous;
    }
    if (causal_gtt.value != gtt_release.value ||
        causal_host.value != host_release.value) {
        ambiguous.reason = UnknownReason::unrelated_demand;
        return ambiguous;
    }
    if (!same_residency(input.before, input.after)) {
        ambiguous.reason = UnknownReason::identity;
        return ambiguous;
    }
    if (input.ledger_commit_generation != input.ledger_before.generation) {
        ambiguous.reason = UnknownReason::ledger_generation;
        return ambiguous;
    }
    if (!input.ledger_commit_succeeds) {
        ambiguous.reason = UnknownReason::ledger_commit;
        return ambiguous;
    }
    const ArithmeticResult next_ledger_generation =
        checked_increment(input.ledger_before.generation);
    if (!next_ledger_generation.known) {
        ambiguous.reason = UnknownReason::arithmetic;
        return ambiguous;
    }

    Verification verified = ambiguous;
    verified.evidence = Evidence::verified;
    verified.disposition = Disposition::reconciled;
    verified.reason = UnknownReason::none;
    verified.ledger_changed = true;
    verified.claims_maximized = false;
    verified.owned_gtt_release_bytes = gtt_release.value;
    verified.owned_host_release_bytes = host_release.value;
    verified.global_gtt_improvement_bytes = global_gtt_improvement.value;
    verified.global_host_improvement_bytes = global_host_improvement.value;
    verified.credit_before_ack_bytes = 0;
    verified.credit_before_physical_bytes = 0;
    verified.credited_gtt_bytes = gtt_release.value;
    verified.credited_host_bytes = host_release.value;
    verified.removed_group_count = 1;
    verified.ledger_after.count = 1;
    verified.ledger_after.generation = next_ledger_generation.value;
    return verified;
}

RuntimeBindings exact_bindings() {
    return RuntimeBindings{
        5001,
        5002,
        5003,
        5004,
        5005,
        5006,
        5007,
        5008,
        true,
        true,
    };
}

RuntimeIdentity exact_identity() {
    return RuntimeIdentity{
        exact_bindings(),
        101,
        11,
        4001,
        7,
        31,
        6001,
    };
}

PhysicalObservation make_observation(
    std::uint64_t generation,
    std::uint64_t cache_gtt_bytes,
    std::uint64_t cache_host_bytes,
    std::uint64_t global_gtt_headroom_bytes,
    std::uint64_t global_host_headroom_bytes) {
    return PhysicalObservation{
        true,
        true,
        true,
        true,
        true,
        generation,
        exact_identity(),
        MeasuredBytes{true, cache_gtt_bytes},
        MeasuredBytes{true, cache_host_bytes},
        MeasuredBytes{true, global_gtt_headroom_bytes},
        MeasuredBytes{true, global_host_headroom_bytes},
        PhysicalIdentity{true, 9001},
        PhysicalIdentity{true, 9002},
        PhysicalIdentity{true, 9003},
        KnownBool{true, true},
    };
}

LedgerSnapshot exact_ledger() {
    return LedgerSnapshot{
        {
            ClaimGroup{
                ClaimId::model_weights,
                Effect::persistent_weights,
                30,
                MeasuredBytes{true, 8192},
                MeasuredBytes{true, 4096},
            },
            ClaimGroup{
                ClaimId::slot_cache,
                Effect::reconstructible_state,
                31,
                MeasuredBytes{true, 2048},
                MeasuredBytes{true, 1024},
            },
        },
        2,
        17,
    };
}

ReleaseInput canonical_input() {
    const SlotSet slots{true, {3, 7}, 2};
    return ReleaseInput{
        Mechanism::llamacpp_slot_cache_erase_v1,
        exact_identity(),
        77,
        ActionLease{
            true,
            false,
            77,
            LeaseState::idle_soft_reclaiming,
            Operation::pressure_soft_release,
            slots,
            17,
            41,
            6001,
            exact_identity(),
        },
        slots,
        true,
        true,
        Acknowledgement{true, true, {3, 7}, 2},
        make_observation(41, 2048, 1024, 4096, 2048),
        make_observation(42, 0, 0, 5632, 2816),
        MeasuredBytes{true, 512},
        MeasuredBytes{true, 256},
        2048,
        1024,
        exact_ledger(),
        17,
        true,
    };
}

void set_unchanged_post_observation(ReleaseInput& input) {
    const ArithmeticResult next_generation =
        checked_increment(input.before.generation);
    input.after = input.before;
    input.after.present = true;
    input.after.fresh = true;
    input.after.within_skew = true;
    input.after.healthy = true;
    input.after.complete = true;
    input.after.cache_gtt.present = true;
    input.after.cache_host.present = true;
    input.after.global_gtt_headroom.present = true;
    input.after.global_host_headroom.present = true;
    input.after.pin.known = true;
    input.after.generation =
        next_generation.known ? next_generation.value : input.before.generation;
    input.unrelated_gtt_growth = MeasuredBytes{true, 0};
    input.unrelated_host_growth = MeasuredBytes{true, 0};
}

ReleaseInput changed_post_input(
    Mechanism mechanism, ChangedPostAxis axis) {
    ReleaseInput input = canonical_input();
    input.mechanism = mechanism;
    input.dispatch_attempted = false;
    input.dispatch_completed = false;
    set_unchanged_post_observation(input);
    switch (axis) {
    case ChangedPostAxis::runtime_identity:
        input.after.identity.resident_id += 1;
        break;
    case ChangedPostAxis::cache_gtt:
        input.after.cache_gtt.value += 1;
        break;
    case ChangedPostAxis::cache_host:
        input.after.cache_host.value += 1;
        break;
    case ChangedPostAxis::global_gtt:
        input.after.global_gtt_headroom.value += 1;
        break;
    case ChangedPostAxis::global_host:
        input.after.global_host_headroom.value += 1;
        break;
    case ChangedPostAxis::observation_generation_replay:
        input.after.generation = input.before.generation;
        break;
    case ChangedPostAxis::observation_generation_zero:
        input.after.generation = 0;
        break;
    case ChangedPostAxis::observation_generation_overflow_to_zero:
        input.before.generation =
            std::numeric_limits<std::uint64_t>::max();
        input.lease.pre_observation_generation = input.before.generation;
        input.after.generation = 0;
        break;
    }
    return input;
}

bool exact_changed_post_axis(
    const ReleaseInput& input, ChangedPostAxis axis) {
    ReleaseInput unchanged = canonical_input();
    unchanged.mechanism = input.mechanism;
    unchanged.dispatch_attempted = false;
    unchanged.dispatch_completed = false;
    set_unchanged_post_observation(unchanged);
    RuntimeIdentity changed_runtime = unchanged.after.identity;
    changed_runtime.resident_id += 1;
    const bool runtime_changed =
        same_identity(input.after.identity, changed_runtime);
    const bool cache_gtt_changed =
        input.after.cache_gtt.present &&
        input.after.cache_gtt.value == unchanged.after.cache_gtt.value + 1;
    const bool cache_host_changed =
        input.after.cache_host.present &&
        input.after.cache_host.value == unchanged.after.cache_host.value + 1;
    const bool global_gtt_changed =
        input.after.global_gtt_headroom.present &&
        input.after.global_gtt_headroom.value ==
            unchanged.after.global_gtt_headroom.value + 1;
    const bool global_host_changed =
        input.after.global_host_headroom.present &&
        input.after.global_host_headroom.value ==
            unchanged.after.global_host_headroom.value + 1;
    const bool runtime_same =
        same_identity(input.after.identity, unchanged.after.identity);
    const bool generation_same =
        input.after.generation == unchanged.after.generation;
    const bool generation_replayed =
        input.after.generation == input.before.generation;
    const bool generation_zero = input.after.generation == 0;
    const bool generation_overflow_to_zero =
        input.before.generation ==
            std::numeric_limits<std::uint64_t>::max() &&
        input.lease.pre_observation_generation == input.before.generation &&
        input.after.generation == 0;
    const bool cache_gtt_same =
        same_measurement(input.after.cache_gtt, unchanged.after.cache_gtt);
    const bool cache_host_same =
        same_measurement(input.after.cache_host, unchanged.after.cache_host);
    const bool global_gtt_same = same_measurement(
        input.after.global_gtt_headroom,
        unchanged.after.global_gtt_headroom);
    const bool global_host_same = same_measurement(
        input.after.global_host_headroom,
        unchanged.after.global_host_headroom);
    const bool residency_same =
        same_residency(input.after, unchanged.after);
    switch (axis) {
    case ChangedPostAxis::runtime_identity:
        return runtime_changed && cache_gtt_same && cache_host_same &&
               global_gtt_same && global_host_same && generation_same &&
               residency_same;
    case ChangedPostAxis::cache_gtt:
        return runtime_same && cache_gtt_changed && cache_host_same &&
               global_gtt_same && global_host_same && generation_same &&
               residency_same;
    case ChangedPostAxis::cache_host:
        return runtime_same && cache_gtt_same && cache_host_changed &&
               global_gtt_same && global_host_same && generation_same &&
               residency_same;
    case ChangedPostAxis::global_gtt:
        return runtime_same && cache_gtt_same && cache_host_same &&
               global_gtt_changed && global_host_same && generation_same &&
               residency_same;
    case ChangedPostAxis::global_host:
        return runtime_same && cache_gtt_same && cache_host_same &&
               global_gtt_same && global_host_changed && generation_same &&
               residency_same;
    case ChangedPostAxis::observation_generation_replay:
        return runtime_same && cache_gtt_same && cache_host_same &&
               global_gtt_same && global_host_same && generation_replayed &&
               !generation_zero && residency_same;
    case ChangedPostAxis::observation_generation_zero:
        return runtime_same && cache_gtt_same && cache_host_same &&
               global_gtt_same && global_host_same && generation_zero &&
               residency_same;
    case ChangedPostAxis::observation_generation_overflow_to_zero:
        return runtime_same && cache_gtt_same && cache_host_same &&
               global_gtt_same && global_host_same &&
               generation_overflow_to_zero && residency_same;
    }
    return false;
}

bool changed_post_quarantined(
    const ReleaseInput& input,
    const Verification& result,
    UnknownReason reason,
    ChangedPostAxis axis) {
    const bool expected_residency =
        axis != ChangedPostAxis::runtime_identity &&
        axis != ChangedPostAxis::observation_generation_replay &&
        axis != ChangedPostAxis::observation_generation_zero &&
        axis != ChangedPostAxis::observation_generation_overflow_to_zero;
    return exact_changed_post_axis(input, axis) &&
           result.evidence == Evidence::unknown &&
           result.disposition == Disposition::quarantine &&
           result.reason == reason && !result.dispatch_called &&
           !result.ledger_changed &&
           same_ledger(input.ledger_before, result.ledger_after) &&
           result.residency_preserved == expected_residency &&
           result.claims_maximized &&
           !result.post_observation_unchanged_fresh &&
           result.credited_gtt_bytes == 0 &&
           result.credited_host_bytes == 0;
}

ReleaseInput no_effect_axis_input(NoEffectAxis axis) {
    ReleaseInput input = canonical_input();
    set_unchanged_post_observation(input);
    switch (axis) {
    case NoEffectAxis::owned_gtt:
        input.after.cache_gtt.value -= 1;
        break;
    case NoEffectAxis::owned_host:
        input.after.cache_host.value -= 1;
        break;
    case NoEffectAxis::global_gtt:
        input.after.global_gtt_headroom.value += 1;
        break;
    case NoEffectAxis::global_host:
        input.after.global_host_headroom.value += 1;
        break;
    case NoEffectAxis::unrelated_gtt:
        input.unrelated_gtt_growth.value = 1;
        break;
    case NoEffectAxis::unrelated_host:
        input.unrelated_host_growth.value = 1;
        break;
    }
    return input;
}

bool exact_no_effect_axis(const ReleaseInput& input, NoEffectAxis axis) {
    ReleaseInput unchanged = canonical_input();
    set_unchanged_post_observation(unchanged);
    const bool owned_gtt_changed =
        input.after.cache_gtt.value + 1 == unchanged.after.cache_gtt.value;
    const bool owned_host_changed =
        input.after.cache_host.value + 1 == unchanged.after.cache_host.value;
    const bool global_gtt_changed =
        input.after.global_gtt_headroom.value ==
            unchanged.after.global_gtt_headroom.value + 1;
    const bool global_host_changed =
        input.after.global_host_headroom.value ==
            unchanged.after.global_host_headroom.value + 1;
    const bool unrelated_gtt_changed =
        input.unrelated_gtt_growth.present &&
        input.unrelated_gtt_growth.value == 1;
    const bool unrelated_host_changed =
        input.unrelated_host_growth.present &&
        input.unrelated_host_growth.value == 1;
    const bool owned_gtt_same =
        same_measurement(input.after.cache_gtt, unchanged.after.cache_gtt);
    const bool owned_host_same =
        same_measurement(input.after.cache_host, unchanged.after.cache_host);
    const bool global_gtt_same = same_measurement(
        input.after.global_gtt_headroom,
        unchanged.after.global_gtt_headroom);
    const bool global_host_same = same_measurement(
        input.after.global_host_headroom,
        unchanged.after.global_host_headroom);
    const bool unrelated_gtt_same = same_measurement(
        input.unrelated_gtt_growth, unchanged.unrelated_gtt_growth);
    const bool unrelated_host_same = same_measurement(
        input.unrelated_host_growth, unchanged.unrelated_host_growth);
    switch (axis) {
    case NoEffectAxis::owned_gtt:
        return owned_gtt_changed && owned_host_same && global_gtt_same &&
               global_host_same && unrelated_gtt_same && unrelated_host_same;
    case NoEffectAxis::owned_host:
        return owned_gtt_same && owned_host_changed && global_gtt_same &&
               global_host_same && unrelated_gtt_same && unrelated_host_same;
    case NoEffectAxis::global_gtt:
        return owned_gtt_same && owned_host_same && global_gtt_changed &&
               global_host_same && unrelated_gtt_same && unrelated_host_same;
    case NoEffectAxis::global_host:
        return owned_gtt_same && owned_host_same && global_gtt_same &&
               global_host_changed && unrelated_gtt_same && unrelated_host_same;
    case NoEffectAxis::unrelated_gtt:
        return owned_gtt_same && owned_host_same && global_gtt_same &&
               global_host_same && unrelated_gtt_changed &&
               unrelated_host_same;
    case NoEffectAxis::unrelated_host:
        return owned_gtt_same && owned_host_same && global_gtt_same &&
               global_host_same && unrelated_gtt_same &&
               unrelated_host_changed;
    }
    return false;
}

bool no_effect_axis_rejected(
    const ReleaseInput& input,
    const Verification& result,
    NoEffectAxis axis) {
    return exact_no_effect_axis(input, axis) &&
           exact_acknowledgement(input.request, input.acknowledgement) &&
           result.evidence == Evidence::unknown &&
           result.disposition == Disposition::quarantine &&
           result.reason == UnknownReason::effect && result.dispatch_called &&
           !result.ledger_changed &&
           same_ledger(input.ledger_before, result.ledger_after) &&
           result.residency_preserved && result.claims_maximized &&
           result.credited_gtt_bytes == 0 &&
           result.credited_host_bytes == 0;
}

ReleaseInput no_effect_context_axis_input(NoEffectContextAxis axis) {
    ReleaseInput input = canonical_input();
    set_unchanged_post_observation(input);
    switch (axis) {
    case NoEffectContextAxis::runtime_identity:
        input.after.identity.resident_id += 1;
        break;
    case NoEffectContextAxis::process_identity:
        input.after.process_identity.value += 1;
        break;
    case NoEffectContextAxis::weights_identity:
        input.after.weights_identity.value += 1;
        break;
    case NoEffectContextAxis::model_residency_identity:
        input.after.model_residency_identity.value += 1;
        break;
    case NoEffectContextAxis::pin:
        input.after.pin.value = !input.after.pin.value;
        break;
    }
    return input;
}

bool exact_no_effect_context_axis(
    const ReleaseInput& input, NoEffectContextAxis axis) {
    ReleaseInput unchanged = canonical_input();
    set_unchanged_post_observation(unchanged);
    RuntimeIdentity changed_runtime = unchanged.after.identity;
    changed_runtime.resident_id += 1;
    PhysicalIdentity changed_process = unchanged.after.process_identity;
    changed_process.value += 1;
    PhysicalIdentity changed_weights = unchanged.after.weights_identity;
    changed_weights.value += 1;
    PhysicalIdentity changed_model =
        unchanged.after.model_residency_identity;
    changed_model.value += 1;
    KnownBool changed_pin = unchanged.after.pin;
    changed_pin.value = !changed_pin.value;
    const bool runtime_same =
        same_identity(input.after.identity, unchanged.after.identity);
    const bool process_same = same_physical_identity(
        input.after.process_identity, unchanged.after.process_identity);
    const bool weights_same = same_physical_identity(
        input.after.weights_identity, unchanged.after.weights_identity);
    const bool model_same = same_physical_identity(
        input.after.model_residency_identity,
        unchanged.after.model_residency_identity);
    const bool pin_same = input.after.pin.known == unchanged.after.pin.known &&
                          input.after.pin.value == unchanged.after.pin.value;
    const bool numeric_same =
        same_measurement(input.after.cache_gtt, unchanged.after.cache_gtt) &&
        same_measurement(input.after.cache_host, unchanged.after.cache_host) &&
        same_measurement(
            input.after.global_gtt_headroom,
            unchanged.after.global_gtt_headroom) &&
        same_measurement(
            input.after.global_host_headroom,
            unchanged.after.global_host_headroom) &&
        same_measurement(
            input.unrelated_gtt_growth, unchanged.unrelated_gtt_growth) &&
        same_measurement(
            input.unrelated_host_growth, unchanged.unrelated_host_growth);
    switch (axis) {
    case NoEffectContextAxis::runtime_identity:
        return same_identity(input.after.identity, changed_runtime) &&
               process_same && weights_same && model_same && pin_same &&
               numeric_same;
    case NoEffectContextAxis::process_identity:
        return runtime_same &&
               same_physical_identity(
                   input.after.process_identity, changed_process) &&
               weights_same && model_same && pin_same && numeric_same;
    case NoEffectContextAxis::weights_identity:
        return runtime_same && process_same &&
               same_physical_identity(
                   input.after.weights_identity, changed_weights) &&
               model_same && pin_same && numeric_same;
    case NoEffectContextAxis::model_residency_identity:
        return runtime_same && process_same && weights_same &&
               same_physical_identity(
                   input.after.model_residency_identity, changed_model) &&
               pin_same && numeric_same;
    case NoEffectContextAxis::pin:
        return runtime_same && process_same && weights_same && model_same &&
               input.after.pin.known == changed_pin.known &&
               input.after.pin.value == changed_pin.value && numeric_same;
    }
    return false;
}

bool no_effect_context_axis_rejected(
    const ReleaseInput& input,
    const Verification& result,
    NoEffectContextAxis axis) {
    return exact_no_effect_context_axis(input, axis) &&
           exact_acknowledgement(input.request, input.acknowledgement) &&
           !valid_no_effect_context(input) &&
           result.evidence == Evidence::unknown &&
           result.disposition == Disposition::quarantine &&
           result.reason == UnknownReason::identity && result.dispatch_called &&
           !result.ledger_changed &&
           same_ledger(input.ledger_before, result.ledger_after) &&
           !result.residency_preserved && result.claims_maximized &&
           result.credited_gtt_bytes == 0 &&
           result.credited_host_bytes == 0;
}

ReportingFixture reporting_fixture(ReportingFault fault) {
    ReportingFixture fixture{
        exact_identity(),
        ReportingEvidence{
            true,
            true,
            true,
            true,
            true,
            exact_identity(),
            ReportingSample{true, 71, exact_identity()},
            ReportingSample{true, 71, exact_identity()},
        },
    };
    switch (fault) {
    case ReportingFault::none:
        break;
    case ReportingFault::missing:
        fixture.evidence.present = false;
        break;
    case ReportingFault::stale:
        fixture.evidence.fresh = false;
        break;
    case ReportingFault::skew:
        fixture.evidence.within_skew = false;
        break;
    case ReportingFault::unhealthy:
        fixture.evidence.healthy = false;
        break;
    case ReportingFault::incomplete:
        fixture.evidence.complete = false;
        break;
    case ReportingFault::incoherent_gtt_sample_missing:
        fixture.evidence.gtt_sample.present = false;
        break;
    case ReportingFault::incoherent_host_sample_missing:
        fixture.evidence.host_sample.present = false;
        break;
    case ReportingFault::incoherent_generation:
        fixture.evidence.host_sample.generation += 1;
        break;
    case ReportingFault::incoherent_identity:
        fixture.evidence.host_sample.identity.resident_id += 1;
        break;
    case ReportingFault::incoherent_generation_zero:
        fixture.evidence.gtt_sample.generation = 0;
        fixture.evidence.host_sample.generation = 0;
        break;
    case ReportingFault::incoherent_generation_overflow:
        fixture.evidence.gtt_sample.generation =
            std::numeric_limits<std::uint64_t>::max();
        fixture.evidence.host_sample.generation =
            std::numeric_limits<std::uint64_t>::max();
        break;
    case ReportingFault::incoherent_equal_invalid_identity:
        fixture.expected_target.resident_id = 0;
        fixture.evidence.affected_target.resident_id = 0;
        fixture.evidence.gtt_sample.identity.resident_id = 0;
        fixture.evidence.host_sample.identity.resident_id = 0;
        break;
    case ReportingFault::incoherent_affected_only_wrong_target:
        fixture.evidence.affected_target.resident_id += 1;
        break;
    case ReportingFault::incoherent_samples_only_wrong_target:
        fixture.evidence.gtt_sample.identity.resident_id += 1;
        fixture.evidence.host_sample.identity.resident_id += 1;
        break;
    case ReportingFault::incoherent_wrong_target:
        fixture.evidence.affected_target.resident_id += 1;
        fixture.evidence.gtt_sample.identity.resident_id += 1;
        fixture.evidence.host_sample.identity.resident_id += 1;
        break;
    }
    return fixture;
}

bool same_reporting_sample(
    const ReportingSample& left, const ReportingSample& right) {
    return left.present == right.present &&
           left.generation == right.generation &&
           same_identity(left.identity, right.identity);
}

bool same_reporting_evidence(
    const ReportingEvidence& left, const ReportingEvidence& right) {
    return left.present == right.present && left.fresh == right.fresh &&
           left.within_skew == right.within_skew &&
           left.healthy == right.healthy &&
           left.complete == right.complete &&
           same_identity(left.affected_target, right.affected_target) &&
           same_reporting_sample(left.gtt_sample, right.gtt_sample) &&
           same_reporting_sample(left.host_sample, right.host_sample);
}

bool same_reporting_fixture(
    const ReportingFixture& left, const ReportingFixture& right) {
    return same_identity(left.expected_target, right.expected_target) &&
           same_reporting_evidence(left.evidence, right.evidence);
}

bool reporting_fault_shape(
    ReportingFault fault, const ReportingFixture& fixture) {
    const ReportingFixture exact = reporting_fixture(ReportingFault::none);
    switch (fault) {
    case ReportingFault::none:
        return same_reporting_fixture(fixture, exact);
    case ReportingFault::missing: {
        ReportingFixture expected = exact;
        expected.evidence.present = false;
        return !fixture.evidence.present &&
               same_reporting_fixture(fixture, expected);
    }
    case ReportingFault::stale: {
        ReportingFixture expected = exact;
        expected.evidence.fresh = false;
        return !fixture.evidence.fresh &&
               same_reporting_fixture(fixture, expected);
    }
    case ReportingFault::skew: {
        ReportingFixture expected = exact;
        expected.evidence.within_skew = false;
        return !fixture.evidence.within_skew &&
               same_reporting_fixture(fixture, expected);
    }
    case ReportingFault::unhealthy: {
        ReportingFixture expected = exact;
        expected.evidence.healthy = false;
        return !fixture.evidence.healthy &&
               same_reporting_fixture(fixture, expected);
    }
    case ReportingFault::incomplete: {
        ReportingFixture expected = exact;
        expected.evidence.complete = false;
        return !fixture.evidence.complete &&
               same_reporting_fixture(fixture, expected);
    }
    case ReportingFault::incoherent_gtt_sample_missing: {
        ReportingFixture expected = exact;
        expected.evidence.gtt_sample.present = false;
        return !fixture.evidence.gtt_sample.present &&
               fixture.evidence.host_sample.present &&
               same_reporting_fixture(fixture, expected);
    }
    case ReportingFault::incoherent_host_sample_missing: {
        ReportingFixture expected = exact;
        expected.evidence.host_sample.present = false;
        return fixture.evidence.gtt_sample.present &&
               !fixture.evidence.host_sample.present &&
               same_reporting_fixture(fixture, expected);
    }
    case ReportingFault::incoherent_generation: {
        ReportingFixture expected = exact;
        expected.evidence.host_sample.generation += 1;
        return fixture.evidence.gtt_sample.generation ==
                   exact.evidence.gtt_sample.generation &&
               fixture.evidence.host_sample.generation ==
                   exact.evidence.host_sample.generation + 1 &&
               same_reporting_fixture(fixture, expected);
    }
    case ReportingFault::incoherent_identity: {
        ReportingFixture expected = exact;
        expected.evidence.host_sample.identity.resident_id += 1;
        return same_identity(
                   fixture.evidence.gtt_sample.identity,
                   exact.evidence.gtt_sample.identity) &&
               !same_identity(
                   fixture.evidence.host_sample.identity,
                   exact.evidence.host_sample.identity) &&
               same_reporting_fixture(fixture, expected);
    }
    case ReportingFault::incoherent_generation_zero: {
        ReportingFixture expected = exact;
        expected.evidence.gtt_sample.generation = 0;
        expected.evidence.host_sample.generation = 0;
        return fixture.evidence.gtt_sample.generation == 0 &&
               fixture.evidence.host_sample.generation == 0 &&
               exact.evidence.gtt_sample.generation != 0 &&
               same_reporting_fixture(fixture, expected);
    }
    case ReportingFault::incoherent_generation_overflow: {
        ReportingFixture expected = exact;
        expected.evidence.gtt_sample.generation =
            std::numeric_limits<std::uint64_t>::max();
        expected.evidence.host_sample.generation =
            std::numeric_limits<std::uint64_t>::max();
        return fixture.evidence.gtt_sample.generation ==
                   std::numeric_limits<std::uint64_t>::max() &&
               fixture.evidence.host_sample.generation ==
                   std::numeric_limits<std::uint64_t>::max() &&
               same_reporting_fixture(fixture, expected);
    }
    case ReportingFault::incoherent_equal_invalid_identity: {
        ReportingFixture expected = exact;
        expected.expected_target.resident_id = 0;
        expected.evidence.affected_target.resident_id = 0;
        expected.evidence.gtt_sample.identity.resident_id = 0;
        expected.evidence.host_sample.identity.resident_id = 0;
        return same_identity(fixture.expected_target,
                             fixture.evidence.affected_target) &&
               same_identity(fixture.expected_target,
                             fixture.evidence.gtt_sample.identity) &&
               same_identity(fixture.expected_target,
                             fixture.evidence.host_sample.identity) &&
               !valid_identity(fixture.expected_target) &&
               !valid_identity(fixture.evidence.affected_target) &&
               !valid_identity(fixture.evidence.gtt_sample.identity) &&
               !valid_identity(fixture.evidence.host_sample.identity) &&
               same_reporting_fixture(fixture, expected);
    }
    case ReportingFault::incoherent_affected_only_wrong_target: {
        ReportingFixture expected = exact;
        expected.evidence.affected_target.resident_id += 1;
        return valid_identity(fixture.expected_target) &&
               valid_identity(fixture.evidence.affected_target) &&
               !same_identity(fixture.evidence.affected_target,
                              fixture.expected_target) &&
               same_identity(fixture.evidence.gtt_sample.identity,
                             fixture.expected_target) &&
               same_identity(fixture.evidence.host_sample.identity,
                             fixture.expected_target) &&
               same_reporting_fixture(fixture, expected);
    }
    case ReportingFault::incoherent_samples_only_wrong_target: {
        ReportingFixture expected = exact;
        expected.evidence.gtt_sample.identity.resident_id += 1;
        expected.evidence.host_sample.identity.resident_id += 1;
        return same_identity(fixture.evidence.affected_target,
                             fixture.expected_target) &&
               valid_identity(fixture.evidence.gtt_sample.identity) &&
               valid_identity(fixture.evidence.host_sample.identity) &&
               same_identity(fixture.evidence.gtt_sample.identity,
                             fixture.evidence.host_sample.identity) &&
               !same_identity(fixture.evidence.gtt_sample.identity,
                              fixture.expected_target) &&
               same_reporting_fixture(fixture, expected);
    }
    case ReportingFault::incoherent_wrong_target: {
        ReportingFixture expected = exact;
        expected.evidence.affected_target.resident_id += 1;
        expected.evidence.gtt_sample.identity.resident_id += 1;
        expected.evidence.host_sample.identity.resident_id += 1;
        return valid_identity(fixture.expected_target) &&
               valid_identity(fixture.evidence.affected_target) &&
               same_identity(fixture.evidence.affected_target,
                             fixture.evidence.gtt_sample.identity) &&
               same_identity(fixture.evidence.affected_target,
                             fixture.evidence.host_sample.identity) &&
               !same_identity(fixture.evidence.affected_target,
                              fixture.expected_target) &&
               same_reporting_fixture(fixture, expected);
    }
    }
    return false;
}

bool coherent_reporting_evidence(const ReportingEvidence& evidence) {
    return evidence.within_skew && evidence.complete &&
           evidence.gtt_sample.present && evidence.host_sample.present &&
           evidence.gtt_sample.generation ==
               evidence.host_sample.generation &&
           same_identity(
               evidence.gtt_sample.identity, evidence.host_sample.identity);
}

bool valid_reporting_evidence(
    const ReportingEvidence& evidence,
    const RuntimeIdentity& expected_target) {
    const ArithmeticResult next_gtt_generation =
        checked_increment(evidence.gtt_sample.generation);
    const ArithmeticResult next_host_generation =
        checked_increment(evidence.host_sample.generation);
    return evidence.present && evidence.fresh && evidence.healthy &&
           coherent_reporting_evidence(evidence) &&
           evidence.gtt_sample.generation != 0 &&
           evidence.host_sample.generation != 0 &&
           next_gtt_generation.known && next_host_generation.known &&
           valid_identity(expected_target) &&
           valid_identity(evidence.affected_target) &&
           valid_identity(evidence.gtt_sample.identity) &&
           valid_identity(evidence.host_sample.identity) &&
           same_identity(evidence.affected_target, expected_target) &&
           same_identity(evidence.gtt_sample.identity, expected_target) &&
           same_identity(evidence.host_sample.identity, expected_target);
}

bool reporting_fault_shapes_pass() {
    constexpr std::array<ReportingFault, 16> faults = {
        ReportingFault::none,
        ReportingFault::missing,
        ReportingFault::stale,
        ReportingFault::skew,
        ReportingFault::unhealthy,
        ReportingFault::incomplete,
        ReportingFault::incoherent_gtt_sample_missing,
        ReportingFault::incoherent_host_sample_missing,
        ReportingFault::incoherent_generation,
        ReportingFault::incoherent_identity,
        ReportingFault::incoherent_generation_zero,
        ReportingFault::incoherent_generation_overflow,
        ReportingFault::incoherent_equal_invalid_identity,
        ReportingFault::incoherent_affected_only_wrong_target,
        ReportingFault::incoherent_samples_only_wrong_target,
        ReportingFault::incoherent_wrong_target,
    };
    for (const ReportingFault fault : faults) {
        const ReportingFixture fixture = reporting_fixture(fault);
        if (!reporting_fault_shape(fault, fixture)) {
            return false;
        }
        const bool valid = valid_reporting_evidence(fixture.evidence,
                                                    fixture.expected_target);
        if (fault == ReportingFault::none && !valid) {
            return false;
        }
        if (fault != ReportingFault::none && valid) {
            return false;
        }
    }
    return true;
}

PressureMode select_pressure_mode(
    const ReportingEvidence& evidence,
    const RuntimeIdentity& expected_target);

ReportingFixture reporting_identity_topology_fixture(
    IdentityAxis axis, ReportingTopology topology) {
    ReportingFixture fixture = reporting_fixture(ReportingFault::none);
    RuntimeIdentity invalid = exact_identity();
    invalidate_identity_axis(invalid, axis);
    RuntimeIdentity changed = exact_identity();
    change_identity_axis(changed, axis);
    switch (topology) {
    case ReportingTopology::all_equal_invalid:
        fixture.evidence.affected_target = invalid;
        fixture.evidence.gtt_sample.identity = invalid;
        fixture.evidence.host_sample.identity = invalid;
        break;
    case ReportingTopology::sample_to_sample_mismatch:
        fixture.evidence.host_sample.identity = changed;
        break;
    case ReportingTopology::affected_only_wrong_target:
        fixture.evidence.affected_target = changed;
        break;
    case ReportingTopology::coherent_samples_only_wrong_target:
        fixture.evidence.gtt_sample.identity = changed;
        fixture.evidence.host_sample.identity = changed;
        break;
    case ReportingTopology::all_three_wrong_target:
        fixture.evidence.affected_target = changed;
        fixture.evidence.gtt_sample.identity = changed;
        fixture.evidence.host_sample.identity = changed;
        break;
    }
    return fixture;
}

bool reporting_identity_topology_shape(
    IdentityAxis axis,
    ReportingTopology topology,
    const ReportingFixture& fixture) {
    ReportingFixture expected = reporting_fixture(ReportingFault::none);
    RuntimeIdentity invalid = exact_identity();
    invalidate_identity_axis(invalid, axis);
    RuntimeIdentity changed = exact_identity();
    change_identity_axis(changed, axis);
    switch (topology) {
    case ReportingTopology::all_equal_invalid:
        expected.evidence.affected_target = invalid;
        expected.evidence.gtt_sample.identity = invalid;
        expected.evidence.host_sample.identity = invalid;
        return valid_identity(fixture.expected_target) &&
               !valid_identity(fixture.evidence.affected_target) &&
               same_identity(fixture.evidence.affected_target,
                             fixture.evidence.gtt_sample.identity) &&
               same_identity(fixture.evidence.affected_target,
                             fixture.evidence.host_sample.identity) &&
               !same_identity(fixture.evidence.affected_target,
                              fixture.expected_target) &&
               same_reporting_fixture(fixture, expected);
    case ReportingTopology::sample_to_sample_mismatch:
        expected.evidence.host_sample.identity = changed;
        return same_identity(fixture.evidence.affected_target,
                             fixture.expected_target) &&
               same_identity(fixture.evidence.gtt_sample.identity,
                             fixture.expected_target) &&
               !same_identity(fixture.evidence.host_sample.identity,
                              fixture.evidence.gtt_sample.identity) &&
               same_reporting_fixture(fixture, expected);
    case ReportingTopology::affected_only_wrong_target:
        expected.evidence.affected_target = changed;
        return !same_identity(fixture.evidence.affected_target,
                              fixture.expected_target) &&
               same_identity(fixture.evidence.gtt_sample.identity,
                             fixture.expected_target) &&
               same_identity(fixture.evidence.host_sample.identity,
                             fixture.expected_target) &&
               same_reporting_fixture(fixture, expected);
    case ReportingTopology::coherent_samples_only_wrong_target:
        expected.evidence.gtt_sample.identity = changed;
        expected.evidence.host_sample.identity = changed;
        return same_identity(fixture.evidence.affected_target,
                             fixture.expected_target) &&
               same_identity(fixture.evidence.gtt_sample.identity,
                             fixture.evidence.host_sample.identity) &&
               !same_identity(fixture.evidence.gtt_sample.identity,
                              fixture.expected_target) &&
               same_reporting_fixture(fixture, expected);
    case ReportingTopology::all_three_wrong_target:
        expected.evidence.affected_target = changed;
        expected.evidence.gtt_sample.identity = changed;
        expected.evidence.host_sample.identity = changed;
        return same_identity(fixture.evidence.affected_target,
                             fixture.evidence.gtt_sample.identity) &&
               same_identity(fixture.evidence.affected_target,
                             fixture.evidence.host_sample.identity) &&
               !same_identity(fixture.evidence.affected_target,
                              fixture.expected_target) &&
               same_reporting_fixture(fixture, expected);
    }
    return false;
}

bool reporting_identity_topology_matrix_passes() {
    constexpr std::array<IdentityAxis, 16> axes = {
        IdentityAxis::device_identity,
        IdentityAxis::backend_artifact_digest,
        IdentityAxis::source_build_dependency_closure,
        IdentityAxis::driver_runtime_closure,
        IdentityAxis::model_manifest_digest,
        IdentityAxis::normalized_configuration_digest,
        IdentityAxis::evidence_index_digest,
        IdentityAxis::evidence_liveness_lease,
        IdentityAxis::evidence_liveness_present,
        IdentityAxis::evidence_liveness_valid,
        IdentityAxis::resident_id,
        IdentityAxis::resident_generation,
        IdentityAxis::backend_instance_birth_token,
        IdentityAxis::topology_generation,
        IdentityAxis::allocation_group_id,
        IdentityAxis::observation_contract_digest,
    };
    constexpr std::array<ReportingTopology, 5> topologies = {
        ReportingTopology::all_equal_invalid,
        ReportingTopology::sample_to_sample_mismatch,
        ReportingTopology::affected_only_wrong_target,
        ReportingTopology::coherent_samples_only_wrong_target,
        ReportingTopology::all_three_wrong_target,
    };
    for (const IdentityAxis axis : axes) {
        for (const ReportingTopology topology : topologies) {
            const ReportingFixture fixture =
                reporting_identity_topology_fixture(axis, topology);
            if (!reporting_identity_topology_shape(axis, topology, fixture) ||
                valid_reporting_evidence(
                    fixture.evidence, fixture.expected_target) ||
                select_pressure_mode(
                    fixture.evidence, fixture.expected_target) !=
                    PressureMode::disabled_invalid_evidence) {
                return false;
            }
        }
    }
    return true;
}

PressureMode select_pressure_mode(
    const ReportingEvidence& evidence,
    const RuntimeIdentity& expected_target) {
    return valid_reporting_evidence(evidence, expected_target)
               ? PressureMode::report_only
               : PressureMode::disabled_invalid_evidence;
}

const char* pressure_mode_name(PressureMode mode) {
    return mode == PressureMode::report_only ? "report_only"
                                              : "disabled_invalid_evidence";
}

const char* select_fallback(FallbackProfile profile, PressureMode mode) {
    if (profile == FallbackProfile::rocm) {
        return mode == PressureMode::report_only
                   ? "hatchery_rocm_pressure_report_only_v1"
                   : "hatchery_rocm_pressure_disabled_invalid_evidence_v1";
    }
    return mode == PressureMode::report_only
               ? "residency_pressure_report_only_unvalidated_v1"
               : "residency_pressure_disabled_invalid_evidence_v1";
}

bool neutral_verification_fields(const Verification& result) {
    return result.owned_gtt_release_bytes == 0 &&
           result.owned_host_release_bytes == 0 &&
           result.global_gtt_improvement_bytes == 0 &&
           result.global_host_improvement_bytes == 0 &&
           result.credit_before_ack_bytes == 0 &&
           result.credit_before_physical_bytes == 0 &&
           result.credited_gtt_bytes == 0 &&
           result.credited_host_bytes == 0 &&
           result.removed_group_count == 0;
}

bool same_action_lease(const ActionLease& left, const ActionLease& right) {
    return left.present == right.present &&
           left.active_use == right.active_use &&
           left.action_lease == right.action_lease &&
           left.state == right.state && left.operation == right.operation &&
           same_slot_set(left.slot_set, right.slot_set) &&
           left.claim_generation == right.claim_generation &&
           left.pre_observation_generation ==
               right.pre_observation_generation &&
           left.observation_contract_digest ==
               right.observation_contract_digest &&
           same_identity(left.identity, right.identity);
}

bool same_acknowledgement(
    const Acknowledgement& left, const Acknowledgement& right) {
    return left.present == right.present && left.complete == right.complete &&
           left.count == right.count && left.ids[0] == right.ids[0] &&
           left.ids[1] == right.ids[1];
}

bool same_physical_observation(
    const PhysicalObservation& left,
    const PhysicalObservation& right) {
    return left.present == right.present && left.fresh == right.fresh &&
           left.within_skew == right.within_skew &&
           left.healthy == right.healthy &&
           left.complete == right.complete &&
           left.generation == right.generation &&
           same_identity(left.identity, right.identity) &&
           same_measurement(left.cache_gtt, right.cache_gtt) &&
           same_measurement(left.cache_host, right.cache_host) &&
           same_measurement(
               left.global_gtt_headroom, right.global_gtt_headroom) &&
           same_measurement(
               left.global_host_headroom, right.global_host_headroom) &&
           same_physical_identity(
               left.process_identity, right.process_identity) &&
           same_physical_identity(
               left.weights_identity, right.weights_identity) &&
           same_physical_identity(
               left.model_residency_identity,
               right.model_residency_identity) &&
           left.pin.known == right.pin.known &&
           left.pin.value == right.pin.value;
}

bool same_unsupported_release_context(
    const ReleaseInput& left, const ReleaseInput& right) {
    return left.mechanism == right.mechanism &&
           same_identity(left.resident, right.resident) &&
           left.requested_action_lease == right.requested_action_lease &&
           same_action_lease(left.lease, right.lease) &&
           same_slot_set(left.request, right.request) &&
           left.dispatch_attempted == right.dispatch_attempted &&
           left.dispatch_completed == right.dispatch_completed &&
           same_acknowledgement(
               left.acknowledgement, right.acknowledgement) &&
           same_measurement(
               left.unrelated_gtt_growth, right.unrelated_gtt_growth) &&
           same_measurement(
               left.unrelated_host_growth, right.unrelated_host_growth) &&
           left.maximum_gtt_release_bytes ==
               right.maximum_gtt_release_bytes &&
           left.maximum_host_release_bytes ==
               right.maximum_host_release_bytes &&
           same_ledger(left.ledger_before, right.ledger_before) &&
           left.ledger_commit_generation ==
               right.ledger_commit_generation &&
           left.ledger_commit_succeeds == right.ledger_commit_succeeds;
}

bool equal_invalid_axis(
    UnsupportedPreObservationFault fault, IdentityAxis& axis) {
    switch (fault) {
    case UnsupportedPreObservationFault::equal_invalid_device_identity:
        axis = IdentityAxis::device_identity;
        return true;
    case UnsupportedPreObservationFault::equal_invalid_backend_artifact_digest:
        axis = IdentityAxis::backend_artifact_digest;
        return true;
    case UnsupportedPreObservationFault::equal_invalid_source_build_dependency_closure:
        axis = IdentityAxis::source_build_dependency_closure;
        return true;
    case UnsupportedPreObservationFault::equal_invalid_driver_runtime_closure:
        axis = IdentityAxis::driver_runtime_closure;
        return true;
    case UnsupportedPreObservationFault::equal_invalid_model_manifest_digest:
        axis = IdentityAxis::model_manifest_digest;
        return true;
    case UnsupportedPreObservationFault::equal_invalid_normalized_configuration_digest:
        axis = IdentityAxis::normalized_configuration_digest;
        return true;
    case UnsupportedPreObservationFault::equal_invalid_evidence_index_digest:
        axis = IdentityAxis::evidence_index_digest;
        return true;
    case UnsupportedPreObservationFault::equal_invalid_evidence_liveness_lease:
        axis = IdentityAxis::evidence_liveness_lease;
        return true;
    case UnsupportedPreObservationFault::equal_invalid_evidence_liveness_present:
        axis = IdentityAxis::evidence_liveness_present;
        return true;
    case UnsupportedPreObservationFault::equal_invalid_evidence_liveness_valid:
        axis = IdentityAxis::evidence_liveness_valid;
        return true;
    case UnsupportedPreObservationFault::equal_invalid_resident_id:
        axis = IdentityAxis::resident_id;
        return true;
    case UnsupportedPreObservationFault::equal_invalid_resident_generation:
        axis = IdentityAxis::resident_generation;
        return true;
    case UnsupportedPreObservationFault::equal_invalid_backend_instance_birth_token:
        axis = IdentityAxis::backend_instance_birth_token;
        return true;
    case UnsupportedPreObservationFault::equal_invalid_topology_generation:
        axis = IdentityAxis::topology_generation;
        return true;
    case UnsupportedPreObservationFault::equal_invalid_allocation_group_id:
        axis = IdentityAxis::allocation_group_id;
        return true;
    case UnsupportedPreObservationFault::equal_invalid_observation_contract_digest:
        axis = IdentityAxis::observation_contract_digest;
        return true;
    default:
        return false;
    }
}

bool resident_before_mismatch_axis(
    UnsupportedPreObservationFault fault, IdentityAxis& axis) {
    switch (fault) {
    case UnsupportedPreObservationFault::resident_before_device_identity_mismatch:
        axis = IdentityAxis::device_identity;
        return true;
    case UnsupportedPreObservationFault::resident_before_backend_artifact_digest_mismatch:
        axis = IdentityAxis::backend_artifact_digest;
        return true;
    case UnsupportedPreObservationFault::resident_before_source_build_dependency_closure_mismatch:
        axis = IdentityAxis::source_build_dependency_closure;
        return true;
    case UnsupportedPreObservationFault::resident_before_driver_runtime_closure_mismatch:
        axis = IdentityAxis::driver_runtime_closure;
        return true;
    case UnsupportedPreObservationFault::resident_before_model_manifest_digest_mismatch:
        axis = IdentityAxis::model_manifest_digest;
        return true;
    case UnsupportedPreObservationFault::resident_before_normalized_configuration_digest_mismatch:
        axis = IdentityAxis::normalized_configuration_digest;
        return true;
    case UnsupportedPreObservationFault::resident_before_evidence_index_digest_mismatch:
        axis = IdentityAxis::evidence_index_digest;
        return true;
    case UnsupportedPreObservationFault::resident_before_evidence_liveness_lease_mismatch:
        axis = IdentityAxis::evidence_liveness_lease;
        return true;
    case UnsupportedPreObservationFault::resident_before_evidence_liveness_present_mismatch:
        axis = IdentityAxis::evidence_liveness_present;
        return true;
    case UnsupportedPreObservationFault::resident_before_evidence_liveness_valid_mismatch:
        axis = IdentityAxis::evidence_liveness_valid;
        return true;
    case UnsupportedPreObservationFault::resident_before_resident_id_mismatch:
        axis = IdentityAxis::resident_id;
        return true;
    case UnsupportedPreObservationFault::resident_before_resident_generation_mismatch:
        axis = IdentityAxis::resident_generation;
        return true;
    case UnsupportedPreObservationFault::resident_before_backend_instance_birth_token_mismatch:
        axis = IdentityAxis::backend_instance_birth_token;
        return true;
    case UnsupportedPreObservationFault::resident_before_topology_generation_mismatch:
        axis = IdentityAxis::topology_generation;
        return true;
    case UnsupportedPreObservationFault::resident_before_allocation_group_id_mismatch:
        axis = IdentityAxis::allocation_group_id;
        return true;
    case UnsupportedPreObservationFault::resident_before_observation_contract_digest_mismatch:
        axis = IdentityAxis::observation_contract_digest;
        return true;
    default:
        return false;
    }
}

void apply_unsupported_pre_observation_fault(
    ReleaseInput& input, UnsupportedPreObservationFault fault) {
    IdentityAxis axis = IdentityAxis::device_identity;
    if (equal_invalid_axis(fault, axis)) {
        invalidate_identity_axis(input.resident, axis);
        invalidate_identity_axis(input.lease.identity, axis);
        invalidate_identity_axis(input.before.identity, axis);
        return;
    }
    if (resident_before_mismatch_axis(fault, axis)) {
        change_identity_axis(input.before.identity, axis);
        return;
    }
    switch (fault) {
    case UnsupportedPreObservationFault::none:
        break;
    case UnsupportedPreObservationFault::missing:
        input.before.present = false;
        break;
    case UnsupportedPreObservationFault::stale:
        input.before.fresh = false;
        break;
    case UnsupportedPreObservationFault::skew:
        input.before.within_skew = false;
        break;
    case UnsupportedPreObservationFault::unhealthy:
        input.before.healthy = false;
        break;
    case UnsupportedPreObservationFault::incomplete:
        input.before.complete = false;
        break;
    case UnsupportedPreObservationFault::cache_gtt_missing:
        input.before.cache_gtt.present = false;
        break;
    case UnsupportedPreObservationFault::cache_host_missing:
        input.before.cache_host.present = false;
        break;
    case UnsupportedPreObservationFault::generation_zero:
        input.before.generation = 0;
        break;
    case UnsupportedPreObservationFault::global_gtt_headroom_missing:
        input.before.global_gtt_headroom.present = false;
        break;
    case UnsupportedPreObservationFault::global_host_headroom_missing:
        input.before.global_host_headroom.present = false;
        break;
    case UnsupportedPreObservationFault::generation_overflow_to_zero:
        input.before.generation =
            std::numeric_limits<std::uint64_t>::max();
        input.lease.pre_observation_generation = input.before.generation;
        break;
    case UnsupportedPreObservationFault::process_identity_missing:
        input.before.process_identity.present = false;
        break;
    case UnsupportedPreObservationFault::process_identity_zero:
        input.before.process_identity.value = 0;
        break;
    case UnsupportedPreObservationFault::weights_identity_missing:
        input.before.weights_identity.present = false;
        break;
    case UnsupportedPreObservationFault::weights_identity_zero:
        input.before.weights_identity.value = 0;
        break;
    case UnsupportedPreObservationFault::model_residency_identity_missing:
        input.before.model_residency_identity.present = false;
        break;
    case UnsupportedPreObservationFault::model_residency_identity_zero:
        input.before.model_residency_identity.value = 0;
        break;
    case UnsupportedPreObservationFault::pin_unknown:
        input.before.pin.known = false;
        break;
    default:
        break;
    }
}

ReleaseInput unsupported_pre_observation_input(
    Mechanism mechanism, UnsupportedPreObservationFault fault) {
    ReleaseInput input = canonical_input();
    input.mechanism = mechanism;
    input.dispatch_attempted = false;
    input.dispatch_completed = false;
    apply_unsupported_pre_observation_fault(input, fault);
    set_unchanged_post_observation(input);
    if (fault ==
        UnsupportedPreObservationFault::generation_overflow_to_zero) {
        input.after.generation = 0;
    }
    return input;
}

bool unsupported_pre_observation_fault_shape(
    const ReleaseInput& input,
    Mechanism mechanism,
    UnsupportedPreObservationFault fault) {
    ReleaseInput expected = canonical_input();
    expected.mechanism = mechanism;
    expected.dispatch_attempted = false;
    expected.dispatch_completed = false;
    apply_unsupported_pre_observation_fault(expected, fault);
    set_unchanged_post_observation(expected);
    if (fault ==
        UnsupportedPreObservationFault::generation_overflow_to_zero) {
        expected.after.generation = 0;
    }
    return !input.dispatch_attempted && !input.dispatch_completed &&
           same_unsupported_release_context(input, expected) &&
           same_physical_observation(input.before, expected.before) &&
           same_physical_observation(input.after, expected.after);
}

bool invalid_reporting_pre_observation(
    UnsupportedPreObservationFault fault) {
    switch (fault) {
    case UnsupportedPreObservationFault::missing:
    case UnsupportedPreObservationFault::stale:
    case UnsupportedPreObservationFault::skew:
    case UnsupportedPreObservationFault::unhealthy:
    case UnsupportedPreObservationFault::incomplete:
    case UnsupportedPreObservationFault::cache_gtt_missing:
    case UnsupportedPreObservationFault::cache_host_missing:
    case UnsupportedPreObservationFault::generation_zero:
        return true;
    default:
        return false;
    }
}

UnknownReason unsupported_pre_observation_reason(
    UnsupportedPreObservationFault fault) {
    switch (fault) {
    case UnsupportedPreObservationFault::global_gtt_headroom_missing:
    case UnsupportedPreObservationFault::global_host_headroom_missing:
        return UnknownReason::observation;
    case UnsupportedPreObservationFault::generation_overflow_to_zero:
        return UnknownReason::observation_generation;
    default:
        return UnknownReason::identity;
    }
}

bool unsupported_pre_observation_residency_preserved(
    UnsupportedPreObservationFault fault) {
    return fault == UnsupportedPreObservationFault::none ||
           invalid_reporting_pre_observation(fault) ||
           fault ==
               UnsupportedPreObservationFault::global_gtt_headroom_missing ||
           fault ==
               UnsupportedPreObservationFault::global_host_headroom_missing;
}

bool valid_unsupported_pre_observation(const ReleaseInput& input) {
    const ArithmeticResult next_generation =
        checked_increment(input.before.generation);
    return valid_observation(input.before) &&
           same_identity(input.resident, input.before.identity) &&
           input.before.generation != 0 && next_generation.known;
}

PressureMode select_unsupported_pressure_mode(
    const ReleaseInput& input, const ReportingEvidence& evidence) {
    if (!valid_unsupported_pre_observation(input)) {
        return PressureMode::disabled_invalid_evidence;
    }
    return select_pressure_mode(evidence, input.resident);
}

UnsupportedObservation evaluate_unsupported(
    Mechanism mechanism,
    ReportingFault reporting_fault,
    UnsupportedPreObservationFault requested_observation_fault) {
    const UnsupportedPreObservationFault observation_fault =
        requested_observation_fault;
    const ReleaseInput input =
        unsupported_pre_observation_input(mechanism, observation_fault);
    const ReportingFixture reporting = reporting_fixture(reporting_fault);
    const Verification result = verify_soft_release(input);
    return UnsupportedObservation{
        select_unsupported_pressure_mode(input, reporting.evidence),
        result.evidence,
        result.disposition,
        result.reason,
        result.claims_maximized,
        input.mechanism != Mechanism::llamacpp_slot_cache_erase_v1,
        !result.dispatch_called,
        result.post_observation_available,
        result.post_observation_unchanged_fresh,
        !result.ledger_changed &&
            same_ledger(input.ledger_before, result.ledger_after),
        result.residency_preserved,
        neutral_verification_fields(result),
    };
}

bool unsupported_branch_passes(
    Mechanism mechanism,
    ReportingFault reporting_fault,
    PressureMode expected_mode) {
    const UnsupportedObservation observation =
        evaluate_unsupported(
            mechanism,
            reporting_fault,
            UnsupportedPreObservationFault::none);
    return observation.mode == expected_mode &&
           observation.evidence == Evidence::unsupported &&
           observation.disposition == Disposition::fallback &&
           observation.reason == UnknownReason::none &&
           !observation.claims_maximized && observation.detected &&
           observation.no_dispatch && observation.post_observation_available &&
           observation.post_observation_unchanged_fresh &&
           observation.ledger_unchanged && observation.residency_preserved &&
           observation.neutral_fields;
}

bool unsupported_pair_passes(
    ReportingFault reporting_fault, PressureMode expected_mode) {
    return unsupported_branch_passes(
               Mechanism::absent, reporting_fault, expected_mode) &&
           unsupported_branch_passes(
               Mechanism::successful_noop,
               reporting_fault,
               expected_mode);
}

bool unsupported_pre_observation_matrix_passes() {
    constexpr std::array<UnsupportedPreObservationFault, 50> faults = {
        UnsupportedPreObservationFault::missing,
        UnsupportedPreObservationFault::stale,
        UnsupportedPreObservationFault::skew,
        UnsupportedPreObservationFault::unhealthy,
        UnsupportedPreObservationFault::incomplete,
        UnsupportedPreObservationFault::cache_gtt_missing,
        UnsupportedPreObservationFault::cache_host_missing,
        UnsupportedPreObservationFault::generation_zero,
        UnsupportedPreObservationFault::global_gtt_headroom_missing,
        UnsupportedPreObservationFault::global_host_headroom_missing,
        UnsupportedPreObservationFault::generation_overflow_to_zero,
        UnsupportedPreObservationFault::equal_invalid_device_identity,
        UnsupportedPreObservationFault::equal_invalid_backend_artifact_digest,
        UnsupportedPreObservationFault::equal_invalid_source_build_dependency_closure,
        UnsupportedPreObservationFault::equal_invalid_driver_runtime_closure,
        UnsupportedPreObservationFault::equal_invalid_model_manifest_digest,
        UnsupportedPreObservationFault::equal_invalid_normalized_configuration_digest,
        UnsupportedPreObservationFault::equal_invalid_evidence_index_digest,
        UnsupportedPreObservationFault::equal_invalid_evidence_liveness_lease,
        UnsupportedPreObservationFault::equal_invalid_evidence_liveness_present,
        UnsupportedPreObservationFault::equal_invalid_evidence_liveness_valid,
        UnsupportedPreObservationFault::equal_invalid_resident_id,
        UnsupportedPreObservationFault::equal_invalid_resident_generation,
        UnsupportedPreObservationFault::equal_invalid_backend_instance_birth_token,
        UnsupportedPreObservationFault::equal_invalid_topology_generation,
        UnsupportedPreObservationFault::equal_invalid_allocation_group_id,
        UnsupportedPreObservationFault::equal_invalid_observation_contract_digest,
        UnsupportedPreObservationFault::resident_before_device_identity_mismatch,
        UnsupportedPreObservationFault::resident_before_backend_artifact_digest_mismatch,
        UnsupportedPreObservationFault::resident_before_source_build_dependency_closure_mismatch,
        UnsupportedPreObservationFault::resident_before_driver_runtime_closure_mismatch,
        UnsupportedPreObservationFault::resident_before_model_manifest_digest_mismatch,
        UnsupportedPreObservationFault::resident_before_normalized_configuration_digest_mismatch,
        UnsupportedPreObservationFault::resident_before_evidence_index_digest_mismatch,
        UnsupportedPreObservationFault::resident_before_evidence_liveness_lease_mismatch,
        UnsupportedPreObservationFault::resident_before_evidence_liveness_present_mismatch,
        UnsupportedPreObservationFault::resident_before_evidence_liveness_valid_mismatch,
        UnsupportedPreObservationFault::resident_before_resident_id_mismatch,
        UnsupportedPreObservationFault::resident_before_resident_generation_mismatch,
        UnsupportedPreObservationFault::resident_before_backend_instance_birth_token_mismatch,
        UnsupportedPreObservationFault::resident_before_topology_generation_mismatch,
        UnsupportedPreObservationFault::resident_before_allocation_group_id_mismatch,
        UnsupportedPreObservationFault::resident_before_observation_contract_digest_mismatch,
        UnsupportedPreObservationFault::process_identity_missing,
        UnsupportedPreObservationFault::process_identity_zero,
        UnsupportedPreObservationFault::weights_identity_missing,
        UnsupportedPreObservationFault::weights_identity_zero,
        UnsupportedPreObservationFault::model_residency_identity_missing,
        UnsupportedPreObservationFault::model_residency_identity_zero,
        UnsupportedPreObservationFault::pin_unknown,
    };
    constexpr std::array<Mechanism, 2> mechanisms = {
        Mechanism::absent,
        Mechanism::successful_noop,
    };
    for (const Mechanism mechanism : mechanisms) {
        const ReleaseInput control = unsupported_pre_observation_input(
            mechanism, UnsupportedPreObservationFault::none);
        const UnsupportedObservation control_observation =
            evaluate_unsupported(
                mechanism,
                ReportingFault::none,
                UnsupportedPreObservationFault::none);
        if (!unsupported_pre_observation_fault_shape(
                control,
                mechanism,
                UnsupportedPreObservationFault::none) ||
            control_observation.mode != PressureMode::report_only ||
            control_observation.evidence != Evidence::unsupported ||
            control_observation.disposition != Disposition::fallback ||
            control_observation.reason != UnknownReason::none ||
            control_observation.claims_maximized ||
            !control_observation.detected ||
            !control_observation.no_dispatch ||
            !control_observation.post_observation_available ||
            !control_observation.post_observation_unchanged_fresh ||
            !control_observation.ledger_unchanged ||
            !control_observation.residency_preserved ||
            !control_observation.neutral_fields) {
            return false;
        }
        for (const UnsupportedPreObservationFault fault : faults) {
            const ReleaseInput input = unsupported_pre_observation_input(
                mechanism, fault);
            const UnsupportedObservation observation = evaluate_unsupported(
                mechanism, ReportingFault::none, fault);
            const bool reporting_fault =
                invalid_reporting_pre_observation(fault);
            const Evidence expected_evidence =
                reporting_fault ? Evidence::unsupported : Evidence::unknown;
            const Disposition expected_disposition =
                reporting_fault ? Disposition::fallback
                                : Disposition::quarantine;
            const UnknownReason expected_reason =
                reporting_fault
                    ? UnknownReason::none
                    : unsupported_pre_observation_reason(fault);
            const bool expected_claims_maximized = !reporting_fault;
            const bool expected_residency =
                unsupported_pre_observation_residency_preserved(fault);
            if (!unsupported_pre_observation_fault_shape(input,
                                                         mechanism,
                                                         fault) ||
                observation.mode != PressureMode::disabled_invalid_evidence ||
                observation.evidence != expected_evidence ||
                observation.disposition != expected_disposition ||
                observation.reason != expected_reason ||
                observation.claims_maximized != expected_claims_maximized ||
                !observation.detected ||
                !observation.no_dispatch ||
                !observation.post_observation_available ||
                observation.post_observation_unchanged_fresh !=
                    unchanged_fresh_observation(input) ||
                !observation.ledger_unchanged ||
                observation.residency_preserved != expected_residency ||
                !observation.neutral_fields) {
                return false;
            }
        }
    }
    return true;
}

auto input_with_fault(Fault fault) -> ReleaseInput;

bool neutral_verification_witnesses_pass() {
    const Verification rejected_with_post = verify_soft_release(
        input_with_fault(Fault::slot_set_missing));
    const Verification rejected_without_post = verify_soft_release(
        input_with_fault(Fault::dispatch_failure));
    const Verification intact = verify_soft_release(
        input_with_fault(Fault::ack_without_physical_delta));
    const UnsupportedObservation fallback_absent = evaluate_unsupported(
        Mechanism::absent,
        ReportingFault::none,
        UnsupportedPreObservationFault::none);
    const UnsupportedObservation fallback_noop = evaluate_unsupported(
        Mechanism::successful_noop,
        ReportingFault::none,
        UnsupportedPreObservationFault::none);
    return neutral_verification_fields(rejected_with_post) &&
           neutral_verification_fields(rejected_without_post) &&
           neutral_verification_fields(intact) &&
           fallback_absent.neutral_fields && fallback_noop.neutral_fields &&
           rejected_with_post.post_observation_available &&
           !rejected_without_post.post_observation_available &&
           intact.post_observation_available &&
           fallback_absent.post_observation_available &&
           fallback_noop.post_observation_available &&
           rejected_with_post.evidence == Evidence::unknown &&
           rejected_with_post.disposition == Disposition::verified_intact &&
           rejected_without_post.evidence == Evidence::unknown &&
           rejected_without_post.disposition == Disposition::quarantine &&
           intact.evidence == Evidence::verified_intact &&
           intact.disposition == Disposition::verified_intact &&
           fallback_absent.detected && fallback_noop.detected;
}

void zero_identity_token(
    RuntimeIdentity& identity, IdentityToken token) {
    switch (token) {
    case IdentityToken::device_identity:
        identity.bindings.device_identity = 0;
        break;
    case IdentityToken::backend_artifact_digest:
        identity.bindings.backend_artifact_digest = 0;
        break;
    case IdentityToken::source_build_dependency_closure:
        identity.bindings.source_build_dependency_closure = 0;
        break;
    case IdentityToken::driver_runtime_closure:
        identity.bindings.driver_runtime_closure = 0;
        break;
    case IdentityToken::model_manifest_digest:
        identity.bindings.model_manifest_digest = 0;
        break;
    case IdentityToken::normalized_configuration_digest:
        identity.bindings.normalized_configuration_digest = 0;
        break;
    case IdentityToken::evidence_index_digest:
        identity.bindings.evidence_index_digest = 0;
        break;
    case IdentityToken::evidence_liveness_lease:
        identity.bindings.evidence_liveness_lease = 0;
        break;
    case IdentityToken::resident_id:
        identity.resident_id = 0;
        break;
    case IdentityToken::resident_generation:
        identity.resident_generation = 0;
        break;
    case IdentityToken::backend_instance_birth_token:
        identity.backend_instance_birth_token = 0;
        break;
    case IdentityToken::topology_generation:
        identity.topology_generation = 0;
        break;
    case IdentityToken::allocation_group_id:
        identity.allocation_group_id = 0;
        break;
    case IdentityToken::observation_contract_digest:
        identity.observation_contract_digest = 0;
        break;
    case IdentityToken::action_lease:
    case IdentityToken::action_lease_claim_generation:
    case IdentityToken::pre_observation_generation:
    case IdentityToken::ledger_generation:
        break;
    }
}

ReleaseInput input_with_zero_identity_token(IdentityToken token) {
    ReleaseInput input = canonical_input();
    zero_identity_token(input.resident, token);
    zero_identity_token(input.lease.identity, token);
    zero_identity_token(input.before.identity, token);
    switch (token) {
    case IdentityToken::observation_contract_digest:
        input.lease.observation_contract_digest = 0;
        break;
    case IdentityToken::action_lease:
        input.requested_action_lease = 0;
        input.lease.action_lease = 0;
        break;
    case IdentityToken::action_lease_claim_generation:
        input.lease.claim_generation = 0;
        break;
    case IdentityToken::pre_observation_generation:
        input.lease.pre_observation_generation = 0;
        input.before.generation = 0;
        break;
    case IdentityToken::ledger_generation:
        input.ledger_before.generation = 0;
        input.ledger_commit_generation = 0;
        break;
    case IdentityToken::device_identity:
    case IdentityToken::backend_artifact_digest:
    case IdentityToken::source_build_dependency_closure:
    case IdentityToken::driver_runtime_closure:
    case IdentityToken::model_manifest_digest:
    case IdentityToken::normalized_configuration_digest:
    case IdentityToken::evidence_index_digest:
    case IdentityToken::evidence_liveness_lease:
    case IdentityToken::resident_id:
    case IdentityToken::resident_generation:
    case IdentityToken::backend_instance_birth_token:
    case IdentityToken::topology_generation:
    case IdentityToken::allocation_group_id:
        break;
    }
    input.dispatch_attempted = false;
    input.dispatch_completed = false;
    set_unchanged_post_observation(input);
    return input;
}

bool zero_identity_token_shape(
    const ReleaseInput& input, IdentityToken token) {
    if (input.dispatch_attempted || input.dispatch_completed) {
        return false;
    }
    switch (token) {
    case IdentityToken::device_identity:
        return input.resident.bindings.device_identity == 0 &&
               input.lease.identity.bindings.device_identity == 0 &&
               input.before.identity.bindings.device_identity == 0 &&
               input.after.identity.bindings.device_identity == 0;
    case IdentityToken::backend_artifact_digest:
        return input.resident.bindings.backend_artifact_digest == 0 &&
               input.lease.identity.bindings.backend_artifact_digest == 0 &&
               input.before.identity.bindings.backend_artifact_digest == 0 &&
               input.after.identity.bindings.backend_artifact_digest == 0;
    case IdentityToken::source_build_dependency_closure:
        return input.resident.bindings.source_build_dependency_closure == 0 &&
               input.lease.identity.bindings.source_build_dependency_closure ==
                   0 &&
               input.before.identity.bindings.source_build_dependency_closure ==
                   0 &&
               input.after.identity.bindings.source_build_dependency_closure ==
                   0;
    case IdentityToken::driver_runtime_closure:
        return input.resident.bindings.driver_runtime_closure == 0 &&
               input.lease.identity.bindings.driver_runtime_closure == 0 &&
               input.before.identity.bindings.driver_runtime_closure == 0 &&
               input.after.identity.bindings.driver_runtime_closure == 0;
    case IdentityToken::model_manifest_digest:
        return input.resident.bindings.model_manifest_digest == 0 &&
               input.lease.identity.bindings.model_manifest_digest == 0 &&
               input.before.identity.bindings.model_manifest_digest == 0 &&
               input.after.identity.bindings.model_manifest_digest == 0;
    case IdentityToken::normalized_configuration_digest:
        return input.resident.bindings.normalized_configuration_digest == 0 &&
               input.lease.identity.bindings.normalized_configuration_digest ==
                   0 &&
               input.before.identity.bindings.normalized_configuration_digest ==
                   0 &&
               input.after.identity.bindings.normalized_configuration_digest ==
                   0;
    case IdentityToken::evidence_index_digest:
        return input.resident.bindings.evidence_index_digest == 0 &&
               input.lease.identity.bindings.evidence_index_digest == 0 &&
               input.before.identity.bindings.evidence_index_digest == 0 &&
               input.after.identity.bindings.evidence_index_digest == 0;
    case IdentityToken::evidence_liveness_lease:
        return input.resident.bindings.evidence_liveness_lease == 0 &&
               input.lease.identity.bindings.evidence_liveness_lease == 0 &&
               input.before.identity.bindings.evidence_liveness_lease == 0 &&
               input.after.identity.bindings.evidence_liveness_lease == 0;
    case IdentityToken::resident_id:
        return input.resident.resident_id == 0 &&
               input.lease.identity.resident_id == 0 &&
               input.before.identity.resident_id == 0 &&
               input.after.identity.resident_id == 0;
    case IdentityToken::resident_generation:
        return input.resident.resident_generation == 0 &&
               input.lease.identity.resident_generation == 0 &&
               input.before.identity.resident_generation == 0 &&
               input.after.identity.resident_generation == 0;
    case IdentityToken::backend_instance_birth_token:
        return input.resident.backend_instance_birth_token == 0 &&
               input.lease.identity.backend_instance_birth_token == 0 &&
               input.before.identity.backend_instance_birth_token == 0 &&
               input.after.identity.backend_instance_birth_token == 0;
    case IdentityToken::topology_generation:
        return input.resident.topology_generation == 0 &&
               input.lease.identity.topology_generation == 0 &&
               input.before.identity.topology_generation == 0 &&
               input.after.identity.topology_generation == 0;
    case IdentityToken::allocation_group_id:
        return input.resident.allocation_group_id == 0 &&
               input.lease.identity.allocation_group_id == 0 &&
               input.before.identity.allocation_group_id == 0 &&
               input.after.identity.allocation_group_id == 0;
    case IdentityToken::observation_contract_digest:
        return input.resident.observation_contract_digest == 0 &&
               input.lease.identity.observation_contract_digest == 0 &&
               input.before.identity.observation_contract_digest == 0 &&
               input.after.identity.observation_contract_digest == 0 &&
               input.lease.observation_contract_digest == 0;
    case IdentityToken::action_lease:
        return input.requested_action_lease == 0 &&
               input.lease.action_lease == 0;
    case IdentityToken::action_lease_claim_generation:
        return input.lease.claim_generation == 0;
    case IdentityToken::pre_observation_generation:
        return input.lease.pre_observation_generation == 0 &&
               input.before.generation == 0 && input.after.generation == 1;
    case IdentityToken::ledger_generation:
        return input.ledger_before.generation == 0 &&
               input.ledger_commit_generation == 0;
    }
    return false;
}

bool is_pre_dispatch_fault(Fault fault) {
    switch (fault) {
    case Fault::slot_set_missing:
    case Fault::slot_set_empty:
    case Fault::slot_id_duplicate:
    case Fault::slot_count_oversized:
    case Fault::pre_identity_mismatch:
    case Fault::evidence_liveness_missing:
    case Fault::evidence_liveness_expired:
    case Fault::action_lease_missing:
    case Fault::action_lease_mismatch:
    case Fault::action_lease_identity_mismatch:
    case Fault::action_lease_evidence_liveness_lease_mismatch:
    case Fault::action_lease_wrong_state:
    case Fault::action_lease_wrong_operation:
    case Fault::action_lease_wrong_slot_set:
    case Fault::action_lease_wrong_claim_generation:
    case Fault::action_lease_wrong_pre_observation_generation:
    case Fault::action_lease_wrong_observation_contract:
    case Fault::observation_before_missing:
    case Fault::observation_before_stale:
    case Fault::observation_before_skew:
    case Fault::observation_before_unhealthy:
    case Fault::observation_before_incomplete:
    case Fault::observation_before_gtt_effect_missing:
    case Fault::observation_before_host_effect_missing:
    case Fault::ledger_claim_group_missing:
    case Fault::ledger_claim_group_duplicate:
    case Fault::ledger_claim_group_wrong_id:
    case Fault::ledger_weights_group_wrong_id:
    case Fault::ledger_claim_group_wrong_effect:
    case Fault::ledger_weights_group_wrong_effect:
    case Fault::ledger_claim_group_wrong_allocation:
    case Fault::ledger_weights_group_wrong_allocation:
    case Fault::ledger_weights_gtt_measurement_missing:
    case Fault::ledger_weights_host_measurement_missing:
    case Fault::ledger_cache_gtt_measurement_missing:
    case Fault::ledger_cache_host_measurement_missing:
    case Fault::ledger_weights_bytes_mismatch:
    case Fault::ledger_weights_gtt_bytes_mismatch:
    case Fault::ledger_weights_host_bytes_mismatch:
    case Fault::ledger_cache_bytes_mismatch:
    case Fault::ledger_cache_gtt_bytes_mismatch:
    case Fault::ledger_cache_host_bytes_mismatch:
    case Fault::each_required_identity_token_zero:
    case Fault::active_use:
        return true;
    case Fault::ack_missing:
    case Fault::ack_count_mismatch:
    case Fault::ack_id_mismatch:
    case Fault::ack_partial:
    case Fault::dispatch_failure:
    case Fault::dispatch_completed_without_attempt:
    case Fault::invalid_precondition_after_dispatch_attempt:
    case Fault::valid_precondition_without_dispatch:
    case Fault::valid_precondition_without_dispatch_changed_post:
    case Fault::unsupported_mechanism_after_dispatch_attempt:
    case Fault::unsupported_mechanism_without_dispatch_changed_post:
    case Fault::ack_without_physical_delta:
    case Fault::ack_without_physical_delta_each_axis_changed:
    case Fault::post_identity_mismatch:
    case Fault::backend_instance_birth_token_mismatch:
    case Fault::process_identity_missing:
    case Fault::process_identity_zero:
    case Fault::process_identity_changed:
    case Fault::weights_identity_missing:
    case Fault::weights_identity_zero:
    case Fault::weights_identity_changed:
    case Fault::model_residency_identity_missing:
    case Fault::model_residency_identity_zero:
    case Fault::model_residency_identity_changed:
    case Fault::pin_missing:
    case Fault::pin_changed:
    case Fault::device_identity_mismatch:
    case Fault::backend_artifact_mismatch:
    case Fault::source_build_dependency_mismatch:
    case Fault::driver_runtime_mismatch:
    case Fault::model_manifest_mismatch:
    case Fault::normalized_configuration_mismatch:
    case Fault::evidence_index_mismatch:
    case Fault::observation_contract_mismatch:
    case Fault::topology_generation_mismatch:
    case Fault::allocation_group_mismatch:
    case Fault::resident_generation_mismatch:
    case Fault::observation_generation_mismatch:
    case Fault::observation_after_missing:
    case Fault::observation_after_stale:
    case Fault::observation_after_skew:
    case Fault::observation_after_unhealthy:
    case Fault::observation_after_incomplete:
    case Fault::release_partial:
    case Fault::gtt_release_partial:
    case Fault::host_release_partial:
    case Fault::cache_nonzero_after:
    case Fault::gtt_cache_nonzero_after:
    case Fault::host_cache_nonzero_after:
    case Fault::gtt_effect_missing:
    case Fault::host_effect_missing:
    case Fault::global_gtt_headroom_missing:
    case Fault::global_host_headroom_missing:
    case Fault::effect_out_of_envelope:
    case Fault::gtt_release_exceeds_maximum:
    case Fault::host_release_exceeds_maximum:
    case Fault::unrelated_demand_miscredit:
    case Fault::gtt_causal_mismatch:
    case Fault::host_causal_mismatch:
    case Fault::unrelated_gtt_growth_missing:
    case Fault::unrelated_host_growth_missing:
    case Fault::observation_generation_increment_overflow:
    case Fault::arithmetic_overflow:
    case Fault::gtt_causal_checked_add_overflow:
    case Fault::host_causal_checked_add_overflow:
    case Fault::ledger_generation_increment_overflow:
    case Fault::global_headroom_subtraction_underflow:
    case Fault::gtt_global_headroom_subtraction_underflow:
    case Fault::host_global_headroom_subtraction_underflow:
    case Fault::cache_release_subtraction_underflow:
    case Fault::gtt_cache_release_subtraction_underflow:
    case Fault::host_cache_release_subtraction_underflow:
    case Fault::ledger_generation_stale:
    case Fault::ledger_commit_failure:
        return false;
    }
    return false;
}

UnknownReason expected_reason(Fault fault) {
    switch (fault) {
    case Fault::slot_set_missing:
    case Fault::slot_set_empty:
    case Fault::slot_id_duplicate:
    case Fault::slot_count_oversized:
    case Fault::invalid_precondition_after_dispatch_attempt:
        return UnknownReason::slot_selection;
    case Fault::ack_missing:
    case Fault::ack_count_mismatch:
    case Fault::ack_id_mismatch:
    case Fault::ack_partial:
        return UnknownReason::acknowledgement;
    case Fault::dispatch_failure:
    case Fault::dispatch_completed_without_attempt:
    case Fault::unsupported_mechanism_after_dispatch_attempt:
    case Fault::unsupported_mechanism_without_dispatch_changed_post:
        return UnknownReason::dispatch;
    case Fault::valid_precondition_without_dispatch:
        return UnknownReason::none;
    case Fault::valid_precondition_without_dispatch_changed_post:
        return UnknownReason::observation;
    case Fault::ack_without_physical_delta:
    case Fault::ack_without_physical_delta_each_axis_changed:
    case Fault::release_partial:
    case Fault::gtt_release_partial:
    case Fault::host_release_partial:
    case Fault::cache_nonzero_after:
    case Fault::gtt_cache_nonzero_after:
    case Fault::host_cache_nonzero_after:
    case Fault::effect_out_of_envelope:
    case Fault::gtt_release_exceeds_maximum:
    case Fault::host_release_exceeds_maximum:
        return UnknownReason::effect;
    case Fault::pre_identity_mismatch:
    case Fault::post_identity_mismatch:
    case Fault::backend_instance_birth_token_mismatch:
    case Fault::process_identity_missing:
    case Fault::process_identity_zero:
    case Fault::process_identity_changed:
    case Fault::weights_identity_missing:
    case Fault::weights_identity_zero:
    case Fault::weights_identity_changed:
    case Fault::model_residency_identity_missing:
    case Fault::model_residency_identity_zero:
    case Fault::model_residency_identity_changed:
    case Fault::pin_missing:
    case Fault::pin_changed:
    case Fault::device_identity_mismatch:
    case Fault::backend_artifact_mismatch:
    case Fault::source_build_dependency_mismatch:
    case Fault::driver_runtime_mismatch:
    case Fault::model_manifest_mismatch:
    case Fault::normalized_configuration_mismatch:
    case Fault::evidence_index_mismatch:
    case Fault::evidence_liveness_missing:
    case Fault::evidence_liveness_expired:
    case Fault::observation_contract_mismatch:
    case Fault::topology_generation_mismatch:
    case Fault::allocation_group_mismatch:
    case Fault::resident_generation_mismatch:
    case Fault::each_required_identity_token_zero:
        return UnknownReason::identity;
    case Fault::action_lease_missing:
    case Fault::action_lease_mismatch:
    case Fault::action_lease_identity_mismatch:
    case Fault::action_lease_evidence_liveness_lease_mismatch:
    case Fault::action_lease_wrong_state:
    case Fault::action_lease_wrong_operation:
    case Fault::action_lease_wrong_slot_set:
    case Fault::action_lease_wrong_claim_generation:
    case Fault::action_lease_wrong_pre_observation_generation:
    case Fault::action_lease_wrong_observation_contract:
        return UnknownReason::action_lease;
    case Fault::observation_generation_mismatch:
    case Fault::observation_generation_increment_overflow:
        return UnknownReason::observation_generation;
    case Fault::observation_before_missing:
    case Fault::observation_before_stale:
    case Fault::observation_before_skew:
    case Fault::observation_before_unhealthy:
    case Fault::observation_before_incomplete:
    case Fault::observation_before_gtt_effect_missing:
    case Fault::observation_before_host_effect_missing:
    case Fault::observation_after_missing:
    case Fault::observation_after_stale:
    case Fault::observation_after_skew:
    case Fault::observation_after_unhealthy:
    case Fault::observation_after_incomplete:
    case Fault::gtt_effect_missing:
    case Fault::host_effect_missing:
    case Fault::global_gtt_headroom_missing:
    case Fault::global_host_headroom_missing:
        return UnknownReason::observation;
    case Fault::unrelated_gtt_growth_missing:
    case Fault::unrelated_host_growth_missing:
        return UnknownReason::unrelated_demand;
    case Fault::ledger_claim_group_missing:
    case Fault::ledger_claim_group_duplicate:
    case Fault::ledger_claim_group_wrong_id:
    case Fault::ledger_weights_group_wrong_id:
    case Fault::ledger_claim_group_wrong_effect:
    case Fault::ledger_weights_group_wrong_effect:
    case Fault::ledger_claim_group_wrong_allocation:
    case Fault::ledger_weights_group_wrong_allocation:
    case Fault::ledger_weights_gtt_measurement_missing:
    case Fault::ledger_weights_host_measurement_missing:
    case Fault::ledger_cache_gtt_measurement_missing:
    case Fault::ledger_cache_host_measurement_missing:
    case Fault::ledger_weights_bytes_mismatch:
    case Fault::ledger_weights_gtt_bytes_mismatch:
    case Fault::ledger_weights_host_bytes_mismatch:
    case Fault::ledger_cache_bytes_mismatch:
    case Fault::ledger_cache_gtt_bytes_mismatch:
    case Fault::ledger_cache_host_bytes_mismatch:
        return UnknownReason::claim_group;
    case Fault::unrelated_demand_miscredit:
    case Fault::gtt_causal_mismatch:
    case Fault::host_causal_mismatch:
        return UnknownReason::unrelated_demand;
    case Fault::arithmetic_overflow:
    case Fault::gtt_causal_checked_add_overflow:
    case Fault::host_causal_checked_add_overflow:
    case Fault::ledger_generation_increment_overflow:
    case Fault::global_headroom_subtraction_underflow:
    case Fault::gtt_global_headroom_subtraction_underflow:
    case Fault::host_global_headroom_subtraction_underflow:
    case Fault::cache_release_subtraction_underflow:
    case Fault::gtt_cache_release_subtraction_underflow:
    case Fault::host_cache_release_subtraction_underflow:
        return UnknownReason::arithmetic;
    case Fault::ledger_generation_stale:
        return UnknownReason::ledger_generation;
    case Fault::ledger_commit_failure:
        return UnknownReason::ledger_commit;
    case Fault::active_use:
        return UnknownReason::active_use;
    }
    return UnknownReason::none;
}

ReleaseInput input_with_fault(Fault fault) {
    ReleaseInput input = canonical_input();
    switch (fault) {
    case Fault::slot_set_missing:
        input.request.present = false;
        input.dispatch_attempted = false;
        break;
    case Fault::slot_set_empty:
        input.request.count = 0;
        input.dispatch_attempted = false;
        break;
    case Fault::slot_id_duplicate:
        input.request.ids[1] = input.request.ids[0];
        input.dispatch_attempted = false;
        break;
    case Fault::slot_count_oversized:
        input.request.count = input.request.ids.size() + 1;
        input.dispatch_attempted = false;
        break;
    case Fault::ack_missing:
        input.acknowledgement.present = false;
        break;
    case Fault::ack_count_mismatch:
        input.acknowledgement.count = 1;
        break;
    case Fault::ack_id_mismatch:
        input.acknowledgement.ids[1] = 9;
        break;
    case Fault::ack_partial:
        input.acknowledgement.complete = false;
        break;
    case Fault::dispatch_failure:
        input.dispatch_completed = false;
        input.after.present = false;
        break;
    case Fault::dispatch_completed_without_attempt:
        input.dispatch_attempted = false;
        input.dispatch_completed = true;
        set_unchanged_post_observation(input);
        break;
    case Fault::invalid_precondition_after_dispatch_attempt:
        input.request.present = false;
        input.dispatch_attempted = true;
        input.dispatch_completed = false;
        set_unchanged_post_observation(input);
        break;
    case Fault::valid_precondition_without_dispatch:
        input.dispatch_attempted = false;
        input.dispatch_completed = false;
        set_unchanged_post_observation(input);
        break;
    case Fault::valid_precondition_without_dispatch_changed_post:
        input.dispatch_attempted = false;
        input.dispatch_completed = false;
        set_unchanged_post_observation(input);
        input.after.identity.resident_id += 1;
        break;
    case Fault::unsupported_mechanism_after_dispatch_attempt:
        input.mechanism = Mechanism::absent;
        input.dispatch_attempted = true;
        input.dispatch_completed = false;
        set_unchanged_post_observation(input);
        break;
    case Fault::unsupported_mechanism_without_dispatch_changed_post:
        input.mechanism = Mechanism::absent;
        input.dispatch_attempted = false;
        input.dispatch_completed = false;
        set_unchanged_post_observation(input);
        input.after.identity.resident_id += 1;
        break;
    case Fault::ack_without_physical_delta:
        set_unchanged_post_observation(input);
        break;
    case Fault::ack_without_physical_delta_each_axis_changed:
        set_unchanged_post_observation(input);
        input.after.cache_gtt.value = input.before.cache_gtt.value - 1;
        break;
    case Fault::pre_identity_mismatch:
        input.before.identity.resident_id += 1;
        input.dispatch_attempted = false;
        break;
    case Fault::post_identity_mismatch:
        input.after.identity.resident_id += 1;
        break;
    case Fault::backend_instance_birth_token_mismatch:
        input.after.identity.backend_instance_birth_token += 1;
        break;
    case Fault::process_identity_missing:
        input.after.process_identity.present = false;
        break;
    case Fault::process_identity_zero:
        input.after.process_identity.value = 0;
        break;
    case Fault::process_identity_changed:
        input.after.process_identity.value += 1;
        break;
    case Fault::weights_identity_missing:
        input.after.weights_identity.present = false;
        break;
    case Fault::weights_identity_zero:
        input.after.weights_identity.value = 0;
        break;
    case Fault::weights_identity_changed:
        input.after.weights_identity.value += 1;
        break;
    case Fault::model_residency_identity_missing:
        input.after.model_residency_identity.present = false;
        break;
    case Fault::model_residency_identity_zero:
        input.after.model_residency_identity.value = 0;
        break;
    case Fault::model_residency_identity_changed:
        input.after.model_residency_identity.value += 1;
        break;
    case Fault::pin_missing:
        input.after.pin.known = false;
        break;
    case Fault::pin_changed:
        input.after.pin.value = !input.after.pin.value;
        break;
    case Fault::device_identity_mismatch:
        input.after.identity.bindings.device_identity += 1;
        break;
    case Fault::backend_artifact_mismatch:
        input.after.identity.bindings.backend_artifact_digest += 1;
        break;
    case Fault::source_build_dependency_mismatch:
        input.after.identity.bindings.source_build_dependency_closure += 1;
        break;
    case Fault::driver_runtime_mismatch:
        input.after.identity.bindings.driver_runtime_closure += 1;
        break;
    case Fault::model_manifest_mismatch:
        input.after.identity.bindings.model_manifest_digest += 1;
        break;
    case Fault::normalized_configuration_mismatch:
        input.after.identity.bindings.normalized_configuration_digest += 1;
        break;
    case Fault::evidence_index_mismatch:
        input.after.identity.bindings.evidence_index_digest += 1;
        break;
    case Fault::evidence_liveness_missing:
        input.lease.identity.bindings.evidence_liveness_present = false;
        input.dispatch_attempted = false;
        break;
    case Fault::evidence_liveness_expired:
        input.lease.identity.bindings.evidence_liveness_valid = false;
        input.dispatch_attempted = false;
        break;
    case Fault::observation_contract_mismatch:
        input.after.identity.observation_contract_digest += 1;
        break;
    case Fault::topology_generation_mismatch:
        input.after.identity.topology_generation += 1;
        break;
    case Fault::allocation_group_mismatch:
        input.after.identity.allocation_group_id += 1;
        break;
    case Fault::action_lease_missing:
        input.lease.present = false;
        input.dispatch_attempted = false;
        break;
    case Fault::action_lease_mismatch:
        input.lease.action_lease += 1;
        input.dispatch_attempted = false;
        break;
    case Fault::action_lease_identity_mismatch:
        input.lease.identity.resident_id += 1;
        input.dispatch_attempted = false;
        break;
    case Fault::action_lease_evidence_liveness_lease_mismatch:
        input.lease.identity.bindings.evidence_liveness_lease += 1;
        input.dispatch_attempted = false;
        break;
    case Fault::action_lease_wrong_state:
        input.lease.state = LeaseState::idle;
        input.dispatch_attempted = false;
        break;
    case Fault::action_lease_wrong_operation:
        input.lease.operation = Operation::pressure_hard_release;
        input.dispatch_attempted = false;
        break;
    case Fault::action_lease_wrong_slot_set:
        input.lease.slot_set.ids[1] = 9;
        input.dispatch_attempted = false;
        break;
    case Fault::action_lease_wrong_claim_generation:
        input.lease.claim_generation += 1;
        input.dispatch_attempted = false;
        break;
    case Fault::action_lease_wrong_pre_observation_generation:
        input.lease.pre_observation_generation += 1;
        input.dispatch_attempted = false;
        break;
    case Fault::action_lease_wrong_observation_contract:
        input.lease.observation_contract_digest += 1;
        input.dispatch_attempted = false;
        break;
    case Fault::resident_generation_mismatch:
        input.after.identity.resident_generation += 1;
        break;
    case Fault::observation_generation_mismatch:
        input.after.generation = input.before.generation;
        break;
    case Fault::each_required_identity_token_zero:
        input.resident.resident_id = 0;
        input.lease.identity.resident_id = 0;
        input.before.identity.resident_id = 0;
        input.dispatch_attempted = false;
        break;
    case Fault::observation_before_missing:
        input.before.present = false;
        input.dispatch_attempted = false;
        break;
    case Fault::observation_before_stale:
        input.before.fresh = false;
        input.dispatch_attempted = false;
        break;
    case Fault::observation_before_skew:
        input.before.within_skew = false;
        input.dispatch_attempted = false;
        break;
    case Fault::observation_before_unhealthy:
        input.before.healthy = false;
        input.dispatch_attempted = false;
        break;
    case Fault::observation_before_incomplete:
        input.before.complete = false;
        input.dispatch_attempted = false;
        break;
    case Fault::observation_before_gtt_effect_missing:
        input.before.cache_gtt.present = false;
        input.dispatch_attempted = false;
        break;
    case Fault::observation_before_host_effect_missing:
        input.before.cache_host.present = false;
        input.dispatch_attempted = false;
        break;
    case Fault::observation_after_missing:
        input.after.present = false;
        break;
    case Fault::observation_after_stale:
        input.after.fresh = false;
        break;
    case Fault::observation_after_skew:
        input.after.within_skew = false;
        break;
    case Fault::observation_after_unhealthy:
        input.after.healthy = false;
        break;
    case Fault::observation_after_incomplete:
        input.after.complete = false;
        break;
    case Fault::release_partial:
        input.after.cache_gtt.value = 1024;
        input.after.cache_host.value = 512;
        break;
    case Fault::gtt_release_partial:
        input.after.cache_gtt.value = 1024;
        break;
    case Fault::host_release_partial:
        input.after.cache_host.value = 512;
        break;
    case Fault::cache_nonzero_after:
        input.after.cache_gtt.value = 1;
        input.after.cache_host.value = 1;
        break;
    case Fault::gtt_cache_nonzero_after:
        input.after.cache_gtt.value = 1;
        break;
    case Fault::host_cache_nonzero_after:
        input.after.cache_host.value = 1;
        break;
    case Fault::gtt_effect_missing:
        input.after.cache_gtt.present = false;
        break;
    case Fault::host_effect_missing:
        input.after.cache_host.present = false;
        break;
    case Fault::global_gtt_headroom_missing:
        input.after.global_gtt_headroom.present = false;
        break;
    case Fault::global_host_headroom_missing:
        input.after.global_host_headroom.present = false;
        break;
    case Fault::ledger_claim_group_missing:
        input.ledger_before.count = 1;
        input.dispatch_attempted = false;
        break;
    case Fault::ledger_claim_group_duplicate:
        input.ledger_before.groups[1] = input.ledger_before.groups[0];
        input.dispatch_attempted = false;
        break;
    case Fault::ledger_claim_group_wrong_id:
        input.ledger_before.groups[1].id = ClaimId::invalid;
        input.dispatch_attempted = false;
        break;
    case Fault::ledger_weights_group_wrong_id:
        input.ledger_before.groups[0].id = ClaimId::invalid;
        input.dispatch_attempted = false;
        break;
    case Fault::ledger_claim_group_wrong_effect:
        input.ledger_before.groups[1].effect = Effect::persistent_weights;
        input.dispatch_attempted = false;
        break;
    case Fault::ledger_weights_group_wrong_effect:
        input.ledger_before.groups[0].effect = Effect::reconstructible_state;
        input.dispatch_attempted = false;
        break;
    case Fault::ledger_claim_group_wrong_allocation:
        input.ledger_before.groups[1].allocation_group_id += 1;
        input.dispatch_attempted = false;
        break;
    case Fault::ledger_weights_group_wrong_allocation:
        input.ledger_before.groups[0].allocation_group_id += 1;
        input.dispatch_attempted = false;
        break;
    case Fault::ledger_weights_gtt_measurement_missing:
        input.ledger_before.groups[0].gtt.present = false;
        input.dispatch_attempted = false;
        break;
    case Fault::ledger_weights_host_measurement_missing:
        input.ledger_before.groups[0].host.present = false;
        input.dispatch_attempted = false;
        break;
    case Fault::ledger_cache_gtt_measurement_missing:
        input.ledger_before.groups[1].gtt.present = false;
        input.dispatch_attempted = false;
        break;
    case Fault::ledger_cache_host_measurement_missing:
        input.ledger_before.groups[1].host.present = false;
        input.dispatch_attempted = false;
        break;
    case Fault::ledger_weights_bytes_mismatch:
        input.ledger_before.groups[0].gtt.value += 1;
        input.ledger_before.groups[0].host.value += 1;
        input.dispatch_attempted = false;
        break;
    case Fault::ledger_weights_gtt_bytes_mismatch:
        input.ledger_before.groups[0].gtt.value += 1;
        input.dispatch_attempted = false;
        break;
    case Fault::ledger_weights_host_bytes_mismatch:
        input.ledger_before.groups[0].host.value += 1;
        input.dispatch_attempted = false;
        break;
    case Fault::ledger_cache_bytes_mismatch:
        input.ledger_before.groups[1].gtt.value += 1;
        input.ledger_before.groups[1].host.value += 1;
        input.dispatch_attempted = false;
        break;
    case Fault::ledger_cache_gtt_bytes_mismatch:
        input.ledger_before.groups[1].gtt.value += 1;
        input.dispatch_attempted = false;
        break;
    case Fault::ledger_cache_host_bytes_mismatch:
        input.ledger_before.groups[1].host.value += 1;
        input.dispatch_attempted = false;
        break;
    case Fault::effect_out_of_envelope:
        input.maximum_gtt_release_bytes = 1024;
        input.maximum_host_release_bytes = 512;
        break;
    case Fault::gtt_release_exceeds_maximum:
        input.maximum_gtt_release_bytes = 1024;
        break;
    case Fault::host_release_exceeds_maximum:
        input.maximum_host_release_bytes = 512;
        break;
    case Fault::unrelated_demand_miscredit:
        input.after.global_gtt_headroom.value = 6144;
        input.after.global_host_headroom.value = 3072;
        break;
    case Fault::gtt_causal_mismatch:
        input.after.global_gtt_headroom.value = 6144;
        break;
    case Fault::host_causal_mismatch:
        input.after.global_host_headroom.value = 3072;
        break;
    case Fault::unrelated_gtt_growth_missing:
        input.unrelated_gtt_growth.present = false;
        break;
    case Fault::unrelated_host_growth_missing:
        input.unrelated_host_growth.present = false;
        break;
    case Fault::observation_generation_increment_overflow:
        input.before.generation =
            std::numeric_limits<std::uint64_t>::max();
        input.lease.pre_observation_generation = input.before.generation;
        input.after.generation = 0;
        break;
    case Fault::arithmetic_overflow:
        input.after.global_gtt_headroom.value =
            input.before.global_gtt_headroom.value + 1;
        input.after.global_host_headroom.value =
            input.before.global_host_headroom.value + 1;
        input.unrelated_gtt_growth.value =
            std::numeric_limits<std::uint64_t>::max();
        input.unrelated_host_growth.value =
            std::numeric_limits<std::uint64_t>::max();
        break;
    case Fault::gtt_causal_checked_add_overflow:
        input.after.global_gtt_headroom.value =
            input.before.global_gtt_headroom.value + 1;
        input.unrelated_gtt_growth.value =
            std::numeric_limits<std::uint64_t>::max();
        break;
    case Fault::host_causal_checked_add_overflow:
        input.after.global_host_headroom.value =
            input.before.global_host_headroom.value + 1;
        input.unrelated_host_growth.value =
            std::numeric_limits<std::uint64_t>::max();
        break;
    case Fault::ledger_generation_increment_overflow:
        input.ledger_before.generation =
            std::numeric_limits<std::uint64_t>::max();
        input.lease.claim_generation = input.ledger_before.generation;
        input.ledger_commit_generation = input.ledger_before.generation;
        break;
    case Fault::global_headroom_subtraction_underflow:
        input.after.global_gtt_headroom.value =
            input.before.global_gtt_headroom.value - 1;
        input.after.global_host_headroom.value =
            input.before.global_host_headroom.value - 1;
        break;
    case Fault::gtt_global_headroom_subtraction_underflow:
        input.after.global_gtt_headroom.value =
            input.before.global_gtt_headroom.value - 1;
        break;
    case Fault::host_global_headroom_subtraction_underflow:
        input.after.global_host_headroom.value =
            input.before.global_host_headroom.value - 1;
        break;
    case Fault::cache_release_subtraction_underflow:
        input.after.cache_gtt.value = input.before.cache_gtt.value + 1;
        input.after.cache_host.value = input.before.cache_host.value + 1;
        break;
    case Fault::gtt_cache_release_subtraction_underflow:
        input.after.cache_gtt.value = input.before.cache_gtt.value + 1;
        break;
    case Fault::host_cache_release_subtraction_underflow:
        input.after.cache_host.value = input.before.cache_host.value + 1;
        break;
    case Fault::ledger_generation_stale:
        input.ledger_commit_generation += 1;
        break;
    case Fault::ledger_commit_failure:
        input.ledger_commit_succeeds = false;
        break;
    case Fault::active_use:
        input.lease.active_use = true;
        input.dispatch_attempted = false;
        break;
    }
    if (is_pre_dispatch_fault(fault)) {
        input.dispatch_attempted = false;
        input.dispatch_completed = false;
        set_unchanged_post_observation(input);
    }
    return input;
}

ReleaseInput input_with_before_physical_identity_missing(Fault fault) {
    ReleaseInput input = canonical_input();
    input.dispatch_attempted = false;
    input.dispatch_completed = false;
    set_unchanged_post_observation(input);
    if (fault == Fault::process_identity_missing) {
        input.before.process_identity.present = false;
    } else if (fault == Fault::weights_identity_missing) {
        input.before.weights_identity.present = false;
    } else {
        input.before.model_residency_identity.present = false;
    }
    return input;
}

bool is_physical_validity_fault(Fault fault) {
    return fault == Fault::process_identity_missing ||
           fault == Fault::process_identity_zero ||
           fault == Fault::weights_identity_missing ||
           fault == Fault::weights_identity_zero ||
           fault == Fault::model_residency_identity_missing ||
           fault == Fault::model_residency_identity_zero ||
           fault == Fault::pin_missing;
}

ReleaseInput input_with_equal_invalid_physical_state(Fault fault) {
    ReleaseInput input = canonical_input();
    input.dispatch_attempted = false;
    input.dispatch_completed = false;
    set_unchanged_post_observation(input);
    if (fault == Fault::process_identity_missing) {
        input.before.process_identity.present = false;
        input.after.process_identity.present = false;
    } else if (fault == Fault::process_identity_zero) {
        input.before.process_identity.value = 0;
        input.after.process_identity.value = 0;
    } else if (fault == Fault::weights_identity_missing) {
        input.before.weights_identity.present = false;
        input.after.weights_identity.present = false;
    } else if (fault == Fault::weights_identity_zero) {
        input.before.weights_identity.value = 0;
        input.after.weights_identity.value = 0;
    } else if (fault == Fault::model_residency_identity_missing) {
        input.before.model_residency_identity.present = false;
        input.after.model_residency_identity.present = false;
    } else if (fault == Fault::model_residency_identity_zero) {
        input.before.model_residency_identity.value = 0;
        input.after.model_residency_identity.value = 0;
    } else if (fault == Fault::pin_missing) {
        input.before.pin.known = false;
        input.after.pin.known = false;
    }
    return input;
}

ReleaseInput input_with_after_invalid_physical_state(Fault fault) {
    ReleaseInput input = canonical_input();
    if (fault == Fault::process_identity_missing) {
        input.after.process_identity.present = false;
    } else if (fault == Fault::process_identity_zero) {
        input.after.process_identity.value = 0;
    } else if (fault == Fault::weights_identity_missing) {
        input.after.weights_identity.present = false;
    } else if (fault == Fault::weights_identity_zero) {
        input.after.weights_identity.value = 0;
    } else if (fault == Fault::model_residency_identity_missing) {
        input.after.model_residency_identity.present = false;
    } else if (fault == Fault::model_residency_identity_zero) {
        input.after.model_residency_identity.value = 0;
    } else if (fault == Fault::pin_missing) {
        input.after.pin.known = false;
    }
    return input;
}

bool exact_physical_validity_state(
    const ReleaseInput& input, Fault fault, bool equal_invalid) {
    const ReleaseInput exact = canonical_input();
    const bool exact_stage =
        input.dispatch_attempted == !equal_invalid &&
        input.dispatch_completed == !equal_invalid;
    const bool process_exact =
        same_physical_identity(
            input.before.process_identity, exact.before.process_identity) &&
        same_physical_identity(
            input.after.process_identity, exact.after.process_identity);
    const bool weights_exact =
        same_physical_identity(
            input.before.weights_identity, exact.before.weights_identity) &&
        same_physical_identity(
            input.after.weights_identity, exact.after.weights_identity);
    const bool model_exact =
        same_physical_identity(
            input.before.model_residency_identity,
            exact.before.model_residency_identity) &&
        same_physical_identity(
            input.after.model_residency_identity,
            exact.after.model_residency_identity);
    const bool pin_exact =
        input.before.pin.known == exact.before.pin.known &&
        input.before.pin.value == exact.before.pin.value &&
        input.after.pin.known == exact.after.pin.known &&
        input.after.pin.value == exact.after.pin.value;
    if (fault == Fault::process_identity_missing) {
        return exact_stage &&
               input.before.process_identity.present == !equal_invalid &&
               !input.after.process_identity.present && weights_exact &&
               model_exact && pin_exact;
    }
    if (fault == Fault::process_identity_zero) {
        return exact_stage &&
               input.before.process_identity.value ==
                   (equal_invalid ? 0 : exact.before.process_identity.value) &&
               input.after.process_identity.value == 0 && weights_exact &&
               model_exact && pin_exact;
    }
    if (fault == Fault::weights_identity_missing) {
        return exact_stage && process_exact &&
               input.before.weights_identity.present == !equal_invalid &&
               !input.after.weights_identity.present && model_exact &&
               pin_exact;
    }
    if (fault == Fault::weights_identity_zero) {
        return exact_stage && process_exact &&
               input.before.weights_identity.value ==
                   (equal_invalid ? 0 : exact.before.weights_identity.value) &&
               input.after.weights_identity.value == 0 && model_exact &&
               pin_exact;
    }
    if (fault == Fault::model_residency_identity_missing) {
        return exact_stage && process_exact && weights_exact &&
               input.before.model_residency_identity.present ==
                   !equal_invalid &&
               !input.after.model_residency_identity.present && pin_exact;
    }
    if (fault == Fault::model_residency_identity_zero) {
        return exact_stage && process_exact && weights_exact &&
               input.before.model_residency_identity.value ==
                   (equal_invalid
                        ? 0
                        : exact.before.model_residency_identity.value) &&
               input.after.model_residency_identity.value == 0 && pin_exact;
    }
    if (fault == Fault::pin_missing) {
        return exact_stage && process_exact && weights_exact && model_exact &&
               input.before.pin.known == !equal_invalid &&
               !input.after.pin.known &&
               input.before.pin.value == exact.before.pin.value &&
               input.after.pin.value == exact.after.pin.value;
    }
    return false;
}

bool physical_validity_guard_witnesses_pass(Fault fault) {
    if (!is_physical_validity_fault(fault)) {
        return true;
    }
    const ReleaseInput equal_invalid_input =
        input_with_equal_invalid_physical_state(fault);
    const ReleaseInput after_invalid_input =
        input_with_after_invalid_physical_state(fault);
    const Verification equal_invalid_result =
        verify_soft_release(equal_invalid_input);
    const Verification after_invalid_result =
        verify_soft_release(after_invalid_input);
    const bool equal_invalid_rejected =
        exact_physical_validity_state(equal_invalid_input, fault, true) &&
        !valid_physical_identity(equal_invalid_input.before) &&
        !valid_physical_identity(equal_invalid_input.after) &&
        equal_invalid_result.evidence == Evidence::unknown &&
        equal_invalid_result.disposition == Disposition::quarantine &&
        equal_invalid_result.reason == UnknownReason::identity &&
        !equal_invalid_result.dispatch_called &&
        equal_invalid_result.claims_maximized &&
        !equal_invalid_result.residency_preserved &&
        !equal_invalid_result.ledger_changed &&
        same_ledger(
            equal_invalid_input.ledger_before,
            equal_invalid_result.ledger_after) &&
        equal_invalid_result.credited_gtt_bytes == 0 &&
        equal_invalid_result.credited_host_bytes == 0;
    const bool after_invalid_rejected =
        exact_physical_validity_state(after_invalid_input, fault, false) &&
        valid_physical_identity(after_invalid_input.before) &&
        !valid_physical_identity(after_invalid_input.after) &&
        after_invalid_result.evidence == Evidence::unknown &&
        after_invalid_result.disposition == Disposition::quarantine &&
        after_invalid_result.reason == UnknownReason::identity &&
        after_invalid_result.dispatch_called &&
        after_invalid_result.claims_maximized &&
        !after_invalid_result.residency_preserved &&
        !after_invalid_result.ledger_changed &&
        same_ledger(
            after_invalid_input.ledger_before,
            after_invalid_result.ledger_after) &&
        after_invalid_result.credited_gtt_bytes == 0 &&
        after_invalid_result.credited_host_bytes == 0;
    return equal_invalid_rejected && after_invalid_rejected;
}

bool exact_dimension_fault(const ReleaseInput& input, Fault fault) {
    const ReleaseInput exact = canonical_input();
    if (fault == Fault::observation_before_gtt_effect_missing) {
        return !input.before.cache_gtt.present &&
               input.before.cache_gtt.value == exact.before.cache_gtt.value &&
               same_measurement(
                   input.before.cache_host, exact.before.cache_host);
    }
    if (fault == Fault::observation_before_host_effect_missing) {
        return !input.before.cache_host.present &&
               input.before.cache_host.value == exact.before.cache_host.value &&
               same_measurement(
                   input.before.cache_gtt, exact.before.cache_gtt);
    }
    if (fault == Fault::gtt_release_partial) {
        return input.after.cache_gtt.present &&
               input.after.cache_gtt.value == 1024 &&
               same_measurement(
                   input.after.cache_host, exact.after.cache_host);
    }
    if (fault == Fault::host_release_partial) {
        return input.after.cache_host.present &&
               input.after.cache_host.value == 512 &&
               same_measurement(
                   input.after.cache_gtt, exact.after.cache_gtt);
    }
    if (fault == Fault::gtt_cache_nonzero_after) {
        return input.after.cache_gtt.present &&
               input.after.cache_gtt.value == 1 &&
               same_measurement(
                   input.after.cache_host, exact.after.cache_host);
    }
    if (fault == Fault::host_cache_nonzero_after) {
        return input.after.cache_host.present &&
               input.after.cache_host.value == 1 &&
               same_measurement(
                   input.after.cache_gtt, exact.after.cache_gtt);
    }
    if (fault == Fault::gtt_effect_missing) {
        return !input.after.cache_gtt.present &&
               input.after.cache_gtt.value == exact.after.cache_gtt.value &&
               same_measurement(
                   input.after.cache_host, exact.after.cache_host);
    }
    if (fault == Fault::host_effect_missing) {
        return !input.after.cache_host.present &&
               input.after.cache_host.value == exact.after.cache_host.value &&
               same_measurement(
                   input.after.cache_gtt, exact.after.cache_gtt);
    }
    if (fault == Fault::global_gtt_headroom_missing) {
        return !input.after.global_gtt_headroom.present &&
               input.after.global_gtt_headroom.value ==
                   exact.after.global_gtt_headroom.value &&
               same_measurement(
                   input.after.global_host_headroom,
                   exact.after.global_host_headroom);
    }
    if (fault == Fault::global_host_headroom_missing) {
        return !input.after.global_host_headroom.present &&
               input.after.global_host_headroom.value ==
                   exact.after.global_host_headroom.value &&
               same_measurement(
                   input.after.global_gtt_headroom,
                   exact.after.global_gtt_headroom);
    }
    if (fault == Fault::ledger_weights_gtt_measurement_missing) {
        return !input.ledger_before.groups[0].gtt.present &&
               input.ledger_before.groups[0].gtt.value ==
                   exact.ledger_before.groups[0].gtt.value &&
               same_measurement(
                   input.ledger_before.groups[0].host,
                   exact.ledger_before.groups[0].host);
    }
    if (fault == Fault::ledger_weights_host_measurement_missing) {
        return !input.ledger_before.groups[0].host.present &&
               input.ledger_before.groups[0].host.value ==
                   exact.ledger_before.groups[0].host.value &&
               same_measurement(
                   input.ledger_before.groups[0].gtt,
                   exact.ledger_before.groups[0].gtt);
    }
    if (fault == Fault::ledger_cache_gtt_measurement_missing) {
        return !input.ledger_before.groups[1].gtt.present &&
               input.ledger_before.groups[1].gtt.value ==
                   exact.ledger_before.groups[1].gtt.value &&
               same_measurement(
                   input.ledger_before.groups[1].host,
                   exact.ledger_before.groups[1].host);
    }
    if (fault == Fault::ledger_cache_host_measurement_missing) {
        return !input.ledger_before.groups[1].host.present &&
               input.ledger_before.groups[1].host.value ==
                   exact.ledger_before.groups[1].host.value &&
               same_measurement(
                   input.ledger_before.groups[1].gtt,
                   exact.ledger_before.groups[1].gtt);
    }
    if (fault == Fault::ledger_weights_gtt_bytes_mismatch) {
        return input.ledger_before.groups[0].gtt.present &&
               input.ledger_before.groups[0].gtt.value ==
                   exact.ledger_before.groups[0].gtt.value + 1 &&
               same_measurement(
                   input.ledger_before.groups[0].host,
                   exact.ledger_before.groups[0].host);
    }
    if (fault == Fault::ledger_weights_host_bytes_mismatch) {
        return input.ledger_before.groups[0].host.present &&
               input.ledger_before.groups[0].host.value ==
                   exact.ledger_before.groups[0].host.value + 1 &&
               same_measurement(
                   input.ledger_before.groups[0].gtt,
                   exact.ledger_before.groups[0].gtt);
    }
    if (fault == Fault::ledger_cache_gtt_bytes_mismatch) {
        return input.ledger_before.groups[1].gtt.present &&
               input.ledger_before.groups[1].gtt.value ==
                   exact.ledger_before.groups[1].gtt.value + 1 &&
               same_measurement(
                   input.ledger_before.groups[1].host,
                   exact.ledger_before.groups[1].host);
    }
    if (fault == Fault::ledger_cache_host_bytes_mismatch) {
        return input.ledger_before.groups[1].host.present &&
               input.ledger_before.groups[1].host.value ==
                   exact.ledger_before.groups[1].host.value + 1 &&
               same_measurement(
                   input.ledger_before.groups[1].gtt,
                   exact.ledger_before.groups[1].gtt);
    }
    if (fault == Fault::effect_out_of_envelope) {
        return input.maximum_gtt_release_bytes == 1024 &&
               input.maximum_host_release_bytes == 512;
    }
    if (fault == Fault::gtt_release_exceeds_maximum) {
        return input.maximum_gtt_release_bytes == 1024 &&
               input.maximum_host_release_bytes ==
                   exact.maximum_host_release_bytes;
    }
    if (fault == Fault::host_release_exceeds_maximum) {
        return input.maximum_host_release_bytes == 512 &&
               input.maximum_gtt_release_bytes ==
                   exact.maximum_gtt_release_bytes;
    }
    if (fault == Fault::unrelated_demand_miscredit) {
        return input.after.global_gtt_headroom.value == 6144 &&
               input.after.global_host_headroom.value == 3072;
    }
    if (fault == Fault::gtt_causal_mismatch) {
        return input.after.global_gtt_headroom.value == 6144 &&
               same_measurement(
                   input.after.global_host_headroom,
                   exact.after.global_host_headroom);
    }
    if (fault == Fault::host_causal_mismatch) {
        return input.after.global_host_headroom.value == 3072 &&
               same_measurement(
                   input.after.global_gtt_headroom,
                   exact.after.global_gtt_headroom);
    }
    if (fault == Fault::unrelated_gtt_growth_missing) {
        return !input.unrelated_gtt_growth.present &&
               input.unrelated_gtt_growth.value ==
                   exact.unrelated_gtt_growth.value &&
               same_measurement(
                   input.unrelated_host_growth,
                   exact.unrelated_host_growth);
    }
    if (fault == Fault::unrelated_host_growth_missing) {
        return !input.unrelated_host_growth.present &&
               input.unrelated_host_growth.value ==
                   exact.unrelated_host_growth.value &&
               same_measurement(
                   input.unrelated_gtt_growth,
                   exact.unrelated_gtt_growth);
    }
    if (fault == Fault::gtt_causal_checked_add_overflow) {
        return input.after.global_gtt_headroom.value ==
                   input.before.global_gtt_headroom.value + 1 &&
               input.unrelated_gtt_growth.value ==
                   std::numeric_limits<std::uint64_t>::max() &&
               same_measurement(
                   input.after.global_host_headroom,
                   exact.after.global_host_headroom) &&
               same_measurement(
                   input.unrelated_host_growth,
                   exact.unrelated_host_growth);
    }
    if (fault == Fault::host_causal_checked_add_overflow) {
        return input.after.global_host_headroom.value ==
                   input.before.global_host_headroom.value + 1 &&
               input.unrelated_host_growth.value ==
                   std::numeric_limits<std::uint64_t>::max() &&
               same_measurement(
                   input.after.global_gtt_headroom,
                   exact.after.global_gtt_headroom) &&
               same_measurement(
                   input.unrelated_gtt_growth,
                   exact.unrelated_gtt_growth);
    }
    if (fault == Fault::arithmetic_overflow) {
        return input.after.global_gtt_headroom.value ==
                   input.before.global_gtt_headroom.value + 1 &&
               input.after.global_host_headroom.value ==
                   input.before.global_host_headroom.value + 1 &&
               input.unrelated_gtt_growth.value ==
                   std::numeric_limits<std::uint64_t>::max() &&
               input.unrelated_host_growth.value ==
                   std::numeric_limits<std::uint64_t>::max();
    }
    if (fault == Fault::global_headroom_subtraction_underflow) {
        return input.after.global_gtt_headroom.value ==
                   input.before.global_gtt_headroom.value - 1 &&
               input.after.global_host_headroom.value ==
                   input.before.global_host_headroom.value - 1;
    }
    if (fault == Fault::gtt_global_headroom_subtraction_underflow) {
        return input.after.global_gtt_headroom.value ==
                   input.before.global_gtt_headroom.value - 1 &&
               same_measurement(
                   input.after.global_host_headroom,
                   exact.after.global_host_headroom);
    }
    if (fault == Fault::host_global_headroom_subtraction_underflow) {
        return input.after.global_host_headroom.value ==
                   input.before.global_host_headroom.value - 1 &&
               same_measurement(
                   input.after.global_gtt_headroom,
                   exact.after.global_gtt_headroom);
    }
    if (fault == Fault::gtt_cache_release_subtraction_underflow) {
        return input.after.cache_gtt.value ==
                   input.before.cache_gtt.value + 1 &&
               same_measurement(
                   input.after.cache_host, exact.after.cache_host);
    }
    if (fault == Fault::host_cache_release_subtraction_underflow) {
        return input.after.cache_host.value ==
                   input.before.cache_host.value + 1 &&
               same_measurement(
                   input.after.cache_gtt, exact.after.cache_gtt);
    }
    return true;
}

bool dimension_cross_wiring_rejected() {
    const ReleaseInput gtt_limit =
        input_with_fault(Fault::gtt_release_exceeds_maximum);
    ReleaseInput gtt_limit_crossed = gtt_limit;
    gtt_limit_crossed.maximum_host_release_bytes = 512;
    const ReleaseInput host_limit =
        input_with_fault(Fault::host_release_exceeds_maximum);
    ReleaseInput host_limit_crossed = host_limit;
    host_limit_crossed.maximum_gtt_release_bytes = 1024;

    const ReleaseInput gtt_causal =
        input_with_fault(Fault::gtt_causal_mismatch);
    ReleaseInput gtt_causal_crossed = gtt_causal;
    gtt_causal_crossed.after.global_host_headroom.value = 3072;
    const ReleaseInput host_causal =
        input_with_fault(Fault::host_causal_mismatch);
    ReleaseInput host_causal_crossed = host_causal;
    host_causal_crossed.after.global_gtt_headroom.value = 6144;

    const ReleaseInput gtt_overflow =
        input_with_fault(Fault::gtt_causal_checked_add_overflow);
    ReleaseInput gtt_overflow_crossed = gtt_overflow;
    gtt_overflow_crossed.after.global_host_headroom.value =
        gtt_overflow_crossed.before.global_host_headroom.value + 1;
    gtt_overflow_crossed.unrelated_host_growth.value =
        std::numeric_limits<std::uint64_t>::max();
    const ReleaseInput host_overflow =
        input_with_fault(Fault::host_causal_checked_add_overflow);
    ReleaseInput host_overflow_crossed = host_overflow;
    host_overflow_crossed.after.global_gtt_headroom.value =
        host_overflow_crossed.before.global_gtt_headroom.value + 1;
    host_overflow_crossed.unrelated_gtt_growth.value =
        std::numeric_limits<std::uint64_t>::max();

    const ReleaseInput gtt_underflow =
        input_with_fault(Fault::gtt_global_headroom_subtraction_underflow);
    ReleaseInput gtt_underflow_crossed = gtt_underflow;
    gtt_underflow_crossed.after.global_host_headroom.value =
        gtt_underflow_crossed.before.global_host_headroom.value - 1;
    const ReleaseInput host_underflow =
        input_with_fault(Fault::host_global_headroom_subtraction_underflow);
    ReleaseInput host_underflow_crossed = host_underflow;
    host_underflow_crossed.after.global_gtt_headroom.value =
        host_underflow_crossed.before.global_gtt_headroom.value - 1;

    return exact_dimension_fault(
               gtt_limit, Fault::gtt_release_exceeds_maximum) &&
           exact_dimension_fault(
               host_limit, Fault::host_release_exceeds_maximum) &&
           exact_dimension_fault(gtt_causal, Fault::gtt_causal_mismatch) &&
           exact_dimension_fault(host_causal, Fault::host_causal_mismatch) &&
           exact_dimension_fault(
               gtt_overflow, Fault::gtt_causal_checked_add_overflow) &&
           exact_dimension_fault(
               host_overflow, Fault::host_causal_checked_add_overflow) &&
           exact_dimension_fault(
               gtt_underflow,
               Fault::gtt_global_headroom_subtraction_underflow) &&
           exact_dimension_fault(
               host_underflow,
               Fault::host_global_headroom_subtraction_underflow) &&
           !exact_dimension_fault(
               gtt_limit_crossed, Fault::gtt_release_exceeds_maximum) &&
           !exact_dimension_fault(
               host_limit_crossed, Fault::host_release_exceeds_maximum) &&
           !exact_dimension_fault(
               gtt_causal_crossed, Fault::gtt_causal_mismatch) &&
           !exact_dimension_fault(
               host_causal_crossed, Fault::host_causal_mismatch) &&
           !exact_dimension_fault(
               gtt_overflow_crossed,
               Fault::gtt_causal_checked_add_overflow) &&
           !exact_dimension_fault(
               host_overflow_crossed,
               Fault::host_causal_checked_add_overflow) &&
           !exact_dimension_fault(
               gtt_underflow_crossed,
               Fault::gtt_global_headroom_subtraction_underflow) &&
           !exact_dimension_fault(
               host_underflow_crossed,
               Fault::host_global_headroom_subtraction_underflow);
}

bool exact_fault_shape(const ReleaseInput& input, Fault fault) {
    const ReleaseInput exact = canonical_input();
    switch (fault) {
    case Fault::slot_set_missing:
        return !input.request.present && input.request.count == 2 &&
               !input.dispatch_attempted && !input.dispatch_completed;
    case Fault::slot_set_empty:
        return input.request.present && input.request.count == 0 &&
               !input.dispatch_attempted && !input.dispatch_completed;
    case Fault::slot_id_duplicate:
        return input.request.ids[0] == input.request.ids[1] &&
               !input.dispatch_attempted && !input.dispatch_completed;
    case Fault::slot_count_oversized:
        return input.request.count == input.request.ids.size() + 1 &&
               !input.dispatch_attempted && !input.dispatch_completed;
    case Fault::ack_missing:
        return !input.acknowledgement.present &&
               input.dispatch_attempted && input.dispatch_completed;
    case Fault::ack_count_mismatch:
        return input.acknowledgement.present &&
               input.acknowledgement.count == 1;
    case Fault::ack_id_mismatch:
        return input.acknowledgement.ids[0] == 3 &&
               input.acknowledgement.ids[1] == 9;
    case Fault::ack_partial:
        return input.acknowledgement.present &&
               !input.acknowledgement.complete;
    case Fault::dispatch_failure:
        return input.dispatch_attempted && !input.dispatch_completed &&
               !input.after.present;
    case Fault::dispatch_completed_without_attempt:
        return !input.dispatch_attempted && input.dispatch_completed &&
               unchanged_fresh_observation(input);
    case Fault::invalid_precondition_after_dispatch_attempt:
        return !input.request.present && input.dispatch_attempted &&
               !input.dispatch_completed &&
               unchanged_fresh_observation(input);
    case Fault::valid_precondition_without_dispatch:
        return !input.dispatch_attempted && !input.dispatch_completed &&
               precondition_failure_reason(input) == UnknownReason::none &&
               unchanged_fresh_observation(input);
    case Fault::valid_precondition_without_dispatch_changed_post:
        return exact_changed_post_axis(
            input, ChangedPostAxis::runtime_identity);
    case Fault::unsupported_mechanism_after_dispatch_attempt:
        return input.mechanism == Mechanism::absent &&
               input.dispatch_attempted && !input.dispatch_completed &&
               unchanged_fresh_observation(input);
    case Fault::unsupported_mechanism_without_dispatch_changed_post:
        return input.mechanism == Mechanism::absent &&
               exact_changed_post_axis(
                   input, ChangedPostAxis::runtime_identity);
    case Fault::ack_without_physical_delta:
        return input.dispatch_attempted && input.dispatch_completed &&
               exact_acknowledgement(
                   input.request, input.acknowledgement) &&
               unchanged_fresh_observation(input) &&
               input.unrelated_gtt_growth.value == 0 &&
               input.unrelated_host_growth.value == 0;
    case Fault::ack_without_physical_delta_each_axis_changed:
        return exact_no_effect_axis(input, NoEffectAxis::owned_gtt);
    case Fault::pre_identity_mismatch:
        return input.before.identity.resident_id ==
                   exact.before.identity.resident_id + 1 &&
               input.after.identity.resident_id ==
                   exact.after.identity.resident_id + 1 &&
               !input.dispatch_attempted && !input.dispatch_completed;
    case Fault::post_identity_mismatch:
        return input.after.identity.resident_id ==
                   exact.after.identity.resident_id + 1 &&
               same_identity(input.before.identity, exact.before.identity);
    case Fault::backend_instance_birth_token_mismatch:
        return input.after.identity.backend_instance_birth_token ==
               exact.after.identity.backend_instance_birth_token + 1;
    case Fault::process_identity_missing:
        return !input.after.process_identity.present &&
               input.after.process_identity.value ==
                   exact.after.process_identity.value;
    case Fault::process_identity_zero:
        return input.after.process_identity.present &&
               input.after.process_identity.value == 0;
    case Fault::process_identity_changed:
        return changed_physical_identity(
            input.before.process_identity, input.after.process_identity);
    case Fault::weights_identity_missing:
        return !input.after.weights_identity.present &&
               input.after.weights_identity.value ==
                   exact.after.weights_identity.value;
    case Fault::weights_identity_zero:
        return input.after.weights_identity.present &&
               input.after.weights_identity.value == 0;
    case Fault::weights_identity_changed:
        return changed_physical_identity(
            input.before.weights_identity, input.after.weights_identity);
    case Fault::model_residency_identity_missing:
        return !input.after.model_residency_identity.present &&
               input.after.model_residency_identity.value ==
                   exact.after.model_residency_identity.value;
    case Fault::model_residency_identity_zero:
        return input.after.model_residency_identity.present &&
               input.after.model_residency_identity.value == 0;
    case Fault::model_residency_identity_changed:
        return changed_physical_identity(
            input.before.model_residency_identity,
            input.after.model_residency_identity);
    case Fault::pin_missing:
        return input.before.pin.known && !input.after.pin.known &&
               input.before.pin.value == input.after.pin.value;
    case Fault::pin_changed:
        return input.before.pin.known && input.after.pin.known &&
               input.before.pin.value != input.after.pin.value;
    case Fault::device_identity_mismatch:
        return input.after.identity.bindings.device_identity ==
               exact.after.identity.bindings.device_identity + 1;
    case Fault::backend_artifact_mismatch:
        return input.after.identity.bindings.backend_artifact_digest ==
               exact.after.identity.bindings.backend_artifact_digest + 1;
    case Fault::source_build_dependency_mismatch:
        return input.after.identity.bindings.source_build_dependency_closure ==
               exact.after.identity.bindings.source_build_dependency_closure + 1;
    case Fault::driver_runtime_mismatch:
        return input.after.identity.bindings.driver_runtime_closure ==
               exact.after.identity.bindings.driver_runtime_closure + 1;
    case Fault::model_manifest_mismatch:
        return input.after.identity.bindings.model_manifest_digest ==
               exact.after.identity.bindings.model_manifest_digest + 1;
    case Fault::normalized_configuration_mismatch:
        return input.after.identity.bindings.normalized_configuration_digest ==
               exact.after.identity.bindings.normalized_configuration_digest + 1;
    case Fault::evidence_index_mismatch:
        return input.after.identity.bindings.evidence_index_digest ==
               exact.after.identity.bindings.evidence_index_digest + 1;
    case Fault::evidence_liveness_missing:
        return !input.lease.identity.bindings.evidence_liveness_present &&
               !input.dispatch_attempted && !input.dispatch_completed;
    case Fault::evidence_liveness_expired:
        return !input.lease.identity.bindings.evidence_liveness_valid &&
               !input.dispatch_attempted && !input.dispatch_completed;
    case Fault::observation_contract_mismatch:
        return input.after.identity.observation_contract_digest ==
               exact.after.identity.observation_contract_digest + 1;
    case Fault::topology_generation_mismatch:
        return input.after.identity.topology_generation ==
               exact.after.identity.topology_generation + 1;
    case Fault::allocation_group_mismatch:
        return input.after.identity.allocation_group_id ==
               exact.after.identity.allocation_group_id + 1;
    case Fault::action_lease_missing:
        return !input.lease.present && !input.dispatch_attempted &&
               !input.dispatch_completed;
    case Fault::action_lease_mismatch:
        return input.lease.action_lease ==
                   exact.lease.action_lease + 1 &&
               input.requested_action_lease ==
                   exact.requested_action_lease;
    case Fault::action_lease_identity_mismatch:
        return input.lease.identity.resident_id ==
                   exact.lease.identity.resident_id + 1 &&
               same_identity(input.resident, exact.resident) &&
               same_identity(input.before.identity, exact.before.identity);
    case Fault::action_lease_evidence_liveness_lease_mismatch:
        return input.lease.identity.bindings.evidence_liveness_lease ==
                   exact.lease.identity.bindings.evidence_liveness_lease + 1 &&
               input.resident.bindings.evidence_liveness_lease ==
                   exact.resident.bindings.evidence_liveness_lease &&
               input.before.identity.bindings.evidence_liveness_lease ==
                   exact.before.identity.bindings.evidence_liveness_lease;
    case Fault::action_lease_wrong_state:
        return input.lease.state == LeaseState::idle;
    case Fault::action_lease_wrong_operation:
        return input.lease.operation == Operation::pressure_hard_release;
    case Fault::action_lease_wrong_slot_set:
        return input.lease.slot_set.ids[0] == 3 &&
               input.lease.slot_set.ids[1] == 9;
    case Fault::action_lease_wrong_claim_generation:
        return input.lease.claim_generation ==
               exact.lease.claim_generation + 1;
    case Fault::action_lease_wrong_pre_observation_generation:
        return input.lease.pre_observation_generation ==
               exact.lease.pre_observation_generation + 1;
    case Fault::action_lease_wrong_observation_contract:
        return input.lease.observation_contract_digest ==
               exact.lease.observation_contract_digest + 1;
    case Fault::resident_generation_mismatch:
        return input.after.identity.resident_generation ==
               exact.after.identity.resident_generation + 1;
    case Fault::observation_generation_mismatch:
        return input.after.generation == input.before.generation;
    case Fault::each_required_identity_token_zero:
        return input.resident.resident_id == 0 &&
               input.lease.identity.resident_id == 0 &&
               input.before.identity.resident_id == 0;
    case Fault::observation_before_missing:
        return !input.before.present && input.after.present;
    case Fault::observation_before_stale:
        return !input.before.fresh && input.after.fresh;
    case Fault::observation_before_skew:
        return !input.before.within_skew && input.after.within_skew;
    case Fault::observation_before_unhealthy:
        return !input.before.healthy && input.after.healthy;
    case Fault::observation_before_incomplete:
        return !input.before.complete && input.after.complete;
    case Fault::observation_before_gtt_effect_missing:
    case Fault::observation_before_host_effect_missing:
    case Fault::gtt_release_partial:
    case Fault::host_release_partial:
    case Fault::gtt_cache_nonzero_after:
    case Fault::host_cache_nonzero_after:
    case Fault::gtt_effect_missing:
    case Fault::host_effect_missing:
    case Fault::global_gtt_headroom_missing:
    case Fault::global_host_headroom_missing:
    case Fault::ledger_weights_gtt_measurement_missing:
    case Fault::ledger_weights_host_measurement_missing:
    case Fault::ledger_cache_gtt_measurement_missing:
    case Fault::ledger_cache_host_measurement_missing:
    case Fault::ledger_weights_gtt_bytes_mismatch:
    case Fault::ledger_weights_host_bytes_mismatch:
    case Fault::ledger_cache_gtt_bytes_mismatch:
    case Fault::ledger_cache_host_bytes_mismatch:
    case Fault::gtt_release_exceeds_maximum:
    case Fault::host_release_exceeds_maximum:
    case Fault::gtt_causal_mismatch:
    case Fault::host_causal_mismatch:
    case Fault::unrelated_gtt_growth_missing:
    case Fault::unrelated_host_growth_missing:
    case Fault::gtt_causal_checked_add_overflow:
    case Fault::host_causal_checked_add_overflow:
    case Fault::gtt_global_headroom_subtraction_underflow:
    case Fault::host_global_headroom_subtraction_underflow:
    case Fault::gtt_cache_release_subtraction_underflow:
    case Fault::host_cache_release_subtraction_underflow:
        return exact_dimension_fault(input, fault);
    case Fault::observation_after_missing:
        return !input.after.present;
    case Fault::observation_after_stale:
        return !input.after.fresh;
    case Fault::observation_after_skew:
        return !input.after.within_skew;
    case Fault::observation_after_unhealthy:
        return !input.after.healthy;
    case Fault::observation_after_incomplete:
        return !input.after.complete;
    case Fault::release_partial:
        return input.after.cache_gtt.value == 1024 &&
               input.after.cache_host.value == 512;
    case Fault::cache_nonzero_after:
        return input.after.cache_gtt.value == 1 &&
               input.after.cache_host.value == 1;
    case Fault::ledger_claim_group_missing:
        return input.ledger_before.count == 1;
    case Fault::ledger_claim_group_duplicate:
        return same_claim(
            input.ledger_before.groups[0], input.ledger_before.groups[1]);
    case Fault::ledger_claim_group_wrong_id:
        return input.ledger_before.groups[1].id == ClaimId::invalid;
    case Fault::ledger_weights_group_wrong_id:
        return input.ledger_before.groups[0].id == ClaimId::invalid;
    case Fault::ledger_claim_group_wrong_effect:
        return input.ledger_before.groups[1].effect ==
               Effect::persistent_weights;
    case Fault::ledger_weights_group_wrong_effect:
        return input.ledger_before.groups[0].effect ==
               Effect::reconstructible_state;
    case Fault::ledger_claim_group_wrong_allocation:
        return input.ledger_before.groups[1].allocation_group_id ==
               exact.ledger_before.groups[1].allocation_group_id + 1;
    case Fault::ledger_weights_group_wrong_allocation:
        return input.ledger_before.groups[0].allocation_group_id ==
               exact.ledger_before.groups[0].allocation_group_id + 1;
    case Fault::ledger_weights_bytes_mismatch:
        return input.ledger_before.groups[0].gtt.value ==
                   exact.ledger_before.groups[0].gtt.value + 1 &&
               input.ledger_before.groups[0].host.value ==
                   exact.ledger_before.groups[0].host.value + 1;
    case Fault::ledger_cache_bytes_mismatch:
        return input.ledger_before.groups[1].gtt.value ==
                   exact.ledger_before.groups[1].gtt.value + 1 &&
               input.ledger_before.groups[1].host.value ==
                   exact.ledger_before.groups[1].host.value + 1;
    case Fault::effect_out_of_envelope:
        return input.maximum_gtt_release_bytes == 1024 &&
               input.maximum_host_release_bytes == 512;
    case Fault::unrelated_demand_miscredit:
        return input.after.global_gtt_headroom.value == 6144 &&
               input.after.global_host_headroom.value == 3072;
    case Fault::observation_generation_increment_overflow:
        return input.dispatch_attempted && input.dispatch_completed &&
               input.before.generation ==
                   std::numeric_limits<std::uint64_t>::max() &&
               input.lease.pre_observation_generation ==
                   input.before.generation &&
               input.after.generation == 0;
    case Fault::arithmetic_overflow:
        return input.after.global_gtt_headroom.value ==
                   input.before.global_gtt_headroom.value + 1 &&
               input.after.global_host_headroom.value ==
                   input.before.global_host_headroom.value + 1 &&
               input.unrelated_gtt_growth.value ==
                   std::numeric_limits<std::uint64_t>::max() &&
               input.unrelated_host_growth.value ==
                   std::numeric_limits<std::uint64_t>::max();
    case Fault::ledger_generation_increment_overflow:
        return input.ledger_before.generation ==
                   std::numeric_limits<std::uint64_t>::max() &&
               input.lease.claim_generation ==
                   input.ledger_before.generation &&
               input.ledger_commit_generation ==
                   input.ledger_before.generation;
    case Fault::global_headroom_subtraction_underflow:
        return input.after.global_gtt_headroom.value + 1 ==
                   input.before.global_gtt_headroom.value &&
               input.after.global_host_headroom.value + 1 ==
                   input.before.global_host_headroom.value;
    case Fault::cache_release_subtraction_underflow:
        return input.after.cache_gtt.value ==
                   input.before.cache_gtt.value + 1 &&
               input.after.cache_host.value ==
                   input.before.cache_host.value + 1;
    case Fault::ledger_generation_stale:
        return input.ledger_commit_generation ==
               input.ledger_before.generation + 1;
    case Fault::ledger_commit_failure:
        return !input.ledger_commit_succeeds;
    case Fault::active_use:
        return input.lease.active_use && !input.dispatch_attempted &&
               !input.dispatch_completed;
    }
    return false;
}

bool is_physical_identity_missing_fault(Fault fault) {
    return fault == Fault::process_identity_missing ||
           fault == Fault::weights_identity_missing ||
           fault == Fault::model_residency_identity_missing;
}

bool exact_physical_identity_missing(
    const ReleaseInput& input, Fault fault, bool before_missing) {
    const PhysicalObservation& missing =
        before_missing ? input.before : input.after;
    const PhysicalObservation& present =
        before_missing ? input.after : input.before;
    const bool common =
        input.before.process_identity.value ==
            input.after.process_identity.value &&
        input.before.weights_identity.value ==
            input.after.weights_identity.value &&
        input.before.model_residency_identity.value ==
            input.after.model_residency_identity.value &&
        input.before.pin.known == input.after.pin.known &&
        input.before.pin.value == input.after.pin.value;
    if (fault == Fault::process_identity_missing) {
        return common && !missing.process_identity.present &&
               present.process_identity.present &&
               missing.weights_identity.present &&
               present.weights_identity.present &&
               missing.model_residency_identity.present &&
               present.model_residency_identity.present;
    }
    if (fault == Fault::weights_identity_missing) {
        return common && missing.process_identity.present &&
               present.process_identity.present &&
               !missing.weights_identity.present &&
               present.weights_identity.present &&
               missing.model_residency_identity.present &&
               present.model_residency_identity.present;
    }
    return common && missing.process_identity.present &&
           present.process_identity.present && missing.weights_identity.present &&
           present.weights_identity.present &&
           !missing.model_residency_identity.present &&
           present.model_residency_identity.present;
}

bool identity_missing_quarantined(
    const ReleaseInput& input,
    const Verification& result,
    bool dispatch_called) {
    return result.evidence == Evidence::unknown &&
           result.disposition == Disposition::quarantine &&
           result.reason == UnknownReason::identity &&
           result.dispatch_called == dispatch_called &&
           !result.ledger_changed &&
           same_ledger(input.ledger_before, result.ledger_after) &&
           !result.residency_preserved && result.claims_maximized &&
           result.credited_gtt_bytes == 0 &&
           result.credited_host_bytes == 0;
}

constexpr std::array<NegativeCase, 118> negative_cases = {
    NegativeCase{Fault::slot_set_missing, "negative.slot_set_missing"},
    NegativeCase{Fault::slot_set_empty, "negative.slot_set_empty"},
    NegativeCase{Fault::slot_id_duplicate, "negative.slot_id_duplicate"},
    NegativeCase{
        Fault::slot_count_oversized,
        "negative.slot_count_oversized",
    },
    NegativeCase{Fault::ack_missing, "negative.ack_missing"},
    NegativeCase{Fault::ack_count_mismatch, "negative.ack_count_mismatch"},
    NegativeCase{Fault::ack_id_mismatch, "negative.ack_id_mismatch"},
    NegativeCase{Fault::ack_partial, "negative.ack_partial"},
    NegativeCase{Fault::dispatch_failure, "negative.dispatch_failure"},
    NegativeCase{
        Fault::dispatch_completed_without_attempt,
        "negative.dispatch_completed_without_attempt",
    },
    NegativeCase{
        Fault::invalid_precondition_after_dispatch_attempt,
        "negative.invalid_precondition_after_dispatch_attempt",
    },
    NegativeCase{
        Fault::valid_precondition_without_dispatch,
        "negative.valid_precondition_without_dispatch",
    },
    NegativeCase{
        Fault::valid_precondition_without_dispatch_changed_post,
        "negative.valid_precondition_without_dispatch_changed_post",
    },
    NegativeCase{
        Fault::unsupported_mechanism_after_dispatch_attempt,
        "negative.unsupported_mechanism_after_dispatch_attempt",
    },
    NegativeCase{
        Fault::unsupported_mechanism_without_dispatch_changed_post,
        "negative.unsupported_mechanism_without_dispatch_changed_post",
    },
    NegativeCase{
        Fault::ack_without_physical_delta,
        "negative.ack_without_physical_delta",
    },
    NegativeCase{
        Fault::ack_without_physical_delta_each_axis_changed,
        "negative.ack_without_physical_delta_each_axis_changed",
    },
    NegativeCase{Fault::pre_identity_mismatch, "negative.pre_identity_mismatch"},
    NegativeCase{Fault::post_identity_mismatch, "negative.post_identity_mismatch"},
    NegativeCase{
        Fault::backend_instance_birth_token_mismatch,
        "negative.backend_instance_birth_token_mismatch",
    },
    NegativeCase{
        Fault::process_identity_missing,
        "negative.process_identity_missing",
    },
    NegativeCase{
        Fault::process_identity_zero,
        "negative.process_identity_zero",
    },
    NegativeCase{
        Fault::process_identity_changed,
        "negative.process_identity_changed",
    },
    NegativeCase{
        Fault::weights_identity_missing,
        "negative.weights_identity_missing",
    },
    NegativeCase{
        Fault::weights_identity_zero,
        "negative.weights_identity_zero",
    },
    NegativeCase{
        Fault::weights_identity_changed,
        "negative.weights_identity_changed",
    },
    NegativeCase{
        Fault::model_residency_identity_missing,
        "negative.model_residency_identity_missing",
    },
    NegativeCase{
        Fault::model_residency_identity_zero,
        "negative.model_residency_identity_zero",
    },
    NegativeCase{
        Fault::model_residency_identity_changed,
        "negative.model_residency_identity_changed",
    },
    NegativeCase{Fault::pin_missing, "negative.pin_missing"},
    NegativeCase{Fault::pin_changed, "negative.pin_changed"},
    NegativeCase{
        Fault::device_identity_mismatch,
        "negative.device_identity_mismatch",
    },
    NegativeCase{
        Fault::backend_artifact_mismatch,
        "negative.backend_artifact_mismatch",
    },
    NegativeCase{
        Fault::source_build_dependency_mismatch,
        "negative.source_build_dependency_mismatch",
    },
    NegativeCase{
        Fault::driver_runtime_mismatch,
        "negative.driver_runtime_mismatch",
    },
    NegativeCase{
        Fault::model_manifest_mismatch,
        "negative.model_manifest_mismatch",
    },
    NegativeCase{
        Fault::normalized_configuration_mismatch,
        "negative.normalized_configuration_mismatch",
    },
    NegativeCase{
        Fault::evidence_index_mismatch,
        "negative.evidence_index_mismatch",
    },
    NegativeCase{
        Fault::evidence_liveness_missing,
        "negative.evidence_liveness_missing",
    },
    NegativeCase{
        Fault::evidence_liveness_expired,
        "negative.evidence_liveness_expired",
    },
    NegativeCase{
        Fault::observation_contract_mismatch,
        "negative.observation_contract_mismatch",
    },
    NegativeCase{
        Fault::topology_generation_mismatch,
        "negative.topology_generation_mismatch",
    },
    NegativeCase{
        Fault::allocation_group_mismatch,
        "negative.allocation_group_mismatch",
    },
    NegativeCase{Fault::action_lease_missing, "negative.action_lease_missing"},
    NegativeCase{Fault::action_lease_mismatch, "negative.action_lease_mismatch"},
    NegativeCase{
        Fault::action_lease_identity_mismatch,
        "negative.action_lease_identity_mismatch",
    },
    NegativeCase{
        Fault::action_lease_evidence_liveness_lease_mismatch,
        "negative.action_lease_evidence_liveness_lease_mismatch",
    },
    NegativeCase{
        Fault::action_lease_wrong_state,
        "negative.action_lease_wrong_state",
    },
    NegativeCase{
        Fault::action_lease_wrong_operation,
        "negative.action_lease_wrong_operation",
    },
    NegativeCase{
        Fault::action_lease_wrong_slot_set,
        "negative.action_lease_wrong_slot_set",
    },
    NegativeCase{
        Fault::action_lease_wrong_claim_generation,
        "negative.action_lease_wrong_claim_generation",
    },
    NegativeCase{
        Fault::action_lease_wrong_pre_observation_generation,
        "negative.action_lease_wrong_pre_observation_generation",
    },
    NegativeCase{
        Fault::action_lease_wrong_observation_contract,
        "negative.action_lease_wrong_observation_contract",
    },
    NegativeCase{
        Fault::resident_generation_mismatch,
        "negative.resident_generation_mismatch",
    },
    NegativeCase{
        Fault::observation_generation_mismatch,
        "negative.observation_generation_mismatch",
    },
    NegativeCase{
        Fault::each_required_identity_token_zero,
        "negative.each_required_identity_token_zero",
    },
    NegativeCase{
        Fault::observation_before_missing,
        "negative.observation_before_missing",
    },
    NegativeCase{
        Fault::observation_before_stale,
        "negative.observation_before_stale",
    },
    NegativeCase{
        Fault::observation_before_skew,
        "negative.observation_before_skew",
    },
    NegativeCase{
        Fault::observation_before_unhealthy,
        "negative.observation_before_unhealthy",
    },
    NegativeCase{
        Fault::observation_before_incomplete,
        "negative.observation_before_incomplete",
    },
    NegativeCase{
        Fault::observation_before_gtt_effect_missing,
        "negative.observation_before_gtt_effect_missing",
    },
    NegativeCase{
        Fault::observation_before_host_effect_missing,
        "negative.observation_before_host_effect_missing",
    },
    NegativeCase{
        Fault::observation_after_missing,
        "negative.observation_after_missing",
    },
    NegativeCase{
        Fault::observation_after_stale,
        "negative.observation_after_stale",
    },
    NegativeCase{
        Fault::observation_after_skew,
        "negative.observation_after_skew",
    },
    NegativeCase{
        Fault::observation_after_unhealthy,
        "negative.observation_after_unhealthy",
    },
    NegativeCase{
        Fault::observation_after_incomplete,
        "negative.observation_after_incomplete",
    },
    NegativeCase{Fault::release_partial, "negative.release_partial"},
    NegativeCase{Fault::gtt_release_partial, "negative.gtt_release_partial"},
    NegativeCase{Fault::host_release_partial, "negative.host_release_partial"},
    NegativeCase{Fault::cache_nonzero_after, "negative.cache_nonzero_after"},
    NegativeCase{
        Fault::gtt_cache_nonzero_after,
        "negative.gtt_cache_nonzero_after",
    },
    NegativeCase{
        Fault::host_cache_nonzero_after,
        "negative.host_cache_nonzero_after",
    },
    NegativeCase{Fault::gtt_effect_missing, "negative.gtt_effect_missing"},
    NegativeCase{Fault::host_effect_missing, "negative.host_effect_missing"},
    NegativeCase{
        Fault::global_gtt_headroom_missing,
        "negative.global_gtt_headroom_missing",
    },
    NegativeCase{
        Fault::global_host_headroom_missing,
        "negative.global_host_headroom_missing",
    },
    NegativeCase{
        Fault::ledger_claim_group_missing,
        "negative.ledger_claim_group_missing",
    },
    NegativeCase{
        Fault::ledger_claim_group_duplicate,
        "negative.ledger_claim_group_duplicate",
    },
    NegativeCase{
        Fault::ledger_claim_group_wrong_id,
        "negative.ledger_claim_group_wrong_id",
    },
    NegativeCase{
        Fault::ledger_weights_group_wrong_id,
        "negative.ledger_weights_group_wrong_id",
    },
    NegativeCase{
        Fault::ledger_claim_group_wrong_effect,
        "negative.ledger_claim_group_wrong_effect",
    },
    NegativeCase{
        Fault::ledger_weights_group_wrong_effect,
        "negative.ledger_weights_group_wrong_effect",
    },
    NegativeCase{
        Fault::ledger_claim_group_wrong_allocation,
        "negative.ledger_claim_group_wrong_allocation",
    },
    NegativeCase{
        Fault::ledger_weights_group_wrong_allocation,
        "negative.ledger_weights_group_wrong_allocation",
    },
    NegativeCase{
        Fault::ledger_weights_gtt_measurement_missing,
        "negative.ledger_weights_gtt_measurement_missing",
    },
    NegativeCase{
        Fault::ledger_weights_host_measurement_missing,
        "negative.ledger_weights_host_measurement_missing",
    },
    NegativeCase{
        Fault::ledger_cache_gtt_measurement_missing,
        "negative.ledger_cache_gtt_measurement_missing",
    },
    NegativeCase{
        Fault::ledger_cache_host_measurement_missing,
        "negative.ledger_cache_host_measurement_missing",
    },
    NegativeCase{
        Fault::ledger_weights_bytes_mismatch,
        "negative.ledger_weights_bytes_mismatch",
    },
    NegativeCase{
        Fault::ledger_weights_gtt_bytes_mismatch,
        "negative.ledger_weights_gtt_bytes_mismatch",
    },
    NegativeCase{
        Fault::ledger_weights_host_bytes_mismatch,
        "negative.ledger_weights_host_bytes_mismatch",
    },
    NegativeCase{
        Fault::ledger_cache_bytes_mismatch,
        "negative.ledger_cache_bytes_mismatch",
    },
    NegativeCase{
        Fault::ledger_cache_gtt_bytes_mismatch,
        "negative.ledger_cache_gtt_bytes_mismatch",
    },
    NegativeCase{
        Fault::ledger_cache_host_bytes_mismatch,
        "negative.ledger_cache_host_bytes_mismatch",
    },
    NegativeCase{
        Fault::effect_out_of_envelope,
        "negative.effect_out_of_envelope",
    },
    NegativeCase{
        Fault::gtt_release_exceeds_maximum,
        "negative.gtt_release_exceeds_maximum",
    },
    NegativeCase{
        Fault::host_release_exceeds_maximum,
        "negative.host_release_exceeds_maximum",
    },
    NegativeCase{
        Fault::unrelated_demand_miscredit,
        "negative.unrelated_demand_miscredit",
    },
    NegativeCase{Fault::gtt_causal_mismatch, "negative.gtt_causal_mismatch"},
    NegativeCase{
        Fault::host_causal_mismatch,
        "negative.host_causal_mismatch",
    },
    NegativeCase{
        Fault::unrelated_gtt_growth_missing,
        "negative.unrelated_gtt_growth_missing",
    },
    NegativeCase{
        Fault::unrelated_host_growth_missing,
        "negative.unrelated_host_growth_missing",
    },
    NegativeCase{
        Fault::observation_generation_increment_overflow,
        "negative.observation_generation_increment_overflow",
    },
    NegativeCase{Fault::arithmetic_overflow, "negative.arithmetic_overflow"},
    NegativeCase{
        Fault::gtt_causal_checked_add_overflow,
        "negative.gtt_causal_checked_add_overflow",
    },
    NegativeCase{
        Fault::host_causal_checked_add_overflow,
        "negative.host_causal_checked_add_overflow",
    },
    NegativeCase{
        Fault::ledger_generation_increment_overflow,
        "negative.ledger_generation_increment_overflow",
    },
    NegativeCase{
        Fault::global_headroom_subtraction_underflow,
        "negative.global_headroom_subtraction_underflow",
    },
    NegativeCase{
        Fault::gtt_global_headroom_subtraction_underflow,
        "negative.gtt_global_headroom_subtraction_underflow",
    },
    NegativeCase{
        Fault::host_global_headroom_subtraction_underflow,
        "negative.host_global_headroom_subtraction_underflow",
    },
    NegativeCase{
        Fault::cache_release_subtraction_underflow,
        "negative.cache_release_subtraction_underflow",
    },
    NegativeCase{
        Fault::gtt_cache_release_subtraction_underflow,
        "negative.gtt_cache_release_subtraction_underflow",
    },
    NegativeCase{
        Fault::host_cache_release_subtraction_underflow,
        "negative.host_cache_release_subtraction_underflow",
    },
    NegativeCase{
        Fault::ledger_generation_stale,
        "negative.ledger_generation_stale",
    },
    NegativeCase{
        Fault::ledger_commit_failure,
        "negative.ledger_commit_failure",
    },
    NegativeCase{Fault::active_use, "negative.active_use"},
};

const char* claim_id_name(ClaimId id) {
    return id == ClaimId::model_weights ? "model_weights" : "slot_cache";
}

const char* effect_name(Effect effect) {
    return effect == Effect::persistent_weights ? "persistent_weights"
                                                : "reconstructible_state";
}

const char* passed_or_failed(bool passed) {
    return passed ? "passed" : "failed";
}

const char* preserved_or_failed(bool preserved) {
    return preserved ? "preserved" : "failed";
}

void emit(const char* key, const char* value) {
    std::cout << key << '=' << value << '\n';
}

void emit_number(const char* key, std::uint64_t value) {
    std::cout << key << '=' << value << '\n';
}

void emit_rows(const char* const* rows, std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        std::cout << rows[index] << '\n';
    }
}

const char* current_platform() {
#ifdef _WIN32
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

int run() {
    const ReleaseInput input = canonical_input();
    const Verification verified = verify_soft_release(input);
    const ReleaseInput slot_missing_input =
        input_with_fault(Fault::slot_set_missing);
    const Verification slot_missing = verify_soft_release(slot_missing_input);
    const ReleaseInput no_effect_input =
        input_with_fault(Fault::ack_without_physical_delta);
    const Verification no_effect = verify_soft_release(no_effect_input);
    const ReleaseInput dispatch_failure_input =
        input_with_fault(Fault::dispatch_failure);
    const Verification dispatch_failure =
        verify_soft_release(dispatch_failure_input);
    const Verification stale_ledger = verify_soft_release(
        input_with_fault(Fault::ledger_generation_stale));
    const Verification failed_commit = verify_soft_release(
        input_with_fault(Fault::ledger_commit_failure));
    const Verification active_use =
        verify_soft_release(input_with_fault(Fault::active_use));

    ReleaseInput absent_input = canonical_input();
    absent_input.mechanism = Mechanism::absent;
    absent_input.dispatch_attempted = false;
    absent_input.dispatch_completed = false;
    set_unchanged_post_observation(absent_input);
    const Verification capability_absent = verify_soft_release(absent_input);
    ReleaseInput noop_input = canonical_input();
    noop_input.mechanism = Mechanism::successful_noop;
    noop_input.dispatch_attempted = false;
    noop_input.dispatch_completed = false;
    set_unchanged_post_observation(noop_input);
    const Verification successful_noop = verify_soft_release(noop_input);

    const ReportingFixture valid_reporting =
        reporting_fixture(ReportingFault::none);
    const ReportingFixture invalid_reporting =
        reporting_fixture(ReportingFault::missing);
    const PressureMode valid_reporting_mode = select_pressure_mode(
        valid_reporting.evidence, valid_reporting.expected_target);
    const PressureMode invalid_reporting_mode = select_pressure_mode(
        invalid_reporting.evidence, invalid_reporting.expected_target);
    const bool reporting_fault_shapes = reporting_fault_shapes_pass();
    const bool reporting_identity_topology_matrix =
        reporting_identity_topology_matrix_passes();
    const bool unsupported_pre_observation_matrix =
        unsupported_pre_observation_matrix_passes();
    const bool neutral_verification_witnesses =
        neutral_verification_witnesses_pass();
    const bool valid_reporting_contract = unsupported_pair_passes(
        ReportingFault::none, PressureMode::report_only);
    const bool incoherent_reporting_contract =
        reporting_identity_topology_matrix &&
        unsupported_pair_passes(
            ReportingFault::incoherent_gtt_sample_missing,
            PressureMode::disabled_invalid_evidence) &&
        unsupported_pair_passes(
            ReportingFault::incoherent_host_sample_missing,
            PressureMode::disabled_invalid_evidence) &&
        unsupported_pair_passes(
            ReportingFault::incoherent_generation,
            PressureMode::disabled_invalid_evidence) &&
        unsupported_pair_passes(
            ReportingFault::incoherent_identity,
            PressureMode::disabled_invalid_evidence) &&
        unsupported_pair_passes(
            ReportingFault::incoherent_generation_zero,
            PressureMode::disabled_invalid_evidence) &&
        unsupported_pair_passes(
            ReportingFault::incoherent_generation_overflow,
            PressureMode::disabled_invalid_evidence) &&
        unsupported_pair_passes(
            ReportingFault::incoherent_equal_invalid_identity,
            PressureMode::disabled_invalid_evidence) &&
        unsupported_pair_passes(
            ReportingFault::incoherent_affected_only_wrong_target,
            PressureMode::disabled_invalid_evidence) &&
        unsupported_pair_passes(
            ReportingFault::incoherent_samples_only_wrong_target,
            PressureMode::disabled_invalid_evidence) &&
        unsupported_pair_passes(
            ReportingFault::incoherent_wrong_target,
            PressureMode::disabled_invalid_evidence);
    const bool reporting_guard_contract =
        reporting_fault_shapes && reporting_identity_topology_matrix;
    const char* rocm_valid_fallback =
        select_fallback(FallbackProfile::rocm, valid_reporting_mode);
    const char* rocm_invalid_fallback =
        select_fallback(FallbackProfile::rocm, invalid_reporting_mode);
    const char* vulkan_valid_fallback =
        select_fallback(FallbackProfile::vulkan, valid_reporting_mode);
    const char* vulkan_invalid_fallback =
        select_fallback(FallbackProfile::vulkan, invalid_reporting_mode);
    constexpr std::array<ReportingFault, 15> invalid_reporting_faults = {
        ReportingFault::missing,
        ReportingFault::stale,
        ReportingFault::skew,
        ReportingFault::unhealthy,
        ReportingFault::incomplete,
        ReportingFault::incoherent_gtt_sample_missing,
        ReportingFault::incoherent_host_sample_missing,
        ReportingFault::incoherent_generation,
        ReportingFault::incoherent_identity,
        ReportingFault::incoherent_generation_zero,
        ReportingFault::incoherent_generation_overflow,
        ReportingFault::incoherent_equal_invalid_identity,
        ReportingFault::incoherent_affected_only_wrong_target,
        ReportingFault::incoherent_samples_only_wrong_target,
        ReportingFault::incoherent_wrong_target,
    };
    constexpr std::array<Mechanism, 2> unsupported_mechanisms = {
        Mechanism::absent,
        Mechanism::successful_noop,
    };
    bool invalid_reporting_modes = true;
    bool invalid_reporting_no_dispatch = true;
    bool invalid_reporting_post_unchanged = true;
    bool invalid_reporting_ledger_unchanged = true;
    bool invalid_reporting_residency_preserved = true;
    for (const ReportingFault reporting_fault : invalid_reporting_faults) {
        for (const Mechanism mechanism : unsupported_mechanisms) {
            const UnsupportedObservation observation =
                evaluate_unsupported(
                    mechanism,
                    reporting_fault,
                    UnsupportedPreObservationFault::none);
            invalid_reporting_modes =
                invalid_reporting_modes && observation.detected &&
                observation.mode ==
                    PressureMode::disabled_invalid_evidence;
            invalid_reporting_no_dispatch =
                invalid_reporting_no_dispatch && observation.no_dispatch;
            invalid_reporting_post_unchanged =
                invalid_reporting_post_unchanged &&
                observation.post_observation_available &&
                observation.post_observation_unchanged_fresh;
            invalid_reporting_ledger_unchanged =
                invalid_reporting_ledger_unchanged &&
                observation.ledger_unchanged;
            invalid_reporting_residency_preserved =
                invalid_reporting_residency_preserved &&
                observation.residency_preserved &&
                observation.neutral_fields;
        }
    }

    constexpr std::array<const char*, 15> source_and_mechanism_rows = {
        "source.llamacpp_slot_erase=logical_cache_reset",
        "source.llamacpp_ack_validation=absent",
        "source.llamacpp_physical_observer=absent",
        "source.llamacpp_ledger_reconciliation=absent",
        "source.llamacpp_empty_slot_success=possible",
        "source.wrapped_server_default_downsize=noop_success",
        "source.wrapped_server_default_support=unsupported",
        "source.downsized_state_physical_proof=absent",
        "source.current_release_authority=fallback",
        "mechanism.id=llamacpp_slot_cache_erase_v1",
        "mechanism.capability=explicit",
        "mechanism.slot_selection=exact",
        "mechanism.acknowledgement=exact",
        "mechanism.physical_observation=required",
        "mechanism.ledger_reconciliation=required",
    };
    emit_rows(source_and_mechanism_rows.data(), source_and_mechanism_rows.size());

    constexpr std::array<const char*, 15> required_identity_rows = {
        "identity.device_identity=required",
        "identity.backend_artifact_digest=required",
        "identity.source_build_dependency_closure=required",
        "identity.driver_runtime_closure=required",
        "identity.model_manifest_digest=required",
        "identity.normalized_configuration_digest=required",
        "identity.evidence_index_digest=required",
        "identity.evidence_liveness_lease=required",
        "identity.resident_id=required",
        "identity.resident_generation=required",
        "identity.backend_instance_birth_token=required",
        "identity.topology_generation=required",
        "identity.allocation_group_id=required",
        "identity.ledger_generation=required",
        "identity.action_lease=required",
    };
    emit_rows(required_identity_rows.data(), required_identity_rows.size());
    emit(
        "identity.action_lease_binding",
        action_lease_matches(input) ? "matched" : "failed");
    emit(
        "identity.action_lease_state",
        input.lease.state == LeaseState::idle_soft_reclaiming
            ? "idle_soft_reclaiming"
            : "failed");
    emit(
        "identity.action_lease_operation",
        input.lease.operation == Operation::pressure_soft_release
            ? "pressure_soft_release"
            : "failed");
    emit(
        "identity.action_lease_slot_set_binding",
        same_slot_set(input.lease.slot_set, input.request) ? "matched" : "failed");
    emit(
        "identity.action_lease_claim_generation_binding",
        input.lease.claim_generation == input.ledger_before.generation
            ? "matched"
            : "failed");
    emit("identity.observation_contract_digest", "required");
    emit("identity.pre_observation_generation", "required");
    emit("identity.post_observation_generation", "derived_checked");
    emit_number(
        "identity.required_token_count", required_identity_tokens.size());
    emit(
        "identity.required_tokens_nonzero",
        passed_or_failed(required_identity_tokens_nonzero(input)));
    emit(
        "identity.exact_match",
        passed_or_failed(
            verified.evidence == Evidence::verified &&
            same_identity(input.resident, input.lease.identity) &&
            same_identity(input.resident, input.before.identity) &&
            same_identity(input.resident, input.after.identity)));

    emit_number("slot.request_count", input.request.count);
    std::cout << "slot.request_ids=" << input.request.ids[0] << '.'
              << input.request.ids[1] << '\n';
    emit_number("slot.ack_count", input.acknowledgement.count);
    std::cout << "slot.ack_ids=" << input.acknowledgement.ids[0] << '.'
              << input.acknowledgement.ids[1] << '\n';
    emit(
        "slot.ack_match",
        passed_or_failed(exact_acknowledgement(
            input.request, input.acknowledgement)));

    emit("observation.before_present", passed_or_failed(input.before.present));
    emit("observation.before_fresh", passed_or_failed(input.before.fresh));
    emit("observation.before_skew", passed_or_failed(input.before.within_skew));
    emit("observation.before_healthy", passed_or_failed(input.before.healthy));
    emit("observation.before_complete", passed_or_failed(input.before.complete));
    emit("observation.after_present", passed_or_failed(input.after.present));
    emit("observation.after_fresh", passed_or_failed(input.after.fresh));
    emit("observation.after_skew", passed_or_failed(input.after.within_skew));
    emit("observation.after_healthy", passed_or_failed(input.after.healthy));
    emit("observation.after_complete", passed_or_failed(input.after.complete));
    emit_number("observation.generation_before", input.before.generation);
    emit_number("observation.generation_after", input.after.generation);
    const ArithmeticResult next_observation_generation =
        checked_increment(input.before.generation);
    emit(
        "observation.generation_increment",
        passed_or_failed(
            next_observation_generation.known &&
            next_observation_generation.value == input.after.generation));

    emit_number(
        "physical.cache_gtt_before_bytes", input.before.cache_gtt.value);
    emit_number("physical.cache_gtt_after_bytes", input.after.cache_gtt.value);
    emit_number(
        "physical.cache_gtt_released_bytes",
        verified.owned_gtt_release_bytes);
    emit_number(
        "physical.cache_host_before_bytes", input.before.cache_host.value);
    emit_number("physical.cache_host_after_bytes", input.after.cache_host.value);
    emit_number(
        "physical.cache_host_released_bytes",
        verified.owned_host_release_bytes);
    emit(
        "physical.global_gtt_headroom_before_present",
        passed_or_failed(input.before.global_gtt_headroom.present));
    emit_number(
        "physical.global_gtt_headroom_before_bytes",
        input.before.global_gtt_headroom.value);
    emit(
        "physical.global_gtt_headroom_after_present",
        passed_or_failed(input.after.global_gtt_headroom.present));
    emit_number(
        "physical.global_gtt_headroom_after_bytes",
        input.after.global_gtt_headroom.value);
    emit(
        "physical.global_host_headroom_before_present",
        passed_or_failed(input.before.global_host_headroom.present));
    emit_number(
        "physical.global_host_headroom_before_bytes",
        input.before.global_host_headroom.value);
    emit(
        "physical.global_host_headroom_after_present",
        passed_or_failed(input.after.global_host_headroom.present));
    emit_number(
        "physical.global_host_headroom_after_bytes",
        input.after.global_host_headroom.value);
    emit_number(
        "physical.unrelated_gtt_growth_bytes",
        input.unrelated_gtt_growth.value);
    emit_number(
        "physical.unrelated_host_growth_bytes",
        input.unrelated_host_growth.value);
    emit_number(
        "physical.global_gtt_headroom_improvement_bytes",
        verified.global_gtt_improvement_bytes);
    emit_number(
        "physical.global_host_headroom_improvement_bytes",
        verified.global_host_improvement_bytes);
    const ArithmeticResult checked_global_gtt = checked_subtract(
        input.after.global_gtt_headroom.value,
        input.before.global_gtt_headroom.value);
    const ArithmeticResult checked_global_host = checked_subtract(
        input.after.global_host_headroom.value,
        input.before.global_host_headroom.value);
    emit(
        "physical.global_headroom_delta_checked",
        passed_or_failed(checked_global_gtt.known && checked_global_host.known));
    const bool target_attribution =
        valid_claim_groups(input.ledger_before, input.resident, input.before) &&
        input.ledger_before.groups[1].allocation_group_id ==
            input.resident.allocation_group_id;
    emit("physical.target_attribution", target_attribution ? "exact" : "failed");
    emit(
        "physical.cache_group_zero",
        passed_or_failed(
            input.after.cache_gtt.present && input.after.cache_host.present &&
            input.after.cache_gtt.value == 0 &&
            input.after.cache_host.value == 0));
    emit(
        "physical.release",
        verified.evidence == Evidence::verified ? "verified" : "failed");

    emit(
        "causal.global_delta_not_credited",
        passed_or_failed(
            verified.credited_gtt_bytes !=
                verified.global_gtt_improvement_bytes &&
            verified.credited_host_bytes !=
                verified.global_host_improvement_bytes));
    emit_number(
        "causal.owned_gtt_release_bytes",
        verified.owned_gtt_release_bytes);
    emit_number(
        "causal.owned_host_release_bytes",
        verified.owned_host_release_bytes);
    emit(
        "causal.mechanism_completion",
        exact_acknowledgement(input.request, input.acknowledgement)
            ? "observed"
            : "failed");
    emit(
        "causal.effect_binding",
        verified.evidence == Evidence::verified && target_attribution
            ? "matched"
            : "failed");

    emit(
        "resident.process_identity",
        preserved_or_failed(
            preserved_physical_identity(
                input.before.process_identity,
                input.after.process_identity)));
    emit(
        "resident.weights",
        preserved_or_failed(
            preserved_physical_identity(
                input.before.weights_identity,
                input.after.weights_identity)));
    emit(
        "resident.pin_before_known",
        passed_or_failed(input.before.pin.known));
    emit(
        "resident.pin_after_known",
        passed_or_failed(input.after.pin.known));
    emit(
        "resident.pin",
        preserved_or_failed(
            input.before.pin.known && input.after.pin.known &&
            input.before.pin.value == input.after.pin.value));
    emit(
        "resident.pin_soft_release",
        input.before.pin.known && input.before.pin.value &&
                verified.evidence == Evidence::verified
            ? "allowed"
            : "failed");
    emit(
        "resident.model_residency",
        preserved_or_failed(
            preserved_physical_identity(
                input.before.model_residency_identity,
                input.after.model_residency_identity)));

    const ClaimGroup& before_weights = input.ledger_before.groups[0];
    const ClaimGroup& before_cache = input.ledger_before.groups[1];
    const ClaimGroup& after_weights = verified.ledger_after.groups[0];
    emit_number("ledger.before_claim_groups", input.ledger_before.count);
    emit("ledger.before_group_0_id", claim_id_name(before_weights.id));
    emit("ledger.before_group_0_effect", effect_name(before_weights.effect));
    emit_number("ledger.before_group_0_gtt_bytes", before_weights.gtt.value);
    emit_number("ledger.before_group_0_host_bytes", before_weights.host.value);
    emit("ledger.before_group_1_id", claim_id_name(before_cache.id));
    emit("ledger.before_group_1_effect", effect_name(before_cache.effect));
    emit_number("ledger.before_group_1_gtt_bytes", before_cache.gtt.value);
    emit_number("ledger.before_group_1_host_bytes", before_cache.host.value);
    emit_number("ledger.after_claim_groups", verified.ledger_after.count);
    emit("ledger.after_group_0_id", claim_id_name(after_weights.id));
    emit("ledger.after_group_0_effect", effect_name(after_weights.effect));
    emit_number("ledger.after_group_0_gtt_bytes", after_weights.gtt.value);
    emit_number("ledger.after_group_0_host_bytes", after_weights.host.value);
    emit(
        "ledger.after_slot_cache",
        !contains_claim(verified.ledger_after, ClaimId::slot_cache) ? "absent"
                                                                   : "failed");
    emit_number("ledger.removed_group_count", verified.removed_group_count);
    emit(
        "ledger.released_claim_group",
        before_cache.effect == Effect::reconstructible_state
            ? "reconstructible_state"
            : "failed");
    emit(
        "ledger.retained_claim_group",
        after_weights.effect == Effect::persistent_weights
            ? "persistent_weights"
            : "failed");
    emit(
        "ledger.other_claims",
        same_claim(before_weights, after_weights) ? "unchanged" : "failed");
    emit_number("ledger.generation_before", input.ledger_before.generation);
    emit_number("ledger.generation_after", verified.ledger_after.generation);
    const ArithmeticResult next_ledger_generation =
        checked_increment(input.ledger_before.generation);
    emit(
        "ledger.generation_increment",
        passed_or_failed(
            next_ledger_generation.known &&
            next_ledger_generation.value == verified.ledger_after.generation));
    emit_number(
        "ledger.credit_before_ack_bytes", verified.credit_before_ack_bytes);
    emit_number(
        "ledger.credit_before_physical_verification_bytes",
        verified.credit_before_physical_bytes);
    emit_number("ledger.credited_gtt_bytes", verified.credited_gtt_bytes);
    emit_number("ledger.credited_host_bytes", verified.credited_host_bytes);
    emit(
        "ledger.reconciliation_after_verification",
        passed_or_failed(
            verified.evidence == Evidence::verified &&
            verified.ledger_changed));
    emit(
        "ledger.atomic_commit",
        passed_or_failed(
            verified.ledger_changed && verified.removed_group_count == 1 &&
            same_claim(before_weights, after_weights)));
    emit(
        "ledger.stale_generation",
        stale_ledger.evidence == Evidence::unknown &&
                stale_ledger.reason == UnknownReason::ledger_generation &&
                !stale_ledger.ledger_changed
            ? "blocked"
            : "failed");
    emit(
        "ledger.commit_failure",
        failed_commit.evidence == Evidence::unknown &&
                failed_commit.disposition == Disposition::quarantine &&
                !failed_commit.ledger_changed
            ? "quarantined"
            : "failed");

    const bool dimension_cross_wiring = dimension_cross_wiring_rejected();
    bool negative_matrix = dimension_cross_wiring;
    bool negative_fixture_shapes = dimension_cross_wiring;
    negative_matrix = negative_matrix && reporting_fault_shapes;
    negative_fixture_shapes =
        negative_fixture_shapes && reporting_fault_shapes;
    negative_matrix =
        negative_matrix && reporting_identity_topology_matrix;
    negative_fixture_shapes =
        negative_fixture_shapes && reporting_identity_topology_matrix;
    negative_matrix = negative_matrix && unsupported_pre_observation_matrix;
    negative_fixture_shapes =
        negative_fixture_shapes && unsupported_pre_observation_matrix;
    negative_matrix = negative_matrix && neutral_verification_witnesses;
    negative_fixture_shapes =
        negative_fixture_shapes && neutral_verification_witnesses;
    for (const NegativeCase& negative : negative_cases) {
        const bool physical_validity_guards =
            physical_validity_guard_witnesses_pass(negative.fault);
        if (negative.fault == Fault::each_required_identity_token_zero) {
            bool every_zero_token_rejected = true;
            for (const IdentityToken token : required_identity_tokens) {
                const ReleaseInput zero_input =
                    input_with_zero_identity_token(token);
                const Verification zero_result =
                    verify_soft_release(zero_input);
                const bool coherent_post =
                    unchanged_fresh_observation(zero_input);
                const bool exact_disposition =
                    coherent_post
                        ? zero_result.disposition ==
                              Disposition::verified_intact &&
                              !zero_result.claims_maximized
                        : zero_result.disposition == Disposition::quarantine &&
                              zero_result.claims_maximized;
                const bool zero_token_rejected =
                    zero_identity_token_shape(zero_input, token) &&
                    !required_identity_tokens_nonzero(zero_input) &&
                    zero_result.evidence == Evidence::unknown &&
                    zero_result.reason == UnknownReason::identity &&
                    exact_disposition && !zero_result.dispatch_called &&
                    !zero_result.ledger_changed &&
                    same_ledger(
                        zero_input.ledger_before, zero_result.ledger_after) &&
                    zero_result.residency_preserved ==
                        post_residency_preserved(zero_input) &&
                    zero_result.post_observation_unchanged_fresh == coherent_post &&
                    zero_result.credited_gtt_bytes == 0 &&
                    zero_result.credited_host_bytes == 0;
                every_zero_token_rejected =
                    every_zero_token_rejected && zero_token_rejected;
            }
            emit(
                negative.key,
                every_zero_token_rejected ? "unknown" : "failed");
            negative_matrix =
                negative_matrix && every_zero_token_rejected;
            negative_fixture_shapes =
                negative_fixture_shapes && every_zero_token_rejected;
            continue;
        }
        if (negative.fault ==
            Fault::valid_precondition_without_dispatch_changed_post) {
            constexpr std::array<ChangedPostAxis, 8> axes = {
                ChangedPostAxis::runtime_identity,
                ChangedPostAxis::cache_gtt,
                ChangedPostAxis::cache_host,
                ChangedPostAxis::global_gtt,
                ChangedPostAxis::global_host,
                ChangedPostAxis::observation_generation_replay,
                ChangedPostAxis::observation_generation_zero,
                ChangedPostAxis::observation_generation_overflow_to_zero,
            };
            bool all_axes_quarantined = true;
            for (const ChangedPostAxis axis : axes) {
                const ReleaseInput changed_input = changed_post_input(
                    Mechanism::llamacpp_slot_cache_erase_v1, axis);
                const Verification changed_result =
                    verify_soft_release(changed_input);
                all_axes_quarantined =
                    all_axes_quarantined &&
                    changed_post_quarantined(
                        changed_input,
                        changed_result,
                        UnknownReason::observation,
                        axis);
            }
            emit(
                negative.key,
                all_axes_quarantined ? "quarantine" : "failed");
            negative_matrix = negative_matrix && all_axes_quarantined;
            negative_fixture_shapes =
                negative_fixture_shapes && all_axes_quarantined;
            continue;
        }
        if (negative.fault ==
            Fault::unsupported_mechanism_without_dispatch_changed_post) {
            constexpr std::array<ChangedPostAxis, 8> axes = {
                ChangedPostAxis::runtime_identity,
                ChangedPostAxis::cache_gtt,
                ChangedPostAxis::cache_host,
                ChangedPostAxis::global_gtt,
                ChangedPostAxis::global_host,
                ChangedPostAxis::observation_generation_replay,
                ChangedPostAxis::observation_generation_zero,
                ChangedPostAxis::observation_generation_overflow_to_zero,
            };
            constexpr std::array<Mechanism, 2> mechanisms = {
                Mechanism::absent,
                Mechanism::successful_noop,
            };
            bool all_axes_quarantined = true;
            for (const Mechanism mechanism : mechanisms) {
                for (const ChangedPostAxis axis : axes) {
                    const ReleaseInput changed_input =
                        changed_post_input(mechanism, axis);
                    const Verification changed_result =
                        verify_soft_release(changed_input);
                    const UnknownReason expected_reason =
                        axis == ChangedPostAxis::
                                    observation_generation_overflow_to_zero
                            ? UnknownReason::observation_generation
                            : UnknownReason::dispatch;
                    all_axes_quarantined =
                        all_axes_quarantined &&
                        changed_post_quarantined(
                            changed_input,
                            changed_result,
                            expected_reason,
                            axis);
                }
            }
            emit(
                negative.key,
                all_axes_quarantined ? "quarantine" : "failed");
            negative_matrix = negative_matrix && all_axes_quarantined;
            negative_fixture_shapes =
                negative_fixture_shapes && all_axes_quarantined;
            continue;
        }
        if (negative.fault ==
            Fault::ack_without_physical_delta_each_axis_changed) {
            constexpr std::array<NoEffectAxis, 6> axes = {
                NoEffectAxis::owned_gtt,
                NoEffectAxis::owned_host,
                NoEffectAxis::global_gtt,
                NoEffectAxis::global_host,
                NoEffectAxis::unrelated_gtt,
                NoEffectAxis::unrelated_host,
            };
            constexpr std::array<NoEffectContextAxis, 5> context_axes = {
                NoEffectContextAxis::runtime_identity,
                NoEffectContextAxis::process_identity,
                NoEffectContextAxis::weights_identity,
                NoEffectContextAxis::model_residency_identity,
                NoEffectContextAxis::pin,
            };
            bool all_axes_rejected = true;
            for (const NoEffectAxis axis : axes) {
                const ReleaseInput changed_input =
                    no_effect_axis_input(axis);
                const Verification changed_result =
                    verify_soft_release(changed_input);
                all_axes_rejected =
                    all_axes_rejected &&
                    no_effect_axis_rejected(
                        changed_input, changed_result, axis);
            }
            for (const NoEffectContextAxis axis : context_axes) {
                const ReleaseInput changed_input =
                    no_effect_context_axis_input(axis);
                const Verification changed_result =
                    verify_soft_release(changed_input);
                all_axes_rejected =
                    all_axes_rejected &&
                    no_effect_context_axis_rejected(
                        changed_input, changed_result, axis);
            }
            emit(
                negative.key,
                all_axes_rejected ? "unknown" : "failed");
            negative_matrix = negative_matrix && all_axes_rejected;
            negative_fixture_shapes =
                negative_fixture_shapes && all_axes_rejected;
            continue;
        }
        if (is_physical_identity_missing_fault(negative.fault)) {
            const ReleaseInput before_missing_input =
                input_with_before_physical_identity_missing(negative.fault);
            const ReleaseInput after_missing_input =
                input_with_fault(negative.fault);
            const Verification before_missing_result =
                verify_soft_release(before_missing_input);
            const Verification after_missing_result =
                verify_soft_release(after_missing_input);
            const bool missing_identity_rejected =
                physical_validity_guards &&
                exact_physical_identity_missing(
                    before_missing_input, negative.fault, true) &&
                exact_physical_identity_missing(
                    after_missing_input, negative.fault, false) &&
                identity_missing_quarantined(
                    before_missing_input, before_missing_result, false) &&
                identity_missing_quarantined(
                    after_missing_input, after_missing_result, true);
            emit(
                negative.key,
                missing_identity_rejected ? "quarantine" : "failed");
            negative_matrix =
                negative_matrix && missing_identity_rejected;
            negative_fixture_shapes =
                negative_fixture_shapes && missing_identity_rejected;
            continue;
        }
        const ReleaseInput negative_input = input_with_fault(negative.fault);
        const Verification result =
            verify_soft_release(negative_input);
        const bool ledger_unchanged =
            same_ledger(negative_input.ledger_before, result.ledger_after);
        const bool expects_intact =
            negative.fault == Fault::ack_without_physical_delta ||
            negative.fault == Fault::valid_precondition_without_dispatch;
        const bool expects_completed_without_attempt =
            negative.fault == Fault::dispatch_completed_without_attempt;
        const bool expects_ack_missing_quarantine =
            negative.fault == Fault::ack_missing;
        const bool expects_residency_change_quarantine =
            negative.fault == Fault::process_identity_zero ||
            negative.fault == Fault::process_identity_changed ||
            negative.fault == Fault::weights_identity_zero ||
            negative.fault == Fault::weights_identity_changed ||
            negative.fault == Fault::model_residency_identity_zero ||
            negative.fault == Fault::model_residency_identity_changed ||
            negative.fault == Fault::pin_missing ||
            negative.fault == Fault::pin_changed;
        const bool expects_unchanged_quarantine =
            expects_completed_without_attempt ||
            negative.fault ==
                Fault::invalid_precondition_after_dispatch_attempt ||
            negative.fault ==
                Fault::unsupported_mechanism_after_dispatch_attempt;
        const bool expects_quarantine_value =
            expects_unchanged_quarantine ||
            expects_residency_change_quarantine ||
            expects_ack_missing_quarantine;
        const bool expects_pre_dispatch_intact =
            is_pre_dispatch_fault(negative.fault);
        const bool pre_dispatch_intact =
            result.evidence == Evidence::unknown &&
            result.disposition == Disposition::verified_intact &&
            !result.dispatch_called && !result.ledger_changed &&
            ledger_unchanged &&
            result.residency_preserved ==
                post_residency_preserved(negative_input) &&
            result.post_observation_unchanged_fresh &&
            result.credited_gtt_bytes == 0 &&
            result.credited_host_bytes == 0;
        const bool post_dispatch_quarantined =
            result.evidence == Evidence::unknown &&
            result.disposition == Disposition::quarantine &&
            result.dispatch_called && !result.ledger_changed &&
            ledger_unchanged && result.claims_maximized &&
            result.credited_gtt_bytes == 0 && result.credited_host_bytes == 0;
        const bool completed_without_attempt_quarantined =
            result.evidence == Evidence::unknown &&
            result.disposition == Disposition::quarantine &&
            !result.dispatch_called && !result.ledger_changed &&
            ledger_unchanged && result.claims_maximized &&
            result.credited_gtt_bytes == 0 && result.credited_host_bytes == 0 &&
            result.post_observation_unchanged_fresh;
        const bool exact_residency_change =
            (negative.fault == Fault::process_identity_zero &&
             negative_input.after.process_identity.present &&
             negative_input.after.process_identity.value == 0 &&
             same_physical_identity(
                 negative_input.before.weights_identity,
                 negative_input.after.weights_identity) &&
             same_physical_identity(
                 negative_input.before.model_residency_identity,
                 negative_input.after.model_residency_identity) &&
             negative_input.before.pin.known ==
                 negative_input.after.pin.known &&
             negative_input.before.pin.value ==
                 negative_input.after.pin.value) ||
            (negative.fault == Fault::process_identity_changed &&
             changed_physical_identity(
                 negative_input.before.process_identity,
                 negative_input.after.process_identity) &&
             same_physical_identity(
                 negative_input.before.weights_identity,
                 negative_input.after.weights_identity) &&
             same_physical_identity(
                 negative_input.before.model_residency_identity,
                 negative_input.after.model_residency_identity) &&
             negative_input.before.pin.known ==
                 negative_input.after.pin.known &&
             negative_input.before.pin.value ==
                 negative_input.after.pin.value) ||
            (negative.fault == Fault::weights_identity_zero &&
             same_physical_identity(
                 negative_input.before.process_identity,
                 negative_input.after.process_identity) &&
             negative_input.after.weights_identity.present &&
             negative_input.after.weights_identity.value == 0 &&
             same_physical_identity(
                 negative_input.before.model_residency_identity,
                 negative_input.after.model_residency_identity) &&
             negative_input.before.pin.known ==
                 negative_input.after.pin.known &&
             negative_input.before.pin.value ==
                 negative_input.after.pin.value) ||
            (negative.fault == Fault::weights_identity_changed &&
             same_physical_identity(
                 negative_input.before.process_identity,
                 negative_input.after.process_identity) &&
             changed_physical_identity(
                 negative_input.before.weights_identity,
                 negative_input.after.weights_identity) &&
             same_physical_identity(
                 negative_input.before.model_residency_identity,
                 negative_input.after.model_residency_identity) &&
             negative_input.before.pin.known ==
                 negative_input.after.pin.known &&
             negative_input.before.pin.value ==
                 negative_input.after.pin.value) ||
            (negative.fault == Fault::model_residency_identity_zero &&
             same_physical_identity(
                 negative_input.before.process_identity,
                 negative_input.after.process_identity) &&
             same_physical_identity(
                 negative_input.before.weights_identity,
                 negative_input.after.weights_identity) &&
             negative_input.after.model_residency_identity.present &&
             negative_input.after.model_residency_identity.value == 0 &&
             negative_input.before.pin.known ==
                 negative_input.after.pin.known &&
             negative_input.before.pin.value ==
                 negative_input.after.pin.value) ||
            (negative.fault == Fault::model_residency_identity_changed &&
             same_physical_identity(
                 negative_input.before.process_identity,
                 negative_input.after.process_identity) &&
             same_physical_identity(
                 negative_input.before.weights_identity,
                 negative_input.after.weights_identity) &&
             changed_physical_identity(
                 negative_input.before.model_residency_identity,
                 negative_input.after.model_residency_identity) &&
             negative_input.before.pin.known ==
                 negative_input.after.pin.known &&
             negative_input.before.pin.value ==
                 negative_input.after.pin.value) ||
            (negative.fault == Fault::pin_missing &&
             same_physical_identity(
                 negative_input.before.process_identity,
                 negative_input.after.process_identity) &&
             same_physical_identity(
                 negative_input.before.weights_identity,
                 negative_input.after.weights_identity) &&
             same_physical_identity(
                 negative_input.before.model_residency_identity,
                 negative_input.after.model_residency_identity) &&
             negative_input.before.pin.known &&
             !negative_input.after.pin.known &&
             negative_input.before.pin.value ==
                 negative_input.after.pin.value) ||
            (negative.fault == Fault::pin_changed &&
             same_physical_identity(
                 negative_input.before.process_identity,
                 negative_input.after.process_identity) &&
             same_physical_identity(
                 negative_input.before.weights_identity,
                 negative_input.after.weights_identity) &&
             same_physical_identity(
                 negative_input.before.model_residency_identity,
                 negative_input.after.model_residency_identity) &&
             negative_input.before.pin.known &&
             negative_input.after.pin.known &&
             negative_input.before.pin.value !=
                 negative_input.after.pin.value);
        const bool residency_change_quarantined =
            post_dispatch_quarantined && exact_residency_change &&
            result.reason == UnknownReason::identity &&
            !result.residency_preserved;
        const bool outcome_passed =
            expects_intact
                ? result.evidence == Evidence::verified_intact &&
                      result.disposition == Disposition::verified_intact &&
                      result.dispatch_called ==
                          (negative.fault ==
                           Fault::ack_without_physical_delta) &&
                      !result.ledger_changed && ledger_unchanged &&
                      result.credited_gtt_bytes == 0 &&
                      result.credited_host_bytes == 0 &&
                      result.post_observation_unchanged_fresh
                : (expects_residency_change_quarantine
                       ? residency_change_quarantined
                       : (expects_completed_without_attempt
                              ? completed_without_attempt_quarantined
                              : (expects_pre_dispatch_intact
                                     ? pre_dispatch_intact
                                     : post_dispatch_quarantined)));
        const bool fixture_shape =
            exact_fault_shape(negative_input, negative.fault);
        const bool passed =
            outcome_passed && result.reason == expected_reason(negative.fault) &&
            fixture_shape && physical_validity_guards &&
            exact_dimension_fault(negative_input, negative.fault) &&
            (!expects_ack_missing_quarantine ||
             (!negative_input.acknowledgement.present &&
              negative_input.dispatch_attempted &&
              negative_input.dispatch_completed)) &&
            (!expects_unchanged_quarantine ||
             result.post_observation_unchanged_fresh);
        emit(
            negative.key,
            passed ? (expects_intact
                          ? "verified_intact"
                          : (expects_quarantine_value ? "quarantine"
                                                      : "unknown"))
                   : "failed");
        negative_matrix = negative_matrix && passed;
        negative_fixture_shapes =
            negative_fixture_shapes && fixture_shape &&
            physical_validity_guards;
    }

    emit(
        "disposition.pre_dispatch_failure",
        slot_missing.evidence == Evidence::unknown &&
                slot_missing.disposition == Disposition::verified_intact &&
                !slot_missing.dispatch_called &&
                same_ledger(
                    slot_missing_input.ledger_before,
                    slot_missing.ledger_after)
            ? "verified_intact"
            : "failed");
    emit(
        "disposition.pre_dispatch_post_observation",
        slot_missing.post_observation_unchanged_fresh ? "unchanged_fresh"
                                                      : "failed");
    emit(
        "disposition.ack_without_effect",
        no_effect.evidence == Evidence::verified_intact ? "verified_intact"
                                                        : "failed");
    emit(
        "disposition.ack_without_effect_ledger",
        !no_effect.ledger_changed &&
                same_ledger(no_effect_input.ledger_before, no_effect.ledger_after)
            ? "unchanged"
            : "failed");
    emit_number(
        "disposition.ack_without_effect_credit_bytes",
        no_effect.credited_gtt_bytes + no_effect.credited_host_bytes);
    emit(
        "disposition.ack_without_effect_post_observation",
        no_effect.post_observation_unchanged_fresh ? "unchanged_fresh"
                                                   : "failed");
    emit(
        "disposition.post_dispatch_ambiguous",
        dispatch_failure.evidence == Evidence::unknown &&
                dispatch_failure.disposition == Disposition::quarantine
            ? "quarantine"
            : "failed");
    emit(
        "disposition.post_dispatch_observation",
        !dispatch_failure.post_observation_available ? "unavailable" : "failed");
    emit(
        "disposition.invalid_post_residency",
        !dispatch_failure.residency_preserved ? "not_preserved" : "failed");
    emit(
        "disposition.ambiguous_claims",
        dispatch_failure.claims_maximized &&
                same_ledger(
                    dispatch_failure_input.ledger_before,
                    dispatch_failure.ledger_after)
            ? "maximum"
            : "failed");
    emit(
        "disposition.active_use",
        active_use.evidence == Evidence::unknown &&
                active_use.disposition == Disposition::verified_intact &&
                active_use.residency_preserved &&
                same_ledger(
                    input_with_fault(Fault::active_use).ledger_before,
                    active_use.ledger_after)
            ? "preserved"
            : "failed");

    emit(
        "unsupported.capability_absent",
        capability_absent.evidence == Evidence::unsupported ? "detected"
                                                             : "failed");
    emit(
        "unsupported.successful_noop",
        successful_noop.evidence == Evidence::unsupported ? "detected"
                                                           : "failed");
    emit_number(
        "unsupported.dispatch_calls",
        capability_absent.dispatch_called || successful_noop.dispatch_called
            ? 1
            : 0);
    emit(
        "unsupported.post_observation",
        capability_absent.post_observation_unchanged_fresh &&
                successful_noop.post_observation_unchanged_fresh
            ? "unchanged_fresh"
            : "failed");
    emit(
        "unsupported.ledger",
        !capability_absent.ledger_changed && !successful_noop.ledger_changed &&
                same_ledger(
                    absent_input.ledger_before, capability_absent.ledger_after) &&
                same_ledger(
                    noop_input.ledger_before, successful_noop.ledger_after)
            ? "unchanged"
            : "failed");
    emit(
        "unsupported.residency",
        capability_absent.residency_preserved &&
                successful_noop.residency_preserved
            ? "preserved"
            : "failed");
    emit(
        "unsupported.pressure_mode",
        valid_reporting_contract ? pressure_mode_name(valid_reporting_mode)
                                 : "failed");
    emit(
        "unsupported.reporting_valid",
        valid_reporting_contract
            ? pressure_mode_name(valid_reporting_mode)
            : "failed");
    emit(
        "unsupported.reporting_missing",
        unsupported_pair_passes(
            ReportingFault::missing,
            PressureMode::disabled_invalid_evidence)
            ? pressure_mode_name(invalid_reporting_mode)
            : "failed");
    emit(
        "unsupported.reporting_stale",
        unsupported_pair_passes(
            ReportingFault::stale,
            PressureMode::disabled_invalid_evidence)
            ? pressure_mode_name(invalid_reporting_mode)
            : "failed");
    emit(
        "unsupported.reporting_skew",
        unsupported_pair_passes(
            ReportingFault::skew,
            PressureMode::disabled_invalid_evidence)
            ? pressure_mode_name(invalid_reporting_mode)
            : "failed");
    emit(
        "unsupported.reporting_unhealthy",
        unsupported_pair_passes(
            ReportingFault::unhealthy,
            PressureMode::disabled_invalid_evidence)
            ? pressure_mode_name(invalid_reporting_mode)
            : "failed");
    emit(
        "unsupported.reporting_incomplete",
        unsupported_pair_passes(
            ReportingFault::incomplete,
            PressureMode::disabled_invalid_evidence)
            ? pressure_mode_name(invalid_reporting_mode)
            : "failed");
    emit(
        "unsupported.reporting_incoherent",
        incoherent_reporting_contract && unsupported_pre_observation_matrix &&
                reporting_guard_contract
            ? pressure_mode_name(invalid_reporting_mode)
            : "failed");
    emit_number(
        "unsupported.invalid_reporting_dispatch_calls",
        invalid_reporting_no_dispatch ? 0 : 1);
    emit(
        "unsupported.invalid_reporting_post_observation",
        invalid_reporting_post_unchanged ? "unchanged_fresh" : "failed");
    emit(
        "unsupported.invalid_reporting_ledger",
        invalid_reporting_ledger_unchanged ? "unchanged" : "failed");
    emit(
        "unsupported.invalid_reporting_residency",
        invalid_reporting_residency_preserved ? "preserved" : "failed");

    emit(
        "selected_fallback.rocm_valid_reporting", rocm_valid_fallback);
    emit(
        "selected_fallback.rocm_invalid_reporting", rocm_invalid_fallback);
    emit(
        "selected_fallback.vulkan_valid_reporting", vulkan_valid_fallback);
    emit(
        "selected_fallback.vulkan_invalid_reporting",
        vulkan_invalid_fallback);

    emit("synthetic.observation_source", "injected");
    emit(
        "synthetic.verified_soft_release",
        passed_or_failed(verified.evidence == Evidence::verified));
    emit("synthetic.fail_closed_matrix", passed_or_failed(negative_matrix));
    emit(
        "synthetic.negative_fixture_shapes",
        passed_or_failed(negative_fixture_shapes));
    emit("synthetic.unsupported_mechanism", "fallback");

    constexpr std::array<const char*, 2> native_rows = {
        "native.llamacpp_rocm_physical_release=deferred",
        "native.llamacpp_vulkan_physical_release=deferred",
    };
    emit_rows(native_rows.data(), native_rows.size());
    emit("fallback_binding.rocm_pressure_invalid", rocm_invalid_fallback);
    emit("fallback_binding.rocm_pressure_report", rocm_valid_fallback);
    emit(
        "fallback_binding.vulkan_pressure_invalid", vulkan_invalid_fallback);
    emit(
        "fallback_binding.vulkan_pressure_report", vulkan_valid_fallback);
    emit("platform.current", current_platform());
    emit("runtime_authority", "none");

    return verified.evidence == Evidence::verified && negative_matrix &&
                   negative_fixture_shapes &&
                   dimension_cross_wiring &&
                   reporting_fault_shapes &&
                   reporting_identity_topology_matrix &&
                   valid_reporting_contract &&
                   incoherent_reporting_contract &&
                   unsupported_pre_observation_matrix &&
                   neutral_verification_witnesses &&
                   reporting_guard_contract &&
                   invalid_reporting_modes && invalid_reporting_no_dispatch &&
                   invalid_reporting_post_unchanged &&
                   invalid_reporting_ledger_unchanged &&
                   invalid_reporting_residency_preserved &&
                   capability_absent.evidence == Evidence::unsupported &&
                   successful_noop.evidence == Evidence::unsupported
               ? 0
               : 1;
}

}

int main() {
    for (const lemon::residency::prototype::NegativeCase& negative :
         lemon::residency::prototype::negative_cases) {
        if (!lemon::residency::prototype::
                 physical_validity_guard_witnesses_pass(negative.fault)) {
            return 1;
        }
    }
    return lemon::residency::prototype::run();
}
