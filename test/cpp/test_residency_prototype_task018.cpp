#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace lemon::residency::prototype {

#ifdef _WIN32
constexpr std::string_view platform_id = "windows";
#elif defined(__APPLE__)
constexpr std::string_view platform_id = "macos";
#elif defined(__linux__)
constexpr std::string_view platform_id = "linux";
#else
constexpr std::string_view platform_id = "unsupported";
#endif

enum class Mode : std::uint64_t {
    direct_process = 0,
    managed_service = 1,
    missing = 2,
    unknown = 3,
};

enum class Unit : std::uint64_t {
    embedding = 0,
    llm = 1,
    transcription = 2,
    missing = 3,
    unknown = 4,
};

enum class Model : std::uint64_t {
    embedding = 0,
    llm = 1,
    transcription = 2,
    missing = 3,
    unknown = 4,
};

enum class ServiceState : std::uint64_t {
    not_applicable = 0,
    missing = 1,
    running = 2,
    stopped = 3,
    unknown = 4,
    wrong = 5,
};

enum class RequestState : std::uint64_t {
    absent = 0,
    rejected = 1,
    accepted = 2,
};

enum class OperationLeaf : std::uint64_t {
    service_termination = 0,
    dead_backend_pruning = 1,
    same_epoch_recovery_cleanup = 2,
    prior_epoch_owner_cleanup = 3,
    artifact_scope_recovery_cleanup = 4,
    unknown = 5,
};

enum class Result : std::uint64_t {
    unknown = 0,
    verified_intact = 1,
    quarantine = 2,
    verified_release = 3,
};

enum class ClaimDisposition : std::uint64_t {
    preserved = 0,
    maximum = 1,
    released = 2,
};

enum class CreditStage : std::uint64_t {
    before_both_proofs = 0,
    after_both_proofs = 1,
};

enum class ValidatedActionContext : std::uint64_t {
    attempted_and_acknowledged = 0,
};

enum class OwnershipState : std::uint64_t {
    exact = 0,
    missing = 1,
    too_broad = 2,
    shared_ambiguous = 3,
};

struct Field {
    bool present;
    std::uint64_t value;
};

enum class CommonToken : std::size_t {
    device_identity = 0,
    backend_artifact_digest = 1,
    source_build_dependency_closure = 2,
    driver_runtime_closure = 3,
    model_manifest_digest = 4,
    normalized_configuration_digest = 5,
    evidence_index_digest = 6,
    evidence_liveness_lease = 7,
    resident_id = 8,
    resident_generation = 9,
    backend_instance_birth_token = 10,
    topology_generation = 11,
    observation_contract_digest = 12,
    termination_action_token = 13,
};

struct CommonIdentity {
    std::array<Field, 14> fields;
};

struct DirectModeIdentity {
    Field controller_identity;
    Field process_identity;
    Field process_birth_token;
    Field executable_digest;
};

struct ManagedModeIdentity {
    Field controller_identity;
    Field service_manager_identity;
    Field service_identity_digest;
    Field service_config_digest;
    Field service_instance_birth_token;
    Field service_start_generation;
    Field process_identity;
    Field process_birth_token;
    Field executable_digest;
};

enum class ModeIdentityKind : std::uint64_t {
    direct_process = 0,
    managed_service = 1,
};

union ModeIdentityValue {
    DirectModeIdentity direct;
    ManagedModeIdentity managed;

    constexpr ModeIdentityValue() : direct{} {}
    constexpr ModeIdentityValue(DirectModeIdentity identity)
        : direct(identity) {}
    constexpr ModeIdentityValue(ManagedModeIdentity identity)
        : managed(identity) {}
};

struct ModeIdentity {
    ModeIdentityKind kind;
    ModeIdentityValue value;

    constexpr ModeIdentity()
        : kind(ModeIdentityKind::direct_process), value() {}
    constexpr ModeIdentity(DirectModeIdentity identity)
        : kind(ModeIdentityKind::direct_process), value(identity) {}
    constexpr ModeIdentity(ManagedModeIdentity identity)
        : kind(ModeIdentityKind::managed_service), value(identity) {}
};

struct Generation {
    Field value;
    bool checked;
};

struct ObservationEnvelope {
    bool present;
    bool fresh;
    bool healthy;
    bool complete;
    Generation generation;
};

struct DeviceClaimSelection {
    Field claim_identity;
    Field claim_generation;
};

struct DeviceClaimKey {
    bool present;
    Field claim_identity;
    Field claim_generation;
};

enum class PreparedAuthorityState : std::uint64_t {
    known = 0,
    profile_unknown = 1,
    missing = 2,
};

struct PreparedAuthority {
    PreparedAuthorityState state;
    Field unit_selector;
    Field model_selector;
    Field mode_selector;
    Field operation_selector;
    CommonIdentity common;
    ModeIdentity identity;
    DeviceClaimSelection claim;
};

enum class MemberClassification : std::uint64_t {
    serving_process = 0,
    unknown = 1,
    external_package = 2,
    model_store = 3,
};

struct RootRecord {
    bool present;
    Field identity;
    OwnershipState ownership;
};

struct MemberRecord {
    bool present;
    Field process_identity;
    Field process_birth_token;
    Field executable_digest;
    MemberClassification classification;
};

struct MembershipSnapshot {
    ObservationEnvelope envelope;
    Field unit_selector;
    Field model_selector;
    Field mode_selector;
    CommonIdentity common;
    ModeIdentity identity;
    std::array<RootRecord, 2> roots;
    std::array<MemberRecord, 3> members;
    std::uint64_t declared_member_count;
    ServiceState service_state;
};

struct DeviceSnapshot {
    ObservationEnvelope envelope;
    Field unit_selector;
    Field model_selector;
    Field mode_selector;
    CommonIdentity common;
    ModeIdentity owner;
    DeviceClaimSelection lookup;
    std::array<DeviceClaimKey, 3> active_claims;
};

struct TerminationTarget {
    Field unit_selector;
    Field model_selector;
    Field mode_selector;
    Field operation_selector;
    CommonIdentity common;
    ModeIdentity identity;
};

struct TerminationEvidence {
    RequestState request;
    bool attempted;
    bool acknowledged;
    TerminationTarget target;
    TerminationTarget acknowledgement_target;
    Field acknowledgement_action_token;
};

struct MembershipReleaseProof {
    Field unit_selector;
    Field model_selector;
    Field mode_selector;
    Field operation_selector;
    CommonIdentity common;
    ModeIdentity owner;
};

struct DeviceReleaseProof {
    Field unit_selector;
    Field model_selector;
    Field mode_selector;
    Field operation_selector;
    CommonIdentity common;
    ModeIdentity owner;
    DeviceClaimSelection claim;
};

union MembershipProofSlotValue {
    std::uint64_t absent;
    MembershipReleaseProof proof;

    constexpr MembershipProofSlotValue() : absent(0) {}
    constexpr MembershipProofSlotValue(MembershipReleaseProof value)
        : proof(value) {}
};

struct MembershipProofSlot {
    bool present;
    MembershipProofSlotValue value;

    constexpr MembershipProofSlot() : present(false), value() {}
    constexpr MembershipProofSlot(MembershipReleaseProof proof)
        : present(true), value(proof) {}
};

union DeviceProofSlotValue {
    std::uint64_t absent;
    DeviceReleaseProof proof;

    constexpr DeviceProofSlotValue() : absent(0) {}
    constexpr DeviceProofSlotValue(DeviceReleaseProof value)
        : proof(value) {}
};

struct DeviceProofSlot {
    bool present;
    DeviceProofSlotValue value;

    constexpr DeviceProofSlot() : present(false), value() {}
    constexpr DeviceProofSlot(DeviceReleaseProof proof)
        : present(true), value(proof) {}
};

struct RecoveryEvidence {
    PreparedAuthority prepared;
    bool liveness_before;
    bool liveness_after;
    CreditStage credit_stage;
    MembershipSnapshot membership_before;
    MembershipSnapshot membership_after;
    DeviceSnapshot device_before;
    DeviceSnapshot device_after;
    TerminationEvidence termination;
};

using ReleaseInput = RecoveryEvidence;

struct ExpectedReleaseBinding {
    Field unit_selector;
    Field model_selector;
    Field mode_selector;
    Field operation_selector;
    CommonIdentity common;
    ModeIdentity owner;
    DeviceClaimSelection claim;
};

struct ReleaseComposition {
    ValidatedActionContext action_context;
    CreditStage credit_stage;
    ExpectedReleaseBinding expected;
    MembershipProofSlot membership;
    DeviceProofSlot device;
};

constexpr bool input_field_equal(const Field& left, const Field& right) {
    return left.present == right.present && left.value == right.value;
}

constexpr bool input_common_equal(
    const CommonIdentity& left,
    const CommonIdentity& right) {
    for (std::size_t index = 0; index < left.fields.size(); ++index) {
        if (!input_field_equal(left.fields[index], right.fields[index])) {
            return false;
        }
    }
    return true;
}

constexpr bool input_mode_identity_equal(
    const ModeIdentity& left,
    const ModeIdentity& right) {
    if (left.kind != right.kind) {
        return false;
    }
    if (left.kind == ModeIdentityKind::direct_process) {
        return input_field_equal(
                   left.value.direct.controller_identity,
                   right.value.direct.controller_identity)
            && input_field_equal(
                left.value.direct.process_identity,
                right.value.direct.process_identity)
            && input_field_equal(
                left.value.direct.process_birth_token,
                right.value.direct.process_birth_token)
            && input_field_equal(
                left.value.direct.executable_digest,
                right.value.direct.executable_digest);
    }
    return input_field_equal(
               left.value.managed.controller_identity,
               right.value.managed.controller_identity)
        && input_field_equal(
            left.value.managed.service_manager_identity,
            right.value.managed.service_manager_identity)
        && input_field_equal(
            left.value.managed.service_identity_digest,
            right.value.managed.service_identity_digest)
        && input_field_equal(
            left.value.managed.service_config_digest,
            right.value.managed.service_config_digest)
        && input_field_equal(
            left.value.managed.service_instance_birth_token,
            right.value.managed.service_instance_birth_token)
        && input_field_equal(
            left.value.managed.service_start_generation,
            right.value.managed.service_start_generation)
        && input_field_equal(
            left.value.managed.process_identity,
            right.value.managed.process_identity)
        && input_field_equal(
            left.value.managed.process_birth_token,
            right.value.managed.process_birth_token)
        && input_field_equal(
            left.value.managed.executable_digest,
            right.value.managed.executable_digest);
}

constexpr bool input_generation_equal(
    const Generation& left,
    const Generation& right) {
    return input_field_equal(left.value, right.value)
        && left.checked == right.checked;
}

constexpr bool input_envelope_equal(
    const ObservationEnvelope& left,
    const ObservationEnvelope& right) {
    return left.present == right.present && left.fresh == right.fresh
        && left.healthy == right.healthy
        && left.complete == right.complete
        && input_generation_equal(left.generation, right.generation);
}

constexpr bool input_claim_selection_equal(
    const DeviceClaimSelection& left,
    const DeviceClaimSelection& right) {
    return input_field_equal(left.claim_identity, right.claim_identity)
        && input_field_equal(left.claim_generation, right.claim_generation);
}

constexpr bool input_claim_key_equal(
    const DeviceClaimKey& left,
    const DeviceClaimKey& right) {
    return left.present == right.present
        && input_field_equal(left.claim_identity, right.claim_identity)
        && input_field_equal(left.claim_generation, right.claim_generation);
}

constexpr bool input_root_record_equal(
    const RootRecord& left,
    const RootRecord& right) {
    return left.present == right.present
        && input_field_equal(left.identity, right.identity)
        && left.ownership == right.ownership;
}

constexpr bool input_member_record_equal(
    const MemberRecord& left,
    const MemberRecord& right) {
    return left.present == right.present
        && input_field_equal(
            left.process_identity,
            right.process_identity)
        && input_field_equal(
            left.process_birth_token,
            right.process_birth_token)
        && input_field_equal(
            left.executable_digest,
            right.executable_digest)
        && left.classification == right.classification;
}

constexpr bool input_prepared_equal(
    const PreparedAuthority& left,
    const PreparedAuthority& right) {
    return left.state == right.state
        && input_field_equal(left.unit_selector, right.unit_selector)
        && input_field_equal(left.model_selector, right.model_selector)
        && input_field_equal(left.mode_selector, right.mode_selector)
        && input_field_equal(
            left.operation_selector,
            right.operation_selector)
        && input_common_equal(left.common, right.common)
        && input_mode_identity_equal(left.identity, right.identity)
        && input_claim_selection_equal(left.claim, right.claim);
}

constexpr bool input_membership_equal(
    const MembershipSnapshot& left,
    const MembershipSnapshot& right) {
    if (!input_envelope_equal(left.envelope, right.envelope)
        || !input_field_equal(left.unit_selector, right.unit_selector)
        || !input_field_equal(left.model_selector, right.model_selector)
        || !input_field_equal(left.mode_selector, right.mode_selector)
        || !input_common_equal(left.common, right.common)
        || !input_mode_identity_equal(left.identity, right.identity)
        || left.declared_member_count != right.declared_member_count
        || left.service_state != right.service_state) {
        return false;
    }
    for (std::size_t index = 0; index < left.roots.size(); ++index) {
        if (!input_root_record_equal(left.roots[index], right.roots[index])) {
            return false;
        }
    }
    for (std::size_t index = 0; index < left.members.size(); ++index) {
        if (!input_member_record_equal(
                left.members[index],
                right.members[index])) {
            return false;
        }
    }
    return true;
}

constexpr bool input_device_equal(
    const DeviceSnapshot& left,
    const DeviceSnapshot& right) {
    if (!input_envelope_equal(left.envelope, right.envelope)
        || !input_field_equal(left.unit_selector, right.unit_selector)
        || !input_field_equal(left.model_selector, right.model_selector)
        || !input_field_equal(left.mode_selector, right.mode_selector)
        || !input_common_equal(left.common, right.common)
        || !input_mode_identity_equal(left.owner, right.owner)
        || !input_claim_selection_equal(left.lookup, right.lookup)) {
        return false;
    }
    for (std::size_t index = 0; index < left.active_claims.size(); ++index) {
        if (!input_claim_key_equal(
                left.active_claims[index],
                right.active_claims[index])) {
            return false;
        }
    }
    return true;
}

constexpr bool input_target_equal(
    const TerminationTarget& left,
    const TerminationTarget& right) {
    return input_field_equal(left.unit_selector, right.unit_selector)
        && input_field_equal(left.model_selector, right.model_selector)
        && input_field_equal(left.mode_selector, right.mode_selector)
        && input_field_equal(
            left.operation_selector,
            right.operation_selector)
        && input_common_equal(left.common, right.common)
        && input_mode_identity_equal(left.identity, right.identity);
}

constexpr bool input_termination_equal(
    const TerminationEvidence& left,
    const TerminationEvidence& right) {
    return left.request == right.request
        && left.attempted == right.attempted
        && left.acknowledged == right.acknowledged
        && input_target_equal(left.target, right.target)
        && input_target_equal(
            left.acknowledgement_target,
            right.acknowledgement_target)
        && input_field_equal(
            left.acknowledgement_action_token,
            right.acknowledgement_action_token);
}

constexpr bool input_membership_proof_equal(
    const MembershipReleaseProof& left,
    const MembershipReleaseProof& right) {
    return input_field_equal(left.unit_selector, right.unit_selector)
        && input_field_equal(left.model_selector, right.model_selector)
        && input_field_equal(left.mode_selector, right.mode_selector)
        && input_field_equal(
            left.operation_selector,
            right.operation_selector)
        && input_common_equal(left.common, right.common)
        && input_mode_identity_equal(left.owner, right.owner);
}

constexpr bool input_device_proof_equal(
    const DeviceReleaseProof& left,
    const DeviceReleaseProof& right) {
    return input_field_equal(left.unit_selector, right.unit_selector)
        && input_field_equal(left.model_selector, right.model_selector)
        && input_field_equal(left.mode_selector, right.mode_selector)
        && input_field_equal(
            left.operation_selector,
            right.operation_selector)
        && input_common_equal(left.common, right.common)
        && input_mode_identity_equal(left.owner, right.owner)
        && input_claim_selection_equal(left.claim, right.claim);
}

constexpr bool input_membership_proof_slot_equal(
    const MembershipProofSlot& left,
    const MembershipProofSlot& right) {
    return left.present == right.present
        && (!left.present
            || input_membership_proof_equal(
                left.value.proof,
                right.value.proof));
}

constexpr bool input_device_proof_slot_equal(
    const DeviceProofSlot& left,
    const DeviceProofSlot& right) {
    return left.present == right.present
        && (!left.present
            || input_device_proof_equal(
                left.value.proof,
                right.value.proof));
}

constexpr bool release_input_equal(
    const ReleaseInput& left,
    const ReleaseInput& right) {
    return input_prepared_equal(left.prepared, right.prepared)
        && left.liveness_before == right.liveness_before
        && left.liveness_after == right.liveness_after
        && left.credit_stage == right.credit_stage
        && input_membership_equal(
            left.membership_before,
            right.membership_before)
        && input_membership_equal(
            left.membership_after,
            right.membership_after)
        && input_device_equal(left.device_before, right.device_before)
        && input_device_equal(left.device_after, right.device_after)
        && input_termination_equal(left.termination, right.termination);
}

constexpr bool input_expected_binding_equal(
    const ExpectedReleaseBinding& left,
    const ExpectedReleaseBinding& right) {
    return input_field_equal(left.unit_selector, right.unit_selector)
        && input_field_equal(left.model_selector, right.model_selector)
        && input_field_equal(left.mode_selector, right.mode_selector)
        && input_field_equal(
            left.operation_selector,
            right.operation_selector)
        && input_common_equal(left.common, right.common)
        && input_mode_identity_equal(left.owner, right.owner)
        && input_claim_selection_equal(left.claim, right.claim);
}

constexpr bool release_composition_equal(
    const ReleaseComposition& left,
    const ReleaseComposition& right) {
    return left.action_context == right.action_context
        && left.credit_stage == right.credit_stage
        && input_expected_binding_equal(left.expected, right.expected)
        && input_membership_proof_slot_equal(
            left.membership,
            right.membership)
        && input_device_proof_slot_equal(left.device, right.device);
}

constexpr std::uint64_t digest_mix(
    std::uint64_t digest,
    std::uint64_t value) {
    return (digest ^ (value + 0x9e3779b97f4a7c15ULL))
        * 0x100000001b3ULL;
}

constexpr std::uint64_t digest_field(
    std::uint64_t digest,
    const Field& field) {
    digest = digest_mix(digest, field.present ? 1 : 0);
    return digest_mix(digest, field.value);
}

constexpr std::uint64_t digest_common(
    std::uint64_t digest,
    const CommonIdentity& identity) {
    for (const Field& field : identity.fields) {
        digest = digest_field(digest, field);
    }
    return digest;
}

constexpr std::uint64_t digest_mode_identity(
    std::uint64_t digest,
    const ModeIdentity& identity) {
    digest = digest_mix(
        digest,
        static_cast<std::uint64_t>(identity.kind));
    if (identity.kind == ModeIdentityKind::direct_process) {
        digest = digest_field(
            digest,
            identity.value.direct.controller_identity);
        digest = digest_field(digest, identity.value.direct.process_identity);
        digest = digest_field(
            digest,
            identity.value.direct.process_birth_token);
        return digest_field(
            digest,
            identity.value.direct.executable_digest);
    }
    digest = digest_field(
        digest,
        identity.value.managed.controller_identity);
    digest = digest_field(
        digest,
        identity.value.managed.service_manager_identity);
    digest = digest_field(
        digest,
        identity.value.managed.service_identity_digest);
    digest = digest_field(
        digest,
        identity.value.managed.service_config_digest);
    digest = digest_field(
        digest,
        identity.value.managed.service_instance_birth_token);
    digest = digest_field(
        digest,
        identity.value.managed.service_start_generation);
    digest = digest_field(digest, identity.value.managed.process_identity);
    digest = digest_field(
        digest,
        identity.value.managed.process_birth_token);
    return digest_field(digest, identity.value.managed.executable_digest);
}

constexpr std::uint64_t digest_generation(
    std::uint64_t digest,
    const Generation& generation) {
    digest = digest_field(digest, generation.value);
    return digest_mix(digest, generation.checked ? 1 : 0);
}

constexpr std::uint64_t digest_envelope(
    std::uint64_t digest,
    const ObservationEnvelope& envelope) {
    digest = digest_mix(digest, envelope.present ? 1 : 0);
    digest = digest_mix(digest, envelope.fresh ? 1 : 0);
    digest = digest_mix(digest, envelope.healthy ? 1 : 0);
    digest = digest_mix(digest, envelope.complete ? 1 : 0);
    return digest_generation(digest, envelope.generation);
}

constexpr std::uint64_t digest_claim_selection(
    std::uint64_t digest,
    const DeviceClaimSelection& selection) {
    digest = digest_field(digest, selection.claim_identity);
    return digest_field(digest, selection.claim_generation);
}

constexpr std::uint64_t digest_claim_key(
    std::uint64_t digest,
    const DeviceClaimKey& key) {
    digest = digest_mix(digest, key.present ? 1 : 0);
    digest = digest_field(digest, key.claim_identity);
    return digest_field(digest, key.claim_generation);
}

constexpr std::uint64_t digest_prepared(
    std::uint64_t digest,
    const PreparedAuthority& prepared) {
    digest = digest_mix(
        digest,
        static_cast<std::uint64_t>(prepared.state));
    digest = digest_field(digest, prepared.unit_selector);
    digest = digest_field(digest, prepared.model_selector);
    digest = digest_field(digest, prepared.mode_selector);
    digest = digest_field(digest, prepared.operation_selector);
    digest = digest_common(digest, prepared.common);
    digest = digest_mode_identity(digest, prepared.identity);
    return digest_claim_selection(digest, prepared.claim);
}

