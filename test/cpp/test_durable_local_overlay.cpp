#include "journal_persistence_test_support.h"
#include "lemon/residency/durable_local_overlay.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <iterator>
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

struct ObservationBundle {
    std::string baseline;
    std::string baseline_sha256;
    std::string workload;
    std::string workload_sha256;
    std::string release;
    std::string release_sha256;
};

ObservationBundle observations_for(std::uint64_t sequence) {
    switch (sequence) {
    case 1:
        return {
            "baseline-observation-1",
            "9cc5353f01512b2022f25a41c1c17d459f5792d2404da3bd0394742340a17f4f",
            "workload-observation-1",
            "d078259f9ad940c0064336039b9ae8255ca86c650183754d97fe2f5dc8409464",
            "release-observation-1",
            "7335bd0c9fc732f38073ec3270d38eab1faaf46a88d9f2a7a8ad531ac338a34a",
        };
    case 2:
        return {
            "baseline-observation-2",
            "513a2a17fb2967bfb80cec18319105c37732e158c440fc3d3cc00013190e120d",
            "workload-observation-2",
            "740a6fe0be06c97e9512ab7f1bd07df3ad624de741911b4fcdee30b585caa326",
            "release-observation-2",
            "309b080a2292179e562f391869e46e85979db39d6a0765269fd5e2e8c26134d0",
        };
    case 3:
        return {
            "baseline-observation-3",
            "b3887a0d5c69ae495fd7c87514858e45e826566fa1404f89778b7128f5cbab90",
            "workload-observation-3",
            "ade37778557fe9ee679cc90e8f06841c12380cd2a67d3e2daa2513938d4e85de",
            "release-observation-3",
            "53112473662268029766049344a8c0338ab8c8c3010f652e61781f1839e4958f",
        };
    default:
        require(false, "observation fixture sequence is unsupported");
        std::abort();
    }
}

