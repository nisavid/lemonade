#include "journal_persistence_test_support.h"
#include "lemon/residency/durable_local_overlay.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace lemon::residency;
using lemon::residency::testing::FaultAction;
using lemon::residency::testing::FaultOperation;
using lemon::residency::testing::FaultPosition;
using lemon::residency::testing::FixedAuthorityChild;
using lemon::residency::testing::JournalTestStorage;
using lemon::residency::testing::PlatformContract;

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
        capacity.push_back(
            ClaimAmount{"gpu/gtt", ClaimUnit::Bytes, bytes});
    }
    return {
        ClaimFamilyClosure{
            ClaimFamily::ConsumableCapacity,
            bytes == 0 ? ClaimCompleteness::KnownZero
                       : ClaimCompleteness::Bounded,
            std::move(capacity)},
        ClaimFamilyClosure{ClaimFamily::SafetyFloor,
                           ClaimCompleteness::KnownZero, {}},
        ClaimFamilyClosure{ClaimFamily::CardinalityPool,
                           ClaimCompleteness::KnownZero, {}},
        ClaimFamilyClosure{ClaimFamily::CompatibilityExclusivity,
                           ClaimCompleteness::NotApplicable, {}},
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
        digest('3'), digest('4'), digest('5'), digest('6'), digest('7'),
        digest('8'), digest('9'), digest('a')};
}

ParsedProfilingInputEnvelope profile_for(std::string deployment_id,
                                         std::uint64_t sequence) {
    ProfilingInputEnvelopeDraft draft;
    draft.deployment_id = std::move(deployment_id);
    draft.sequence = sequence;
    draft.profiling_transaction_id =
        "profiling/" + std::to_string(sequence);
    draft.selector = selector();
    draft.generations = OverlaySourceGenerations{1, 2, 3, 4, 5, 6, 7};
    draft.attributed_claims = claims(4096);
    draft.baseline_observation_sha256 = digest('b');
    draft.workload_observation_sha256 = digest('c');
    draft.release_observation_sha256 = digest('d');
    draft.observation_contract_sha256 = digest('e');
    draft.predictor_contract_sha256 = digest('f');
    draft.observed_at = "2026-08-23T10:00:00Z";
    draft.fresh_until = "2026-08-23T10:05:00Z";
    draft.max_clock_skew_milliseconds = 1000;
    draft.attribution_complete = true;
    draft.external_demand_absent = true;
    draft.lifecycle_release_verified = true;
    auto sealed = seal_profiling_input(std::move(draft));
    require(sealed.accepted(), sealed.diagnostic);
    return std::move(*sealed.candidate);
}

ParsedLocalOverlayObject overlay_for(
    const ParsedProfilingInputEnvelope &profile) {
    LocalOverlayObjectDraft draft;
    draft.deployment_id = std::string(profile.deployment_id());
    draft.sequence = profile.sequence();
    draft.profiling_input_sha256 = std::string(profile.checksum_sha256());
    draft.selector = profile.selector();
    draft.method = LocalOverlayMethodIdentity{
        "method/exact-profile", digest('1'),
        LocalOverlayMethodScope::DeploymentExact, OperationKind::Admission,
        std::nullopt, digest('2')};
    draft.bound_claims = claims(4096);
    draft.uncertainty_claims = claims(512);
    draft.safety_margin_claims = claims(256);
    draft.confidence_basis_points = 9900;
    draft.qualified_at = "2026-08-23T10:01:00Z";
    draft.expires_at = "2026-09-23T10:01:00Z";
    draft.status = LocalOverlayObjectStatus::Qualified;
    draft.decision_trace_sha256 = digest('3');
    auto sealed = seal_local_overlay(std::move(draft));
    require(sealed.accepted(), sealed.diagnostic);
    return std::move(*sealed.candidate);
}