constexpr std::uint64_t digest_membership(
    std::uint64_t digest,
    const MembershipSnapshot& snapshot) {
    digest = digest_envelope(digest, snapshot.envelope);
    digest = digest_field(digest, snapshot.unit_selector);
    digest = digest_field(digest, snapshot.model_selector);
    digest = digest_field(digest, snapshot.mode_selector);
    digest = digest_common(digest, snapshot.common);
    digest = digest_mode_identity(digest, snapshot.identity);
    for (const RootRecord& root : snapshot.roots) {
        digest = digest_mix(digest, root.present ? 1 : 0);
        digest = digest_field(digest, root.identity);
        digest = digest_mix(
            digest,
            static_cast<std::uint64_t>(root.ownership));
    }
    for (const MemberRecord& member : snapshot.members) {
        digest = digest_mix(digest, member.present ? 1 : 0);
        digest = digest_field(digest, member.process_identity);
        digest = digest_field(digest, member.process_birth_token);
        digest = digest_field(digest, member.executable_digest);
        digest = digest_mix(
            digest,
            static_cast<std::uint64_t>(member.classification));
    }
    digest = digest_mix(digest, snapshot.declared_member_count);
    return digest_mix(
        digest,
        static_cast<std::uint64_t>(snapshot.service_state));
}

constexpr std::uint64_t digest_device(
    std::uint64_t digest,
    const DeviceSnapshot& snapshot) {
    digest = digest_envelope(digest, snapshot.envelope);
    digest = digest_field(digest, snapshot.unit_selector);
    digest = digest_field(digest, snapshot.model_selector);
    digest = digest_field(digest, snapshot.mode_selector);
    digest = digest_common(digest, snapshot.common);
    digest = digest_mode_identity(digest, snapshot.owner);
    digest = digest_claim_selection(digest, snapshot.lookup);
    for (const DeviceClaimKey& key : snapshot.active_claims) {
        digest = digest_claim_key(digest, key);
    }
    return digest;
}

constexpr std::uint64_t digest_target(
    std::uint64_t digest,
    const TerminationTarget& target) {
    digest = digest_field(digest, target.unit_selector);
    digest = digest_field(digest, target.model_selector);
    digest = digest_field(digest, target.mode_selector);
    digest = digest_field(digest, target.operation_selector);
    digest = digest_common(digest, target.common);
    return digest_mode_identity(digest, target.identity);
}

constexpr std::uint64_t digest_termination(
    std::uint64_t digest,
    const TerminationEvidence& termination) {
    digest = digest_mix(
        digest,
        static_cast<std::uint64_t>(termination.request));
    digest = digest_mix(digest, termination.attempted ? 1 : 0);
    digest = digest_mix(digest, termination.acknowledged ? 1 : 0);
    digest = digest_target(digest, termination.target);
    digest = digest_target(digest, termination.acknowledgement_target);
    return digest_field(digest, termination.acknowledgement_action_token);
}

constexpr std::uint64_t release_input_digest(const ReleaseInput& input) {
    std::uint64_t digest = 0xcbf29ce484222325ULL;
    digest = digest_prepared(digest, input.prepared);
    digest = digest_mix(digest, input.liveness_before ? 1 : 0);
    digest = digest_mix(digest, input.liveness_after ? 1 : 0);
    digest = digest_mix(
        digest,
        static_cast<std::uint64_t>(input.credit_stage));
    digest = digest_membership(digest, input.membership_before);
    digest = digest_membership(digest, input.membership_after);
    digest = digest_device(digest, input.device_before);
    digest = digest_device(digest, input.device_after);
    return digest_termination(digest, input.termination);
}

constexpr std::uint64_t digest_membership_proof(
    std::uint64_t digest,
    const MembershipReleaseProof& proof) {
    digest = digest_field(digest, proof.unit_selector);
    digest = digest_field(digest, proof.model_selector);
    digest = digest_field(digest, proof.mode_selector);
    digest = digest_field(digest, proof.operation_selector);
    digest = digest_common(digest, proof.common);
    return digest_mode_identity(digest, proof.owner);
}

constexpr std::uint64_t digest_device_proof(
    std::uint64_t digest,
    const DeviceReleaseProof& proof) {
    digest = digest_field(digest, proof.unit_selector);
    digest = digest_field(digest, proof.model_selector);
    digest = digest_field(digest, proof.mode_selector);
    digest = digest_field(digest, proof.operation_selector);
    digest = digest_common(digest, proof.common);
    digest = digest_mode_identity(digest, proof.owner);
    return digest_claim_selection(digest, proof.claim);
}

constexpr std::uint64_t digest_membership_proof_slot(
    std::uint64_t digest,
    const MembershipProofSlot& slot) {
    digest = digest_mix(digest, slot.present ? 1 : 0);
    return slot.present
        ? digest_membership_proof(digest, slot.value.proof)
        : digest;
}

constexpr std::uint64_t digest_device_proof_slot(
    std::uint64_t digest,
    const DeviceProofSlot& slot) {
    digest = digest_mix(digest, slot.present ? 1 : 0);
    return slot.present
        ? digest_device_proof(digest, slot.value.proof)
        : digest;
}

constexpr std::uint64_t release_composition_digest(
    const ReleaseComposition& composition) {
    std::uint64_t digest = 0x84222325cbf29ce4ULL;
    digest = digest_mix(
        digest,
        static_cast<std::uint64_t>(composition.action_context));
    digest = digest_mix(
        digest,
        static_cast<std::uint64_t>(composition.credit_stage));
    digest = digest_field(digest, composition.expected.unit_selector);
    digest = digest_field(digest, composition.expected.model_selector);
    digest = digest_field(digest, composition.expected.mode_selector);
    digest = digest_field(digest, composition.expected.operation_selector);
    digest = digest_common(digest, composition.expected.common);
    digest = digest_mode_identity(digest, composition.expected.owner);
    digest = digest_claim_selection(digest, composition.expected.claim);
    digest = digest_membership_proof_slot(digest, composition.membership);
    return digest_device_proof_slot(digest, composition.device);
}

struct Decision {
    Result result;
    ClaimDisposition claims;
    std::uint64_t effect_calls;
    std::uint64_t release_credit;
};

constexpr Field missing_field() {
    return {false, 0};
}

constexpr Field make_field(std::uint64_t value) {
    return {true, value};
}

constexpr bool field_valid(const Field& value) {
    return value.present && value.value != 0;
}

constexpr bool field_exact(const Field& candidate, const Field& expected) {
    return field_valid(candidate) && field_valid(expected)
        && candidate.value == expected.value;
}

constexpr void apply_field_fault(Field& field, std::uint64_t fault) {
    if (fault == 0) {
        field = {false, 0};
    } else if (fault == 1) {
        field.value = 0;
    } else {
        ++field.value;
    }
}

constexpr bool generation_valid(const Generation& generation) {
    return generation.checked && field_valid(generation.value);
}

constexpr bool generation_successor(
    const Generation& before,
    const Generation& after) {
    return generation_valid(before) && generation_valid(after)
        && before.value.value != std::numeric_limits<std::uint64_t>::max()
        && after.value.value == before.value.value + 1;
}

constexpr Field selector_field(Unit unit) {
    return make_field(static_cast<std::uint64_t>(unit) + 1);
}

constexpr Field selector_field(Model model) {
    return make_field(static_cast<std::uint64_t>(model) + 1);
}

constexpr Field selector_field(Mode mode) {
    return make_field(static_cast<std::uint64_t>(mode) + 1);
}

constexpr Field selector_field(OperationLeaf operation) {
    return make_field(static_cast<std::uint64_t>(operation) + 1);
}

constexpr bool selector_in_range(
    const Field& selector,
    std::uint64_t maximum) {
    return selector.present && selector.value >= 1
        && selector.value <= maximum;
}

constexpr bool unit_selector_valid(const Field& selector) {
    return selector_in_range(selector, 3);
}

constexpr bool model_selector_valid(const Field& selector) {
    return selector_in_range(selector, 3);
}

constexpr bool mode_selector_valid(const Field& selector) {
    return selector_in_range(selector, 2);
}

constexpr bool unit_model_selectors_match(
    const Field& unit,
    const Field& model) {
    return unit_selector_valid(unit) && model_selector_valid(model)
        && unit.value == model.value;
}

constexpr Mode selected_mode(const Field& selector) {
    return mode_selector_valid(selector)
        ? static_cast<Mode>(selector.value - 1)
        : Mode::unknown;
}

constexpr std::uint64_t input_seed(Unit unit, Mode mode) {
    return static_cast<std::uint64_t>(unit) * 2
        + static_cast<std::uint64_t>(mode) + 1;
}

constexpr CommonIdentity make_common(std::uint64_t base) {
    CommonIdentity identity{};
    for (std::size_t index = 0; index < identity.fields.size(); ++index) {
        identity.fields[index] = make_field(base + index + 1);
    }
    return identity;
}

constexpr bool common_valid(const CommonIdentity& identity) {
    for (const Field& field : identity.fields) {
        if (!field_valid(field)) {
            return false;
        }
    }
    return true;
}

constexpr bool common_exact(
    const CommonIdentity& candidate,
    const CommonIdentity& expected) {
    for (std::size_t index = 0; index < candidate.fields.size(); ++index) {
        if (!field_exact(candidate.fields[index], expected.fields[index])) {
            return false;
        }
    }
    return true;
}

constexpr const Field& common_field(
    const CommonIdentity& identity,
    CommonToken token) {
    return identity.fields[static_cast<std::size_t>(token)];
}

constexpr ModeIdentity make_prepared_identity(Mode mode, std::uint64_t base) {
    if (mode == Mode::direct_process) {
        return ModeIdentity{DirectModeIdentity{
            make_field(base + 1),
            make_field(base + 7),
            make_field(base + 8),
            make_field(base + 9),
        }};
    }
    return ModeIdentity{ManagedModeIdentity{
        make_field(base + 1),
        make_field(base + 2),
        make_field(base + 3),
        make_field(base + 4),
        make_field(base + 5),
        make_field(13),
        missing_field(),
        missing_field(),
        missing_field(),
    }};
}

constexpr ModeIdentity make_member_identity(
    Mode mode,
    const ModeIdentity& prepared,
    std::uint64_t base) {
    ModeIdentity identity = prepared;
    if (mode == Mode::managed_service) {
        identity.value.managed.process_identity = make_field(base + 7);
        identity.value.managed.process_birth_token = make_field(base + 8);
        identity.value.managed.executable_digest = make_field(base + 9);
    }
    return identity;
}

constexpr Field& mode_field(ModeIdentity& identity, std::size_t index) {
    if (identity.kind == ModeIdentityKind::direct_process) {
        if (index == 0) {
            return identity.value.direct.controller_identity;
        }
        if (index == 6) {
            return identity.value.direct.process_identity;
        }
        if (index == 7) {
            return identity.value.direct.process_birth_token;
        }
        return identity.value.direct.executable_digest;
    }
    if (index == 0) {
        return identity.value.managed.controller_identity;
    }
    if (index == 1) {
        return identity.value.managed.service_manager_identity;
    }
    if (index == 2) {
        return identity.value.managed.service_identity_digest;
    }
    if (index == 3) {
        return identity.value.managed.service_config_digest;
    }
    if (index == 4) {
        return identity.value.managed.service_instance_birth_token;
    }
    if (index == 5) {
        return identity.value.managed.service_start_generation;
    }
    if (index == 6) {
        return identity.value.managed.process_identity;
    }
    if (index == 7) {
        return identity.value.managed.process_birth_token;
    }
    return identity.value.managed.executable_digest;
}

constexpr const Field& mode_field(
    const ModeIdentity& identity,
    std::size_t index) {
    if (identity.kind == ModeIdentityKind::direct_process) {
        if (index == 0) {
            return identity.value.direct.controller_identity;
        }
        if (index == 6) {
            return identity.value.direct.process_identity;
        }
        if (index == 7) {
            return identity.value.direct.process_birth_token;
        }
        return identity.value.direct.executable_digest;
    }
    if (index == 0) {
        return identity.value.managed.controller_identity;
    }
    if (index == 1) {
        return identity.value.managed.service_manager_identity;
    }
    if (index == 2) {
        return identity.value.managed.service_identity_digest;
    }
    if (index == 3) {
        return identity.value.managed.service_config_digest;
    }
    if (index == 4) {
        return identity.value.managed.service_instance_birth_token;
    }
    if (index == 5) {
        return identity.value.managed.service_start_generation;
    }
    if (index == 6) {
        return identity.value.managed.process_identity;
    }
    if (index == 7) {
        return identity.value.managed.process_birth_token;
    }
    return identity.value.managed.executable_digest;
}

constexpr std::size_t full_identity_count(Mode mode) {
    return mode == Mode::direct_process ? 4 : 9;
}

constexpr std::size_t selected_identity_index(
    Mode mode,
    std::size_t ordinal) {
    constexpr std::array<std::size_t, 4> direct_indices = {0, 6, 7, 8};
    return mode == Mode::direct_process ? direct_indices[ordinal] : ordinal;
}

constexpr std::size_t full_identity_index(Mode mode, std::size_t ordinal) {
    return mode == Mode::direct_process
        ? selected_identity_index(mode, ordinal)
        : ordinal;
}

constexpr bool mode_identity_shape_valid(
    Mode mode,
    const ModeIdentity& identity) {
    if (mode != Mode::direct_process && mode != Mode::managed_service) {
        return false;
    }
    const ModeIdentityKind expected_kind = mode == Mode::direct_process
        ? ModeIdentityKind::direct_process
        : ModeIdentityKind::managed_service;
    if (identity.kind != expected_kind) {
        return false;
    }
    for (std::size_t ordinal = 0;
         ordinal < full_identity_count(mode);
         ++ordinal) {
        if (!field_valid(mode_field(
                identity,
                full_identity_index(mode, ordinal)))) {
            return false;
        }
    }
    return true;
}

constexpr bool full_identity_exact(
    Mode mode,
    const ModeIdentity& candidate,
    const ModeIdentity& expected) {
    const ModeIdentityKind expected_kind = mode == Mode::direct_process
        ? ModeIdentityKind::direct_process
        : ModeIdentityKind::managed_service;
    if (candidate.kind != expected_kind || expected.kind != expected_kind) {
        return false;
    }
    for (std::size_t ordinal = 0;
         ordinal < full_identity_count(mode);
         ++ordinal) {
        const std::size_t index = full_identity_index(mode, ordinal);
        if (!field_exact(mode_field(candidate, index), mode_field(expected, index))) {
            return false;
        }
    }
    return true;
}

constexpr bool envelope_valid(const ObservationEnvelope& envelope) {
    return envelope.present && envelope.fresh && envelope.healthy
        && envelope.complete && generation_valid(envelope.generation);
}

constexpr Field membership_root(
    Mode mode,
    const ModeIdentity& identity) {
    return mode == Mode::direct_process
        ? identity.value.direct.process_identity
        : identity.value.managed.service_identity_digest;
}

constexpr std::size_t active_root_count(
    const MembershipSnapshot& snapshot) {
    std::size_t count = 0;
    for (const RootRecord& root : snapshot.roots) {
        if (root.present) {
            ++count;
        }
    }
    return count;
}

constexpr std::size_t matching_root_count(
    const MembershipSnapshot& snapshot,
    const Field& expected) {
    std::size_t count = 0;
    for (const RootRecord& root : snapshot.roots) {
        if (root.present && field_exact(root.identity, expected)) {
            ++count;
        }
    }
    return count;
}

constexpr std::size_t active_member_count(
    const MembershipSnapshot& snapshot) {
    std::size_t count = 0;
    for (const MemberRecord& member : snapshot.members) {
        if (member.present) {
            ++count;
        }
    }
    return count;
}

constexpr bool membership_records_valid(
    const PreparedAuthority& prepared,
    const MembershipSnapshot& snapshot,
    std::size_t expected_members) {
    const Mode mode = selected_mode(prepared.mode_selector);
    const Field expected_root = membership_root(
        mode,
        prepared.identity);
    if (active_root_count(snapshot) != 1
        || matching_root_count(snapshot, expected_root) != 1
        || active_member_count(snapshot) != expected_members
        || snapshot.declared_member_count != expected_members) {
        return false;
    }
    for (const RootRecord& root : snapshot.roots) {
        if (root.present) {
            if (!field_valid(root.identity)
                || root.ownership != OwnershipState::exact) {
                return false;
            }
        } else if (root.identity.present || root.identity.value != 0
                   || root.ownership != OwnershipState::exact) {
            return false;
        }
    }
    for (std::size_t left = 0; left < snapshot.members.size(); ++left) {
        const MemberRecord& member = snapshot.members[left];
        if (!member.present) {
            if (member.process_identity.present
                || member.process_identity.value != 0
                || member.process_birth_token.present
                || member.process_birth_token.value != 0
                || member.executable_digest.present
                || member.executable_digest.value != 0
                || member.classification
                    != MemberClassification::serving_process) {
                return false;
            }
            continue;
        }
        if (!field_exact(
                member.process_identity,
                mode_field(prepared.identity, 6))
            || !field_exact(
                member.process_birth_token,
                mode_field(prepared.identity, 7))
            || !field_exact(
                member.executable_digest,
                mode_field(prepared.identity, 8))
            || member.classification
                != MemberClassification::serving_process) {
            return false;
        }
        for (std::size_t right = left + 1;
             right < snapshot.members.size();
             ++right) {
            if (snapshot.members[right].present
                && field_exact(
                    member.process_identity,
                    snapshot.members[right].process_identity)
                && field_exact(
                    member.process_birth_token,
                    snapshot.members[right].process_birth_token)
                && field_exact(
                    member.executable_digest,
                    snapshot.members[right].executable_digest)) {
                return false;
            }
        }
    }
    return true;
}

constexpr bool prepared_valid(const PreparedAuthority& prepared) {
    return unit_model_selectors_match(
               prepared.unit_selector,
               prepared.model_selector)
        && prepared.state == PreparedAuthorityState::known
        && mode_selector_valid(prepared.mode_selector)
        && field_exact(
            prepared.operation_selector,
            selector_field(OperationLeaf::service_termination))
        && common_valid(prepared.common)
        && mode_identity_shape_valid(
            selected_mode(prepared.mode_selector),
            prepared.identity)
        && field_valid(prepared.claim.claim_identity)
        && field_valid(prepared.claim.claim_generation);
}

constexpr bool leg_identity_matches(
    const Field& unit,
    const Field& model,
    const Field& mode,
    const PreparedAuthority& prepared) {
    return field_exact(unit, prepared.unit_selector)
        && field_exact(model, prepared.model_selector)
        && field_exact(mode, prepared.mode_selector);
}

constexpr bool membership_before_valid(
    const PreparedAuthority& prepared,
    const MembershipSnapshot& snapshot) {
    if (!envelope_valid(snapshot.envelope)
        || !leg_identity_matches(
            snapshot.unit_selector,
            snapshot.model_selector,
            snapshot.mode_selector,
            prepared)
        || !common_exact(snapshot.common, prepared.common)
        || !full_identity_exact(
            selected_mode(prepared.mode_selector),
            snapshot.identity,
            prepared.identity)
        || !mode_identity_shape_valid(
            selected_mode(prepared.mode_selector),
            snapshot.identity)
        || !membership_records_valid(prepared, snapshot, 1)) {
        return false;
    }
    return selected_mode(prepared.mode_selector) == Mode::managed_service
        ? snapshot.service_state == ServiceState::running
        : snapshot.service_state == ServiceState::not_applicable;
}

constexpr bool claim_key_valid(const DeviceClaimKey& key) {
    return field_valid(key.claim_identity)
        && field_valid(key.claim_generation);
}

constexpr bool claim_key_exact(
    const DeviceClaimKey& candidate,
    const DeviceClaimSelection& selected) {
    return field_exact(candidate.claim_identity, selected.claim_identity)
        && field_exact(candidate.claim_generation, selected.claim_generation);
}

constexpr bool active_claims_valid(const DeviceSnapshot& snapshot) {
    for (const DeviceClaimKey& key : snapshot.active_claims) {
        if (key.present) {
            if (!claim_key_valid(key)) {
                return false;
            }
        } else if (key.claim_identity.present || key.claim_identity.value != 0
                   || key.claim_generation.present
                   || key.claim_generation.value != 0) {
            return false;
        }
    }
    return true;
}

constexpr std::size_t target_claim_count(
    const DeviceSnapshot& snapshot,
    const DeviceClaimSelection& selected) {
    std::size_t count = 0;
    for (const DeviceClaimKey& key : snapshot.active_claims) {
        if (key.present && claim_key_exact(key, selected)) {
            ++count;
        }
    }
    return count;
}

constexpr std::size_t target_claim_candidate_count(
    const DeviceSnapshot& snapshot,
    const DeviceClaimSelection& selected) {
    std::size_t count = 0;
    for (const DeviceClaimKey& key : snapshot.active_claims) {
        if (key.present && field_exact(
                key.claim_identity,
                selected.claim_identity)) {
            ++count;
        }
    }
    return count;
}

constexpr bool device_before_valid(
    const PreparedAuthority& prepared,
    const MembershipSnapshot& membership,
    const DeviceSnapshot& snapshot) {
    return envelope_valid(snapshot.envelope)
        && leg_identity_matches(
            snapshot.unit_selector,
            snapshot.model_selector,
            snapshot.mode_selector,
            prepared)
        && common_exact(snapshot.common, prepared.common)
        && mode_identity_shape_valid(
            selected_mode(prepared.mode_selector),
            snapshot.owner)
        && full_identity_exact(
            selected_mode(prepared.mode_selector),
            snapshot.owner,
            membership.identity)
        && field_exact(
            snapshot.lookup.claim_identity,
            prepared.claim.claim_identity)
        && field_exact(
            snapshot.lookup.claim_generation,
            prepared.claim.claim_generation)
        && active_claims_valid(snapshot)
        && target_claim_candidate_count(snapshot, prepared.claim) == 1
        && target_claim_count(snapshot, prepared.claim) == 1;
}

constexpr bool target_valid(
    const PreparedAuthority& prepared,
    const MembershipSnapshot& membership,
    const TerminationTarget& target) {
    return field_exact(target.unit_selector, prepared.unit_selector)
        && field_exact(target.model_selector, prepared.model_selector)
        && field_exact(target.mode_selector, prepared.mode_selector)
        && field_exact(
            target.operation_selector,
            prepared.operation_selector)
        && common_exact(target.common, prepared.common)
        && mode_identity_shape_valid(
            selected_mode(prepared.mode_selector),
            target.identity)
        && full_identity_exact(
            selected_mode(prepared.mode_selector),
            target.identity,
            membership.identity);
}