std::vector<ClaimFamilyClosure> claims_for(std::string constraint_id,
                                           std::uint64_t bytes) {
    std::vector<ClaimAmount> capacity;
    if (bytes != 0) {
        capacity.push_back(
            ClaimAmount{std::move(constraint_id), ClaimUnit::Bytes, bytes});
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

std::vector<ClaimFamilyClosure> claims(std::uint64_t bytes) {
    return claims_for("gpu/gtt", bytes);
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
        digest('3'), digest('4'), digest('5'), digest('6'), digest('7'),
        digest('8'), digest('9'), digest('a')};
}

ParsedProfilingInputEnvelope profile_for(std::string deployment_id,
                                         std::uint64_t sequence,
                                         const ObservationBundle &observations) {
    ProfilingInputEnvelopeDraft draft;
    draft.deployment_id = std::move(deployment_id);
    draft.sequence = sequence;
    draft.profiling_transaction_id =
        "profiling/" + std::to_string(sequence);
    draft.selector = selector();
    draft.generations = OverlaySourceGenerations{1, 2, 3, 4, 5, 6, 7};
    draft.attributed_claims = claims(4096);
    draft.baseline_observation_sha256 = observations.baseline_sha256;
    draft.workload_observation_sha256 = observations.workload_sha256;
    draft.release_observation_sha256 = observations.release_sha256;
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

LocalOverlayObjectDraft overlay_draft_for_claims(
    const ParsedProfilingInputEnvelope &profile,
    std::vector<ClaimFamilyClosure> bound_claims,
    std::vector<ClaimFamilyClosure> uncertainty_claims,
    std::vector<ClaimFamilyClosure> safety_margin_claims) {
    LocalOverlayObjectDraft draft;
    draft.deployment_id = std::string(profile.deployment_id());
    draft.sequence = profile.sequence();
    draft.profiling_input_sha256 = std::string(profile.checksum_sha256());
    draft.selector = profile.selector();
    draft.method = LocalOverlayMethodIdentity{
        "method/exact-profile", digest('1'),
        LocalOverlayMethodScope::DeploymentExact, OperationKind::Admission,
        std::nullopt, digest('2')};
    draft.bound_claims = std::move(bound_claims);
    draft.uncertainty_claims = std::move(uncertainty_claims);
    draft.safety_margin_claims = std::move(safety_margin_claims);
    draft.confidence_basis_points = 9900;
    draft.qualified_at = "2026-08-23T10:01:00Z";
    draft.expires_at = "2026-09-23T10:01:00Z";
    draft.status = LocalOverlayObjectStatus::Qualified;
    draft.decision_trace_sha256 = digest('3');
    return draft;
}

ParsedLocalOverlayObject overlay_for_claims(
    const ParsedProfilingInputEnvelope &profile,
    std::vector<ClaimFamilyClosure> bound_claims,
    std::vector<ClaimFamilyClosure> uncertainty_claims,
    std::vector<ClaimFamilyClosure> safety_margin_claims) {
    auto sealed = seal_local_overlay(overlay_draft_for_claims(
        profile, std::move(bound_claims), std::move(uncertainty_claims),
        std::move(safety_margin_claims)));
    require(sealed.accepted(), sealed.diagnostic);
    return std::move(*sealed.candidate);
}

ParsedLocalOverlayObject overlay_for(
    const ParsedProfilingInputEnvelope &profile) {
    return overlay_for_claims(profile, claims(4096), claims(512), claims(256));
}

ParsedOverlayActivationRoot root_for(
    std::string deployment_id, std::uint64_t generation,
    std::optional<std::string> previous_root_sha256,
    const ParsedLocalOverlayObject &overlay, std::uint64_t high_water,
    char decision_trace, std::string activated_at) {
    OverlayActivationRootDraft draft;
    draft.deployment_id = std::move(deployment_id);
    draft.generation = generation;
    draft.previous_root_sha256 = std::move(previous_root_sha256);
    draft.transition = OverlayRootTransition::Qualification;
    draft.selected_overlay_sha256 =
        std::string(overlay.checksum_sha256());
    draft.selected_overlay_sequence = overlay.sequence();
    draft.sequence_high_water = high_water;
    draft.selected_selector_sha256 =
        std::string(overlay.selector_sha256());
    draft.selected_method_sha256 = std::string(overlay.method_sha256());
    draft.decision_trace_sha256 = digest(decision_trace);
    draft.activated_at = std::move(activated_at);
    draft.expires_at = std::string(overlay.expires_at());
    auto sealed = seal_overlay_activation_root(std::move(draft));
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
activate_qualification(LocalOverlayStore &store,
                       PublishedLocalOverlay &&expected,
                       ParsedProfilingInputEnvelope profile,
                       ParsedLocalOverlayObject overlay,
                       ObservationBundle observations, char decision_trace,
                       std::string activated_at) {
    return store.activate(
        std::move(expected),
        LocalOverlayActivation::qualification(
            std::move(profile), std::move(overlay),
            std::move(observations.baseline),
            std::move(observations.workload),
            std::move(observations.release), digest(decision_trace),
            std::move(activated_at)));
}

LocalOverlayStoreResult
qualify(LocalOverlayStore &store, PublishedLocalOverlay &&expected,
        std::uint64_t sequence, char decision_trace,
        std::string activated_at) {
    const auto deployment_id = std::string(expected.deployment_id());
    auto observations = observations_for(sequence);
    auto profile = profile_for(deployment_id, sequence, observations);
    auto overlay = overlay_for(profile);
    return activate_qualification(
        store, std::move(expected), std::move(profile), std::move(overlay),
        std::move(observations), decision_trace, std::move(activated_at));
}

struct PublishedFixture {
    JournalTestStorage storage;
    std::string root_bytes;
    std::string root_sha256;
    std::string overlay_sha256;
    std::string overlay_bytes;
    std::string deployment_id;
    ObservationBundle observations;
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
        observations_for(1),
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
    auto observations = observations_for(1);
    auto profile = profile_for(deployment_id, 1, observations);
    auto overlay = overlay_for(profile);
    const auto overlay_sha256 = std::string(overlay.checksum_sha256());
    auto activation = LocalOverlayActivation::qualification(
        std::move(profile), std::move(overlay), observations.baseline,
        observations.workload, observations.release, digest('4'),
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
                activated.authority->active_baseline_observation_bytes() ==
                    observations.baseline &&
                activated.authority->active_workload_observation_bytes() ==
                    observations.workload &&
                activated.authority->active_release_observation_bytes() ==
                    observations.release &&
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
                visible.authority->active_profiling_input() != nullptr &&
                visible.authority->active_baseline_observation_bytes() ==
                    observations.baseline &&
                visible.authority->active_workload_observation_bytes() ==
                    observations.workload &&
                visible.authority->active_release_observation_bytes() ==
                    observations.release,
            "same-deployment reader did not observe one complete snapshot");
}

void require_fake_fixed_namespace_replacement_fails_closed() {
    for (const auto platform : {PlatformContract::Linux,
                                PlatformContract::MacOS}) {
        auto storage = JournalTestStorage::fresh(platform);
        {
            auto stale_store = storage.make_overlay_store(generous_limits());
            auto initial = stale_store.snapshot(
                TrustedLocalOverlayReplayFloor::uninitialized());
            require(initial.status == LocalOverlayStoreStatus::Empty &&
                        initial.authority.has_value(),
                    "fake fixed namespace did not start empty");
            const auto stale_deployment_id =
                std::string(initial.authority->deployment_id());

            std::optional<LocalOverlayStoreResult> stale;
            storage.pause_after(FaultOperation::AuthorityIdentityRead);
            std::thread stale_reader([&] {
                stale.emplace(stale_store.snapshot(
                    TrustedLocalOverlayReplayFloor::uninitialized()));
            });
            require(storage.wait_until_paused(5000),
                    "fake stale reader did not pause with authority locked");
            storage.rename_and_replace_authority_directory();
            storage.release_pause();
            stale_reader.join();
            require(stale.has_value() &&
                        stale->status ==
                            LocalOverlayStoreStatus::RecoveryRequired &&
                        !stale->authority.has_value(),
                    "fake stale namespace binding retained authority after "
                    "replacement");

            auto replacement_store =
                storage.make_overlay_store(generous_limits());
            auto replacement = replacement_store.snapshot(
                TrustedLocalOverlayReplayFloor::uninitialized());
            require(replacement.status == LocalOverlayStoreStatus::Empty &&
                        replacement.authority.has_value() &&
                        replacement.authority->deployment_id() !=
                            stale_deployment_id,
                    "fake replacement namespace did not establish new "
                    "authority");
        }
        require(storage.snapshot().open_handles.empty(),
                "fake namespace replacement leaked adapter handles");
    }
}

void require_native_fixed_namespace_lifetime_binding() {
#ifdef __linux__
    TemporaryCache cache;
    const auto original_parent = cache.path() / "cache-original";
    const auto renamed_parent = cache.path() / "cache-renamed";
    std::filesystem::create_directory(original_parent);
    const auto open_descriptor_count = [] {
        return static_cast<std::size_t>(std::distance(
            std::filesystem::directory_iterator("/proc/self/fd"),
            std::filesystem::directory_iterator{}));
    };
    const auto descriptors_before = open_descriptor_count();
    {
        auto stale_store =
            LocalOverlayStore::native(original_parent, generous_limits());
        auto initial = stale_store.snapshot(
            TrustedLocalOverlayReplayFloor::uninitialized());
        require(initial.status == LocalOverlayStoreStatus::Empty &&
                    initial.authority.has_value(),
                "native fixed namespace did not start empty");
        const auto deployment_id =
            std::string(initial.authority->deployment_id());

        std::filesystem::rename(original_parent, renamed_parent);
        auto after_parent_rename = stale_store.snapshot(
            TrustedLocalOverlayReplayFloor::uninitialized());
        require(after_parent_rename.status == LocalOverlayStoreStatus::Empty &&
                    after_parent_rename.authority.has_value() &&
                    after_parent_rename.authority->deployment_id() ==
                        deployment_id,
                "renaming the bound parent changed deployment authority");

        const auto child = LocalOverlayStore::namespace_path(renamed_parent);
        const auto retired_child = renamed_parent / "retired-local-overlay";
        std::filesystem::rename(child, retired_child);
        std::filesystem::create_directory(child);

        auto replacement_store =
            LocalOverlayStore::native(renamed_parent, generous_limits());
        auto replacement = replacement_store.snapshot(
            TrustedLocalOverlayReplayFloor::uninitialized());
        require(replacement.status == LocalOverlayStoreStatus::Empty &&
                    replacement.authority.has_value() &&
                    replacement.authority->deployment_id() != deployment_id,
                "replacement namespace did not establish independent "
                "authority");

        auto stale = stale_store.snapshot(
            TrustedLocalOverlayReplayFloor::uninitialized());
        require(stale.status == LocalOverlayStoreStatus::UnsupportedStorage &&
                    !stale.authority.has_value(),
                "stale native namespace binding retained authority after "
                "replacement");
    }
    require(open_descriptor_count() == descriptors_before,
            "native fixed namespace adapters leaked directory handles");
#endif
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

void require_full_history_rejects_qualification_before_writes() {
    auto storage = JournalTestStorage::fresh();
    const LocalOverlayStoreLimits limits{64 * 1024, 1};
    auto store = storage.make_overlay_store(limits);
    auto empty = store.snapshot(TrustedLocalOverlayReplayFloor::uninitialized());
    require(empty.status == LocalOverlayStoreStatus::Empty &&
                empty.authority.has_value(),
            "history-limit fixture did not start empty");

    auto first = qualify(store, std::move(*empty.authority), 1, '4',
                         "2026-08-23T10:02:00Z");
    require(first.status == LocalOverlayStoreStatus::Activated &&
                first.authority.has_value(),
            "history-limit fixture did not publish its only root");
    const auto first_root = std::string(first.authority->root_bytes());

    storage.reset_observations();
    auto rejected = qualify(store, std::move(*first.authority), 2, '5',
                            "2026-08-23T10:04:00Z");
    const auto after = storage.snapshot();
    require(rejected.status == LocalOverlayStoreStatus::LimitExceeded &&
                !rejected.authority.has_value() &&
                after.authority_root_bytes == first_root &&
                !after.authority_mutation_attempted,
            "full history admitted a qualification before rejecting it");

    auto reader = storage.make_overlay_store(limits);
    auto replayed = reader.snapshot(
        TrustedLocalOverlayReplayFloor::exact_root(first_root));
    require(replayed.status == LocalOverlayStoreStatus::Ready &&
                replayed.authority.has_value() &&
                replayed.authority->generation() == 1,
            "rejected qualification made the full history unreadable");
}

void require_full_history_rejects_rollback_before_writes() {
    auto storage = JournalTestStorage::fresh();
    const LocalOverlayStoreLimits limits{64 * 1024, 2};
    auto store = storage.make_overlay_store(limits);
    auto empty = store.snapshot(TrustedLocalOverlayReplayFloor::uninitialized());
    require(empty.status == LocalOverlayStoreStatus::Empty &&
                empty.authority.has_value(),
            "rollback history-limit fixture did not start empty");

    auto first = qualify(store, std::move(*empty.authority), 1, '4',
                         "2026-08-23T10:02:00Z");
    require(first.status == LocalOverlayStoreStatus::Activated &&
                first.authority.has_value(),
            "rollback history-limit fixture did not publish sequence one");
    const auto first_root = std::string(first.authority->root_bytes());
    const auto first_overlay =
        std::string(first.authority->active_overlay()->checksum_sha256());
    auto second = qualify(store, std::move(*first.authority), 2, '5',
                          "2026-08-23T10:04:00Z");
    require(second.status == LocalOverlayStoreStatus::Activated &&
                second.authority.has_value(),
            "rollback history-limit fixture did not fill its history");
    const auto second_root = std::string(second.authority->root_bytes());

    storage.reset_observations();
    auto rejected = store.activate(
        std::move(*second.authority),
        LocalOverlayActivation::rollback(first_overlay, digest('6'),
                                         "2026-08-23T10:05:00Z"));
    const auto after = storage.snapshot();
    require(rejected.status == LocalOverlayStoreStatus::LimitExceeded &&
                !rejected.authority.has_value() &&
                after.authority_root_bytes == second_root &&
                !after.authority_mutation_attempted,
            "full history admitted a rollback before rejecting it");

    auto reader = storage.make_overlay_store(limits);
    auto replayed = reader.snapshot(
        TrustedLocalOverlayReplayFloor::exact_root(first_root));
    require(replayed.status == LocalOverlayStoreStatus::Ready &&
                replayed.authority.has_value() &&
                replayed.authority->root_bytes() == second_root &&
                replayed.authority->generation() == 2,
            "rejected rollback made the full history unreadable");
}

void require_observation_admission_fails_before_writes() {
    for (const auto failure : {LocalOverlayStoreStatus::MissingObject,
                               LocalOverlayStoreStatus::DigestMismatch,
                               LocalOverlayStoreStatus::LimitExceeded}) {
        for (const auto observation_index : {0, 1, 2}) {
            auto storage = JournalTestStorage::fresh();
            auto store = storage.make_overlay_store(generous_limits());
            auto empty = store.snapshot(
                TrustedLocalOverlayReplayFloor::uninitialized());
            require(empty.status == LocalOverlayStoreStatus::Empty &&
                        empty.authority.has_value(),
                    "observation admission fixture did not start empty");

            auto observations = observations_for(1);
            auto profile = profile_for(
                std::string(empty.authority->deployment_id()), 1,
                observations);
            auto overlay = overlay_for(profile);
            const std::array<std::string *, 3> supplied_bytes{
                &observations.baseline,
                &observations.workload,
                &observations.release,
            };
            if (failure == LocalOverlayStoreStatus::MissingObject) {
                supplied_bytes[observation_index]->clear();
            } else if (failure == LocalOverlayStoreStatus::DigestMismatch) {
                *supplied_bytes[observation_index] += "-wrong";
            } else {
                supplied_bytes[observation_index]->assign(
                    generous_limits().max_object_bytes + 1, 'x');
            }

            storage.reset_observations();
            auto rejected = activate_qualification(
                store, std::move(*empty.authority), std::move(profile),
                std::move(overlay), std::move(observations), '4',
                "2026-08-23T10:02:00Z");
            const auto after = storage.snapshot();
            require(rejected.status == failure &&
                        !rejected.authority.has_value() &&
                        after.authority_root_bytes.empty() &&
                        !after.authority_mutation_attempted,
                    "invalid observation bytes caused an object or root "
                    "write");
        }
    }
}

void require_claim_admission_fails_before_writes() {
    for (const bool unrelated_safety_margin : {false, true}) {
        auto storage = JournalTestStorage::fresh();
        auto store = storage.make_overlay_store(generous_limits());
        auto empty =
            store.snapshot(TrustedLocalOverlayReplayFloor::uninitialized());
        require(empty.status == LocalOverlayStoreStatus::Empty &&
                    empty.authority.has_value(),
                "claim admission fixture did not start empty");

        auto observations = observations_for(1);
        auto profile = profile_for(
            std::string(empty.authority->deployment_id()), 1, observations);
        auto overlay = unrelated_safety_margin
                           ? overlay_for_claims(
                                 profile, claims(4096), claims(512),
                                 claims_for("host/ram", 256))
                           : overlay_for_claims(
                                 profile, claims(3000), claims(400),
                                 claims(200));

        storage.reset_observations();
        auto rejected = activate_qualification(
            store, std::move(*empty.authority), std::move(profile),
            std::move(overlay), std::move(observations), '4',
            "2026-08-23T10:02:00Z");
        const auto after = storage.snapshot();
        require(rejected.status ==
                        LocalOverlayStoreStatus::CorruptOrRollback &&
                    !rejected.authority.has_value() &&
                    after.authority_root_bytes.empty() &&
                    !after.authority_mutation_attempted,
                "unsafe claim envelope caused an object or root write");
    }

    auto storage = JournalTestStorage::fresh();
    auto store = storage.make_overlay_store(generous_limits());
    auto empty = store.snapshot(TrustedLocalOverlayReplayFloor::uninitialized());
    require(empty.status == LocalOverlayStoreStatus::Empty &&
                empty.authority.has_value(),
            "claim completeness fixture did not start empty");
    auto observations = observations_for(1);
    auto profile = profile_for(
        std::string(empty.authority->deployment_id()), 1, observations);
    auto bound = claims(4096);
    auto uncertainty = claims(512);
    auto safety_margin = claims(256);
    for (auto *closure : {&bound, &uncertainty, &safety_margin}) {
        (*closure)[1].completeness = ClaimCompleteness::NotApplicable;
    }
    auto overlay = overlay_for_claims(
        profile, std::move(bound), std::move(uncertainty),
        std::move(safety_margin));
    storage.reset_observations();
    auto rejected = activate_qualification(
        store, std::move(*empty.authority), std::move(profile),
        std::move(overlay), std::move(observations), '4',
        "2026-08-23T10:02:00Z");
    const auto after = storage.snapshot();
    require(rejected.status == LocalOverlayStoreStatus::CorruptOrRollback &&
                !rejected.authority.has_value() &&
                !after.authority_mutation_attempted,
            "not-applicable conservative claims covered an applicable "
            "attributed family");
}

void require_qualification_at_fresh_until_fails_before_writes() {
    auto storage = JournalTestStorage::fresh();
    auto store = storage.make_overlay_store(generous_limits());
    auto empty = store.snapshot(TrustedLocalOverlayReplayFloor::uninitialized());
    require(empty.status == LocalOverlayStoreStatus::Empty &&
                empty.authority.has_value(),
            "freshness-boundary fixture did not start empty");

    auto observations = observations_for(1);
    auto profile = profile_for(
        std::string(empty.authority->deployment_id()), 1, observations);
    const auto freshness_boundary = std::string(profile.fresh_until());
    auto overlay_draft = overlay_draft_for_claims(
        profile, claims(4096), claims(512), claims(256));
    overlay_draft.qualified_at = freshness_boundary;
    auto sealed_overlay = seal_local_overlay(std::move(overlay_draft));
    require(sealed_overlay.accepted(),
            "freshness-boundary overlay was not canonical");

    storage.reset_observations();
    auto rejected = activate_qualification(
        store, std::move(*empty.authority), std::move(profile),
        std::move(*sealed_overlay.candidate), std::move(observations), '4',
        freshness_boundary);
    const auto after = storage.snapshot();
    require(rejected.status == LocalOverlayStoreStatus::CorruptOrRollback &&
                !rejected.authority.has_value() &&
                after.authority_root_bytes.empty() &&
                !after.authority_mutation_attempted,
            "qualification at fresh_until caused an object or root write");
}

void require_unreclosable_claims_cannot_seal_or_activate() {
    auto storage = JournalTestStorage::fresh();
    auto store = storage.make_overlay_store(generous_limits());
    auto empty = store.snapshot(TrustedLocalOverlayReplayFloor::uninitialized());
    require(empty.status == LocalOverlayStoreStatus::Empty &&
                empty.authority.has_value(),
            "unreclosable-claim fixture did not start empty");

    auto observations = observations_for(1);
    auto profile = profile_for(
        std::string(empty.authority->deployment_id()), 1, observations);
    storage.reset_observations();
    auto sealed = seal_local_overlay(overlay_draft_for_claims(
        profile, compatibility_claims(true), compatibility_claims(false),
        compatibility_claims(true)));
    const auto after = storage.snapshot();
    require(!sealed.accepted() && !sealed.candidate.has_value() &&
                sealed.status == OverlayContractStatus::InvalidClaimClosure &&
                after.authority_root_bytes.empty() &&
                !after.authority_mutation_attempted,
            "unreclosable conservative claims reached durable activation");
}

void require_same_observation_digest_is_deduplicated() {
    auto storage = JournalTestStorage::fresh();
    auto store = storage.make_overlay_store(generous_limits());
    auto empty = store.snapshot(TrustedLocalOverlayReplayFloor::uninitialized());
    require(empty.status == LocalOverlayStoreStatus::Empty &&
                empty.authority.has_value(),
            "observation deduplication fixture did not start empty");

    ObservationBundle observations{
        "shared-observation",
        "35566907aa5c6cce8e1c57026f44acdbe3944b18149430be57d257dd78e495e7",
        "shared-observation",
        "35566907aa5c6cce8e1c57026f44acdbe3944b18149430be57d257dd78e495e7",
        "shared-observation",
        "35566907aa5c6cce8e1c57026f44acdbe3944b18149430be57d257dd78e495e7",
    };
    auto profile = profile_for(
        std::string(empty.authority->deployment_id()), 1, observations);
    auto overlay = overlay_for(profile);
    storage.reset_observations();
    auto activated = activate_qualification(
        store, std::move(*empty.authority), std::move(profile),
        std::move(overlay), observations, '4', "2026-08-23T10:02:00Z");
    require(activated.status == LocalOverlayStoreStatus::Activated &&
                activated.authority.has_value() &&
                activated.authority->active_baseline_observation_bytes() ==
                    "shared-observation" &&
                activated.authority->active_workload_observation_bytes() ==
                    "shared-observation" &&
                activated.authority->active_release_observation_bytes() ==
                    "shared-observation" &&
                storage.attempt_count(FaultOperation::ObjectPublish) == 4,
            "equal observation digests did not reuse one immutable object");
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
    const auto first_observations = observations_for(1);

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
                rolled_back.authority->active_overlay()->sequence() == 1 &&
                rolled_back.authority
                        ->active_baseline_observation_bytes() ==
                    first_observations.baseline &&
                rolled_back.authority
                        ->active_workload_observation_bytes() ==
                    first_observations.workload &&
                rolled_back.authority->active_release_observation_bytes() ==
                    first_observations.release,
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

    for (const auto missing_observation : {0, 1, 2}) {
        auto fixture = published_fixture();
        const std::array<std::string, 3> observation_sha256{
            fixture.observations.baseline_sha256,
            fixture.observations.workload_sha256,
            fixture.observations.release_sha256,
        };
        fixture.storage.remove_immutable_object(
            observation_sha256[missing_observation]);
        auto store = fixture.storage.make_overlay_store(generous_limits());
        auto result = store.snapshot(
            TrustedLocalOverlayReplayFloor::exact_root(fixture.root_bytes));
        require(result.status == LocalOverlayStoreStatus::MissingObject &&
                    !result.authority.has_value(),
                "missing observation did not fail closed");
    }

    for (const auto corrupt_observation : {0, 1, 2}) {
        auto fixture = published_fixture();
        const std::array<std::string, 3> observation_sha256{
            fixture.observations.baseline_sha256,
            fixture.observations.workload_sha256,
            fixture.observations.release_sha256,
        };
        fixture.storage.overwrite_immutable_object(
            observation_sha256[corrupt_observation], "corrupt-observation");
        auto store = fixture.storage.make_overlay_store(generous_limits());
        auto result = store.snapshot(
            TrustedLocalOverlayReplayFloor::exact_root(fixture.root_bytes));
        require(result.status == LocalOverlayStoreStatus::DigestMismatch &&
                    !result.authority.has_value(),
                "same-address observation corruption was not distinguished");
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

void require_historic_observation_corruption_fails_closed() {
    for (const bool remove_observation : {false, true}) {
        auto storage = JournalTestStorage::fresh();
        auto store = storage.make_overlay_store(generous_limits());
        auto empty =
            store.snapshot(TrustedLocalOverlayReplayFloor::uninitialized());
        require(empty.status == LocalOverlayStoreStatus::Empty &&
                    empty.authority.has_value(),
                "historic observation fixture did not start empty");
        auto first = qualify(store, std::move(*empty.authority), 1, '4',
                             "2026-08-23T10:02:00Z");
        require(first.status == LocalOverlayStoreStatus::Activated &&
                    first.authority.has_value(),
                "historic observation fixture did not publish generation one");
        const auto first_root = std::string(first.authority->root_bytes());
        auto second = qualify(store, std::move(*first.authority), 2, '5',
                              "2026-08-23T10:04:00Z");
        require(second.status == LocalOverlayStoreStatus::Activated &&
                    second.authority.has_value() &&
                    second.authority->generation() == 2,
                "historic observation fixture did not publish generation two");

        const auto generation_one = observations_for(1);
        if (remove_observation) {
            storage.remove_immutable_object(
                generation_one.baseline_sha256);
        } else {
            storage.overwrite_immutable_object(
                generation_one.baseline_sha256, "corrupt-historic-observation");
        }
        auto reader = storage.make_overlay_store(generous_limits());
        auto replayed = reader.snapshot(
            TrustedLocalOverlayReplayFloor::exact_root(first_root));
        require(replayed.status ==
                        (remove_observation
                             ? LocalOverlayStoreStatus::MissingObject
                             : LocalOverlayStoreStatus::DigestMismatch) &&
                    !replayed.authority.has_value(),
                "generation two replay ignored a corrupt generation-one "
                "observation");
    }
}

void require_historic_under_bound_overlay_fails_closed() {
    auto storage = JournalTestStorage::fresh();
    auto store = storage.make_overlay_store(generous_limits());
    auto empty = store.snapshot(TrustedLocalOverlayReplayFloor::uninitialized());
    require(empty.status == LocalOverlayStoreStatus::Empty &&
                empty.authority.has_value(),
            "historic claim fixture did not start empty");
    const auto deployment_id = std::string(empty.authority->deployment_id());

    const auto first_observations = observations_for(1);
    auto first_input = profile_for(deployment_id, 1, first_observations);
    auto first_overlay = overlay_for_claims(
        first_input, claims(3000), claims(400), claims(200));
    auto first_root = root_for(deployment_id, 1, std::nullopt, first_overlay, 1,
                               '4', "2026-08-23T10:02:00Z");

    const auto second_observations = observations_for(2);
    auto second_input = profile_for(deployment_id, 2, second_observations);
    auto second_overlay = overlay_for(second_input);
    auto second_root = root_for(
        deployment_id, 2, std::string(first_root.checksum_sha256()),
        second_overlay, 2, '5', "2026-08-23T10:04:00Z");

    auto persist_node = [&](const ObservationBundle &observations,
                            const ParsedProfilingInputEnvelope &input,
                            const ParsedLocalOverlayObject &overlay,
                            const ParsedOverlayActivationRoot &root) {
        storage.overwrite_immutable_object(observations.baseline_sha256,
                                           observations.baseline);
        storage.overwrite_immutable_object(observations.workload_sha256,
                                           observations.workload);
        storage.overwrite_immutable_object(observations.release_sha256,
                                           observations.release);
        storage.overwrite_immutable_object(
            std::string(input.checksum_sha256()),
            std::string(input.canonical_bytes()));
        storage.overwrite_immutable_object(
            std::string(overlay.checksum_sha256()),
            std::string(overlay.canonical_bytes()));
        storage.overwrite_immutable_object(
            std::string(root.checksum_sha256()),
            std::string(root.canonical_bytes()));
    };
    persist_node(first_observations, first_input, first_overlay, first_root);
    persist_node(second_observations, second_input, second_overlay, second_root);
    storage.overwrite_fixed_child_bytes(
        FixedAuthorityChild::Root,
        std::string(second_root.canonical_bytes()));

    auto reader = storage.make_overlay_store(generous_limits());
    auto replayed = reader.snapshot(TrustedLocalOverlayReplayFloor::exact_root(
        std::string(first_root.canonical_bytes())));
    require(replayed.status == LocalOverlayStoreStatus::CorruptOrRollback &&
                !replayed.authority.has_value(),
            "generation two replay accepted a historic under-bound overlay");
}

void require_historic_freshness_boundary_fails_closed() {
    auto storage = JournalTestStorage::fresh();
    auto store = storage.make_overlay_store(generous_limits());
    auto empty = store.snapshot(TrustedLocalOverlayReplayFloor::uninitialized());
    require(empty.status == LocalOverlayStoreStatus::Empty &&
                empty.authority.has_value(),
            "historic freshness-boundary fixture did not start empty");
    const auto deployment_id = std::string(empty.authority->deployment_id());

    const auto first_observations = observations_for(1);
    auto first_input = profile_for(deployment_id, 1, first_observations);
    auto first_overlay_draft = overlay_draft_for_claims(
        first_input, claims(4096), claims(512), claims(256));
    first_overlay_draft.qualified_at =
        std::string(first_input.fresh_until());
    auto sealed_first_overlay =
        seal_local_overlay(std::move(first_overlay_draft));
    require(sealed_first_overlay.accepted(),
            "historic freshness-boundary overlay was not canonical");
    auto first_overlay = std::move(*sealed_first_overlay.candidate);
    auto first_root = root_for(deployment_id, 1, std::nullopt, first_overlay, 1,
                               '4', "2026-08-23T10:05:00Z");

    const auto second_observations = observations_for(2);
    auto second_input = profile_for(deployment_id, 2, second_observations);
    auto second_overlay = overlay_for(second_input);
    auto second_root = root_for(
        deployment_id, 2, std::string(first_root.checksum_sha256()),
        second_overlay, 2, '5', "2026-08-23T10:06:00Z");

    auto persist_node = [&](const ObservationBundle &observations,
                            const ParsedProfilingInputEnvelope &input,
                            const ParsedLocalOverlayObject &overlay,
                            const ParsedOverlayActivationRoot &root) {
        storage.overwrite_immutable_object(observations.baseline_sha256,
                                           observations.baseline);
        storage.overwrite_immutable_object(observations.workload_sha256,
                                           observations.workload);
        storage.overwrite_immutable_object(observations.release_sha256,
                                           observations.release);
        storage.overwrite_immutable_object(
            std::string(input.checksum_sha256()),
            std::string(input.canonical_bytes()));
        storage.overwrite_immutable_object(
            std::string(overlay.checksum_sha256()),
            std::string(overlay.canonical_bytes()));
        storage.overwrite_immutable_object(
            std::string(root.checksum_sha256()),
            std::string(root.canonical_bytes()));
    };
    persist_node(first_observations, first_input, first_overlay, first_root);
    persist_node(second_observations, second_input, second_overlay, second_root);
    storage.overwrite_fixed_child_bytes(
        FixedAuthorityChild::Root,
        std::string(second_root.canonical_bytes()));

    auto reader = storage.make_overlay_store(generous_limits());
    auto replayed = reader.snapshot(TrustedLocalOverlayReplayFloor::exact_root(
        std::string(first_root.canonical_bytes())));
    require(replayed.status == LocalOverlayStoreStatus::CorruptOrRollback &&
                !replayed.authority.has_value(),
            "fresh snapshot accepted a historic overlay at fresh_until");
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

enum class CrashObjectTarget {
    BaselineObservation,
    WorkloadObservation,
    ReleaseObservation,
    ProfilingInput,
    Overlay,
    Root,
};

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
                !recovered.authority
                     ->active_baseline_observation_bytes()
                     .empty() &&
                !recovered.authority
                     ->active_workload_observation_bytes()
                     .empty() &&
                !recovered.authority
                     ->active_release_observation_bytes()
                     .empty() &&
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
        for (const auto target : {CrashObjectTarget::BaselineObservation,
                                  CrashObjectTarget::WorkloadObservation,
                                  CrashObjectTarget::ReleaseObservation,
                                  CrashObjectTarget::ProfilingInput,
                                  CrashObjectTarget::Overlay,
                                  CrashObjectTarget::Root}) {
            for (const auto operation : operations) {
                if (platform == PlatformContract::Windows &&
                    operation == FaultOperation::ObjectStageCleanup) {
                    continue;
                }
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

                    auto observations = observations_for(2);
                    auto input = profile_for(
                        std::string(expected.authority->deployment_id()), 2,
                        observations);
                    auto overlay = overlay_for(input);
                    if (target != CrashObjectTarget::BaselineObservation) {
                        fixture.storage.overwrite_immutable_object(
                            observations.baseline_sha256,
                            observations.baseline);
                    }
                    if (target != CrashObjectTarget::BaselineObservation &&
                        target != CrashObjectTarget::WorkloadObservation) {
                        fixture.storage.overwrite_immutable_object(
                            observations.workload_sha256,
                            observations.workload);
                    }
                    if (target == CrashObjectTarget::ProfilingInput ||
                        target == CrashObjectTarget::Overlay ||
                        target == CrashObjectTarget::Root) {
                        fixture.storage.overwrite_immutable_object(
                            observations.release_sha256,
                            observations.release);
                    }
                    if (target == CrashObjectTarget::Overlay ||
                        target == CrashObjectTarget::Root) {
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
                        std::move(input), std::move(overlay),
                        std::move(observations.baseline),
                        std::move(observations.workload),
                        std::move(observations.release), digest('5'),
                        "2026-08-23T10:03:00Z");
                    fixture.storage.reset_observations();
                    fixture.storage.arm_fault(operation, position,
                                              FaultAction::Crash);
                    static_cast<void>(store.activate(
                        std::move(*expected.authority),
                        std::move(activation)));
                    require(fixture.storage.attempt_count(operation) != 0,
                            "requested object-publication crash point was not "
                            "observed");
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
                auto observations = observations_for(2);
                auto input = profile_for(
                    std::string(expected.authority->deployment_id()), 2,
                    observations);
                auto overlay = overlay_for(input);
                auto activation = LocalOverlayActivation::qualification(
                    std::move(input), std::move(overlay),
                    std::move(observations.baseline),
                    std::move(observations.workload),
                    std::move(observations.release), digest('5'),
                    "2026-08-23T10:03:00Z");
                fixture.storage.reset_observations();
                fixture.storage.arm_fault(operation, position,
                                          FaultAction::Crash);
                static_cast<void>(store.activate(
                    std::move(*expected.authority), std::move(activation)));
                require(fixture.storage.attempt_count(operation) != 0,
                        "requested pointer-publication crash point was not "
                        "observed");
                require_recoverable_snapshot(fixture.storage,
                                             fixture.root_bytes);
            }
        }
    }
}

} // namespace

int main() {
    require_empty_activate_and_same_deployment_visibility();
    require_fake_fixed_namespace_replacement_fails_closed();
    require_native_fixed_namespace_lifetime_binding();
    require_stale_cas_conflicts_before_object_writes();
    require_full_history_rejects_qualification_before_writes();
    require_full_history_rejects_rollback_before_writes();
    require_observation_admission_fails_before_writes();
    require_claim_admission_fails_before_writes();
    require_qualification_at_fresh_until_fails_before_writes();
    require_unreclosable_claims_cannot_seal_or_activate();
    require_same_observation_digest_is_deduplicated();
    require_rollback_advances_generation_and_preserves_history();
    require_rollback_can_select_another_prior_overlay();
    require_unlock_failure_withholds_authority();
    require_copied_store_and_object_corruption_fail_closed();
    require_historic_observation_corruption_fails_closed();
    require_historic_under_bound_overlay_fails_closed();
    require_historic_freshness_boundary_fails_closed();
    require_generation_replay_is_rejected();
    require_concurrent_activators_share_one_owner_lock();
    require_object_publication_crash_boundaries();
    require_pointer_publication_crash_boundaries();
    std::cout << "durable local-overlay store passed\n";
    return 0;
}