class TemporaryCache {
public:
    TemporaryCache() {
        static std::atomic<std::uint64_t> sequence{0};
        const auto now = std::chrono::steady_clock::now()
                             .time_since_epoch()
                             .count();
        path_ = std::filesystem::temp_directory_path() /
                ("lemonade-local-overlay-" + std::to_string(now) + "-" +
                 std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryCache() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path &path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

LocalOverlayStoreLimits generous_limits() {
    return LocalOverlayStoreLimits{64 * 1024, 128};
}

LocalOverlayStoreResult
qualify(LocalOverlayStore &store, PublishedLocalOverlay &&expected,
        std::uint64_t sequence, char decision_trace,
        std::string activated_at) {
    const auto deployment_id = std::string(expected.deployment_id());
    auto profile = profile_for(deployment_id, sequence);
    auto overlay = overlay_for(profile);
    return store.activate(
        std::move(expected),
        LocalOverlayActivation::qualification(
            std::move(profile), std::move(overlay), digest(decision_trace),
            std::move(activated_at)));
}

struct PublishedFixture {
    JournalTestStorage storage;
    std::string root_bytes;
    std::string root_sha256;
    std::string overlay_sha256;
    std::string overlay_bytes;
    std::string deployment_id;
};

PublishedFixture
published_fixture(PlatformContract platform = PlatformContract::Linux) {
    auto storage = JournalTestStorage::fresh(platform);
    auto store = storage.make_overlay_store(generous_limits());
    auto empty = store.snapshot(TrustedLocalOverlayReplayFloor::uninitialized());
    require(empty.status == LocalOverlayStoreStatus::Empty &&
                empty.authority.has_value(),
            "published fixture did not start empty");
    auto activated = qualify(store, std::move(*empty.authority), 1, '4',
                             "2026-08-23T10:02:00Z");
    require(activated.status == LocalOverlayStoreStatus::Activated &&
                activated.authority.has_value() &&
                activated.authority->active_overlay() != nullptr,
            "published fixture did not activate");
    return PublishedFixture{
        std::move(storage),
        std::string(activated.authority->root_bytes()),
        std::string(activated.authority->root_sha256()),
        std::string(
            activated.authority->active_overlay()->checksum_sha256()),
        std::string(activated.authority->active_overlay()->canonical_bytes()),
        std::string(activated.authority->deployment_id()),
    };
}

void require_empty_activate_and_same_deployment_visibility() {
    TemporaryCache cache;
    auto store = LocalOverlayStore::native(cache.path(), generous_limits());
    auto empty = store.snapshot(TrustedLocalOverlayReplayFloor::uninitialized());
    require(empty.status == LocalOverlayStoreStatus::Empty &&
                empty.authority.has_value() && empty.authority->empty(),
            "fresh overlay namespace did not return an empty CAS authority");

    const auto deployment_id =
        std::string(empty.authority->deployment_id());
    auto profile = profile_for(deployment_id, 1);
    auto overlay = overlay_for(profile);
    const auto overlay_sha256 = std::string(overlay.checksum_sha256());
    auto activation = LocalOverlayActivation::qualification(
        std::move(profile), std::move(overlay), digest('4'),
        "2026-08-23T10:02:00Z");
    auto activated = store.activate(std::move(*empty.authority),
                                    std::move(activation));
    require(activated.status == LocalOverlayStoreStatus::Activated &&
                activated.authority.has_value() &&
                !activated.authority->empty(),
            "first local overlay activation did not publish");
    require(activated.authority->generation() == 1 &&
                activated.authority->sequence_high_water() == 1 &&
                activated.authority->active_overlay() != nullptr &&
                activated.authority->active_profiling_input() != nullptr &&
                activated.authority->active_overlay()->checksum_sha256() ==
                    overlay_sha256,
            "published overlay snapshot was incomplete");

    const auto root = std::string(activated.authority->root_bytes());
    auto reader = LocalOverlayStore::native(cache.path(), generous_limits());
    auto visible = reader.snapshot(
        TrustedLocalOverlayReplayFloor::exact_root(root));
    require(visible.status == LocalOverlayStoreStatus::Ready &&
                visible.authority.has_value() &&
                visible.authority->root_bytes() == root &&
                visible.authority->deployment_id() == deployment_id &&
                visible.authority->active_overlay() != nullptr &&
                visible.authority->active_profiling_input() != nullptr,
            "same-deployment reader did not observe one complete snapshot");
}

void require_stale_cas_conflicts_before_object_writes() {
    auto storage = JournalTestStorage::fresh();
    auto first_store = storage.make_overlay_store(generous_limits());
    auto second_store = storage.make_overlay_store(generous_limits());
    auto first = first_store.snapshot(
        TrustedLocalOverlayReplayFloor::uninitialized());
    auto second = second_store.snapshot(
        TrustedLocalOverlayReplayFloor::uninitialized());
    require(first.status == LocalOverlayStoreStatus::Empty &&
                first.authority.has_value() &&
                second.status == LocalOverlayStoreStatus::Empty &&
                second.authority.has_value(),
            "two empty readers did not receive CAS capabilities");

    auto winner = qualify(first_store, std::move(*first.authority), 1, '4',
                          "2026-08-23T10:02:00Z");
    require(winner.status == LocalOverlayStoreStatus::Activated &&
                winner.authority.has_value(),
            "first fake-store activation did not publish");
    const auto committed_root = std::string(winner.authority->root_bytes());

    storage.reset_observations();
    auto loser = qualify(second_store, std::move(*second.authority), 1, '5',
                         "2026-08-23T10:03:00Z");
    const auto after = storage.snapshot();
    require(loser.status == LocalOverlayStoreStatus::ConflictBeforeWrite &&
                !loser.authority.has_value() &&
                after.authority_root_bytes == committed_root &&
                !after.authority_mutation_attempted,
            "stale CAS did not conflict before immutable-object writes");
}

void require_rollback_advances_generation_and_preserves_history() {
    auto storage = JournalTestStorage::fresh();
    auto store = storage.make_overlay_store(generous_limits());
    auto empty = store.snapshot(TrustedLocalOverlayReplayFloor::uninitialized());
    require(empty.status == LocalOverlayStoreStatus::Empty &&
                empty.authority.has_value(),
            "rollback fixture did not start empty");

    auto first = qualify(store, std::move(*empty.authority), 1, '4',
                         "2026-08-23T10:02:00Z");
    require(first.status == LocalOverlayStoreStatus::Activated &&
                first.authority.has_value(),
            "rollback fixture did not publish sequence one");
    const auto first_root = std::string(first.authority->root_bytes());
    const auto first_overlay =
        std::string(first.authority->active_overlay()->checksum_sha256());

    auto second = qualify(store, std::move(*first.authority), 2, '5',
                          "2026-08-23T10:04:00Z");
    require(second.status == LocalOverlayStoreStatus::Activated &&
                second.authority.has_value() &&
                second.authority->generation() == 2 &&
                second.authority->sequence_high_water() == 2,
            "second qualification did not advance generation and sequence");
    const auto second_root_sha256 =
        std::string(second.authority->root_sha256());

    auto rolled_back = store.activate(
        std::move(*second.authority),
        LocalOverlayActivation::rollback(first_overlay, digest('6'),
                                         "2026-08-23T10:05:00Z"));
    require(rolled_back.status == LocalOverlayStoreStatus::Activated &&
                rolled_back.authority.has_value() &&
                rolled_back.authority->generation() == 3 &&
                rolled_back.authority->sequence_high_water() == 2 &&
                rolled_back.authority->active_root() != nullptr &&
                rolled_back.authority->active_root()->transition() ==
                    OverlayRootTransition::Rollback &&
                rolled_back.authority->active_root()
                        ->previous_root_sha256() ==
                    std::optional<std::string>{second_root_sha256} &&
                rolled_back.authority->active_overlay() != nullptr &&
                rolled_back.authority->active_overlay()->sequence() == 1,
            "rollback rewrote history or changed the sequence high-water");

    const auto rollback_root =
        std::string(rolled_back.authority->root_bytes());
    auto reader = storage.make_overlay_store(generous_limits());
    auto replayed = reader.snapshot(
        TrustedLocalOverlayReplayFloor::exact_root(first_root));
    require(replayed.status == LocalOverlayStoreStatus::Ready &&
                replayed.authority.has_value() &&
                replayed.authority->root_bytes() == rollback_root &&
                replayed.authority->generation() == 3,
            "trusted ancestor did not replay through append-only rollback");

    auto unfloored_reader = storage.make_overlay_store(generous_limits());
    auto unfloored = unfloored_reader.snapshot(
        TrustedLocalOverlayReplayFloor::uninitialized());
    require(unfloored.status == LocalOverlayStoreStatus::ReplayFloorRequired &&
                !unfloored.authority.has_value(),
            "non-empty authority accepted an uninitialized replay floor");
}

void require_rollback_can_select_another_prior_overlay() {
    auto storage = JournalTestStorage::fresh();
    auto store = storage.make_overlay_store(generous_limits());
    auto empty = store.snapshot(TrustedLocalOverlayReplayFloor::uninitialized());
    require(empty.status == LocalOverlayStoreStatus::Empty &&
                empty.authority.has_value(),
            "rollback-of-rollback fixture did not start empty");

    auto first = qualify(store, std::move(*empty.authority), 1, '4',
                         "2026-08-23T10:02:00Z");
    require(first.status == LocalOverlayStoreStatus::Activated &&
                first.authority.has_value(),
            "rollback-of-rollback fixture did not publish sequence one");
    const auto first_overlay =
        std::string(first.authority->active_overlay()->checksum_sha256());
    auto second = qualify(store, std::move(*first.authority), 2, '5',
                          "2026-08-23T10:03:00Z");
    require(second.status == LocalOverlayStoreStatus::Activated &&
                second.authority.has_value(),
            "rollback-of-rollback fixture did not publish sequence two");
    const auto second_overlay =
        std::string(second.authority->active_overlay()->checksum_sha256());
    auto third = qualify(store, std::move(*second.authority), 3, '6',
                         "2026-08-23T10:04:00Z");
    require(third.status == LocalOverlayStoreStatus::Activated &&
                third.authority.has_value(),
            "rollback-of-rollback fixture did not publish sequence three");

    auto rollback_one = store.activate(
        std::move(*third.authority),
        LocalOverlayActivation::rollback(first_overlay, digest('7'),
                                         "2026-08-23T10:05:00Z"));
    require(rollback_one.status == LocalOverlayStoreStatus::Activated &&
                rollback_one.authority.has_value() &&
                rollback_one.authority->generation() == 4 &&
                rollback_one.authority->sequence_high_water() == 3 &&
                rollback_one.authority->active_overlay()->sequence() == 1,
            "first rollback did not retain the sequence high-water");

    auto rollback_two = store.activate(
        std::move(*rollback_one.authority),
        LocalOverlayActivation::rollback(second_overlay, digest('8'),
                                         "2026-08-23T10:06:00Z"));
    require(rollback_two.status == LocalOverlayStoreStatus::Activated &&
                rollback_two.authority.has_value() &&
                rollback_two.authority->generation() == 5 &&
                rollback_two.authority->sequence_high_water() == 3 &&
                rollback_two.authority->active_overlay()->sequence() == 2,
            "rollback could not select another prior overlay below high-water");
}

void require_unlock_failure_withholds_authority() {
    auto storage = JournalTestStorage::fresh();
    storage.arm_fault(FaultOperation::AuthorityUnlock, FaultPosition::After,
                      FaultAction::Error);
    auto store = storage.make_overlay_store(generous_limits());
    auto failed =
        store.snapshot(TrustedLocalOverlayReplayFloor::uninitialized());
    require(failed.status == LocalOverlayStoreStatus::RecoveryRequired &&
                !failed.authority.has_value(),
            "failed authority unlock returned a usable empty snapshot");

    auto recovery = storage.make_overlay_store(generous_limits());
    auto recovered =
        recovery.snapshot(TrustedLocalOverlayReplayFloor::uninitialized());
    require(recovered.status == LocalOverlayStoreStatus::Empty &&
                recovered.authority.has_value(),
            "locked snapshot did not reconcile the retained unlock fence");
}

void require_copied_store_and_object_corruption_fail_closed() {
    {
        auto fixture = published_fixture();
        auto copied = fixture.storage.clone();
        auto copied_store = copied.make_overlay_store(generous_limits());
        auto result = copied_store.snapshot(
            TrustedLocalOverlayReplayFloor::exact_root(fixture.root_bytes));
        require(result.status == LocalOverlayStoreStatus::DeploymentMismatch &&
                    !result.authority.has_value(),
                "copied overlay namespace retained deployment authority");
    }

    {
        auto fixture = published_fixture();
        fixture.storage.remove_immutable_object(fixture.overlay_sha256);
        auto store = fixture.storage.make_overlay_store(generous_limits());
        auto result = store.snapshot(
            TrustedLocalOverlayReplayFloor::exact_root(fixture.root_bytes));
        require(result.status == LocalOverlayStoreStatus::MissingObject &&
                    !result.authority.has_value(),
                "missing selected overlay did not fail closed");
    }

    {
        auto fixture = published_fixture();
        auto corrupt_overlay = fixture.overlay_bytes;
        const auto confidence =
            corrupt_overlay.find("\"confidence_basis_points\":9900");
        require(confidence != std::string::npos,
                "overlay fixture lacks canonical confidence field");
        const auto amount = corrupt_overlay.find("9900", confidence);
        require(amount != std::string::npos,
                "overlay fixture lacks canonical confidence amount");
        corrupt_overlay[amount + 1] = '8';
        fixture.storage.overwrite_immutable_object(fixture.overlay_sha256,
                                                   corrupt_overlay);
        auto store = fixture.storage.make_overlay_store(generous_limits());
        auto result = store.snapshot(
            TrustedLocalOverlayReplayFloor::exact_root(fixture.root_bytes));
        require(result.status == LocalOverlayStoreStatus::DigestMismatch &&
                    !result.authority.has_value(),
                "same-address overlay corruption was not distinguished");
    }

    {
        auto fixture = published_fixture();
        fixture.storage.remove_immutable_object(fixture.root_sha256);
        auto store = fixture.storage.make_overlay_store(generous_limits());
        auto result = store.snapshot(
            TrustedLocalOverlayReplayFloor::exact_root(fixture.root_bytes));
        require(result.status == LocalOverlayStoreStatus::MissingObject &&
                    !result.authority.has_value(),
                "missing active root object did not fail closed");
    }

    {
        auto fixture = published_fixture();
        fixture.storage.overwrite_fixed_child_bytes(FixedAuthorityChild::Root,
                                                    "{");
        auto store = fixture.storage.make_overlay_store(generous_limits());
        auto result = store.snapshot(
            TrustedLocalOverlayReplayFloor::exact_root(fixture.root_bytes));
        require(result.status == LocalOverlayStoreStatus::CorruptOrRollback &&
                    !result.authority.has_value(),
                "malformed active root did not fail closed");
    }
}

void require_generation_replay_is_rejected() {
    auto fixture = published_fixture();
    auto parsed = parse_overlay_activation_root(fixture.root_bytes);
    require(parsed.accepted(), "published root was not parseable");
    const auto &root = *parsed.candidate;

    OverlayActivationRootDraft forged;
    forged.deployment_id = std::string(root.deployment_id());
    forged.generation = root.generation() + 2;
    forged.previous_root_sha256 = std::string(root.checksum_sha256());
    forged.transition = OverlayRootTransition::Qualification;
    forged.authority_status = root.authority_status();
    forged.selected_overlay_sha256 =
        std::string(root.selected_overlay_sha256());
    forged.selected_overlay_sequence = root.selected_overlay_sequence();
    forged.sequence_high_water = root.sequence_high_water();
    forged.selected_selector_sha256 =
        std::string(root.selected_selector_sha256());
    forged.selected_method_sha256 =
        std::string(root.selected_method_sha256());
    forged.decision_trace_sha256 =
        std::string(root.decision_trace_sha256());
    forged.activated_at = "2026-08-23T10:03:00Z";
    forged.expires_at = std::string(root.expires_at());
    auto sealed = seal_overlay_activation_root(std::move(forged));
    require(sealed.accepted(), "generation-gap fixture was not canonical");

    fixture.storage.overwrite_immutable_object(
        std::string(sealed.candidate->checksum_sha256()),
        std::string(sealed.candidate->canonical_bytes()));
    fixture.storage.overwrite_fixed_child_bytes(
        FixedAuthorityChild::Root,
        std::string(sealed.candidate->canonical_bytes()));
    auto store = fixture.storage.make_overlay_store(generous_limits());
    auto result = store.snapshot(
        TrustedLocalOverlayReplayFloor::exact_root(fixture.root_bytes));
    require(result.status == LocalOverlayStoreStatus::CorruptOrRollback &&
                !result.authority.has_value(),
            "generation-gap root replay was accepted");
}

void require_concurrent_activators_share_one_owner_lock() {
    auto storage = JournalTestStorage::fresh();
    auto first_store = storage.make_overlay_store(generous_limits());
    auto second_store = storage.make_overlay_store(generous_limits());
    auto first = first_store.snapshot(
        TrustedLocalOverlayReplayFloor::uninitialized());
    auto second = second_store.snapshot(
        TrustedLocalOverlayReplayFloor::uninitialized());
    require(first.status == LocalOverlayStoreStatus::Empty &&
                first.authority.has_value() &&
                second.status == LocalOverlayStoreStatus::Empty &&
                second.authority.has_value(),
            "concurrent activators did not share one empty authority");

    std::optional<LocalOverlayStoreResult> first_result;
    std::optional<LocalOverlayStoreResult> second_result;
    storage.pause_after(
        lemon::residency::testing::FaultOperation::ObjectPublish);
    std::thread first_thread([&] {
        first_result.emplace(
            qualify(first_store, std::move(*first.authority), 1, '4',
                    "2026-08-23T10:02:00Z"));
    });
    require(storage.wait_until_paused(5000),
            "first activator did not pause while holding authority");
    std::thread second_thread([&] {
        second_result.emplace(
            qualify(second_store, std::move(*second.authority), 1, '5',
                    "2026-08-23T10:03:00Z"));
    });
    require(storage.wait_until_lock_waiter(5000),
            "second activator did not wait on the shared owner lock");
    storage.release_pause();
    first_thread.join();
    second_thread.join();

    const auto after = storage.snapshot();
    require(first_result.has_value() && second_result.has_value() &&
                first_result->status == LocalOverlayStoreStatus::Activated &&
                first_result->authority.has_value() &&
                second_result->status ==
                    LocalOverlayStoreStatus::ConflictBeforeWrite &&
                !second_result->authority.has_value() &&
                after.all_authority_io_under_lock,
            "concurrent activators did not serialize to one CAS winner");
}

enum class CrashObjectTarget { ProfilingInput, Overlay, Root };

void require_recoverable_snapshot(JournalTestStorage &storage,
                                  std::string_view trusted_root) {
    storage.restart();
    storage.reset_observations();
    auto reader = storage.make_overlay_store(generous_limits());
    auto recovered = reader.snapshot(
        TrustedLocalOverlayReplayFloor::exact_root(
            std::string(trusted_root)));
    const auto after = storage.snapshot();
    require(recovered.status == LocalOverlayStoreStatus::Ready &&
                recovered.authority.has_value() &&
                recovered.authority->active_root() != nullptr &&
                recovered.authority->active_overlay() != nullptr &&
                recovered.authority->active_profiling_input() != nullptr &&
                (recovered.authority->generation() == 1 ||
                 recovered.authority->generation() == 2) &&
                recovered.authority->sequence_high_water() ==
                    recovered.authority->generation() &&
                after.authority_root_bytes ==
                    recovered.authority->root_bytes() &&
                after.all_authority_io_under_lock &&
                after.open_handles.empty(),
            "crash exposed a partial local-overlay snapshot");
}

void require_object_publication_crash_boundaries() {
    const std::vector<FaultOperation> operations{
        FaultOperation::ObjectStageCreate,
        FaultOperation::ObjectWrite,
        FaultOperation::ObjectFlush,
        FaultOperation::ObjectClose,
        FaultOperation::ObjectPublish,
        FaultOperation::ObjectNamespaceDurability,
        FaultOperation::ObjectStageCleanup,
    };
    for (const auto platform : {PlatformContract::Linux,
                                PlatformContract::MacOS,
                                PlatformContract::Windows}) {
        for (const auto target : {CrashObjectTarget::ProfilingInput,
                                  CrashObjectTarget::Overlay,
                                  CrashObjectTarget::Root}) {
            for (const auto operation : operations) {
                for (const auto position : {FaultPosition::Before,
                                            FaultPosition::After}) {
                    auto fixture = published_fixture(platform);
                    auto store =
                        fixture.storage.make_overlay_store(generous_limits());
                    auto expected = store.snapshot(
                        TrustedLocalOverlayReplayFloor::exact_root(
                            fixture.root_bytes));
                    require(expected.status == LocalOverlayStoreStatus::Ready &&
                                expected.authority.has_value(),
                            "crash fixture did not recover its expected root");

                    auto input = profile_for(
                        std::string(expected.authority->deployment_id()), 2);
                    auto overlay = overlay_for(input);
                    if (target != CrashObjectTarget::ProfilingInput) {
                        fixture.storage.overwrite_immutable_object(
                            std::string(input.checksum_sha256()),
                            std::string(input.canonical_bytes()));
                    }
                    if (target == CrashObjectTarget::Root) {
                        fixture.storage.overwrite_immutable_object(
                            std::string(overlay.checksum_sha256()),
                            std::string(overlay.canonical_bytes()));
                    }
                    auto activation = LocalOverlayActivation::qualification(
                        std::move(input), std::move(overlay), digest('5'),
                        "2026-08-23T10:03:00Z");
                    fixture.storage.reset_observations();
                    fixture.storage.arm_fault(operation, position,
                                              FaultAction::Crash);
                    static_cast<void>(store.activate(
                        std::move(*expected.authority),
                        std::move(activation)));
                    require_recoverable_snapshot(fixture.storage,
                                                 fixture.root_bytes);
                }
            }
        }
    }
}

void require_pointer_publication_crash_boundaries() {
    const std::vector<FaultOperation> operations{
        FaultOperation::RootStageCreate,
        FaultOperation::RootStageWrite,
        FaultOperation::RootStageFlush,
        FaultOperation::RootStageClose,
        FaultOperation::RootReplace,
        FaultOperation::NamespaceDurability,
    };
    for (const auto platform : {PlatformContract::Linux,
                                PlatformContract::MacOS,
                                PlatformContract::Windows}) {
        for (const auto operation : operations) {
            for (const auto position : {FaultPosition::Before,
                                        FaultPosition::After}) {
                auto fixture = published_fixture(platform);
                auto store =
                    fixture.storage.make_overlay_store(generous_limits());
                auto expected = store.snapshot(
                    TrustedLocalOverlayReplayFloor::exact_root(
                        fixture.root_bytes));
                require(expected.status == LocalOverlayStoreStatus::Ready &&
                            expected.authority.has_value(),
                        "pointer crash fixture did not recover its expected "
                        "root");
                auto input = profile_for(
                    std::string(expected.authority->deployment_id()), 2);
                auto overlay = overlay_for(input);
                auto activation = LocalOverlayActivation::qualification(
                    std::move(input), std::move(overlay), digest('5'),
                    "2026-08-23T10:03:00Z");
                fixture.storage.reset_observations();
                fixture.storage.arm_fault(operation, position,
                                          FaultAction::Crash);
                static_cast<void>(store.activate(
                    std::move(*expected.authority), std::move(activation)));
                require_recoverable_snapshot(fixture.storage,
                                             fixture.root_bytes);
            }
        }
    }
}

} // namespace

int main() {
    require_empty_activate_and_same_deployment_visibility();
    require_stale_cas_conflicts_before_object_writes();
    require_rollback_advances_generation_and_preserves_history();
    require_rollback_can_select_another_prior_overlay();
    require_unlock_failure_withholds_authority();
    require_copied_store_and_object_corruption_fail_closed();
    require_generation_replay_is_rejected();
    require_concurrent_activators_share_one_owner_lock();
    require_object_publication_crash_boundaries();
    require_pointer_publication_crash_boundaries();
    std::cout << "durable local-overlay store passed\n";
    return 0;
}