constexpr bool target_exact(
    const TerminationTarget& candidate,
    const TerminationTarget& expected) {
    return input_field_equal(candidate.unit_selector, expected.unit_selector)
        && input_field_equal(
            candidate.model_selector,
            expected.model_selector)
        && input_field_equal(candidate.mode_selector, expected.mode_selector)
        && input_field_equal(
            candidate.operation_selector,
            expected.operation_selector)
        && common_exact(candidate.common, expected.common)
        && input_mode_identity_equal(
            candidate.identity,
            expected.identity);
}

constexpr bool membership_after_shape_valid(
    const PreparedAuthority& prepared,
    const MembershipSnapshot& before,
    const MembershipSnapshot& after) {
    if (!envelope_valid(after.envelope)
        || !generation_successor(
            before.envelope.generation,
            after.envelope.generation)
        || !leg_identity_matches(
            after.unit_selector,
            after.model_selector,
            after.mode_selector,
            prepared)
        || !common_exact(after.common, prepared.common)
        || !full_identity_exact(
            selected_mode(prepared.mode_selector),
            after.identity,
            before.identity)
        || !mode_identity_shape_valid(
            selected_mode(prepared.mode_selector),
            after.identity)) {
        return false;
    }
    return true;
}

constexpr bool membership_released(
    const PreparedAuthority& prepared,
    const MembershipSnapshot& before,
    const MembershipSnapshot& after) {
    if (!membership_after_shape_valid(prepared, before, after)
        || !membership_records_valid(prepared, after, 0)) {
        return false;
    }
    return selected_mode(prepared.mode_selector) == Mode::managed_service
        ? after.service_state == ServiceState::stopped
        : after.service_state == ServiceState::not_applicable;
}

constexpr bool membership_intact(
    const PreparedAuthority& prepared,
    const MembershipSnapshot& before,
    const MembershipSnapshot& after) {
    if (!membership_after_shape_valid(prepared, before, after)
        || !membership_records_valid(prepared, after, 1)) {
        return false;
    }
    return selected_mode(prepared.mode_selector) == Mode::managed_service
        ? after.service_state == ServiceState::running
        : after.service_state == ServiceState::not_applicable;
}

constexpr bool device_after_shape_valid(
    const PreparedAuthority& prepared,
    const MembershipSnapshot& membership,
    const DeviceSnapshot& before,
    const DeviceSnapshot& after) {
    return envelope_valid(after.envelope)
        && generation_successor(
            before.envelope.generation,
            after.envelope.generation)
        && leg_identity_matches(
            after.unit_selector,
            after.model_selector,
            after.mode_selector,
            prepared)
        && common_exact(after.common, prepared.common)
        && full_identity_exact(
            selected_mode(prepared.mode_selector),
            after.owner,
            membership.identity)
        && mode_identity_shape_valid(
            selected_mode(prepared.mode_selector),
            after.owner)
        && input_claim_selection_equal(after.lookup, before.lookup)
        && active_claims_valid(after);
}

constexpr bool device_released(
    const PreparedAuthority& prepared,
    const MembershipSnapshot& membership,
    const DeviceSnapshot& before,
    const DeviceSnapshot& after) {
    return device_after_shape_valid(prepared, membership, before, after)
        && target_claim_candidate_count(after, prepared.claim) == 0
        && target_claim_count(after, prepared.claim) == 0;
}

constexpr bool device_intact(
    const PreparedAuthority& prepared,
    const MembershipSnapshot& membership,
    const DeviceSnapshot& before,
    const DeviceSnapshot& after) {
    return device_after_shape_valid(prepared, membership, before, after)
        && target_claim_candidate_count(after, prepared.claim) == 1
        && target_claim_count(after, prepared.claim) == 1;
}

constexpr bool membership_release_proof_valid(
    const ExpectedReleaseBinding& expected,
    const MembershipProofSlot& slot) {
    if (!slot.present) {
        return false;
    }
    const MembershipReleaseProof& proof = slot.value.proof;
    return field_exact(proof.unit_selector, expected.unit_selector)
        && field_exact(proof.model_selector, expected.model_selector)
        && field_exact(proof.mode_selector, expected.mode_selector)
        && field_exact(
            proof.operation_selector,
            expected.operation_selector)
        && common_exact(proof.common, expected.common)
        && mode_identity_shape_valid(
            selected_mode(expected.mode_selector),
            expected.owner)
        && mode_identity_shape_valid(
            selected_mode(expected.mode_selector),
            proof.owner)
        && full_identity_exact(
            selected_mode(expected.mode_selector),
            proof.owner,
            expected.owner);
}

constexpr bool device_release_proof_valid(
    const ExpectedReleaseBinding& expected,
    const DeviceProofSlot& slot) {
    if (!slot.present) {
        return false;
    }
    const DeviceReleaseProof& proof = slot.value.proof;
    return field_exact(proof.unit_selector, expected.unit_selector)
        && field_exact(proof.model_selector, expected.model_selector)
        && field_exact(proof.mode_selector, expected.mode_selector)
        && field_exact(
            proof.operation_selector,
            expected.operation_selector)
        && common_exact(proof.common, expected.common)
        && mode_identity_shape_valid(
            selected_mode(expected.mode_selector),
            expected.owner)
        && mode_identity_shape_valid(
            selected_mode(expected.mode_selector),
            proof.owner)
        && full_identity_exact(
            selected_mode(expected.mode_selector),
            proof.owner,
            expected.owner)
        && field_exact(
            proof.claim.claim_identity,
            expected.claim.claim_identity)
        && field_exact(
            proof.claim.claim_generation,
            expected.claim.claim_generation);
}

constexpr bool release_proofs_exact(
    const MembershipProofSlot& membership_slot,
    const DeviceProofSlot& device_slot) {
    if (!membership_slot.present || !device_slot.present) {
        return false;
    }
    const MembershipReleaseProof& membership =
        membership_slot.value.proof;
    const DeviceReleaseProof& device = device_slot.value.proof;
    return field_exact(membership.unit_selector, device.unit_selector)
        && field_exact(membership.model_selector, device.model_selector)
        && field_exact(membership.mode_selector, device.mode_selector)
        && field_exact(
            membership.operation_selector,
            device.operation_selector)
        && common_exact(membership.common, device.common)
        && full_identity_exact(
            selected_mode(membership.mode_selector),
            membership.owner,
            device.owner);
}

constexpr bool release_composition_valid(
    const ReleaseComposition& composition) {
    return composition.action_context
            == ValidatedActionContext::attempted_and_acknowledged
        && composition.credit_stage == CreditStage::after_both_proofs
        && membership_release_proof_valid(
               composition.expected,
               composition.membership)
        && device_release_proof_valid(
            composition.expected,
            composition.device)
        && release_proofs_exact(
            composition.membership,
            composition.device);
}

constexpr Decision decide_composition(
    const ReleaseComposition& composition) {
    return release_composition_valid(composition)
        ? Decision{
            Result::verified_release,
            ClaimDisposition::released,
            1,
            1,
        }
        : Decision{
            Result::quarantine,
            ClaimDisposition::maximum,
            1,
            0,
        };
}

constexpr ReleaseComposition derive_release_composition(
    const RecoveryEvidence& evidence) {
    const ExpectedReleaseBinding expected = {
        evidence.prepared.unit_selector,
        evidence.prepared.model_selector,
        evidence.prepared.mode_selector,
        evidence.prepared.operation_selector,
        evidence.prepared.common,
        evidence.prepared.identity,
        evidence.prepared.claim,
    };
    const MembershipReleaseProof membership = {
        evidence.membership_after.unit_selector,
        evidence.membership_after.model_selector,
        evidence.membership_after.mode_selector,
        evidence.prepared.operation_selector,
        evidence.membership_after.common,
        evidence.membership_after.identity,
    };
    const DeviceReleaseProof device = {
        evidence.device_after.unit_selector,
        evidence.device_after.model_selector,
        evidence.device_after.mode_selector,
        evidence.prepared.operation_selector,
        evidence.device_after.common,
        evidence.device_after.owner,
        {
            evidence.device_after.lookup.claim_identity,
            evidence.device_after.lookup.claim_generation,
        },
    };
    return {
        ValidatedActionContext::attempted_and_acknowledged,
        CreditStage::after_both_proofs,
        expected,
        MembershipProofSlot(membership),
        DeviceProofSlot(device),
    };
}

constexpr Decision decide(const RecoveryEvidence& evidence) {
    const TerminationEvidence& termination = evidence.termination;
    const std::uint64_t effect_calls = termination.attempted ? 1 : 0;
    if (!evidence.liveness_before
        || !prepared_valid(evidence.prepared)
        || !membership_before_valid(
            evidence.prepared,
            evidence.membership_before)
        || !device_before_valid(
            evidence.prepared,
            evidence.membership_before,
            evidence.device_before)) {
        if (termination.attempted || termination.acknowledged) {
            return {
                Result::quarantine,
                ClaimDisposition::maximum,
                effect_calls,
                0,
            };
        }
        return {Result::unknown, ClaimDisposition::preserved, 0, 0};
    }

    const bool valid_target = target_valid(
        evidence.prepared,
        evidence.membership_before,
        termination.target);
    if (!valid_target) {
        if (termination.attempted || termination.acknowledged) {
            return {
                Result::quarantine,
                ClaimDisposition::maximum,
                effect_calls,
                0,
            };
        }
        return {Result::unknown, ClaimDisposition::preserved, 0, 0};
    }

    if (evidence.credit_stage == CreditStage::before_both_proofs) {
        return termination.attempted || termination.acknowledged
            ? Decision{
                Result::quarantine,
                ClaimDisposition::maximum,
                effect_calls,
                0,
            }
            : Decision{Result::unknown, ClaimDisposition::preserved, 0, 0};
    }

    if (!termination.attempted) {
        if (termination.acknowledged) {
            return {Result::quarantine, ClaimDisposition::maximum, 0, 0};
        }
        const bool intact = membership_intact(
                                evidence.prepared,
                                evidence.membership_before,
                                evidence.membership_after)
            && device_intact(
                evidence.prepared,
                evidence.membership_before,
                evidence.device_before,
                evidence.device_after);
        if (!evidence.liveness_after || !intact) {
            return {Result::quarantine, ClaimDisposition::maximum, 0, 0};
        }
        return termination.request == RequestState::absent
            ? Decision{Result::unknown, ClaimDisposition::preserved, 0, 0}
            : Decision{
                Result::verified_intact,
                ClaimDisposition::preserved,
                0,
                0,
            };
    }

    if (termination.request != RequestState::accepted) {
        return {Result::quarantine, ClaimDisposition::maximum, 1, 0};
    }

    if (!termination.acknowledged
        || !target_exact(
            termination.acknowledgement_target,
            termination.target)
        || !field_exact(
            termination.acknowledgement_action_token,
            common_field(
                evidence.prepared.common,
                CommonToken::termination_action_token))) {
        return {Result::quarantine, ClaimDisposition::maximum, 1, 0};
    }

    if (!evidence.liveness_after) {
        return {Result::quarantine, ClaimDisposition::maximum, 1, 0};
    }

    const bool member_release = membership_released(
        evidence.prepared,
        evidence.membership_before,
        evidence.membership_after);
    const bool device_release = device_released(
        evidence.prepared,
        evidence.membership_before,
        evidence.device_before,
        evidence.device_after);
    return member_release && device_release
            && release_composition_valid(
                derive_release_composition(evidence))
        ? Decision{Result::verified_release, ClaimDisposition::released, 1, 1}
        : Decision{Result::quarantine, ClaimDisposition::maximum, 1, 0};
}

constexpr ObservationEnvelope make_envelope(std::uint64_t generation) {
    return {true, true, true, true, {make_field(generation), true}};
}

constexpr RecoveryEvidence make_release_evidence(Unit unit, Mode mode) {
    const std::uint64_t seed = input_seed(unit, mode);
    const CommonIdentity common = make_common(seed * 100);
    const ModeIdentity control_identity = make_prepared_identity(
        mode,
        seed * 1000);
    const ModeIdentity identity = make_member_identity(
        mode,
        control_identity,
        seed * 1000);
    const DeviceClaimSelection claim = {
        make_field(seed * 10000 + 1),
        make_field(23),
    };
    const PreparedAuthority prepared = {
        PreparedAuthorityState::known,
        selector_field(unit),
        selector_field(
            static_cast<Model>(static_cast<std::uint64_t>(unit))),
        selector_field(mode),
        selector_field(OperationLeaf::service_termination),
        common,
        identity,
        claim,
    };
    const MembershipSnapshot before = {
        make_envelope(mode == Mode::direct_process ? 7 : 11),
        prepared.unit_selector,
        prepared.model_selector,
        prepared.mode_selector,
        common,
        identity,
        {{
            {
                true,
                membership_root(mode, identity),
                OwnershipState::exact,
            },
            {false, missing_field(), OwnershipState::exact},
        }},
        {{
            {
                true,
                mode_field(identity, 6),
                mode_field(identity, 7),
                mode_field(identity, 8),
                MemberClassification::serving_process,
            },
            {
                false,
                missing_field(),
                missing_field(),
                missing_field(),
                MemberClassification::serving_process,
            },
            {
                false,
                missing_field(),
                missing_field(),
                missing_field(),
                MemberClassification::serving_process,
            },
        }},
        1,
        mode == Mode::managed_service
            ? ServiceState::running
            : ServiceState::not_applicable,
    };
    MembershipSnapshot after = before;
    after.envelope = make_envelope(before.envelope.generation.value.value + 1);
    after.members[0].present = false;
    after.members[0].process_identity = missing_field();
    after.members[0].process_birth_token = missing_field();
    after.members[0].executable_digest = missing_field();
    after.declared_member_count = 0;
    if (mode == Mode::managed_service) {
        after.service_state = ServiceState::stopped;
    }
    const DeviceSnapshot device_before = {
        make_envelope(17),
        prepared.unit_selector,
        prepared.model_selector,
        prepared.mode_selector,
        common,
        identity,
        {
            prepared.claim.claim_identity,
            prepared.claim.claim_generation,
        },
        {{
            {
                true,
                prepared.claim.claim_identity,
                prepared.claim.claim_generation,
            },
            {
                true,
                make_field(seed * 10000 + 3),
                make_field(seed * 10000 + 4),
            },
            {
                false,
                missing_field(),
                missing_field(),
            },
        }},
    };
    DeviceSnapshot device_after = device_before;
    device_after.envelope = make_envelope(
        device_before.envelope.generation.value.value + 1);
    device_after.active_claims[0].present = false;
    device_after.active_claims[0].claim_identity = missing_field();
    device_after.active_claims[0].claim_generation = missing_field();
    const TerminationTarget target = {
        prepared.unit_selector,
        prepared.model_selector,
        prepared.mode_selector,
        prepared.operation_selector,
        common,
        identity,
    };
    const TerminationEvidence termination = {
        RequestState::accepted,
        true,
        true,
        target,
        target,
        common_field(common, CommonToken::termination_action_token),
    };
    return {
        prepared,
        true,
        true,
        CreditStage::after_both_proofs,
        before,
        after,
        device_before,
        device_after,
        termination,
    };
}
constexpr void make_intact_after(RecoveryEvidence& evidence) {
    evidence.membership_after.members[0].present = true;
    evidence.membership_after.members[0].process_identity =
        mode_field(evidence.membership_after.identity, 6);
    evidence.membership_after.members[0].process_birth_token =
        mode_field(evidence.membership_after.identity, 7);
    evidence.membership_after.members[0].executable_digest =
        mode_field(evidence.membership_after.identity, 8);
    evidence.membership_after.members[0].classification =
        MemberClassification::serving_process;
    evidence.membership_after.declared_member_count = 1;
    evidence.membership_after.service_state =
        selected_mode(evidence.membership_after.mode_selector)
                == Mode::managed_service
        ? ServiceState::running
        : ServiceState::not_applicable;
    evidence.device_after.active_claims[0] = {
        true,
        evidence.device_after.lookup.claim_identity,
        evidence.device_after.lookup.claim_generation,
    };
}

constexpr void clear_action(RecoveryEvidence& evidence) {
    make_intact_after(evidence);
    evidence.termination.attempted = false;
    evidence.termination.acknowledged = false;
}

constexpr bool decision_is(
    const RecoveryEvidence& evidence,
    Result result,
    ClaimDisposition claims,
    std::uint64_t effects,
    std::uint64_t credit) {
    const Decision decision = decide(evidence);
    return decision.result == result && decision.claims == claims
        && decision.effect_calls == effects
        && decision.release_credit == credit;
}

constexpr bool decisions_equal(const Decision& left, const Decision& right) {
    return left.result == right.result && left.claims == right.claims
        && left.effect_calls == right.effect_calls
        && left.release_credit == right.release_credit;
}

struct RawFixtureEntry {
    bool used;
    std::uint64_t digest;
    ReleaseInput input;
};

struct CompositionFixtureEntry {
    bool used;
    std::uint64_t digest;
    ReleaseComposition input;
};

std::array<RawFixtureEntry, 32768> raw_fixture_table{};
std::array<CompositionFixtureEntry, 1024> composition_fixture_table{};

struct FixtureAudit {
    std::size_t raw_count;
    std::size_t composition_count;
    std::size_t precondition_count;
    bool passed;

    bool insert_raw(const ReleaseInput& input) {
        const std::uint64_t digest = release_input_digest(input);
        std::size_t slot = static_cast<std::size_t>(digest) & 32767;
        for (std::size_t probe = 0; probe < raw_fixture_table.size(); ++probe) {
            RawFixtureEntry& entry = raw_fixture_table[slot];
            if (!entry.used) {
                entry = {true, digest, input};
                ++raw_count;
                return true;
            }
            if (entry.digest == digest
                && release_input_equal(entry.input, input)) {
                return false;
            }
            slot = (slot + 1) & 32767;
        }
        return false;
    }

    bool insert_composition(const ReleaseComposition& input) {
        const std::uint64_t digest = release_composition_digest(input);
        std::size_t slot = static_cast<std::size_t>(digest) & 1023;
        for (std::size_t probe = 0;
             probe < composition_fixture_table.size();
             ++probe) {
            CompositionFixtureEntry& entry = composition_fixture_table[slot];
            if (!entry.used) {
                entry = {true, digest, input};
                ++composition_count;
                return true;
            }
            if (entry.digest == digest
                && release_composition_equal(entry.input, input)) {
                return false;
            }
            slot = (slot + 1) & 1023;
        }
        return false;
    }

    void raw(const ReleaseInput& input, const Decision& expected) {
        if (!passed || !decisions_equal(decide(input), expected)
            || !insert_raw(input)) {
            passed = false;
        }
    }

    void composition(
        const ReleaseComposition& input,
        const Decision& expected) {
        if (!passed || !decisions_equal(decide_composition(input), expected)
            || !insert_composition(input)) {
            passed = false;
        }
    }
};

constexpr Decision unknown_preserved_decision() {
    return {Result::unknown, ClaimDisposition::preserved, 0, 0};
}

constexpr Decision quarantine_decision(std::uint64_t effects) {
    return {Result::quarantine, ClaimDisposition::maximum, effects, 0};
}

constexpr Decision verified_intact_decision() {
    return {Result::verified_intact, ClaimDisposition::preserved, 0, 0};
}

constexpr Decision verified_release_decision() {
    return {Result::verified_release, ClaimDisposition::released, 1, 1};
}

constexpr void set_intact_action_state(
    RecoveryEvidence& evidence,
    RequestState request,
    bool attempted,
    bool acknowledged) {
    make_intact_after(evidence);
    evidence.termination.request = request;
    evidence.termination.attempted = attempted;
    evidence.termination.acknowledged = acknowledged;
}

void audit_precondition_fixture(
    FixtureAudit& audit,
    const RecoveryEvidence& fault) {
    ++audit.precondition_count;
    RecoveryEvidence input = fault;
    set_intact_action_state(
        input,
        RequestState::absent,
        false,
        false);
    audit.raw(input, unknown_preserved_decision());
    for (std::uint64_t request_value = 0; request_value < 3; ++request_value) {
        for (std::uint64_t acknowledged = 0;
             acknowledged < 2;
             ++acknowledged) {
            input = fault;
            input.termination.request = static_cast<RequestState>(request_value);
            input.termination.attempted = true;
            input.termination.acknowledged = acknowledged != 0;
            audit.raw(input, quarantine_decision(1));
        }
        input = fault;
        set_intact_action_state(
            input,
            static_cast<RequestState>(request_value),
            false,
            true);
        audit.raw(input, quarantine_decision(0));
    }
}

constexpr Field selector_unknown(std::size_t field) {
    if (field == 0) {
        return selector_field(Unit::unknown);
    }
    if (field == 1) {
        return selector_field(Model::unknown);
    }
    return selector_field(Mode::unknown);
}

constexpr Field& prepared_selector(
    PreparedAuthority& prepared,
    std::size_t field) {
    if (field == 0) {
        return prepared.unit_selector;
    }
    if (field == 1) {
        return prepared.model_selector;
    }
    return prepared.mode_selector;
}

constexpr Field& target_selector(
    TerminationTarget& target,
    std::size_t field) {
    if (field == 0) {
        return target.unit_selector;
    }
    if (field == 1) {
        return target.model_selector;
    }
    if (field == 2) {
        return target.operation_selector;
    }
    return target.mode_selector;
}

constexpr Field unknown_target_selector(std::size_t field) {
    if (field == 0) {
        return selector_field(Unit::unknown);
    }
    if (field == 1) {
        return selector_field(Model::unknown);
    }
    return selector_field(Mode::unknown);
}

constexpr void apply_absent_claim(DeviceClaimKey& claim) {
    claim.present = false;
    claim.claim_identity = missing_field();
    claim.claim_generation = missing_field();
}

constexpr void select_other_field(
    MembershipSnapshot& snapshot,
    Unit other,
    std::size_t field) {
    if (field == 0) {
        snapshot.unit_selector = selector_field(other);
    } else {
        snapshot.model_selector = selector_field(
            static_cast<Model>(static_cast<std::uint64_t>(other)));
    }
}

constexpr void select_other_field(
    DeviceSnapshot& snapshot,
    Unit other,
    std::size_t field) {
    if (field == 0) {
        snapshot.unit_selector = selector_field(other);
    } else {
        snapshot.model_selector = selector_field(
            static_cast<Model>(static_cast<std::uint64_t>(other)));
    }
}

constexpr bool positive_matrix_passes() {
    for (std::uint64_t unit_value = 0; unit_value < 3; ++unit_value) {
        for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
            const RecoveryEvidence evidence = make_release_evidence(
                static_cast<Unit>(unit_value),
                static_cast<Mode>(mode_value));
            if (!decision_is(
                    evidence,
                    Result::verified_release,
                    ClaimDisposition::released,
                    1,
                    1)) {
                return false;
            }
        }
    }
    return true;
}

constexpr bool common_fault_matrix_passes() {
    for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
        const Mode mode = static_cast<Mode>(mode_value);
        for (std::size_t index = 0; index < 14; ++index) {
            for (std::uint64_t fault = 0; fault < 3; ++fault) {
                RecoveryEvidence evidence = make_release_evidence(Unit::llm, mode);
                apply_field_fault(evidence.prepared.common.fields[index], fault);
                clear_action(evidence);
                if (decide(evidence).result != Result::unknown) {
                    return false;
                }
                evidence = make_release_evidence(Unit::llm, mode);
                apply_field_fault(
                    evidence.membership_before.common.fields[index],
                    fault);
                clear_action(evidence);
                if (decide(evidence).result != Result::unknown) {
                    return false;
                }
                evidence = make_release_evidence(Unit::llm, mode);
                apply_field_fault(
                    evidence.termination.target.common.fields[index],
                    fault);
                clear_action(evidence);
                if (decide(evidence).result != Result::unknown) {
                    return false;
                }
                evidence = make_release_evidence(Unit::llm, mode);
                apply_field_fault(
                    evidence.membership_after.common.fields[index],
                    fault);
                if (decide(evidence).result != Result::quarantine) {
                    return false;
                }
                evidence = make_release_evidence(Unit::llm, mode);
                apply_field_fault(
                    evidence.termination.acknowledgement_target.common.fields[index],
                    fault);
                if (decide(evidence).result != Result::quarantine) {
                    return false;
                }
                evidence = make_release_evidence(Unit::llm, mode);
                apply_field_fault(
                    evidence.device_before.common.fields[index],
                    fault);
                clear_action(evidence);
                if (decide(evidence).result != Result::unknown) {
                    return false;
                }
                evidence = make_release_evidence(Unit::llm, mode);
                apply_field_fault(
                    evidence.device_after.common.fields[index],
                    fault);
                if (decide(evidence).result != Result::quarantine) {
                    return false;
                }
            }
        }
    }
    return true;
}

constexpr bool mode_identity_fault_matrix_passes() {
    for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
        const Mode mode = static_cast<Mode>(mode_value);
        for (std::size_t ordinal = 0;
             ordinal < full_identity_count(mode);
             ++ordinal) {
            const std::size_t index = full_identity_index(mode, ordinal);
            for (std::uint64_t fault = 0; fault < 3; ++fault) {
                RecoveryEvidence evidence = make_release_evidence(
                    Unit::embedding,
                    mode);
                apply_field_fault(
                    mode_field(evidence.prepared.identity, index),
                    fault);
                clear_action(evidence);
                if (decide(evidence).result != Result::unknown) {
                    return false;
                }
                evidence = make_release_evidence(Unit::embedding, mode);
                apply_field_fault(
                    mode_field(evidence.membership_before.identity, index),
                    fault);
                clear_action(evidence);
                if (decide(evidence).result != Result::unknown) {
                    return false;
                }
            }
        }
        for (std::size_t ordinal = 0;
             ordinal < full_identity_count(mode);
             ++ordinal) {
            const std::size_t index = full_identity_index(mode, ordinal);
            for (std::uint64_t fault = 0; fault < 3; ++fault) {
                RecoveryEvidence evidence = make_release_evidence(
                    Unit::transcription,
                    mode);
                apply_field_fault(
                    mode_field(evidence.termination.target.identity, index),
                    fault);
                clear_action(evidence);
                if (decide(evidence).result != Result::unknown) {
                    return false;
                }
                evidence = make_release_evidence(Unit::transcription, mode);
                apply_field_fault(
                    mode_field(
                        evidence.termination.acknowledgement_target.identity,
                        index),
                    fault);
                if (decide(evidence).result != Result::quarantine) {
                    return false;
                }
                evidence = make_release_evidence(Unit::transcription, mode);
                apply_field_fault(
                    mode_field(evidence.membership_after.identity, index),
                    fault);
                if (decide(evidence).result != Result::quarantine) {
                    return false;
                }
                evidence = make_release_evidence(Unit::transcription, mode);
                apply_field_fault(
                    mode_field(evidence.device_before.owner, index),
                    fault);
                clear_action(evidence);
                if (decide(evidence).result != Result::unknown) {
                    return false;
                }
                evidence = make_release_evidence(Unit::transcription, mode);
                apply_field_fault(
                    mode_field(evidence.device_after.owner, index),
                    fault);
                if (decide(evidence).result != Result::quarantine) {
                    return false;
                }
            }
        }
    }
    return true;
}

constexpr void select_other(
    MembershipSnapshot& snapshot,
    Unit unit) {
    snapshot.unit_selector = selector_field(unit);
    snapshot.model_selector = selector_field(
        static_cast<Model>(static_cast<std::uint64_t>(unit)));
}

constexpr void select_other(
    DeviceSnapshot& snapshot,
    Unit unit) {
    snapshot.unit_selector = selector_field(unit);
    snapshot.model_selector = selector_field(
        static_cast<Model>(static_cast<std::uint64_t>(unit)));
}

constexpr Field& raw_selector(
    MembershipSnapshot& snapshot,
    std::size_t field) {
    if (field == 0) {
        return snapshot.unit_selector;
    }
    if (field == 1) {
        return snapshot.model_selector;
    }
    return snapshot.mode_selector;
}

constexpr Field& raw_selector(
    DeviceSnapshot& snapshot,
    std::size_t field) {
    if (field == 0) {
        return snapshot.unit_selector;
    }
    if (field == 1) {
        return snapshot.model_selector;
    }
    return snapshot.mode_selector;
}

constexpr Field raw_selector_mutation(
    Unit unit,
    Mode mode,
    std::size_t field,
    std::size_t mutation) {
    if (mutation == 0) {
        return missing_field();
    }
    if (mutation == 1) {
        return make_field(0);
    }
    if (mutation == 2) {
        if (field == 0) {
            return selector_field(Unit::unknown);
        }
        if (field == 1) {
            return selector_field(Model::unknown);
        }
        return selector_field(Mode::unknown);
    }
    if (field == 0) {
        return selector_field(static_cast<Unit>(
            (static_cast<std::uint64_t>(unit) + 1) % 3));
    }
    if (field == 1) {
        return selector_field(static_cast<Model>(
            (static_cast<std::uint64_t>(unit) + 1) % 3));
    }
    return selector_field(mode == Mode::direct_process
            ? Mode::managed_service
            : Mode::direct_process);
}

constexpr bool raw_selector_fault_matrix_passes() {
    for (std::uint64_t unit_value = 0; unit_value < 3; ++unit_value) {
        const Unit unit = static_cast<Unit>(unit_value);
        for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
            const Mode mode = static_cast<Mode>(mode_value);
            for (std::size_t field = 0; field < 3; ++field) {
                for (std::size_t mutation = 0; mutation < 4; ++mutation) {
                    RecoveryEvidence evidence = make_release_evidence(
                        unit,
                        mode);
                    raw_selector(evidence.membership_before, field) =
                        raw_selector_mutation(unit, mode, field, mutation);
                    clear_action(evidence);
                    if (!decision_is(
                            evidence,
                            Result::unknown,
                            ClaimDisposition::preserved,
                            0,
                            0)) {
                        return false;
                    }
                    evidence = make_release_evidence(unit, mode);
                    raw_selector(evidence.device_before, field) =
                        raw_selector_mutation(unit, mode, field, mutation);
                    clear_action(evidence);
                    if (!decision_is(
                            evidence,
                            Result::unknown,
                            ClaimDisposition::preserved,
                            0,
                            0)) {
                        return false;
                    }
                    evidence = make_release_evidence(unit, mode);
                    raw_selector(evidence.membership_after, field) =
                        raw_selector_mutation(unit, mode, field, mutation);
                    if (!decision_is(
                            evidence,
                            Result::quarantine,
                            ClaimDisposition::maximum,
                            1,
                            0)) {
                        return false;
                    }
                    evidence = make_release_evidence(unit, mode);
                    raw_selector(evidence.device_after, field) =
                        raw_selector_mutation(unit, mode, field, mutation);
                    if (!decision_is(
                            evidence,
                            Result::quarantine,
                            ClaimDisposition::maximum,
                            1,
                            0)) {
                        return false;
                    }
                }
            }
            const Field opposite_mode = selector_field(
                mode == Mode::direct_process
                    ? Mode::managed_service
                    : Mode::direct_process);
            for (std::size_t shape = 0; shape < 3; ++shape) {
                RecoveryEvidence evidence = make_release_evidence(unit, mode);
                if (shape != 1) {
                    evidence.membership_before.mode_selector = opposite_mode;
                }
                if (shape != 0) {
                    evidence.device_before.mode_selector = opposite_mode;
                }
                clear_action(evidence);
                if (decide(evidence).result != Result::unknown) {
                    return false;
                }
                evidence = make_release_evidence(unit, mode);
                if (shape != 1) {
                    evidence.membership_after.mode_selector = opposite_mode;
                }
                if (shape != 0) {
                    evidence.device_after.mode_selector = opposite_mode;
                }
                if (decide(evidence).result != Result::quarantine) {
                    return false;
                }
            }
        }
    }
    return true;
}

constexpr bool selection_leg_crosswire_matrix_passes() {
    for (std::uint64_t selected_value = 0; selected_value < 3; ++selected_value) {
        const Unit selected = static_cast<Unit>(selected_value);
        for (std::uint64_t other_value = 0; other_value < 3; ++other_value) {
            if (selected_value == other_value) {
                continue;
            }
            const Unit other = static_cast<Unit>(other_value);
            for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
                const Mode mode = static_cast<Mode>(mode_value);
                for (std::uint64_t shape = 0; shape < 3; ++shape) {
                    RecoveryEvidence evidence = make_release_evidence(
                        selected,
                        mode);
                    if (shape != 1) {
                        select_other(evidence.membership_before, other);
                    }
                    if (shape != 0) {
                        select_other(evidence.device_before, other);
                    }
                    clear_action(evidence);
                    if (!decision_is(
                            evidence,
                            Result::unknown,
                            ClaimDisposition::preserved,
                            0,
                            0)) {
                        return false;
                    }
                    evidence = make_release_evidence(selected, mode);
                    if (shape != 1) {
                        select_other(evidence.membership_after, other);
                    }
                    if (shape != 0) {
                        select_other(evidence.device_after, other);
                    }
                    if (!decision_is(
                            evidence,
                            Result::quarantine,
                            ClaimDisposition::maximum,
                            1,
                            0)) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

constexpr bool unit_model_crosswire_matrix_passes() {
    for (std::uint64_t unit_value = 0; unit_value < 3; ++unit_value) {
        for (std::uint64_t model_value = 0; model_value < 3; ++model_value) {
            if (unit_value == model_value) {
                continue;
            }
            for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
                RecoveryEvidence evidence = make_release_evidence(
                    static_cast<Unit>(unit_value),
                    static_cast<Mode>(mode_value));
                evidence.prepared.model_selector = selector_field(
                    static_cast<Model>(model_value));
                clear_action(evidence);
                if (!decision_is(
                        evidence,
                        Result::unknown,
                        ClaimDisposition::preserved,
                        0,
                        0)) {
                    return false;
                }
            }
        }
    }
    return true;
}

constexpr void activate_canonical_member(
    MembershipSnapshot& snapshot,
    std::size_t index) {
    snapshot.members[index] = {
        true,
        mode_field(snapshot.identity, 6),
        mode_field(snapshot.identity, 7),
        mode_field(snapshot.identity, 8),
        MemberClassification::serving_process,
    };
}

constexpr void apply_membership_shape_fault(
    MembershipSnapshot& snapshot,
    std::uint64_t fault) {
    if (fault == 0) {
        snapshot.roots[0].present = false;
        snapshot.roots[0].identity = missing_field();
    } else if (fault == 1) {
        snapshot.members[0].present = false;
        snapshot.members[0].process_identity = missing_field();
        snapshot.members[0].process_birth_token = missing_field();
        snapshot.members[0].executable_digest = missing_field();
        snapshot.members[0].classification =
            MemberClassification::serving_process;
        snapshot.declared_member_count = 0;
    } else if (fault == 2) {
        ++snapshot.roots[0].identity.value;
    } else if (fault == 3) {
        snapshot.roots[1] = snapshot.roots[0];
        snapshot.roots[1].present = true;
    } else if (fault == 4) {
        ++snapshot.declared_member_count;
    } else if (fault == 5) {
        activate_canonical_member(snapshot, 0);
        ++snapshot.members[0].process_identity.value;
        ++snapshot.members[0].process_birth_token.value;
        ++snapshot.members[0].executable_digest.value;
        snapshot.members[0].classification = MemberClassification::unknown;
        snapshot.declared_member_count = active_member_count(snapshot);
    } else if (fault == 6) {
        activate_canonical_member(snapshot, 0);
        snapshot.members[1] = snapshot.members[0];
        snapshot.members[1].present = true;
        snapshot.declared_member_count = active_member_count(snapshot);
    } else if (fault == 7) {
        activate_canonical_member(snapshot, 0);
        snapshot.members[0].process_identity.value += 10;
        snapshot.members[0].process_birth_token.value += 10;
        snapshot.members[0].executable_digest.value += 10;
        snapshot.members[0].classification =
            MemberClassification::external_package;
        snapshot.declared_member_count = active_member_count(snapshot);
    } else if (fault == 8) {
        activate_canonical_member(snapshot, 0);
        snapshot.members[0].process_identity.value += 20;
        snapshot.members[0].process_birth_token.value += 20;
        snapshot.members[0].executable_digest.value += 20;
        snapshot.members[0].classification = MemberClassification::model_store;
        snapshot.declared_member_count = active_member_count(snapshot);
    } else {
        activate_canonical_member(snapshot, 0);
        snapshot.declared_member_count = 1;
        const std::size_t identity_field = (fault - 9) / 3;
        const std::uint64_t mutation = (fault - 9) % 3;
        Field* selected = &snapshot.members[0].process_identity;
        if (identity_field == 1) {
            selected = &snapshot.members[0].process_birth_token;
        } else if (identity_field == 2) {
            selected = &snapshot.members[0].executable_digest;
        }
        apply_field_fault(*selected, mutation);
    }
}

constexpr CommonIdentity& pre_common_locus(
    RecoveryEvidence& evidence,
    std::size_t locus) {
    if (locus == 0) {
        return evidence.prepared.common;
    }
    if (locus == 1) {
        return evidence.membership_before.common;
    }
    if (locus == 2) {
        return evidence.termination.target.common;
    }
    return evidence.device_before.common;
}

void generate_precondition_fixtures(FixtureAudit& audit) {
    for (std::uint64_t unit_value = 0; unit_value < 3; ++unit_value) {
        const Unit unit = static_cast<Unit>(unit_value);
        for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
            const Mode mode = static_cast<Mode>(mode_value);
            RecoveryEvidence evidence = make_release_evidence(unit, mode);
            evidence.prepared.state = PreparedAuthorityState::profile_unknown;
            audit_precondition_fixture(audit, evidence);

            evidence = make_release_evidence(unit, mode);
            evidence.prepared.state = PreparedAuthorityState::missing;
            audit_precondition_fixture(audit, evidence);

            for (std::size_t field = 0; field < 3; ++field) {
                evidence = make_release_evidence(unit, mode);
                prepared_selector(evidence.prepared, field) = missing_field();
                audit_precondition_fixture(audit, evidence);

                evidence = make_release_evidence(unit, mode);
                prepared_selector(evidence.prepared, field) =
                    selector_unknown(field);
                audit_precondition_fixture(audit, evidence);
            }

            evidence = make_release_evidence(unit, mode);
            evidence.prepared.operation_selector = missing_field();
            audit_precondition_fixture(audit, evidence);
            for (std::uint64_t leaf = 1; leaf <= 5; ++leaf) {
                evidence = make_release_evidence(unit, mode);
                evidence.prepared.operation_selector = selector_field(
                    static_cast<OperationLeaf>(leaf));
                audit_precondition_fixture(audit, evidence);
            }

            for (std::size_t envelope_fault = 0;
                 envelope_fault < 4;
                 ++envelope_fault) {
                evidence = make_release_evidence(unit, mode);
                if (envelope_fault == 0) {
                    evidence.membership_before.envelope.present = false;
                } else if (envelope_fault == 1) {
                    evidence.membership_before.envelope.fresh = false;
                } else if (envelope_fault == 2) {
                    evidence.membership_before.envelope.complete = false;
                } else {
                    evidence.membership_before.envelope.healthy = false;
                }
                audit_precondition_fixture(audit, evidence);
            }

            for (std::uint64_t shape = 0; shape < 18; ++shape) {
                evidence = make_release_evidence(unit, mode);
                apply_membership_shape_fault(
                    evidence.membership_before,
                    shape);
                audit_precondition_fixture(audit, evidence);
            }

            for (std::uint64_t ownership = 1; ownership <= 3; ++ownership) {
                evidence = make_release_evidence(unit, mode);
                evidence.membership_before.roots[0].ownership =
                    static_cast<OwnershipState>(ownership);
                audit_precondition_fixture(audit, evidence);
            }

            for (std::size_t leg = 0; leg < 2; ++leg) {
                for (std::size_t field = 0; field < 3; ++field) {
                    evidence = make_release_evidence(unit, mode);
                    if (leg == 0) {
                        raw_selector(evidence.membership_before, field) =
                            missing_field();
                    } else {
                        raw_selector(evidence.device_before, field) =
                            missing_field();
                    }
                    audit_precondition_fixture(audit, evidence);

                    evidence = make_release_evidence(unit, mode);
                    if (leg == 0) {
                        raw_selector(evidence.membership_before, field) =
                            selector_unknown(field);
                    } else {
                        raw_selector(evidence.device_before, field) =
                            selector_unknown(field);
                    }
                    audit_precondition_fixture(audit, evidence);
                }
            }

            for (std::size_t field = 0; field < 14; ++field) {
                for (std::size_t locus = 0; locus < 4; ++locus) {
                    evidence = make_release_evidence(unit, mode);
                    apply_field_fault(
                        pre_common_locus(evidence, locus).fields[field],
                        0);
                    audit_precondition_fixture(audit, evidence);

                    evidence = make_release_evidence(unit, mode);
                    apply_field_fault(
                        pre_common_locus(evidence, locus).fields[field],
                        1);
                    audit_precondition_fixture(audit, evidence);
                }

                evidence = make_release_evidence(unit, mode);
                apply_field_fault(evidence.prepared.common.fields[field], 2);
                audit_precondition_fixture(audit, evidence);

                for (std::size_t locus = 1; locus < 4; ++locus) {
                    evidence = make_release_evidence(unit, mode);
                    apply_field_fault(
                        pre_common_locus(evidence, locus).fields[field],
                        2);
                    audit_precondition_fixture(audit, evidence);
                }
            }

            evidence = make_release_evidence(unit, mode);
            evidence.liveness_before = false;
            audit_precondition_fixture(audit, evidence);
        }
    }

    for (std::uint64_t selected_value = 0; selected_value < 3; ++selected_value) {
        const Unit selected = static_cast<Unit>(selected_value);
        for (std::uint64_t other_value = 0; other_value < 3; ++other_value) {
            if (selected_value == other_value) {
                continue;
            }
            const Unit other = static_cast<Unit>(other_value);
            for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
                const Mode mode = static_cast<Mode>(mode_value);
                RecoveryEvidence evidence = make_release_evidence(selected, mode);
                evidence.prepared.model_selector = selector_field(
                    static_cast<Model>(other_value));
                audit_precondition_fixture(audit, evidence);

                for (std::size_t shape = 0; shape < 3; ++shape) {
                    for (std::size_t field = 0; field < 2; ++field) {
                        evidence = make_release_evidence(selected, mode);
                        if (shape != 1) {
                            select_other_field(
                                evidence.membership_before,
                                other,
                                field);
                        }
                        if (shape != 0) {
                            select_other_field(
                                evidence.device_before,
                                other,
                                field);
                        }
                        audit_precondition_fixture(audit, evidence);
                    }
                }
            }
        }
    }

    for (std::uint64_t unit_value = 0; unit_value < 3; ++unit_value) {
        const Unit unit = static_cast<Unit>(unit_value);
        for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
            const Mode mode = static_cast<Mode>(mode_value);
            const Mode other_mode = mode == Mode::direct_process
                ? Mode::managed_service
                : Mode::direct_process;
            RecoveryEvidence evidence = make_release_evidence(unit, mode);
            evidence.prepared.identity =
                make_release_evidence(unit, other_mode).prepared.identity;
            audit_precondition_fixture(audit, evidence);

            for (std::size_t shape = 0; shape < 3; ++shape) {
                evidence = make_release_evidence(unit, mode);
                if (shape != 1) {
                    evidence.membership_before.mode_selector =
                        selector_field(other_mode);
                }
                if (shape != 0) {
                    evidence.device_before.mode_selector =
                        selector_field(other_mode);
                }
                audit_precondition_fixture(audit, evidence);
            }
        }
    }
}

constexpr std::size_t pre_identity_count(Mode mode) {
    return mode == Mode::direct_process ? 4 : 8;
}

constexpr std::size_t pre_identity_index(Mode mode, std::size_t ordinal) {
    if (mode == Mode::direct_process) {
        return selected_identity_index(mode, ordinal);
    }
    return ordinal < 5 ? ordinal : ordinal + 1;
}

void generate_precondition_fixtures_part_two(FixtureAudit& audit) {
    for (std::uint64_t unit_value = 0; unit_value < 3; ++unit_value) {
        const Unit unit = static_cast<Unit>(unit_value);
        for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
            const Mode mode = static_cast<Mode>(mode_value);
            RecoveryEvidence evidence = make_release_evidence(unit, mode);

            for (std::uint64_t fault = 0; fault < 3; ++fault) {
                evidence = make_release_evidence(unit, mode);
                apply_field_fault(
                    evidence.membership_before.envelope.generation.value,
                    fault < 2 ? fault : 0);
                if (fault == 2) {
                    evidence.membership_before.envelope.generation = {
                        make_field(mode == Mode::direct_process ? 7 : 11),
                        false,
                    };
                }
                audit_precondition_fixture(audit, evidence);

                evidence = make_release_evidence(unit, mode);
                apply_field_fault(
                    evidence.device_before.envelope.generation.value,
                    fault < 2 ? fault : 0);
                if (fault == 2) {
                    evidence.device_before.envelope.generation = {
                        make_field(17),
                        false,
                    };
                }
                audit_precondition_fixture(audit, evidence);
            }

            for (std::size_t field = 0; field < 4; ++field) {
                evidence = make_release_evidence(unit, mode);
                target_selector(evidence.termination.target, field) =
                    missing_field();
                audit_precondition_fixture(audit, evidence);

                evidence = make_release_evidence(unit, mode);
                target_selector(evidence.termination.target, field) =
                    make_field(0);
                audit_precondition_fixture(audit, evidence);

                if (field != 2) {
                    evidence = make_release_evidence(unit, mode);
                    target_selector(evidence.termination.target, field) =
                        unknown_target_selector(field);
                    audit_precondition_fixture(audit, evidence);
                }
            }

            for (std::size_t ordinal = 0;
                 ordinal < full_identity_count(mode);
                 ++ordinal) {
                const std::size_t index = full_identity_index(mode, ordinal);
                for (std::uint64_t fault = 0; fault < 3; ++fault) {
                    evidence = make_release_evidence(unit, mode);
                    apply_field_fault(
                        mode_field(evidence.termination.target.identity, index),
                        fault);
                    audit_precondition_fixture(audit, evidence);
                }
            }

            for (std::uint64_t other_value = 0; other_value < 3; ++other_value) {
                if (other_value == unit_value) {
                    continue;
                }
                for (std::size_t field = 0; field < 2; ++field) {
                    evidence = make_release_evidence(unit, mode);
                    if (field == 0) {
                        evidence.termination.target.unit_selector =
                            selector_field(static_cast<Unit>(other_value));
                    } else {
                        evidence.termination.target.model_selector =
                            selector_field(static_cast<Model>(other_value));
                    }
                    audit_precondition_fixture(audit, evidence);
                }
            }
            for (std::uint64_t leaf = 1; leaf <= 5; ++leaf) {
                evidence = make_release_evidence(unit, mode);
                evidence.termination.target.operation_selector = selector_field(
                    static_cast<OperationLeaf>(leaf));
                audit_precondition_fixture(audit, evidence);
            }
            evidence = make_release_evidence(unit, mode);
            evidence.termination.target.mode_selector = selector_field(
                mode == Mode::direct_process
                    ? Mode::managed_service
                    : Mode::direct_process);
            audit_precondition_fixture(audit, evidence);

            for (std::size_t ordinal = 0;
                 ordinal < pre_identity_count(mode);
                 ++ordinal) {
                const std::size_t index = pre_identity_index(mode, ordinal);
                for (std::uint64_t fault = 0; fault < 3; ++fault) {
                    evidence = make_release_evidence(unit, mode);
                    apply_field_fault(
                        mode_field(evidence.prepared.identity, index),
                        fault);
                    audit_precondition_fixture(audit, evidence);

                    evidence = make_release_evidence(unit, mode);
                    apply_field_fault(
                        mode_field(evidence.membership_before.identity, index),
                        fault);
                    audit_precondition_fixture(audit, evidence);
                }
            }

            for (std::uint64_t fault = 0; fault < 3; ++fault) {
                evidence = make_release_evidence(unit, mode);
                apply_field_fault(evidence.prepared.claim.claim_identity, fault);
                audit_precondition_fixture(audit, evidence);

                for (std::size_t ordinal = 0;
                     ordinal < full_identity_count(mode);
                     ++ordinal) {
                    evidence = make_release_evidence(unit, mode);
                    apply_field_fault(
                        mode_field(
                            evidence.device_before.owner,
                            full_identity_index(mode, ordinal)),
                        fault);
                    audit_precondition_fixture(audit, evidence);
                }

                evidence = make_release_evidence(unit, mode);
                apply_field_fault(
                    evidence.device_before.lookup.claim_identity,
                    fault);
                audit_precondition_fixture(audit, evidence);
            }

            if (mode == Mode::managed_service) {
                for (std::uint64_t fault = 0; fault < 3; ++fault) {
                    evidence = make_release_evidence(unit, mode);
                    apply_field_fault(
                        evidence.prepared.identity.value.managed
                            .service_start_generation,
                        fault);
                    audit_precondition_fixture(audit, evidence);

                    evidence = make_release_evidence(unit, mode);
                    apply_field_fault(
                        evidence.membership_before.identity.value.managed
                            .service_start_generation,
                        fault);
                    audit_precondition_fixture(audit, evidence);
                }

                for (ServiceState state : {
                         ServiceState::missing,
                         ServiceState::unknown,
                         ServiceState::stopped,
                     }) {
                    evidence = make_release_evidence(unit, mode);
                    evidence.membership_before.service_state = state;
                    audit_precondition_fixture(audit, evidence);
                }
            }

            for (std::size_t envelope_fault = 0;
                 envelope_fault < 4;
                 ++envelope_fault) {
                evidence = make_release_evidence(unit, mode);
                if (envelope_fault == 0) {
                    evidence.device_before.envelope.present = false;
                } else if (envelope_fault == 1) {
                    evidence.device_before.envelope.fresh = false;
                } else if (envelope_fault == 2) {
                    evidence.device_before.envelope.healthy = false;
                } else {
                    evidence.device_before.envelope.complete = false;
                }
                audit_precondition_fixture(audit, evidence);
            }

            evidence = make_release_evidence(unit, mode);
            apply_absent_claim(evidence.device_before.active_claims[0]);
            audit_precondition_fixture(audit, evidence);

            evidence = make_release_evidence(unit, mode);
            evidence.device_before.active_claims[2] =
                evidence.device_before.active_claims[0];
            audit_precondition_fixture(audit, evidence);

            evidence = make_release_evidence(unit, mode);
            evidence.device_before.active_claims[2] =
                evidence.device_before.active_claims[0];
            ++evidence.device_before.active_claims[2]
                  .claim_generation.value;
            audit_precondition_fixture(audit, evidence);

            for (std::size_t field = 0; field < 2; ++field) {
                for (std::uint64_t fault = 0; fault < 2; ++fault) {
                    evidence = make_release_evidence(unit, mode);
                    Field& selected = field == 0
                        ? evidence.device_before.active_claims[1]
                              .claim_identity
                        : evidence.device_before.active_claims[1]
                              .claim_generation;
                    apply_field_fault(selected, fault);
                    audit_precondition_fixture(audit, evidence);
                }
            }

            for (std::uint64_t fault = 0; fault < 3; ++fault) {
                evidence = make_release_evidence(unit, mode);
                apply_field_fault(
                    evidence.prepared.claim.claim_generation,
                    fault);
                audit_precondition_fixture(audit, evidence);

                evidence = make_release_evidence(unit, mode);
                apply_field_fault(
                    evidence.device_before.lookup.claim_generation,
                    fault);
                audit_precondition_fixture(audit, evidence);
            }
        }
    }
}

constexpr CommonIdentity& post_common_locus(
    RecoveryEvidence& evidence,
    std::size_t locus) {
    if (locus == 0) {
        return evidence.membership_after.common;
    }
    if (locus == 1) {
        return evidence.termination.acknowledgement_target.common;
    }
    return evidence.device_after.common;
}

void generate_action_fixtures(FixtureAudit& audit) {
    for (std::uint64_t unit_value = 0; unit_value < 3; ++unit_value) {
        const Unit unit = static_cast<Unit>(unit_value);
        for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
            const Mode mode = static_cast<Mode>(mode_value);
            for (std::uint64_t request_value = 0;
                 request_value < 3;
                 ++request_value) {
                const RequestState request =
                    static_cast<RequestState>(request_value);
                for (std::uint64_t attempted = 0;
                     attempted < 2;
                     ++attempted) {
                    for (std::uint64_t acknowledged = 0;
                         acknowledged < 2;
                         ++acknowledged) {
                        if (request == RequestState::accepted && attempted == 1
                            && acknowledged == 1) {
                            continue;
                        }
                        RecoveryEvidence evidence = make_release_evidence(
                            unit,
                            mode);
                        if (attempted == 0) {
                            make_intact_after(evidence);
                        }
                        evidence.termination.request = request;
                        evidence.termination.attempted = attempted != 0;
                        evidence.termination.acknowledged = acknowledged != 0;
                        if (request == RequestState::absent && attempted == 0
                            && acknowledged == 0) {
                            audit.raw(
                                evidence,
                                unknown_preserved_decision());
                        } else if (request != RequestState::absent
                                   && attempted == 0
                                   && acknowledged == 0) {
                            audit.raw(
                                evidence,
                                verified_intact_decision());
                        } else {
                            audit.raw(
                                evidence,
                                quarantine_decision(attempted));
                        }
                    }
                }
            }

            for (std::uint64_t request_value = 0;
                 request_value < 3;
                 ++request_value) {
                const RecoveryEvidence released = make_release_evidence(
                    unit,
                    mode);
                for (std::size_t changed_leg = 0;
                     changed_leg < 3;
                     ++changed_leg) {
                    RecoveryEvidence evidence = released;
                    make_intact_after(evidence);
                    if (changed_leg != 1) {
                        evidence.membership_after = released.membership_after;
                    }
                    if (changed_leg != 0) {
                        evidence.device_after = released.device_after;
                    }
                    evidence.termination.request =
                        static_cast<RequestState>(request_value);
                    evidence.termination.attempted = false;
                    evidence.termination.acknowledged = false;
                    audit.raw(evidence, quarantine_decision(0));
                }
            }
        }
    }
}

void generate_post_membership_and_selector_fixtures(FixtureAudit& audit) {
    for (std::uint64_t unit_value = 0; unit_value < 3; ++unit_value) {
        const Unit unit = static_cast<Unit>(unit_value);
        for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
            const Mode mode = static_cast<Mode>(mode_value);
            RecoveryEvidence evidence{};
            for (std::uint64_t shape : {0ULL, 2ULL, 3ULL, 4ULL}) {
                evidence = make_release_evidence(unit, mode);
                apply_membership_shape_fault(
                    evidence.membership_after,
                    shape);
                audit.raw(evidence, quarantine_decision(1));
            }
            for (std::uint64_t ownership = 1; ownership <= 3; ++ownership) {
                evidence = make_release_evidence(unit, mode);
                evidence.membership_after.roots[0].ownership =
                    static_cast<OwnershipState>(ownership);
                audit.raw(evidence, quarantine_decision(1));
            }

            for (std::size_t envelope_fault = 0;
                 envelope_fault < 4;
                 ++envelope_fault) {
                evidence = make_release_evidence(unit, mode);
                if (envelope_fault == 0) {
                    evidence.membership_after.envelope.present = false;
                } else if (envelope_fault == 1) {
                    evidence.membership_after.envelope.fresh = false;
                } else if (envelope_fault == 2) {
                    evidence.membership_after.envelope.complete = false;
                } else {
                    evidence.membership_after.envelope.healthy = false;
                }
                audit.raw(evidence, quarantine_decision(1));
            }

            evidence = make_release_evidence(unit, mode);
            activate_canonical_member(evidence.membership_after, 0);
            evidence.membership_after.declared_member_count = 1;
            audit.raw(evidence, quarantine_decision(1));

            if (mode == Mode::direct_process) {
                evidence = make_release_evidence(unit, mode);
                activate_canonical_member(evidence.membership_after, 0);
                ++evidence.membership_after.members[0]
                      .process_birth_token.value;
                evidence.membership_after.declared_member_count = 1;
                audit.raw(evidence, quarantine_decision(1));
            }

            for (std::size_t leg = 0; leg < 2; ++leg) {
                for (std::size_t field = 0; field < 3; ++field) {
                    evidence = make_release_evidence(unit, mode);
                    if (leg == 0) {
                        raw_selector(evidence.membership_after, field) =
                            missing_field();
                    } else {
                        raw_selector(evidence.device_after, field) =
                            missing_field();
                    }
                    audit.raw(evidence, quarantine_decision(1));

                    evidence = make_release_evidence(unit, mode);
                    if (leg == 0) {
                        raw_selector(evidence.membership_after, field) =
                            selector_unknown(field);
                    } else {
                        raw_selector(evidence.device_after, field) =
                            selector_unknown(field);
                    }
                    audit.raw(evidence, quarantine_decision(1));
                }
            }

            for (std::size_t field = 0; field < 14; ++field) {
                for (std::size_t locus = 0; locus < 3; ++locus) {
                    evidence = make_release_evidence(unit, mode);
                    apply_field_fault(
                        post_common_locus(evidence, locus).fields[field],
                        0);
                    audit.raw(evidence, quarantine_decision(1));

                    evidence = make_release_evidence(unit, mode);
                    apply_field_fault(
                        post_common_locus(evidence, locus).fields[field],
                        1);
                    audit.raw(evidence, quarantine_decision(1));

                    evidence = make_release_evidence(unit, mode);
                    apply_field_fault(
                        post_common_locus(evidence, locus).fields[field],
                        2);
                    audit.raw(evidence, quarantine_decision(1));
                }
            }

            evidence = make_release_evidence(unit, mode);
            evidence.liveness_after = false;
            audit.raw(evidence, quarantine_decision(1));
            for (RequestState request : {
                     RequestState::rejected,
                     RequestState::accepted,
                 }) {
                evidence = make_release_evidence(unit, mode);
                make_intact_after(evidence);
                evidence.termination.request = request;
                evidence.termination.attempted = false;
                evidence.termination.acknowledged = false;
                evidence.liveness_after = false;
                audit.raw(evidence, quarantine_decision(0));
            }
        }
    }

    for (std::uint64_t selected_value = 0; selected_value < 3; ++selected_value) {
        const Unit selected = static_cast<Unit>(selected_value);
        for (std::uint64_t other_value = 0; other_value < 3; ++other_value) {
            if (selected_value == other_value) {
                continue;
            }
            const Unit other = static_cast<Unit>(other_value);
            for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
                const Mode mode = static_cast<Mode>(mode_value);
                for (std::size_t shape = 0; shape < 3; ++shape) {
                    for (std::size_t field = 0; field < 2; ++field) {
                        RecoveryEvidence evidence = make_release_evidence(
                            selected,
                            mode);
                        if (shape != 1) {
                            select_other_field(
                                evidence.membership_after,
                                other,
                                field);
                        }
                        if (shape != 0) {
                            select_other_field(
                                evidence.device_after,
                                other,
                                field);
                        }
                        audit.raw(evidence, quarantine_decision(1));
                    }
                }
            }
        }
    }

    for (std::uint64_t unit_value = 0; unit_value < 3; ++unit_value) {
        const Unit unit = static_cast<Unit>(unit_value);
        for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
            const Mode mode = static_cast<Mode>(mode_value);
            const Field other_mode = selector_field(
                mode == Mode::direct_process
                    ? Mode::managed_service
                    : Mode::direct_process);
            for (std::size_t shape = 0; shape < 3; ++shape) {
                RecoveryEvidence evidence = make_release_evidence(unit, mode);
                if (shape != 1) {
                    evidence.membership_after.mode_selector = other_mode;
                }
                if (shape != 0) {
                    evidence.device_after.mode_selector = other_mode;
                }
                audit.raw(evidence, quarantine_decision(1));
            }
        }
    }
}

constexpr void apply_after_generation_fault(
    Generation& before,
    Generation& after,
    std::size_t fault) {
    if (fault == 0) {
        after.value = missing_field();
    } else if (fault == 1) {
        after.value = make_field(0);
    } else if (fault == 2) {
        after.value = before.value;
    } else if (fault == 3) {
        after.value = make_field(before.value.value - 1);
    } else if (fault == 4) {
        after.value = make_field(before.value.value + 2);
    } else if (fault == 5) {
        before.value = make_field(
            std::numeric_limits<std::uint64_t>::max());
        after.value = make_field(0);
    } else {
        after.checked = false;
    }
}

void generate_post_generation_identity_device_ack_fixtures(
    FixtureAudit& audit) {
    for (std::uint64_t unit_value = 0; unit_value < 3; ++unit_value) {
        const Unit unit = static_cast<Unit>(unit_value);
        for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
            const Mode mode = static_cast<Mode>(mode_value);
            RecoveryEvidence evidence{};
            for (std::size_t fault = 0; fault < 7; ++fault) {
                evidence = make_release_evidence(unit, mode);
                apply_after_generation_fault(
                    evidence.membership_before.envelope.generation,
                    evidence.membership_after.envelope.generation,
                    fault);
                audit.raw(evidence, quarantine_decision(1));

                evidence = make_release_evidence(unit, mode);
                apply_after_generation_fault(
                    evidence.device_before.envelope.generation,
                    evidence.device_after.envelope.generation,
                    fault);
                audit.raw(evidence, quarantine_decision(1));
            }

            for (std::uint64_t fault = 0; fault < 3; ++fault) {
                for (std::size_t ordinal = 0;
                     ordinal < full_identity_count(mode);
                     ++ordinal) {
                    evidence = make_release_evidence(unit, mode);
                    apply_field_fault(
                        mode_field(
                            evidence.device_after.owner,
                            full_identity_index(mode, ordinal)),
                        fault);
                    audit.raw(evidence, quarantine_decision(1));
                }
                evidence = make_release_evidence(unit, mode);
                apply_field_fault(
                    evidence.device_after.lookup.claim_identity,
                    fault);
                audit.raw(evidence, quarantine_decision(1));

                for (std::size_t ordinal = 0;
                     ordinal < full_identity_count(mode);
                     ++ordinal) {
                    evidence = make_release_evidence(unit, mode);
                    apply_field_fault(
                        mode_field(
                            evidence.membership_after.identity,
                            full_identity_index(mode, ordinal)),
                        fault);
                    audit.raw(evidence, quarantine_decision(1));
                }

                evidence = make_release_evidence(unit, mode);
                apply_field_fault(
                    evidence.termination.acknowledgement_action_token,
                    fault);
                audit.raw(evidence, quarantine_decision(1));

                evidence = make_release_evidence(unit, mode);
                apply_field_fault(
                    evidence.device_after.lookup.claim_generation,
                    fault);
                audit.raw(evidence, quarantine_decision(1));
            }

            if (mode == Mode::managed_service) {
                evidence = make_release_evidence(unit, mode);
                evidence.membership_after.service_state = ServiceState::running;
                ++evidence.membership_after.identity.value.managed
                      .service_start_generation.value;
                audit.raw(evidence, quarantine_decision(1));

                for (ServiceState state : {
                         ServiceState::missing,
                         ServiceState::unknown,
                         ServiceState::running,
                         ServiceState::wrong,
                     }) {
                    evidence = make_release_evidence(unit, mode);
                    evidence.membership_after.service_state = state;
                    audit.raw(evidence, quarantine_decision(1));
                }
            }

            for (std::size_t envelope_fault = 0;
                 envelope_fault < 4;
                 ++envelope_fault) {
                evidence = make_release_evidence(unit, mode);
                if (envelope_fault == 0) {
                    evidence.device_after.envelope.present = false;
                } else if (envelope_fault == 1) {
                    evidence.device_after.envelope.fresh = false;
                } else if (envelope_fault == 2) {
                    evidence.device_after.envelope.healthy = false;
                } else {
                    evidence.device_after.envelope.complete = false;
                }
                audit.raw(evidence, quarantine_decision(1));
            }

            evidence = make_release_evidence(unit, mode);
            evidence.device_after.active_claims[0] = {
                true,
                evidence.device_after.lookup.claim_identity,
                evidence.device_after.lookup.claim_generation,
            };
            audit.raw(evidence, quarantine_decision(1));

            evidence = make_release_evidence(unit, mode);
            evidence.device_after.active_claims[0] = {
                true,
                evidence.device_after.lookup.claim_identity,
                evidence.device_after.lookup.claim_generation,
            };
            ++evidence.device_after.active_claims[0]
                  .claim_generation.value;
            audit.raw(evidence, quarantine_decision(1));

            for (std::size_t field = 0; field < 2; ++field) {
                for (std::uint64_t fault = 0; fault < 2; ++fault) {
                    evidence = make_release_evidence(unit, mode);
                    Field& selected = field == 0
                        ? evidence.device_after.active_claims[1]
                              .claim_identity
                        : evidence.device_after.active_claims[1]
                              .claim_generation;
                    apply_field_fault(selected, fault);
                    audit.raw(evidence, quarantine_decision(1));
                }
            }

            for (std::size_t field = 0; field < 4; ++field) {
                evidence = make_release_evidence(unit, mode);
                target_selector(
                    evidence.termination.acknowledgement_target,
                    field) = missing_field();
                audit.raw(evidence, quarantine_decision(1));

                evidence = make_release_evidence(unit, mode);
                target_selector(
                    evidence.termination.acknowledgement_target,
                    field) = make_field(0);
                audit.raw(evidence, quarantine_decision(1));

                if (field != 2) {
                    evidence = make_release_evidence(unit, mode);
                    target_selector(
                        evidence.termination.acknowledgement_target,
                        field) = unknown_target_selector(field);
                    audit.raw(evidence, quarantine_decision(1));
                }
            }

            for (std::size_t ordinal = 0;
                 ordinal < full_identity_count(mode);
                 ++ordinal) {
                const std::size_t index = full_identity_index(mode, ordinal);
                for (std::uint64_t fault = 0; fault < 3; ++fault) {
                    evidence = make_release_evidence(unit, mode);
                    apply_field_fault(
                        mode_field(
                            evidence.termination.acknowledgement_target
                                .identity,
                            index),
                        fault);
                    audit.raw(evidence, quarantine_decision(1));
                }
            }

            for (std::uint64_t other_value = 0; other_value < 3; ++other_value) {
                if (other_value == unit_value) {
                    continue;
                }
                for (std::size_t field = 0; field < 2; ++field) {
                    evidence = make_release_evidence(unit, mode);
                    if (field == 0) {
                        evidence.termination.acknowledgement_target
                            .unit_selector = selector_field(
                            static_cast<Unit>(other_value));
                    } else {
                        evidence.termination.acknowledgement_target
                            .model_selector = selector_field(
                            static_cast<Model>(other_value));
                    }
                    audit.raw(evidence, quarantine_decision(1));
                }
            }
            for (std::uint64_t leaf = 1; leaf <= 5; ++leaf) {
                evidence = make_release_evidence(unit, mode);
                evidence.termination.acknowledgement_target
                    .operation_selector = selector_field(
                    static_cast<OperationLeaf>(leaf));
                audit.raw(evidence, quarantine_decision(1));
            }
            evidence = make_release_evidence(unit, mode);
            evidence.termination.acknowledgement_target.mode_selector =
                selector_field(mode == Mode::direct_process
                        ? Mode::managed_service
                        : Mode::direct_process);
            audit.raw(evidence, quarantine_decision(1));
        }
    }
}

void generate_early_credit_raw_fixtures(FixtureAudit& audit) {
    for (std::uint64_t unit_value = 0; unit_value < 3; ++unit_value) {
        const Unit unit = static_cast<Unit>(unit_value);
        for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
            const Mode mode = static_cast<Mode>(mode_value);
            RecoveryEvidence evidence = make_release_evidence(unit, mode);
            make_intact_after(evidence);
            evidence.credit_stage = CreditStage::before_both_proofs;
            evidence.termination.request = RequestState::absent;
            evidence.termination.attempted = false;
            evidence.termination.acknowledged = false;
            audit.raw(evidence, unknown_preserved_decision());

            for (std::uint64_t acknowledged = 0;
                 acknowledged < 2;
                 ++acknowledged) {
                evidence = make_release_evidence(unit, mode);
                evidence.credit_stage = CreditStage::before_both_proofs;
                evidence.termination.request = RequestState::accepted;
                evidence.termination.attempted = true;
                evidence.termination.acknowledged = acknowledged != 0;
                audit.raw(evidence, quarantine_decision(1));
            }
        }
    }
}

constexpr Field& membership_proof_selector(
    MembershipReleaseProof& proof,
    std::size_t field) {
    if (field == 0) {
        return proof.unit_selector;
    }
    if (field == 1) {
        return proof.model_selector;
    }
    if (field == 2) {
        return proof.mode_selector;
    }
    return proof.operation_selector;
}

constexpr MembershipReleaseProof& membership_proof(
    ReleaseComposition& composition) {
    return composition.membership.value.proof;
}

constexpr DeviceReleaseProof& device_proof(
    ReleaseComposition& composition) {
    return composition.device.value.proof;
}

constexpr Field& device_proof_selector(
    DeviceReleaseProof& proof,
    std::size_t field) {
    if (field == 0) {
        return proof.unit_selector;
    }
    if (field == 1) {
        return proof.model_selector;
    }
    if (field == 2) {
        return proof.mode_selector;
    }
    return proof.operation_selector;
}

void generate_composition_fixtures(FixtureAudit& audit) {
    for (std::uint64_t unit_value = 0; unit_value < 3; ++unit_value) {
        const Unit unit = static_cast<Unit>(unit_value);
        for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
            const Mode mode = static_cast<Mode>(mode_value);
            const RecoveryEvidence raw = make_release_evidence(unit, mode);
            ReleaseComposition composition = derive_release_composition(raw);
            composition.membership = MembershipProofSlot();
            audit.composition(composition, quarantine_decision(1));

            composition = derive_release_composition(raw);
            composition.device = DeviceProofSlot();
            audit.composition(composition, quarantine_decision(1));

            for (std::uint64_t other_value = 0;
                 other_value < 3;
                 ++other_value) {
                if (other_value == unit_value) {
                    continue;
                }
                for (std::size_t proof = 0; proof < 2; ++proof) {
                    for (std::size_t field = 0; field < 2; ++field) {
                        composition = derive_release_composition(raw);
                        if (proof == 0) {
                            membership_proof_selector(
                                membership_proof(composition),
                                field) = field == 0
                                ? selector_field(static_cast<Unit>(other_value))
                                : selector_field(static_cast<Model>(other_value));
                        } else {
                            device_proof_selector(
                                device_proof(composition),
                                field) = field == 0
                                ? selector_field(static_cast<Unit>(other_value))
                                : selector_field(static_cast<Model>(other_value));
                        }
                        audit.composition(
                            composition,
                            quarantine_decision(1));
                    }
                }
            }

            for (std::size_t proof = 0; proof < 2; ++proof) {
                composition = derive_release_composition(raw);
                const Field other_mode = selector_field(
                    mode == Mode::direct_process
                        ? Mode::managed_service
                        : Mode::direct_process);
                if (proof == 0) {
                    membership_proof(composition).mode_selector = other_mode;
                } else {
                    device_proof(composition).mode_selector = other_mode;
                }
                audit.composition(composition, quarantine_decision(1));

                for (std::uint64_t leaf = 1; leaf <= 5; ++leaf) {
                    composition = derive_release_composition(raw);
                    if (proof == 0) {
                        membership_proof(composition).operation_selector =
                            selector_field(static_cast<OperationLeaf>(leaf));
                    } else {
                        device_proof(composition).operation_selector =
                            selector_field(static_cast<OperationLeaf>(leaf));
                    }
                    audit.composition(composition, quarantine_decision(1));
                }

                for (std::size_t ordinal = 0;
                     ordinal < full_identity_count(mode);
                     ++ordinal) {
                    composition = derive_release_composition(raw);
                    if (proof == 0) {
                        ++mode_field(
                              membership_proof(composition).owner,
                              full_identity_index(mode, ordinal))
                              .value;
                    } else {
                        ++mode_field(
                              device_proof(composition).owner,
                              full_identity_index(mode, ordinal))
                              .value;
                    }
                    audit.composition(composition, quarantine_decision(1));
                }

                for (std::size_t field = 0; field < 14; ++field) {
                    composition = derive_release_composition(raw);
                    if (proof == 0) {
                        ++membership_proof(composition)
                              .common.fields[field].value;
                    } else {
                        ++device_proof(composition).common.fields[field].value;
                    }
                    audit.composition(composition, quarantine_decision(1));
                }
            }

            for (std::size_t field = 0; field < 2; ++field) {
                for (std::uint64_t mutation = 0;
                     mutation < 3;
                     ++mutation) {
                    composition = derive_release_composition(raw);
                    Field& selected = field == 0
                        ? device_proof(composition).claim.claim_identity
                        : device_proof(composition).claim.claim_generation;
                    apply_field_fault(selected, mutation);
                    audit.composition(composition, quarantine_decision(1));
                }
            }
        }
    }
}

constexpr bool membership_shape_fault_matrix_passes() {
    for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
        const Mode mode = static_cast<Mode>(mode_value);
        for (std::uint64_t fault = 0; fault < 18; ++fault) {
            RecoveryEvidence evidence = make_release_evidence(Unit::llm, mode);
            apply_membership_shape_fault(
                evidence.membership_before,
                fault);
            clear_action(evidence);
            if (!decision_is(
                    evidence,
                    Result::unknown,
                    ClaimDisposition::preserved,
                    0,
                    0)) {
                return false;
            }
            if (fault == 0 || fault == 2 || fault == 3 || fault == 4) {
                evidence = make_release_evidence(Unit::llm, mode);
                apply_membership_shape_fault(
                    evidence.membership_after,
                    fault);
                if (!decision_is(
                        evidence,
                        Result::quarantine,
                        ClaimDisposition::maximum,
                        1,
                        0)) {
                    return false;
                }
            }
        }
    }
    return true;
}

constexpr Field& member_identity_field(
    MemberRecord& member,
    std::size_t field) {
    if (field == 0) {
        return member.process_identity;
    }
    if (field == 1) {
        return member.process_birth_token;
    }
    return member.executable_digest;
}

constexpr bool member_identity_fault_matrix_passes() {
    for (std::uint64_t unit_value = 0; unit_value < 3; ++unit_value) {
        const Unit unit = static_cast<Unit>(unit_value);
        for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
            const Mode mode = static_cast<Mode>(mode_value);
            for (std::size_t field = 0; field < 3; ++field) {
                for (std::uint64_t fault = 0; fault < 3; ++fault) {
                    RecoveryEvidence evidence = make_release_evidence(
                        unit,
                        mode);
                    apply_field_fault(
                        member_identity_field(
                            evidence.membership_before.members[0],
                            field),
                        fault);
                    clear_action(evidence);
                    if (decide(evidence).result != Result::unknown) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

constexpr void apply_envelope_fault(
    ObservationEnvelope& envelope,
    std::uint64_t fault) {
    if (fault == 0) {
        envelope.present = false;
    } else if (fault == 1) {
        envelope.fresh = false;
    } else if (fault == 2) {
        envelope.healthy = false;
    } else {
        envelope.complete = false;
    }
}

constexpr bool envelope_fault_matrix_passes() {
    for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
        const Mode mode = static_cast<Mode>(mode_value);
        for (std::uint64_t fault = 0; fault < 4; ++fault) {
            RecoveryEvidence evidence = make_release_evidence(Unit::llm, mode);
            apply_envelope_fault(
                evidence.membership_before.envelope,
                fault);
            clear_action(evidence);
            if (!decision_is(
                    evidence,
                    Result::unknown,
                    ClaimDisposition::preserved,
                    0,
                    0)) {
                return false;
            }
            evidence = make_release_evidence(Unit::llm, mode);
            apply_envelope_fault(
                evidence.membership_after.envelope,
                fault);
            if (!decision_is(
                    evidence,
                    Result::quarantine,
                    ClaimDisposition::maximum,
                    1,
                    0)) {
                return false;
            }
            evidence = make_release_evidence(Unit::llm, mode);
            apply_envelope_fault(evidence.device_before.envelope, fault);
            clear_action(evidence);
            if (!decision_is(
                    evidence,
                    Result::unknown,
                    ClaimDisposition::preserved,
                    0,
                    0)) {
                return false;
            }
            evidence = make_release_evidence(Unit::llm, mode);
            apply_envelope_fault(evidence.device_after.envelope, fault);
            if (!decision_is(
                    evidence,
                    Result::quarantine,
                    ClaimDisposition::maximum,
                    1,
                    0)) {
                return false;
            }
        }
    }
    return true;
}

constexpr void apply_generation_after_fault(
    Generation& before,
    Generation& after,
    std::uint64_t fault) {
    if (fault == 0) {
        after.value.value = 0;
    } else if (fault == 1) {
        after.value.value = before.value.value;
    } else if (fault == 2) {
        after.value.value = before.value.value - 1;
    } else if (fault == 3) {
        after.value.value = before.value.value + 2;
    } else if (fault == 4) {
        before.value.value = std::numeric_limits<std::uint64_t>::max();
        after.value.value = 0;
    } else if (fault == 5) {
        after.checked = false;
    } else {
        after.value = missing_field();
    }
}

constexpr bool observation_generation_fault_matrix_passes() {
    for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
        const Mode mode = static_cast<Mode>(mode_value);
        RecoveryEvidence evidence = make_release_evidence(Unit::embedding, mode);
        evidence.membership_before.envelope.generation.value.value = 0;
        clear_action(evidence);
        if (decide(evidence).result != Result::unknown) {
            return false;
        }
        evidence = make_release_evidence(Unit::embedding, mode);
        evidence.membership_before.envelope.generation.value = missing_field();
        clear_action(evidence);
        if (decide(evidence).result != Result::unknown) {
            return false;
        }
        evidence = make_release_evidence(Unit::embedding, mode);
        evidence.membership_before.envelope.generation.checked = false;
        clear_action(evidence);
        if (decide(evidence).result != Result::unknown) {
            return false;
        }
        evidence = make_release_evidence(Unit::embedding, mode);
        evidence.device_before.envelope.generation.value.value = 0;
        clear_action(evidence);
        if (decide(evidence).result != Result::unknown) {
            return false;
        }
        evidence = make_release_evidence(Unit::embedding, mode);
        evidence.device_before.envelope.generation.value = missing_field();
        clear_action(evidence);
        if (decide(evidence).result != Result::unknown) {
            return false;
        }
        evidence = make_release_evidence(Unit::embedding, mode);
        evidence.device_before.envelope.generation.checked = false;
        clear_action(evidence);
        if (decide(evidence).result != Result::unknown) {
            return false;
        }
        for (std::uint64_t fault = 0; fault < 7; ++fault) {
            evidence = make_release_evidence(Unit::embedding, mode);
            apply_generation_after_fault(
                evidence.membership_before.envelope.generation,
                evidence.membership_after.envelope.generation,
                fault);
            if (decide(evidence).result != Result::quarantine) {
                return false;
            }
            evidence = make_release_evidence(Unit::embedding, mode);
            apply_generation_after_fault(
                evidence.device_before.envelope.generation,
                evidence.device_after.envelope.generation,
                fault);
            if (decide(evidence).result != Result::quarantine) {
                return false;
            }
        }
    }
    return true;
}

constexpr bool generation_fault_matrix_passes() {
    RecoveryEvidence evidence = make_release_evidence(Unit::llm, Mode::direct_process);
    evidence.membership_after.envelope.generation.value = missing_field();
    if (decide(evidence).result != Result::quarantine) {
        return false;
    }
    evidence = make_release_evidence(Unit::llm, Mode::direct_process);
    evidence.membership_after.envelope.generation.value.value =
        evidence.membership_before.envelope.generation.value.value;
    if (decide(evidence).result != Result::quarantine) {
        return false;
    }
    evidence = make_release_evidence(Unit::llm, Mode::direct_process);
    evidence.membership_before.envelope.generation.value.value =
        std::numeric_limits<std::uint64_t>::max();
    evidence.membership_after.envelope.generation.value.value = 0;
    if (decide(evidence).result != Result::quarantine) {
        return false;
    }
    evidence = make_release_evidence(Unit::llm, Mode::managed_service);
    evidence.device_after.envelope.generation.checked = false;
    if (decide(evidence).result != Result::quarantine) {
        return false;
    }
    evidence = make_release_evidence(Unit::llm, Mode::managed_service);
    evidence.device_after.lookup.claim_generation.value++;
    if (decide(evidence).result != Result::quarantine) {
        return false;
    }
    evidence = make_release_evidence(Unit::llm, Mode::managed_service);
    evidence.device_before.lookup.claim_generation.present = false;
    clear_action(evidence);
    if (decide(evidence).result != Result::unknown) {
        return false;
    }
    evidence = make_release_evidence(Unit::llm, Mode::managed_service);
    evidence.device_before.lookup.claim_generation.value = 0;
    clear_action(evidence);
    if (decide(evidence).result != Result::unknown) {
        return false;
    }
    evidence = make_release_evidence(Unit::llm, Mode::managed_service);
    evidence.device_before.lookup.claim_generation.value++;
    clear_action(evidence);
    return decide(evidence).result == Result::unknown;
}

constexpr bool precondition_fault_matrix_passes() {
    RecoveryEvidence evidence = make_release_evidence(
        Unit::transcription,
        Mode::direct_process);
    evidence.prepared.mode_selector = selector_field(Mode::unknown);
    clear_action(evidence);
    if (!decision_is(
            evidence,
            Result::unknown,
            ClaimDisposition::preserved,
            0,
            0)) {
        return false;
    }
    evidence = make_release_evidence(
        Unit::transcription,
        Mode::direct_process);
    evidence.membership_before.roots[0].ownership = OwnershipState::missing;
    clear_action(evidence);
    if (decide(evidence).result != Result::unknown) {
        return false;
    }
    evidence = make_release_evidence(
        Unit::transcription,
        Mode::direct_process);
    evidence.membership_before.roots[0].ownership = OwnershipState::too_broad;
    clear_action(evidence);
    if (decide(evidence).result != Result::unknown) {
        return false;
    }
    evidence = make_release_evidence(
        Unit::transcription,
        Mode::direct_process);
    evidence.membership_before.roots[0].ownership =
        OwnershipState::shared_ambiguous;
    clear_action(evidence);
    if (decide(evidence).result != Result::unknown) {
        return false;
    }
    evidence = make_release_evidence(
        Unit::transcription,
        Mode::direct_process);
    evidence.device_before.lookup.claim_identity.present = false;
    clear_action(evidence);
    if (decide(evidence).result != Result::unknown) {
        return false;
    }
    evidence = make_release_evidence(
        Unit::transcription,
        Mode::direct_process);
    evidence.device_before.lookup.claim_identity.value = 0;
    clear_action(evidence);
    if (decide(evidence).result != Result::unknown) {
        return false;
    }
    evidence = make_release_evidence(
        Unit::transcription,
        Mode::direct_process);
    evidence.device_before.lookup.claim_identity.value++;
    clear_action(evidence);
    if (decide(evidence).result != Result::unknown) {
        return false;
    }
    evidence = make_release_evidence(
        Unit::transcription,
        Mode::direct_process);
    evidence.device_after.lookup.claim_identity.present = false;
    return decide(evidence).result == Result::quarantine;
}

constexpr bool device_and_ack_fault_matrix_passes() {
    for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
        const Mode mode = static_cast<Mode>(mode_value);
        for (std::uint64_t fault = 0; fault < 3; ++fault) {
            RecoveryEvidence evidence = make_release_evidence(Unit::llm, mode);
            apply_field_fault(evidence.prepared.claim.claim_identity, fault);
            clear_action(evidence);
            if (decide(evidence).result != Result::unknown) {
                return false;
            }
            evidence = make_release_evidence(Unit::llm, mode);
            apply_field_fault(evidence.prepared.claim.claim_generation, fault);
            clear_action(evidence);
            if (decide(evidence).result != Result::unknown) {
                return false;
            }
            evidence = make_release_evidence(Unit::llm, mode);
            apply_field_fault(
                evidence.device_before.lookup.claim_identity,
                fault);
            clear_action(evidence);
            if (decide(evidence).result != Result::unknown) {
                return false;
            }
            evidence = make_release_evidence(Unit::llm, mode);
            apply_field_fault(
                evidence.device_after.lookup.claim_identity,
                fault);
            if (decide(evidence).result != Result::quarantine) {
                return false;
            }
            evidence = make_release_evidence(Unit::llm, mode);
            apply_field_fault(
                evidence.device_before.lookup.claim_generation,
                fault);
            clear_action(evidence);
            if (decide(evidence).result != Result::unknown) {
                return false;
            }
            evidence = make_release_evidence(Unit::llm, mode);
            apply_field_fault(
                evidence.device_after.lookup.claim_generation,
                fault);
            if (decide(evidence).result != Result::quarantine) {
                return false;
            }
            evidence = make_release_evidence(Unit::llm, mode);
            apply_field_fault(
                evidence.termination.acknowledgement_action_token,
                fault);
            if (decide(evidence).result != Result::quarantine) {
                return false;
            }
        }
    }
    return true;
}

constexpr bool action_state_matrix_passes() {
    RecoveryEvidence evidence = make_release_evidence(Unit::embedding, Mode::direct_process);
    evidence.termination.acknowledgement_action_token.value++;
    if (decide(evidence).result != Result::quarantine) {
        return false;
    }
    evidence = make_release_evidence(Unit::embedding, Mode::managed_service);
    evidence.termination.acknowledgement_target.identity.value.managed
        .service_instance_birth_token.value++;
    if (decide(evidence).result != Result::quarantine) {
        return false;
    }
    evidence = make_release_evidence(Unit::embedding, Mode::direct_process);
    evidence.termination.request = RequestState::rejected;
    evidence.termination.attempted = false;
    evidence.termination.acknowledged = false;
    make_intact_after(evidence);
    if (decide(evidence).result != Result::verified_intact) {
        return false;
    }
    evidence.membership_after.declared_member_count = 0;
    if (decide(evidence).result != Result::quarantine) {
        return false;
    }
    evidence = make_release_evidence(Unit::embedding, Mode::managed_service);
    evidence.termination.attempted = false;
    evidence.termination.acknowledged = false;
    make_intact_after(evidence);
    if (decide(evidence).result != Result::verified_intact) {
        return false;
    }
    evidence.device_after.active_claims[0].present = false;
    evidence.device_after.active_claims[0].claim_identity = missing_field();
    evidence.device_after.active_claims[0].claim_generation = missing_field();
    return decide(evidence).result == Result::quarantine;
}

constexpr bool lifecycle_fault_matrix_passes() {
    for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
        const Mode mode = static_cast<Mode>(mode_value);
        RecoveryEvidence evidence = make_release_evidence(Unit::llm, mode);
        evidence.prepared.state = PreparedAuthorityState::missing;
        clear_action(evidence);
        if (!decision_is(
                evidence,
                Result::unknown,
                ClaimDisposition::preserved,
                0,
                0)) {
            return false;
        }
        evidence = make_release_evidence(Unit::llm, mode);
        evidence.prepared.operation_selector = selector_field(
            OperationLeaf::dead_backend_pruning);
        clear_action(evidence);
        if (decide(evidence).result != Result::unknown) {
            return false;
        }
        evidence = make_release_evidence(Unit::llm, mode);
        evidence.membership_before.mode_selector = selector_field(
            mode == Mode::direct_process
                ? Mode::managed_service
                : Mode::direct_process);
        clear_action(evidence);
        if (decide(evidence).result != Result::unknown) {
            return false;
        }
        evidence = make_release_evidence(Unit::llm, mode);
        evidence.termination.target.mode_selector = selector_field(
            mode == Mode::direct_process
                ? Mode::managed_service
                : Mode::direct_process);
        clear_action(evidence);
        if (decide(evidence).result != Result::unknown) {
            return false;
        }
        evidence = make_release_evidence(Unit::llm, mode);
        evidence.termination.request = RequestState::absent;
        evidence.termination.attempted = false;
        evidence.termination.acknowledged = false;
        make_intact_after(evidence);
        if (decide(evidence).result != Result::unknown) {
            return false;
        }
        evidence.termination.attempted = true;
        if (decide(evidence).result != Result::quarantine) {
            return false;
        }
        evidence = make_release_evidence(Unit::llm, mode);
        evidence.termination.acknowledged = false;
        if (decide(evidence).result != Result::quarantine) {
            return false;
        }
        evidence = make_release_evidence(Unit::llm, mode);
        evidence.termination.attempted = false;
        if (decide(evidence).result != Result::quarantine) {
            return false;
        }
        evidence = make_release_evidence(Unit::llm, mode);
        evidence.membership_after.declared_member_count = 1;
        activate_canonical_member(evidence.membership_after, 0);
        if (decide(evidence).result != Result::quarantine) {
            return false;
        }
        evidence = make_release_evidence(Unit::llm, mode);
        evidence.device_before.active_claims[0].present = false;
        evidence.device_before.active_claims[0].claim_identity = missing_field();
        evidence.device_before.active_claims[0].claim_generation =
            missing_field();
        clear_action(evidence);
        if (decide(evidence).result != Result::unknown) {
            return false;
        }
        evidence = make_release_evidence(Unit::llm, mode);
        evidence.device_after.active_claims[0] = {
            true,
            evidence.prepared.claim.claim_identity,
            evidence.prepared.claim.claim_generation,
        };
        if (decide(evidence).result != Result::quarantine) {
            return false;
        }
    }
    RecoveryEvidence evidence = make_release_evidence(
        Unit::llm,
        Mode::direct_process);
    evidence.membership_after.declared_member_count = 1;
    evidence.membership_after.members[0] = {
        true,
        mode_field(evidence.prepared.identity, 6),
        make_field(
            mode_field(evidence.prepared.identity, 7).value + 1),
        mode_field(evidence.prepared.identity, 8),
        MemberClassification::serving_process,
    };
    if (decide(evidence).result != Result::quarantine) {
        return false;
    }
    constexpr std::array<ServiceState, 5> invalid_direct_states = {
        ServiceState::missing,
        ServiceState::running,
        ServiceState::stopped,
        ServiceState::unknown,
        ServiceState::wrong,
    };
    for (ServiceState state : invalid_direct_states) {
        evidence = make_release_evidence(Unit::llm, Mode::direct_process);
        evidence.membership_before.service_state = state;
        clear_action(evidence);
        if (decide(evidence).result != Result::unknown) {
            return false;
        }
        evidence = make_release_evidence(Unit::llm, Mode::direct_process);
        evidence.membership_after.service_state = state;
        if (decide(evidence).result != Result::quarantine) {
            return false;
        }
    }
    constexpr std::array<ServiceState, 3> invalid_before = {
        ServiceState::missing,
        ServiceState::stopped,
        ServiceState::unknown,
    };
    for (ServiceState state : invalid_before) {
        evidence = make_release_evidence(Unit::llm, Mode::managed_service);
        evidence.membership_before.service_state = state;
        clear_action(evidence);
        if (decide(evidence).result != Result::unknown) {
            return false;
        }
    }
    constexpr std::array<ServiceState, 4> invalid_after = {
        ServiceState::missing,
        ServiceState::running,
        ServiceState::unknown,
        ServiceState::wrong,
    };
    for (ServiceState state : invalid_after) {
        evidence = make_release_evidence(Unit::llm, Mode::managed_service);
        evidence.membership_after.service_state = state;
        if (decide(evidence).result != Result::quarantine) {
            return false;
        }
    }
    evidence = make_release_evidence(Unit::llm, Mode::managed_service);
    evidence.membership_after.service_state = ServiceState::running;
    ++evidence.membership_after.identity.value.managed
          .service_start_generation.value;
    return decide(evidence).result == Result::quarantine;
}

constexpr bool selector_fault_matrix_passes() {
    constexpr std::array<OperationLeaf, 5> invalid_operations = {
        OperationLeaf::dead_backend_pruning,
        OperationLeaf::same_epoch_recovery_cleanup,
        OperationLeaf::prior_epoch_owner_cleanup,
        OperationLeaf::artifact_scope_recovery_cleanup,
        OperationLeaf::unknown,
    };
    for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
        const Mode mode = static_cast<Mode>(mode_value);
        for (std::uint64_t fault = 0; fault < 3; ++fault) {
            RecoveryEvidence evidence = make_release_evidence(Unit::llm, mode);
            if (fault < 2) {
                apply_field_fault(evidence.prepared.unit_selector, fault);
            } else {
                evidence.prepared.unit_selector = selector_field(Unit::unknown);
            }
            clear_action(evidence);
            if (decide(evidence).result != Result::unknown) {
                return false;
            }
            evidence = make_release_evidence(Unit::llm, mode);
            if (fault < 2) {
                apply_field_fault(evidence.prepared.model_selector, fault);
            } else {
                evidence.prepared.model_selector = selector_field(Model::unknown);
            }
            clear_action(evidence);
            if (decide(evidence).result != Result::unknown) {
                return false;
            }
            evidence = make_release_evidence(Unit::llm, mode);
            if (fault < 2) {
                apply_field_fault(evidence.prepared.mode_selector, fault);
            } else {
                evidence.prepared.mode_selector = selector_field(Mode::unknown);
            }
            clear_action(evidence);
            if (decide(evidence).result != Result::unknown) {
                return false;
            }
        }
        for (OperationLeaf operation : invalid_operations) {
            RecoveryEvidence evidence = make_release_evidence(Unit::llm, mode);
            evidence.prepared.operation_selector = selector_field(operation);
            clear_action(evidence);
            if (decide(evidence).result != Result::unknown) {
                return false;
            }
        }
        for (std::size_t selector = 0; selector < 4; ++selector) {
            for (std::uint64_t fault = 0; fault < 3; ++fault) {
                RecoveryEvidence evidence = make_release_evidence(Unit::llm, mode);
                apply_field_fault(
                    target_selector(evidence.termination.target, selector),
                    fault);
                clear_action(evidence);
                if (decide(evidence).result != Result::unknown) {
                    return false;
                }
                evidence = make_release_evidence(Unit::llm, mode);
                apply_field_fault(
                    target_selector(
                        evidence.termination.acknowledgement_target,
                        selector),
                    fault);
                if (decide(evidence).result != Result::quarantine) {
                    return false;
                }
            }
        }
        for (std::uint64_t other_value = 0; other_value < 3; ++other_value) {
            if (other_value == static_cast<std::uint64_t>(Unit::llm)) {
                continue;
            }
            RecoveryEvidence evidence = make_release_evidence(Unit::llm, mode);
            evidence.termination.target.unit_selector = selector_field(
                static_cast<Unit>(other_value));
            evidence.termination.target.model_selector = selector_field(
                static_cast<Model>(other_value));
            clear_action(evidence);
            if (decide(evidence).result != Result::unknown) {
                return false;
            }
            evidence = make_release_evidence(Unit::llm, mode);
            evidence.termination.acknowledgement_target.unit_selector =
                selector_field(static_cast<Unit>(other_value));
            evidence.termination.acknowledgement_target.model_selector =
                selector_field(static_cast<Model>(other_value));
            if (decide(evidence).result != Result::quarantine) {
                return false;
            }
        }
    }
    return true;
}
constexpr bool ownership_fault_matrix_passes() {
    constexpr std::array<OwnershipState, 3> invalid_ownership = {
        OwnershipState::missing,
        OwnershipState::too_broad,
        OwnershipState::shared_ambiguous,
    };
    for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
        const Mode mode = static_cast<Mode>(mode_value);
        for (OwnershipState ownership : invalid_ownership) {
            RecoveryEvidence evidence = make_release_evidence(Unit::llm, mode);
            evidence.membership_before.roots[0].ownership = ownership;
            clear_action(evidence);
            if (!decision_is(
                    evidence,
                    Result::unknown,
                    ClaimDisposition::preserved,
                    0,
                    0)) {
                return false;
            }
            evidence = make_release_evidence(Unit::llm, mode);
            evidence.membership_after.roots[0].ownership = ownership;
            if (!decision_is(
                    evidence,
                    Result::quarantine,
                    ClaimDisposition::maximum,
                    1,
                    0)) {
                return false;
            }
        }
    }
    return true;
}

constexpr bool release_crossleg_proof_matrix_passes() {
    constexpr std::array<OperationLeaf, 5> invalid_operations = {
        OperationLeaf::dead_backend_pruning,
        OperationLeaf::same_epoch_recovery_cleanup,
        OperationLeaf::prior_epoch_owner_cleanup,
        OperationLeaf::artifact_scope_recovery_cleanup,
        OperationLeaf::unknown,
    };
    for (std::uint64_t unit_value = 0; unit_value < 3; ++unit_value) {
        const Unit unit = static_cast<Unit>(unit_value);
        for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
            const Mode mode = static_cast<Mode>(mode_value);
            const RecoveryEvidence raw = make_release_evidence(unit, mode);
            ReleaseComposition composition = derive_release_composition(raw);
            if (decide_composition(composition).result
                != Result::verified_release) {
                return false;
            }
            composition.membership = MembershipProofSlot();
            if (decide_composition(composition).result != Result::quarantine) {
                return false;
            }
            composition = derive_release_composition(raw);
            composition.device = DeviceProofSlot();
            if (decide_composition(composition).result != Result::quarantine) {
                return false;
            }
            for (std::uint64_t other_value = 0;
                 other_value < 3;
                 ++other_value) {
                if (other_value == unit_value) {
                    continue;
                }
                composition = derive_release_composition(raw);
                device_proof(composition).unit_selector = selector_field(
                    static_cast<Unit>(other_value));
                device_proof(composition).model_selector = selector_field(
                    static_cast<Model>(other_value));
                if (decide_composition(composition).result
                    != Result::quarantine) {
                    return false;
                }
            }
            composition = derive_release_composition(raw);
            device_proof(composition).mode_selector = selector_field(
                mode == Mode::direct_process
                    ? Mode::managed_service
                    : Mode::direct_process);
            if (decide_composition(composition).result != Result::quarantine) {
                return false;
            }
            for (OperationLeaf operation : invalid_operations) {
                composition = derive_release_composition(raw);
                device_proof(composition).operation_selector =
                    selector_field(operation);
                if (decide_composition(composition).result
                    != Result::quarantine) {
                    return false;
                }
            }
            for (std::size_t index = 0; index < 14; ++index) {
                composition = derive_release_composition(raw);
                ++device_proof(composition).common.fields[index].value;
                if (decide_composition(composition).result
                    != Result::quarantine) {
                    return false;
                }
            }
            for (std::size_t ordinal = 0;
                 ordinal < full_identity_count(mode);
                 ++ordinal) {
                composition = derive_release_composition(raw);
                ++mode_field(
                    device_proof(composition).owner,
                    full_identity_index(mode, ordinal)).value;
                if (decide_composition(composition).result
                    != Result::quarantine) {
                    return false;
                }
            }
            composition = derive_release_composition(raw);
            ++device_proof(composition).claim.claim_identity.value;
            if (decide_composition(composition).result != Result::quarantine) {
                return false;
            }
            composition = derive_release_composition(raw);
            ++device_proof(composition).claim.claim_generation.value;
            const Decision rejected = decide_composition(composition);
            if (rejected.result != Result::quarantine
                || rejected.claims != ClaimDisposition::maximum
                || rejected.effect_calls != 1
                || rejected.release_credit != 0) {
                return false;
            }
        }
    }
    return true;
}
constexpr bool action_truth_table_passes() {
    for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
        const Mode mode = static_cast<Mode>(mode_value);
        for (std::uint64_t request_value = 0;
             request_value < 3;
             ++request_value) {
            const RequestState request = static_cast<RequestState>(request_value);
            for (std::uint64_t attempted = 0; attempted < 2; ++attempted) {
                for (std::uint64_t acknowledged = 0;
                     acknowledged < 2;
                     ++acknowledged) {
                    RecoveryEvidence evidence = make_release_evidence(
                        Unit::llm,
                        mode);
                    evidence.termination.request = request;
                    evidence.termination.attempted = attempted != 0;
                    evidence.termination.acknowledged = acknowledged != 0;
                    if (attempted == 0) {
                        make_intact_after(evidence);
                    }
                    Result result = Result::quarantine;
                    ClaimDisposition claims = ClaimDisposition::maximum;
                    std::uint64_t credit = 0;
                    if (attempted == 0 && acknowledged == 0) {
                        result = request == RequestState::absent
                            ? Result::unknown
                            : Result::verified_intact;
                        claims = ClaimDisposition::preserved;
                    } else if (attempted != 0 && acknowledged != 0
                               && request == RequestState::accepted) {
                        result = Result::verified_release;
                        claims = ClaimDisposition::released;
                        credit = 1;
                    }
                    if (!decision_is(
                            evidence,
                            result,
                            claims,
                            attempted,
                            credit)) {
                        return false;
                    }
                }
            }
            for (std::uint64_t changed_leg = 0;
                 changed_leg < 3;
                 ++changed_leg) {
                RecoveryEvidence evidence = make_release_evidence(
                    Unit::llm,
                    mode);
                const RecoveryEvidence released = evidence;
                evidence.termination.request = request;
                clear_action(evidence);
                make_intact_after(evidence);
                if (changed_leg != 1) {
                    evidence.membership_after = released.membership_after;
                }
                if (changed_leg != 0) {
                    evidence.device_after = released.device_after;
                }
                if (!decision_is(
                        evidence,
                        Result::quarantine,
                        ClaimDisposition::maximum,
                        0,
                        0)) {
                    return false;
                }
            }
        }
    }
    return true;
}

constexpr bool action_precedence_matrix_passes() {
    for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
        const Mode mode = static_cast<Mode>(mode_value);
        for (std::uint64_t fault = 0; fault < 2; ++fault) {
            for (std::uint64_t action = 0; action < 3; ++action) {
                RecoveryEvidence evidence = make_release_evidence(Unit::llm, mode);
                if (fault == 0) {
                    evidence.prepared.state = PreparedAuthorityState::missing;
                } else {
                    evidence.termination.target.operation_selector =
                        selector_field(OperationLeaf::dead_backend_pruning);
                }
                evidence.termination.attempted = action == 1;
                evidence.termination.acknowledged = action == 2;
                const Result result = action == 0
                    ? Result::unknown
                    : Result::quarantine;
                const ClaimDisposition claims = action == 0
                    ? ClaimDisposition::preserved
                    : ClaimDisposition::maximum;
                if (!decision_is(
                        evidence,
                        result,
                        claims,
                        action == 1 ? 1 : 0,
                        0)) {
                    return false;
                }
            }
        }
    }
    return true;
}

constexpr bool target_claim_lookup_matrix_passes() {
    for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
        const Mode mode = static_cast<Mode>(mode_value);
        RecoveryEvidence evidence = make_release_evidence(Unit::llm, mode);
        if (target_claim_count(
                evidence.device_after,
                evidence.prepared.claim) != 0
            || decide(evidence).result != Result::verified_release) {
            return false;
        }
        for (std::uint64_t shape = 0; shape < 2; ++shape) {
            evidence = make_release_evidence(Unit::llm, mode);
            evidence.device_before.active_claims[2] =
                evidence.device_before.active_claims[0];
            if (shape == 1) {
                ++evidence.device_before.active_claims[2]
                    .claim_generation.value;
            }
            clear_action(evidence);
            if (!decision_is(
                    evidence,
                    Result::unknown,
                    ClaimDisposition::preserved,
                    0,
                    0)) {
                return false;
            }
        }
        evidence = make_release_evidence(Unit::llm, mode);
        evidence.device_after.active_claims[0] =
            evidence.device_before.active_claims[0];
        if (!decision_is(
                evidence,
                Result::quarantine,
                ClaimDisposition::maximum,
                1,
                0)) {
            return false;
        }
        evidence = make_release_evidence(Unit::llm, mode);
        evidence.device_after.active_claims[0] =
            evidence.device_before.active_claims[0];
        ++evidence.device_after.active_claims[0]
              .claim_generation.value;
        if (!decision_is(
                evidence,
                Result::quarantine,
                ClaimDisposition::maximum,
                1,
                0)) {
            return false;
        }
        for (std::size_t field = 0; field < 2; ++field) {
            for (std::uint64_t fault = 0; fault < 2; ++fault) {
                evidence = make_release_evidence(Unit::llm, mode);
                Field& before_field = field == 0
                    ? evidence.device_before.active_claims[1]
                          .claim_identity
                    : evidence.device_before.active_claims[1]
                          .claim_generation;
                apply_field_fault(before_field, fault);
                clear_action(evidence);
                if (!decision_is(
                        evidence,
                        Result::unknown,
                        ClaimDisposition::preserved,
                        0,
                        0)) {
                    return false;
                }
                evidence = make_release_evidence(Unit::llm, mode);
                Field& after_field = field == 0
                    ? evidence.device_after.active_claims[1]
                          .claim_identity
                    : evidence.device_after.active_claims[1]
                          .claim_generation;
                apply_field_fault(after_field, fault);
                if (!decision_is(
                        evidence,
                        Result::quarantine,
                        ClaimDisposition::maximum,
                        1,
                        0)) {
                    return false;
                }
            }
        }
    }
    return true;
}

constexpr bool liveness_and_early_credit_matrix_passes() {
    for (std::uint64_t mode_value = 0; mode_value < 2; ++mode_value) {
        const Mode mode = static_cast<Mode>(mode_value);
        RecoveryEvidence evidence = make_release_evidence(Unit::llm, mode);
        evidence.liveness_before = false;
        clear_action(evidence);
        if (!decision_is(
                evidence,
                Result::unknown,
                ClaimDisposition::preserved,
                0,
                0)) {
            return false;
        }
        evidence = make_release_evidence(Unit::llm, mode);
        evidence.liveness_after = false;
        if (!decision_is(
                evidence,
                Result::quarantine,
                ClaimDisposition::maximum,
                1,
                0)) {
            return false;
        }
        for (std::uint64_t request_value = 0;
             request_value < 3;
             ++request_value) {
            evidence = make_release_evidence(Unit::llm, mode);
            clear_action(evidence);
            evidence.termination.request =
                static_cast<RequestState>(request_value);
            evidence.liveness_after = false;
            if (!decision_is(
                    evidence,
                    Result::quarantine,
                    ClaimDisposition::maximum,
                    0,
                    0)) {
                return false;
            }
        }
        evidence = make_release_evidence(Unit::llm, mode);
        evidence.credit_stage = CreditStage::before_both_proofs;
        evidence.termination.request = RequestState::absent;
        clear_action(evidence);
        make_intact_after(evidence);
        if (!decision_is(
                evidence,
                Result::unknown,
                ClaimDisposition::preserved,
                0,
                0)) {
            return false;
        }
        evidence = make_release_evidence(Unit::llm, mode);
        evidence.credit_stage = CreditStage::before_both_proofs;
        if (!decision_is(
                evidence,
                Result::quarantine,
                ClaimDisposition::maximum,
                1,
                0)) {
            return false;
        }
        evidence = make_release_evidence(Unit::llm, mode);
        evidence.credit_stage = CreditStage::before_both_proofs;
        make_intact_after(evidence);
        evidence.termination.attempted = false;
        if (!decision_is(
                evidence,
                Result::quarantine,
                ClaimDisposition::maximum,
                0,
                0)) {
            return false;
        }
    }
    return true;
}

bool canonical_fixture_matrix_passes() {
    FixtureAudit audit{0, 0, 0, true};
    generate_precondition_fixtures(audit);
    generate_precondition_fixtures_part_two(audit);
    if (!audit.passed || audit.precondition_count != 2205
        || audit.raw_count != 22050) {
        return false;
    }
    generate_action_fixtures(audit);
    if (!audit.passed || audit.raw_count != 22170) {
        return false;
    }
    generate_post_membership_and_selector_fixtures(audit);
    if (!audit.passed || audit.raw_count != 23181) {
        return false;
    }
    generate_post_generation_identity_device_ack_fixtures(audit);
    if (!audit.passed || audit.raw_count != 23871) {
        return false;
    }
    generate_early_credit_raw_fixtures(audit);
    if (!audit.passed || audit.raw_count != 23889) {
        return false;
    }
    generate_composition_fixtures(audit);
    const std::size_t total = audit.raw_count + audit.composition_count;
    return audit.passed && audit.composition_count == 414
        && total == 24303
        && total * (total - 1) / 2 == 295305753;
}

bool self_check() {
    return platform_id != "unsupported" && positive_matrix_passes()
        && common_fault_matrix_passes()
        && mode_identity_fault_matrix_passes()
        && raw_selector_fault_matrix_passes()
        && selection_leg_crosswire_matrix_passes()
        && unit_model_crosswire_matrix_passes()
        && membership_shape_fault_matrix_passes()
        && member_identity_fault_matrix_passes()
        && envelope_fault_matrix_passes()
        && observation_generation_fault_matrix_passes()
        && generation_fault_matrix_passes()
        && precondition_fault_matrix_passes()
        && device_and_ack_fault_matrix_passes()
        && action_state_matrix_passes()
        && lifecycle_fault_matrix_passes()
        && selector_fault_matrix_passes()
        && ownership_fault_matrix_passes()
        && release_crossleg_proof_matrix_passes()
        && action_truth_table_passes()
        && action_precedence_matrix_passes()
        && target_claim_lookup_matrix_passes()
        && liveness_and_early_credit_matrix_passes()
        && canonical_fixture_matrix_passes();
}

constexpr std::array<std::string_view, 409> output_rows = {
    "upstream.repository=fastflowlm_fastflowlm",
    "upstream.release=v0.9.46",
    "upstream.tag_object=40e98422f4fc475dbc51a0fc74279bb2dddce154",
    "upstream.commit=c3825404ea7c20b3a38c775d761d564254e08925",
    "upstream.main_blob=d0500fd701ea10a149e09d30163651ba0236007f",
    "upstream.main_sha256=ef567ecd181862509a804f421de71c239650de2c2b61b55ec2974a071c52117b",
    "upstream.server_blob=b7bf6683ad20e585a156cf8ada79f7a07b073d7f",
    "upstream.server_sha256=455312589db43fdd172b3052785fa4e1e448df9c0ca9fd51d4a2ee12084fbcc8",
    "source.topology=monolithic_direct_process",
    "source.server_execution=in_process_threads",
    "source.persistent_serving_child=absent_in_inspected_paths",
    "source.external_service_contract=absent",
    "source.topology_audit=passed",
    "current.launch=direct_process",
    "current.handle_identity=pid_or_process_handle",
    "current.ownership_scope=exact_child",
    "current.posix_termination=pid_signal_and_reap",
    "current.windows_termination=process_handle",
    "current.process_group_membership=absent",
    "current.windows_job_membership=absent",
    "current.scm_service_membership=absent",
    "current.service_instance_birth_token=absent",
    "current.device_claim=absent",
    "current.verified_membership_release=absent",
    "current.verified_device_release=absent",
    "current.recovery_authority=fallback",
    "current.release_decision=fallback",
    "current.claim_disposition=maximum",
    "profile.id=flm_system_managed",
    "profile.launch=prepared",
    "profile.containment=serving_process_or_service_membership_excluding_external_package_and_model_store",
    "profile.external_package=excluded",
    "profile.model_store=excluded",
    "profile.ownership=lemonade_serving_process_or_service_only",
    "profile.release_membership=required",
    "profile.release_device_claim=required",
    "profile.release_composition=both_required",
    "profile.inventory_contract=passed",
    "inventory.unit_runtime_bindings=absent",
    "inventory.unit_material_profiles=empty",
    "inventory.runtime_binding_count=8",
    "inventory.runtime_binding_digest=bd25c10593b16860298412dcdf3bf6c433a6ce7db305b37836917c833bf5a16d",
    "unit.embedding.id=matched",
    "unit.embedding.model_type=embedding",
    "unit.embedding.direct_process=verified_release",
    "unit.embedding.managed_service=verified_release",
    "unit.llm.id=matched",
    "unit.llm.model_type=llm",
    "unit.llm.direct_process=verified_release",
    "unit.llm.managed_service=verified_release",
    "unit.transcription.id=matched",
    "unit.transcription.model_type=transcription",
    "unit.transcription.direct_process=verified_release",
    "unit.transcription.managed_service=verified_release",
    "operation.behavioral_leaf=service_termination",
    "operation.other_rec_leaves=inventory_applicability_only",
    "runtime_binding.device_identity=required",
    "runtime_binding.backend_artifact_digest=required",
    "runtime_binding.source_build_dependency_closure=required",
    "runtime_binding.driver_runtime_closure=required",
    "runtime_binding.model_manifest_digest=required",
    "runtime_binding.normalized_configuration_digest=required",
    "runtime_binding.evidence_index_digest=required",
    "runtime_binding.evidence_liveness_lease=required",
    "runtime_identity.resident_id=required",
    "runtime_identity.resident_generation=required",
    "runtime_identity.backend_instance_birth_token=required",
    "runtime_identity.topology_generation=required",
    "runtime_identity.observation_contract_digest=required",
    "runtime_identity.termination_action_token=required",
    "runtime_identity.required_token_count=14",
    "runtime_identity.required_tokens_nonzero=passed",
    "runtime_identity.exact_match_across_owner_membership_action_device=passed",
    "runtime_identity.evidence_locus_count=7",
    "runtime_identity.exact_match_across_all_loci=passed",
    "runtime_identity.common_token_storage=one_value_per_token_per_locus",
    "runtime_identity.variant_selector=derived_from_closed_selector",
    "runtime_identity.hidden_causal_identity=absent",
    "runtime_identity.selector_authority=single_typed_logical_field",
    "runtime_identity.prepared_authority=single_structural_input",
    "runtime_identity.prepared_authority_identity=one_full_mode_identity",
    "membership.input_contract=bounded_root_and_member_records",
    "membership.member_record_identity=process_pid_birth_executable",
    "direct.profile=direct_process",
    "direct.identity_shape=controller_and_process_pid_birth_executable",
    "direct.service_state=not_applicable",
    "direct.managed_only_fields=structurally_absent",
    "direct.prepared_launch=passed",
    "direct.controller_identity=present_nonzero_exact",
    "direct.process_identity=present_nonzero_exact",
    "direct.process_birth_token=present_nonzero_exact",
    "direct.executable_digest=present_nonzero_exact",
    "direct.prepared_membership_identity_binding=matched",
    "direct.membership_generation_before=7",
    "direct.membership_snapshot_before=present_fresh_healthy_complete",
    "direct.membership_shape_facts=derived_from_bounded_records",
    "direct.membership_root=matched",
    "direct.common_causal_tokens=14_exact",
    "direct.membership_count_before=1",
    "direct.membership_direct_process=matched",
    "direct.external_package_member_count=0",
    "direct.model_store_member_count=0",
    "direct.ownership_scope=serving_process_only",
    "direct.termination_requested=passed",
    "direct.termination_target_mode=direct_process",
    "direct.termination_target_semantic_tuple=unit_model_leaf_mode_common14_and_full_identity",
    "direct.termination_target_controller_identity=present_nonzero_exact",
    "direct.termination_target_process_identity=present_nonzero_exact",
    "direct.termination_target_birth_token=present_nonzero_exact",
    "direct.termination_target_executable_digest=present_nonzero_exact",
    "direct.termination_action_token=present_nonzero_exact",
    "direct.termination_attempted=passed",
    "direct.termination_effect_calls=1",
    "direct.termination_acknowledged=completion_only",
    "direct.termination_ack_target=semantic_tuple_matched",
    "direct.termination_ack_action_token=matched",
    "direct.termination_state_unambiguous=derived_from_typed_ack_and_post_evidence",
    "direct.membership_generation_after=8",
    "direct.membership_generation_increment=checked_uint64_successor",
    "direct.membership_snapshot_after=present_fresh_healthy_complete",
    "direct.membership_after_scope=matched",
    "direct.membership_count_after=0",
    "direct.termination_verified=passed",
    "managed.profile=managed_service",
    "managed.identity_shape=controller_manager_service_instance_generation_and_serving_process",
    "managed.prepared_control=passed",
    "managed.controller_identity=present_nonzero_exact",
    "managed.service_manager_identity=present_nonzero_exact",
    "managed.service_identity_digest=present_nonzero_exact",
    "managed.service_config_digest=present_nonzero_exact",
    "managed.service_instance_birth_token=present_nonzero_exact",
    "managed.service_start_generation=13",
    "managed.service_start_generation_binding=present_nonzero_exact",
    "managed.service_state_before=running",
    "managed.service_state_before_generation=matched",
    "managed.membership_generation_before=11",
    "managed.membership_snapshot_before=present_fresh_healthy_complete",
    "managed.membership_shape_facts=derived_from_bounded_records",
    "managed.membership_service_identity=matched",
    "managed.membership_serving_process_identity=present_nonzero_exact",
    "managed.membership_serving_process_birth_token=present_nonzero_exact",
    "managed.membership_serving_process_executable_digest=present_nonzero_exact",
    "managed.prepared_membership_identity_binding=matched",
    "managed.common_causal_tokens=14_exact",
    "managed.membership_count_before=1",
    "managed.external_package_member_count=0",
    "managed.model_store_member_count=0",
    "managed.ownership_scope=serving_service_only",
    "managed.termination_requested=passed",
    "managed.termination_target_mode=managed_service",
    "managed.termination_target_semantic_tuple=unit_model_leaf_mode_common14_and_full_identity",
    "managed.termination_target_controller_identity=present_nonzero_exact",
    "managed.termination_target_service_manager_identity=present_nonzero_exact",
    "managed.termination_target_service_identity=present_nonzero_exact",
    "managed.termination_target_service_instance_birth_token=present_nonzero_exact",
    "managed.termination_target_config_digest=present_nonzero_exact",
    "managed.termination_target_start_generation=present_nonzero_exact",
    "managed.termination_target_serving_process_identity=present_nonzero_exact",
    "managed.termination_target_serving_process_birth_token=present_nonzero_exact",
    "managed.termination_target_serving_process_executable_digest=present_nonzero_exact",
    "managed.termination_action_token=present_nonzero_exact",
    "managed.termination_attempted=passed",
    "managed.termination_effect_calls=1",
    "managed.termination_acknowledged=completion_only",
    "managed.termination_ack_target=semantic_tuple_matched",
    "managed.termination_ack_action_token=matched",
    "managed.termination_state_unambiguous=derived_from_typed_ack_and_post_evidence",
    "managed.service_state_after=stopped",
    "managed.membership_generation_after=12",
    "managed.membership_generation_increment=checked_uint64_successor",
    "managed.membership_snapshot_after=present_fresh_healthy_complete",
    "managed.membership_after_scope=matched",
    "managed.membership_count_after=0",
    "managed.termination_verified=passed",
    "device.before_present=passed",
    "device.before_fresh=passed",
    "device.before_healthy=passed",
    "device.before_complete=passed",
    "device.observation_generation_before=17",
    "device.common_causal_tokens=14_exact",
    "device.device_identity=present_nonzero_exact",
    "device.device_identity_alias=runtime_binding_device_identity",
    "device.owner_identity=present_nonzero_exact",
    "device.owner_identity_shape=full_mode_specific_identity",
    "device.direct_owner_identity_fields=4_exact",
    "device.managed_owner_identity_fields=9_exact",
    "device.claim_identity=matched",
    "device.prepared_claim_owner_identity=present_nonzero_exact",
    "device.prepared_claim_identity=present_nonzero_exact",
    "device.prepared_claim_generation=23",
    "device.before_owner_identity=matched",
    "device.before_claim_identity=matched",
    "device.before_claim_anchor_binding=matched",
    "device.action_token=common14_binding",
    "device.claim_lookup_scope=keyed_target_claim",
    "device.unrelated_claims=out_of_scope",
    "device.target_claim_cardinality=zero_or_one",
    "device.duplicate_target_keys=rejected",
    "device.claim_presence=derived_from_target_key_count",
    "device.target_claim_before=present",
    "device.after_present=passed",
    "device.after_fresh=passed",
    "device.after_healthy=passed",
    "device.after_complete=passed",
    "device.observation_generation_after=18",
    "device.observation_generation_increment=checked_uint64_successor",
    "device.claim_generation_before=23",
    "device.claim_generation_after=23",
    "device.claim_generation_binding=present_nonzero_exact",
    "device.after_owner_identity=matched",
    "device.after_claim_identity=matched",
    "device.target_claim_after=absent",
    "device.release_verified=passed",
    "device.direct_owner_binding=matched",
    "device.managed_owner_binding=matched",
    "release.direct_membership_and_device=verified_release",
    "release.managed_membership_and_device=verified_release",
    "release.unit_binding=matched",
    "release.runtime_identity=all_14_matched",
    "release.owner_identity=matched",
    "release.action_token=common14_binding",
    "release.membership_proof_scope=membership_and_owner_only",
    "release.device_proof_scope=exact_device_claim_tuple",
    "release.device_proof_prepared_selection_binding=matched",
    "release.device_claim_tuple=device_identity_claim_identity_generation_matched",
    "release.device_identity_composition_axis=common14_runtime_binding_device_identity",
    "release.proof_types=distinct_membership_and_device",
    "release.shared_proof_binding=unit_model_mode_operation_common14_owner",
    "release.raw_decision_proof_input=absent",
    "release.composition_expected_binding=prepared_authority",
    "release.composition_validated_action_context=attempted_and_acknowledged",
    "release.composition_membership_proof_slot=present",
    "release.composition_device_proof_slot=present",
    "release.composition_proof_validity=derived_from_payload",
    "release.composition_credit_stage=after_both_proofs",
    "negative.profile_unknown=unknown",
    "negative.prepared_authority_missing=unknown",
    "negative.each_prepared_selection_missing=unknown",
    "negative.each_prepared_selection_unknown=unknown",
    "negative.each_unit_model_crosswire=unknown",
    "negative.operation_leaf_missing=unknown",
    "negative.operation_leaf_mismatch=unknown",
    "negative.membership_before_missing=unknown",
    "negative.membership_before_stale=unknown",
    "negative.membership_before_incomplete=unknown",
    "negative.each_membership_shape_pre=unknown",
    "negative.each_membership_shape_post=quarantine",
    "negative.each_ownership_pre_invalid=unknown",
    "negative.each_ownership_post_invalid=quarantine",
    "negative.direct_evidence_for_managed_profile=unknown",
    "negative.managed_evidence_for_direct_profile=unknown",
    "negative.each_action_state_pre_dispatch_clean=unknown",
    "negative.each_action_state_no_action_intact=verified_intact",
    "negative.each_action_state_contradiction=quarantine",
    "negative.each_no_action_changed_leg=quarantine",
    "negative.each_precondition_fault_attempted=quarantine",
    "negative.each_precondition_fault_ack_only=quarantine",
    "negative.membership_after_missing=quarantine",
    "negative.membership_after_stale=quarantine",
    "negative.membership_after_incomplete=quarantine",
    "negative.membership_after_nonempty=quarantine",
    "negative.direct_process_identity_reused=quarantine",
    "negative.release_credit_before_both_proofs_no_action=unknown",
    "negative.release_credit_before_both_proofs_attempted=quarantine",
    "negative.each_leg_selector_pre_missing=unknown",
    "negative.each_leg_selector_pre_unknown=unknown",
    "negative.each_selection_leg_crosswire_pre=unknown",
    "negative.each_leg_selector_post_missing=quarantine",
    "negative.each_leg_selector_post_unknown=quarantine",
    "negative.each_selection_leg_crosswire_post=quarantine",
    "negative.each_common_token_pre_missing=unknown",
    "negative.each_common_token_pre_zero=unknown",
    "negative.each_common_token_pre_anchor_mismatch=unknown",
    "negative.each_common_token_post_missing=quarantine",
    "negative.each_common_token_post_zero=quarantine",
    "negative.each_common_token_pre_crossleg_mismatch=unknown",
    "negative.each_common_token_post_crossleg_mismatch=quarantine",
    "negative.each_evidence_liveness_expired_pre=unknown",
    "negative.each_evidence_liveness_expired_no_action=quarantine",
    "negative.each_evidence_liveness_expired_post=quarantine",
    "negative.membership_before_unhealthy=unknown",
    "negative.membership_after_unhealthy=quarantine",
    "negative.membership_generation_before_missing=unknown",
    "negative.membership_generation_before_zero=unknown",
    "negative.membership_generation_before_unchecked=unknown",
    "negative.membership_generation_after_missing=quarantine",
    "negative.membership_generation_after_zero=quarantine",
    "negative.membership_generation_replay=quarantine",
    "negative.membership_generation_regressed=quarantine",
    "negative.membership_generation_skipped=quarantine",
    "negative.membership_generation_overflow_to_zero=quarantine",
    "negative.membership_generation_after_unchecked=quarantine",
    "negative.device_generation_before_missing=unknown",
    "negative.device_generation_before_zero=unknown",
    "negative.device_generation_before_unchecked=unknown",
    "negative.device_generation_after_missing=quarantine",
    "negative.device_generation_after_zero=quarantine",
    "negative.device_generation_replay=quarantine",
    "negative.device_generation_regressed=quarantine",
    "negative.device_generation_skipped=quarantine",
    "negative.device_generation_overflow_to_zero=quarantine",
    "negative.device_generation_after_unchecked=quarantine",
    "negative.each_action_target_missing=unknown",
    "negative.each_action_target_zero=unknown",
    "negative.each_action_target_mismatch=unknown",
    "negative.each_action_target_crosswire=unknown",
    "negative.each_mode_specific_pre_identity_missing=unknown",
    "negative.each_mode_specific_pre_identity_zero=unknown",
    "negative.each_mode_specific_pre_identity_mismatch=unknown",
    "negative.each_device_specific_pre_identity_missing=unknown",
    "negative.each_device_specific_pre_identity_zero=unknown",
    "negative.each_device_specific_pre_identity_mismatch=unknown",
    "negative.each_device_specific_post_identity_missing=quarantine",
    "negative.each_device_specific_post_identity_zero=quarantine",
    "negative.each_device_specific_post_identity_mismatch=quarantine",
    "negative.managed_service_start_generation_missing=unknown",
    "negative.managed_service_start_generation_zero=unknown",
    "negative.managed_service_start_generation_mismatch=unknown",
    "negative.managed_service_post_restart_generation=quarantine",
    "negative.managed_service_state_before_missing=unknown",
    "negative.managed_service_state_before_unknown=unknown",
    "negative.managed_service_state_before_not_running=unknown",
    "negative.managed_service_state_after_missing=quarantine",
    "negative.managed_service_state_after_unknown=quarantine",
    "negative.managed_service_state_after_running=quarantine",
    "negative.managed_service_state_after_wrong=quarantine",
    "negative.device_before_missing=unknown",
    "negative.device_before_stale=unknown",
    "negative.device_before_unhealthy=unknown",
    "negative.device_before_incomplete=unknown",
    "negative.device_before_claim_absent=unknown",
    "negative.device_after_missing=quarantine",
    "negative.device_after_stale=quarantine",
    "negative.device_after_unhealthy=quarantine",
    "negative.device_after_incomplete=quarantine",
    "negative.device_after_claim_present=quarantine",
    "negative.each_device_target_key_pre_invalid=unknown",
    "negative.each_device_target_key_post_invalid=quarantine",
    "negative.release_membership_proof_missing=quarantine",
    "negative.release_device_proof_missing=quarantine",
    "negative.each_release_proof_unit_model_splice=quarantine",
    "negative.each_release_proof_mode_splice=quarantine",
    "negative.each_release_proof_operation_splice=quarantine",
    "negative.each_release_proof_owner_mismatch=quarantine",
    "negative.each_release_proof_common_token_mismatch=quarantine",
    "negative.each_release_device_claim_tuple_missing=quarantine",
    "negative.each_release_device_claim_tuple_zero=quarantine",
    "negative.each_release_device_claim_tuple_present_nonzero_nonexact=quarantine",
    "negative.termination_ack_target_missing=quarantine",
    "negative.termination_ack_target_zero=quarantine",
    "negative.termination_ack_target_mismatch=quarantine",
    "negative.termination_ack_target_crosswire=quarantine",
    "negative.termination_ack_action_token_missing=quarantine",
    "negative.termination_ack_action_token_zero=quarantine",
    "negative.termination_ack_action_token_mismatch=quarantine",
    "negative.device_claim_generation_before_missing=unknown",
    "negative.device_claim_generation_before_zero=unknown",
    "negative.device_claim_generation_before_mismatch=unknown",
    "negative.device_claim_generation_after_missing=quarantine",
    "negative.device_claim_generation_after_zero=quarantine",
    "negative.device_claim_generation_after_mismatch=quarantine",
    "negative.each_direct_mode_specific_post_identity_missing=quarantine",
    "negative.each_direct_mode_specific_post_identity_zero=quarantine",
    "negative.each_direct_mode_specific_post_identity_mismatch=quarantine",
    "negative.each_managed_mode_specific_post_identity_missing=quarantine",
    "negative.each_managed_mode_specific_post_identity_zero=quarantine",
    "negative.each_managed_mode_specific_post_identity_mismatch=quarantine",
    "disposition.precondition_failure=unknown",
    "disposition.precondition_effect_calls=0",
    "disposition.precondition_claims=preserved",
    "disposition.effect_calls_equal_attempted=passed",
    "disposition.rejected_without_effect=verified_intact",
    "disposition.rejected_without_effect_claims=preserved",
    "disposition.intact_no_action_claims=preserved",
    "disposition.post_action_ambiguity=quarantine",
    "disposition.post_action_claims=maximum",
    "disposition.invalid_precondition_attempt_dominance=quarantine",
    "disposition.membership_only_release=quarantine",
    "disposition.device_only_release=quarantine",
    "disposition.partial_proof_claims=maximum",
    "disposition.partial_proof_release_credit=0",
    "disposition.pre_dispatch_unavailable_authority=fallback",
    "disposition.precondition_neutral_state=unchanged_no_action",
    "disposition.fallback_release_credit=0",
    "disposition.quarantine_release_credit=0",
    "disposition.unverified_release_credit=0",
    "synthetic.observation_source=injected",
    "synthetic.direct_process_envelope=verified_release",
    "synthetic.managed_service_envelope=verified_release",
    "synthetic.fail_closed_matrix=passed",
    "synthetic.negative_fixture_shapes=passed",
    "synthetic.action_state_matrix=passed",
    "synthetic.precondition_action_dominance=passed",
    "synthetic.device_target_claim_lookup=passed",
    "synthetic.negative_fault_class_count=132",
    "synthetic.negative_variant_count=24303",
    "synthetic.negative_case_manifest_sha256=f655d1d22e21d61de56531bd95565dc016ccf235f5e80fd073f0bc567baa7342",
    "synthetic.negative_labeled_manifest_sha256=5c5be17cd31b46feb4f32c83428f95cf87e52f39e6adeb9955fa1efe4e700a85",
    "synthetic.negative_unordered_pair_count=295305753",
    "synthetic.negative_input_descriptor_unique=passed",
    "synthetic.negative_pairwise_unique=passed",
    "synthetic.negative_input_execution=passed",
    "synthetic.input_domain_partition=raw_verification_and_proof_composition",
    "synthetic.zero_release_credit_matrix=passed",
    "native.windows_service_membership=deferred",
    "native.windows_npu_device_claim=deferred",
    "fallback_binding.embedding=residency_recovery_block_unproven_release_v1",
    "fallback_binding.llm=residency_recovery_block_unproven_release_v1",
    "fallback_binding.transcription=residency_recovery_block_unproven_release_v1",
};

}

int main() {
    if (!lemon::residency::prototype::self_check()) {
        return 1;
    }
    for (std::string_view row : lemon::residency::prototype::output_rows) {
        std::cout << row << '\n';
    }
    std::cout << "platform.current="
              << lemon::residency::prototype::platform_id << '\n';
    std::cout << "runtime_authority=none\n";
    return 0;
}
