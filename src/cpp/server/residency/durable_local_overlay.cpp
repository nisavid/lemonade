#include "lemon/residency/durable_local_overlay.h"

#include "authority_fence.h"
#include "platform/durable_file_adapter.h"

#ifdef LEMONADE_RESIDENCY_DURABLE_TESTING
#include "durable_local_overlay_test_factory.h"
#endif

#include <mbedtls/md.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace lemon::residency {

namespace {

constexpr char deployment_identity_domain[] =
    "lemonade.residency.local-overlay-deployment/v1\0";

LocalOverlayStoreResult store_result(LocalOverlayStoreStatus status) {
    return {status, std::nullopt};
}

using Sha256Digest = std::array<unsigned char, 32>;

std::optional<Sha256Digest>
sha256_digest(std::initializer_list<std::string_view> parts) {
    const auto *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == nullptr) {
        return std::nullopt;
    }

    mbedtls_md_context_t context;
    mbedtls_md_init(&context);
    Sha256Digest digest{};
    bool failed = mbedtls_md_setup(&context, info, 0) != 0 ||
                  mbedtls_md_starts(&context) != 0;
    for (const auto part : parts) {
        if (!failed &&
            mbedtls_md_update(
                &context,
                reinterpret_cast<const unsigned char *>(part.data()),
                part.size()) != 0) {
            failed = true;
        }
    }
    if (!failed && mbedtls_md_finish(&context, digest.data()) != 0) {
        failed = true;
    }
    mbedtls_md_free(&context);
    if (failed) {
        return std::nullopt;
    }
    return digest;
}

std::string hex_encode(const Sha256Digest &digest) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const auto byte : digest) {
        result.push_back(hex[(byte >> 4) & 0x0f]);
        result.push_back(hex[byte & 0x0f]);
    }
    return result;
}

std::optional<std::string>
deployment_id_for_storage(std::string_view storage_identity) {
    const auto directory = detail::directory_identity(storage_identity);
    const auto digest = sha256_digest(
        {std::string_view(deployment_identity_domain,
                          sizeof(deployment_identity_domain) - 1),
         directory});
    return digest.has_value() ? std::optional<std::string>(hex_encode(*digest))
                              : std::nullopt;
}

std::optional<std::string> raw_sha256(std::string_view bytes) {
    const auto digest = sha256_digest({bytes});
    return digest.has_value() ? std::optional<std::string>(hex_encode(*digest))
                              : std::nullopt;
}

LocalOverlayStoreStatus
contract_failure_status(OverlayContractStatus status) noexcept {
    switch (status) {
    case OverlayContractStatus::InputTooLarge:
    case OverlayContractStatus::LimitExceeded:
        return LocalOverlayStoreStatus::LimitExceeded;
    case OverlayContractStatus::DigestMismatch:
        return LocalOverlayStoreStatus::DigestMismatch;
    default:
        return LocalOverlayStoreStatus::CorruptOrRollback;
    }
}

LocalOverlayStoreStatus
read_failure_status(const detail::DurableReadResult &read,
                    LocalOverlayStoreStatus missing_status) noexcept {
    if (read.result.status == detail::DurableFileStatus::NotFound) {
        return missing_status;
    }
    if (read.result.effect_may_have_occurred()) {
        return LocalOverlayStoreStatus::RecoveryRequired;
    }
    return LocalOverlayStoreStatus::UnsupportedStorage;
}

struct LoadedOverlayNode {
    ParsedOverlayActivationRoot root;
    ParsedLocalOverlayObject overlay;
    ParsedProfilingInputEnvelope profiling_input;
    std::string baseline_observation_bytes;
    std::string workload_observation_bytes;
    std::string release_observation_bytes;
};

struct LoadedOverlayGraph {
    std::string storage_identity;
    std::string deployment_id;
    std::string root_bytes;
    std::vector<LoadedOverlayNode> newest_first;
};

struct GraphLoadResult {
    LocalOverlayStoreStatus status =
        LocalOverlayStoreStatus::CorruptOrRollback;
    std::optional<LoadedOverlayGraph> graph;
};

bool qualification_claims_are_safe(
    const ParsedProfilingInputEnvelope &input,
    const ParsedLocalOverlayObject &overlay);

bool node_references_match(const LoadedOverlayNode &node,
                           std::string_view deployment_id) {
    const auto &root = node.root;
    const auto &overlay = node.overlay;
    const auto &input = node.profiling_input;
    return root.deployment_id() == deployment_id &&
           overlay.deployment_id() == deployment_id &&
           input.deployment_id() == deployment_id &&
           root.selected_overlay_sha256() == overlay.checksum_sha256() &&
           root.selected_overlay_sequence() == overlay.sequence() &&
           root.selected_selector_sha256() == overlay.selector_sha256() &&
           root.selected_method_sha256() == overlay.method_sha256() &&
           root.expires_at() == overlay.expires_at() &&
           overlay.profiling_input_sha256() == input.checksum_sha256() &&
           overlay.sequence() == input.sequence() &&
           overlay.selector_sha256() == input.selector_sha256() &&
           input.observed_at() <= overlay.qualified_at() &&
           overlay.qualified_at() < input.fresh_until() &&
           overlay.qualified_at() <= root.activated_at() &&
           (root.transition() != OverlayRootTransition::Qualification ||
            root.activated_at() < input.fresh_until()) &&
           qualification_claims_are_safe(input, overlay);
}

bool transition_matches(const LoadedOverlayNode &child,
                        const LoadedOverlayNode &parent) noexcept {
    if (parent.root.generation() ==
            std::numeric_limits<std::uint64_t>::max() ||
        child.root.generation() != parent.root.generation() + 1 ||
        child.root.previous_root_sha256() !=
            std::optional<std::string>{
                std::string(parent.root.checksum_sha256())}) {
        return false;
    }
    if (child.root.transition() == OverlayRootTransition::Qualification) {
        return parent.root.sequence_high_water() !=
                   std::numeric_limits<std::uint64_t>::max() &&
               child.root.sequence_high_water() ==
                   parent.root.sequence_high_water() + 1 &&
               child.root.selected_overlay_sequence() ==
                   child.root.sequence_high_water();
    }
    return child.root.sequence_high_water() ==
               parent.root.sequence_high_water() &&
           child.root.selected_overlay_sequence() <
               child.root.sequence_high_water() &&
           child.root.selected_selector_sha256() ==
               parent.root.selected_selector_sha256();
}

bool rollback_target_was_reachable(
    const std::vector<LoadedOverlayNode> &nodes,
    std::size_t child_index) noexcept {
    const auto &child = nodes[child_index];
    for (auto index = child_index + 1; index < nodes.size(); ++index) {
        const auto &ancestor = nodes[index];
        if (ancestor.root.selected_overlay_sha256() ==
                child.root.selected_overlay_sha256() &&
            ancestor.root.selected_overlay_sequence() ==
                child.root.selected_overlay_sequence() &&
            ancestor.root.selected_selector_sha256() ==
                child.root.selected_selector_sha256() &&
            ancestor.root.selected_method_sha256() ==
                child.root.selected_method_sha256()) {
            return true;
        }
    }
    return false;
}

bool qualification_claims_are_safe(
    const ParsedProfilingInputEnvelope &input,
    const ParsedLocalOverlayObject &overlay) {
    auto attributed = check_claim_closure(input.attributed_claims());
    auto conservative = check_claim_closure(overlay.conservative_claims());
    if (!attributed.accepted() || !conservative.accepted() ||
        !checked_subtract(*conservative.claims, *attributed.claims)
             .accepted()) {
        return false;
    }

    constexpr std::array families{
        ClaimFamily::ConsumableCapacity,
        ClaimFamily::SafetyFloor,
        ClaimFamily::CardinalityPool,
        ClaimFamily::CompatibilityExclusivity,
    };
    for (const auto family : families) {
        const auto attributed_completeness =
            attributed.claims->completeness(family);
        const auto conservative_completeness =
            conservative.claims->completeness(family);
        if ((attributed_completeness == ClaimCompleteness::KnownZero &&
             conservative_completeness == ClaimCompleteness::NotApplicable) ||
            (attributed_completeness == ClaimCompleteness::Bounded &&
             conservative_completeness != ClaimCompleteness::Bounded)) {
            return false;
        }
    }

    for (const auto &attributed_family : input.attributed_claims()) {
        if (attributed_family.completeness != ClaimCompleteness::Bounded) {
            continue;
        }
        for (const auto &safety_family : overlay.safety_margin_claims()) {
            if (safety_family.family != attributed_family.family) {
                continue;
            }
            for (const auto &attributed_entry : attributed_family.entries) {
                for (const auto &safety_entry : safety_family.entries) {
                    if (safety_entry.constraint_id ==
                        attributed_entry.constraint_id) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

} // namespace

class PublishedLocalOverlay::Impl {
public:
    Impl(std::string selected_storage_identity,
         std::string selected_deployment_id)
        : storage_identity(std::move(selected_storage_identity)),
          deployment_id(std::move(selected_deployment_id)) {}

    Impl(std::string selected_storage_identity,
         std::string selected_deployment_id, std::string selected_root_bytes,
         ParsedOverlayActivationRoot selected_root,
         ParsedLocalOverlayObject selected_overlay,
         ParsedProfilingInputEnvelope selected_profiling_input,
         std::string selected_baseline_observation_bytes,
         std::string selected_workload_observation_bytes,
         std::string selected_release_observation_bytes)
        : storage_identity(std::move(selected_storage_identity)),
          deployment_id(std::move(selected_deployment_id)),
          root_bytes(std::move(selected_root_bytes)),
          root(std::move(selected_root)), overlay(std::move(selected_overlay)),
          profiling_input(std::move(selected_profiling_input)),
          baseline_observation_bytes(
              std::move(selected_baseline_observation_bytes)),
          workload_observation_bytes(
              std::move(selected_workload_observation_bytes)),
          release_observation_bytes(
              std::move(selected_release_observation_bytes)) {}

    std::string storage_identity;
    std::string deployment_id;
    std::string root_bytes;
    std::optional<ParsedOverlayActivationRoot> root;
    std::optional<ParsedLocalOverlayObject> overlay;
    std::optional<ParsedProfilingInputEnvelope> profiling_input;
    std::string baseline_observation_bytes;
    std::string workload_observation_bytes;
    std::string release_observation_bytes;
};

class LocalOverlayStore::Impl {
public:
    Impl(std::unique_ptr<detail::DurableFileAdapter> selected_adapter,
         LocalOverlayStoreLimits selected_limits)
        : adapter(std::move(selected_adapter)), limits(selected_limits) {}

    bool limits_are_usable() const noexcept {
        return limits.max_object_bytes != 0 && limits.max_history_roots != 0;
    }

    LocalOverlayStoreStatus acquire_identity(std::string &identity) {
        if (!adapter) {
            return LocalOverlayStoreStatus::UnsupportedStorage;
        }
        const auto locked = adapter->lock_authority();
        if (!locked.succeeded()) {
            if (locked.effect_may_have_occurred()) {
                static_cast<void>(adapter->unlock_authority());
                return LocalOverlayStoreStatus::RecoveryRequired;
            }
            return LocalOverlayStoreStatus::UnsupportedStorage;
        }
        lock_held = true;
        auto observed = adapter->authority_identity();
        if (!observed.result.succeeded() || observed.identity.empty()) {
            const auto status = observed.result.effect_may_have_occurred()
                                    ? LocalOverlayStoreStatus::RecoveryRequired
                                    : LocalOverlayStoreStatus::UnsupportedStorage;
            return release_lock().succeeded()
                       ? status
                       : LocalOverlayStoreStatus::RecoveryRequired;
        }
        identity = std::move(observed.identity);
        return LocalOverlayStoreStatus::Ready;
    }

    detail::DurableFileResult release_lock() {
        if (!lock_held) {
            return {detail::DurableFileStatus::Succeeded, {}};
        }
        auto released = adapter->unlock_authority();
        lock_held = false;
        return released;
    }

    LocalOverlayStoreResult release_with_status(
        std::string_view identity, LocalOverlayStoreStatus status) {
        const auto unlocked = release_lock();
        if (!unlocked.succeeded()) {
            detail::fence_identity(identity);
            return store_result(LocalOverlayStoreStatus::RecoveryRequired);
        }
        return store_result(status);
    }

    LocalOverlayStoreStatus inspect_namespace(bool &root_present) {
        const auto preflight = adapter->preflight_capabilities();
        if (!preflight.succeeded()) {
            return preflight.effect_may_have_occurred()
                       ? LocalOverlayStoreStatus::RecoveryRequired
                       : LocalOverlayStoreStatus::UnsupportedStorage;
        }
        const auto fixed = adapter->inspect_fixed_namespace();
        if (!fixed.result.succeeded()) {
            return fixed.result.effect_may_have_occurred()
                       ? LocalOverlayStoreStatus::RecoveryRequired
                       : LocalOverlayStoreStatus::UnsupportedStorage;
        }
        if (!fixed.lock_present) {
            return LocalOverlayStoreStatus::UnsupportedStorage;
        }
        if (fixed.journal_present || fixed.journal_stage_present) {
            return LocalOverlayStoreStatus::CorruptOrRollback;
        }
        root_present = fixed.root_present;
        return LocalOverlayStoreStatus::Ready;
    }

    GraphLoadResult load_graph(std::string storage_identity,
                               std::string deployment_id,
                               bool fixed_root_present) {
        auto current_read = adapter->read_root(limits.max_object_bytes);
        if (!current_read.result.succeeded()) {
            if (current_read.result.status ==
                    detail::DurableFileStatus::NotFound &&
                !fixed_root_present) {
                LoadedOverlayGraph empty;
                empty.storage_identity = std::move(storage_identity);
                empty.deployment_id = std::move(deployment_id);
                return {LocalOverlayStoreStatus::Empty, std::move(empty)};
            }
            return {read_failure_status(
                        current_read,
                        LocalOverlayStoreStatus::CorruptOrRollback),
                    std::nullopt};
        }
        if (!fixed_root_present) {
            return {LocalOverlayStoreStatus::CorruptOrRollback, std::nullopt};
        }
        if (current_read.truncated) {
            return {LocalOverlayStoreStatus::LimitExceeded, std::nullopt};
        }

        auto parsed_current = parse_overlay_activation_root(current_read.bytes);
        if (!parsed_current.accepted()) {
            return {contract_failure_status(parsed_current.status),
                    std::nullopt};
        }

        LoadedOverlayGraph graph;
        graph.storage_identity = std::move(storage_identity);
        graph.deployment_id = std::move(deployment_id);
        graph.root_bytes = current_read.bytes;

        auto root = std::move(*parsed_current.candidate);
        std::unordered_set<std::string> visited_roots;
        while (true) {
            if (!visited_roots.emplace(root.checksum_sha256()).second) {
                return {LocalOverlayStoreStatus::CorruptOrRollback,
                        std::nullopt};
            }
            if (graph.newest_first.size() == limits.max_history_roots) {
                return {LocalOverlayStoreStatus::LimitExceeded, std::nullopt};
            }

            const auto root_object = adapter->read_immutable_object(
                root.checksum_sha256(), limits.max_object_bytes);
            if (!root_object.result.succeeded()) {
                return {read_failure_status(
                            root_object, LocalOverlayStoreStatus::MissingObject),
                        std::nullopt};
            }
            if (root_object.truncated) {
                return {LocalOverlayStoreStatus::LimitExceeded, std::nullopt};
            }
            auto parsed_root =
                parse_overlay_activation_root(root_object.bytes);
            if (!parsed_root.accepted()) {
                return {contract_failure_status(parsed_root.status),
                        std::nullopt};
            }
            if (parsed_root.candidate->checksum_sha256() !=
                    root.checksum_sha256() ||
                parsed_root.candidate->canonical_bytes() !=
                    root.canonical_bytes()) {
                return {LocalOverlayStoreStatus::DigestMismatch, std::nullopt};
            }

            const auto overlay_object = adapter->read_immutable_object(
                root.selected_overlay_sha256(), limits.max_object_bytes);
            if (!overlay_object.result.succeeded()) {
                return {read_failure_status(
                            overlay_object,
                            LocalOverlayStoreStatus::MissingObject),
                        std::nullopt};
            }
            if (overlay_object.truncated) {
                return {LocalOverlayStoreStatus::LimitExceeded, std::nullopt};
            }
            auto parsed_overlay = parse_local_overlay(overlay_object.bytes);
            if (!parsed_overlay.accepted()) {
                return {contract_failure_status(parsed_overlay.status),
                        std::nullopt};
            }
            if (parsed_overlay.candidate->checksum_sha256() !=
                root.selected_overlay_sha256()) {
                return {LocalOverlayStoreStatus::DigestMismatch, std::nullopt};
            }

            const auto input_object = adapter->read_immutable_object(
                parsed_overlay.candidate->profiling_input_sha256(),
                limits.max_object_bytes);
            if (!input_object.result.succeeded()) {
                return {read_failure_status(
                            input_object,
                            LocalOverlayStoreStatus::MissingObject),
                        std::nullopt};
            }
            if (input_object.truncated) {
                return {LocalOverlayStoreStatus::LimitExceeded, std::nullopt};
            }
            auto parsed_input = parse_profiling_input(input_object.bytes);
            if (!parsed_input.accepted()) {
                return {contract_failure_status(parsed_input.status),
                        std::nullopt};
            }
            if (parsed_input.candidate->checksum_sha256() !=
                parsed_overlay.candidate->profiling_input_sha256()) {
                return {LocalOverlayStoreStatus::DigestMismatch, std::nullopt};
            }

            auto read_observation = [&](std::string_view sha256,
                                        std::string &bytes)
                -> LocalOverlayStoreStatus {
                const auto object = adapter->read_immutable_object(
                    sha256, limits.max_object_bytes);
                if (!object.result.succeeded()) {
                    return read_failure_status(
                        object, LocalOverlayStoreStatus::MissingObject);
                }
                if (object.truncated) {
                    return LocalOverlayStoreStatus::LimitExceeded;
                }
                if (object.bytes.empty()) {
                    return LocalOverlayStoreStatus::MissingObject;
                }
                const auto observed_sha256 = raw_sha256(object.bytes);
                if (!observed_sha256.has_value()) {
                    return LocalOverlayStoreStatus::UnsupportedStorage;
                }
                if (*observed_sha256 != sha256) {
                    return LocalOverlayStoreStatus::DigestMismatch;
                }
                bytes = object.bytes;
                return LocalOverlayStoreStatus::Ready;
            };

            std::string baseline_observation_bytes;
            std::string workload_observation_bytes;
            std::string release_observation_bytes;
            for (const auto &[sha256, bytes] :
                 std::array<std::pair<std::string_view, std::string *>, 3>{
                     std::pair<std::string_view, std::string *>{
                         parsed_input.candidate->baseline_observation_sha256(),
                         &baseline_observation_bytes},
                     {parsed_input.candidate->workload_observation_sha256(),
                      &workload_observation_bytes},
                     {parsed_input.candidate->release_observation_sha256(),
                      &release_observation_bytes}}) {
                const auto observation_status =
                    read_observation(sha256, *bytes);
                if (observation_status != LocalOverlayStoreStatus::Ready) {
                    return {observation_status, std::nullopt};
                }
            }

            LoadedOverlayNode node{
                std::move(*parsed_root.candidate),
                std::move(*parsed_overlay.candidate),
                std::move(*parsed_input.candidate),
                std::move(baseline_observation_bytes),
                std::move(workload_observation_bytes),
                std::move(release_observation_bytes),
            };
            if (!node_references_match(node, graph.deployment_id)) {
                const bool deployment_mismatch =
                    node.root.deployment_id() != graph.deployment_id ||
                    node.overlay.deployment_id() != graph.deployment_id ||
                    node.profiling_input.deployment_id() != graph.deployment_id;
                return {deployment_mismatch
                            ? LocalOverlayStoreStatus::DeploymentMismatch
                            : LocalOverlayStoreStatus::CorruptOrRollback,
                        std::nullopt};
            }
            graph.newest_first.emplace_back(std::move(node));

            const auto &selected = graph.newest_first.back().root;
            if (selected.generation() == 1) {
                if (selected.previous_root_sha256().has_value() ||
                    selected.transition() !=
                        OverlayRootTransition::Qualification ||
                    selected.sequence_high_water() != 1 ||
                    selected.selected_overlay_sequence() != 1) {
                    return {LocalOverlayStoreStatus::CorruptOrRollback,
                            std::nullopt};
                }
                break;
            }
            if (!selected.previous_root_sha256().has_value()) {
                return {LocalOverlayStoreStatus::CorruptOrRollback,
                        std::nullopt};
            }
            const auto previous = adapter->read_immutable_object(
                *selected.previous_root_sha256(), limits.max_object_bytes);
            if (!previous.result.succeeded()) {
                return {read_failure_status(
                            previous, LocalOverlayStoreStatus::MissingObject),
                        std::nullopt};
            }
            if (previous.truncated) {
                return {LocalOverlayStoreStatus::LimitExceeded, std::nullopt};
            }
            auto parsed_previous =
                parse_overlay_activation_root(previous.bytes);
            if (!parsed_previous.accepted()) {
                return {contract_failure_status(parsed_previous.status),
                        std::nullopt};
            }
            if (parsed_previous.candidate->checksum_sha256() !=
                *selected.previous_root_sha256()) {
                return {LocalOverlayStoreStatus::DigestMismatch, std::nullopt};
            }
            root = std::move(*parsed_previous.candidate);
        }

        for (std::size_t index = 0;
             index + 1 < graph.newest_first.size(); ++index) {
            const auto &child = graph.newest_first[index];
            const auto &parent = graph.newest_first[index + 1];
            if (child.root.deployment_id() != parent.root.deployment_id() ||
                !transition_matches(child, parent) ||
                (child.root.transition() == OverlayRootTransition::Rollback &&
                 !rollback_target_was_reachable(graph.newest_first, index))) {
                return {LocalOverlayStoreStatus::CorruptOrRollback,
                        std::nullopt};
            }
        }
        return {LocalOverlayStoreStatus::Ready, std::move(graph)};
    }

    bool final_identity_and_root_match(std::string_view identity,
                                       std::string_view root_bytes,
                                       bool expect_root) {
        const auto observed = adapter->authority_identity();
        if (!observed.result.succeeded() || observed.identity != identity) {
            return false;
        }
        const auto fixed = adapter->inspect_fixed_namespace();
        if (!fixed.result.succeeded() || !fixed.lock_present ||
            fixed.journal_present || fixed.journal_stage_present ||
            fixed.root_present != expect_root) {
            return false;
        }
        const auto root = adapter->read_root(limits.max_object_bytes);
        if (!expect_root) {
            return root.result.status == detail::DurableFileStatus::NotFound;
        }
        return root.result.succeeded() && !root.truncated &&
               root.bytes == root_bytes;
    }

    std::unique_ptr<detail::DurableFileAdapter> adapter;
    LocalOverlayStoreLimits limits;
    std::mutex calls;
    bool lock_held = false;
};

TrustedLocalOverlayReplayFloor::TrustedLocalOverlayReplayFloor(
    std::optional<std::string> root)
    : root_(std::move(root)) {}

TrustedLocalOverlayReplayFloor
TrustedLocalOverlayReplayFloor::uninitialized() {
    return TrustedLocalOverlayReplayFloor(std::nullopt);
}

TrustedLocalOverlayReplayFloor
TrustedLocalOverlayReplayFloor::exact_root(std::string root) {
    return TrustedLocalOverlayReplayFloor(std::move(root));
}

LocalOverlayActivation::LocalOverlayActivation(
    LocalOverlayActivationKind kind,
    std::optional<ParsedProfilingInputEnvelope> profiling_input,
    std::optional<ParsedLocalOverlayObject> overlay,
    std::optional<std::string> rollback_overlay_sha256,
    std::optional<std::string> baseline_observation_bytes,
    std::optional<std::string> workload_observation_bytes,
    std::optional<std::string> release_observation_bytes,
    std::string decision_trace_sha256, std::string activated_at)
    : kind_(kind), profiling_input_(std::move(profiling_input)),
      overlay_(std::move(overlay)),
      rollback_overlay_sha256_(std::move(rollback_overlay_sha256)),
      baseline_observation_bytes_(std::move(baseline_observation_bytes)),
      workload_observation_bytes_(std::move(workload_observation_bytes)),
      release_observation_bytes_(std::move(release_observation_bytes)),
      decision_trace_sha256_(std::move(decision_trace_sha256)),
      activated_at_(std::move(activated_at)) {}

LocalOverlayActivation LocalOverlayActivation::qualification(
    ParsedProfilingInputEnvelope profiling_input,
    ParsedLocalOverlayObject overlay, std::string baseline_observation_bytes,
    std::string workload_observation_bytes,
    std::string release_observation_bytes, std::string decision_trace_sha256,
    std::string activated_at) {
    return LocalOverlayActivation(
        LocalOverlayActivationKind::Qualification, std::move(profiling_input),
        std::move(overlay), std::nullopt,
        std::move(baseline_observation_bytes),
        std::move(workload_observation_bytes),
        std::move(release_observation_bytes),
        std::move(decision_trace_sha256), std::move(activated_at));
}

LocalOverlayActivation
LocalOverlayActivation::rollback(std::string overlay_sha256,
                                 std::string decision_trace_sha256,
                                 std::string activated_at) {
    return LocalOverlayActivation(
        LocalOverlayActivationKind::Rollback, std::nullopt, std::nullopt,
        std::move(overlay_sha256), std::nullopt, std::nullopt, std::nullopt,
        std::move(decision_trace_sha256), std::move(activated_at));
}

LocalOverlayActivationKind LocalOverlayActivation::kind() const noexcept {
    return kind_;
}

PublishedLocalOverlay::PublishedLocalOverlay(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

PublishedLocalOverlay::PublishedLocalOverlay(
    PublishedLocalOverlay &&) noexcept = default;

PublishedLocalOverlay &PublishedLocalOverlay::operator=(
    PublishedLocalOverlay &&) noexcept = default;

PublishedLocalOverlay::~PublishedLocalOverlay() = default;

bool PublishedLocalOverlay::available() const noexcept {
    return impl_ != nullptr;
}

bool PublishedLocalOverlay::empty() const noexcept {
    return impl_ != nullptr && !impl_->root.has_value();
}

std::string_view PublishedLocalOverlay::deployment_id() const noexcept {
    return impl_ == nullptr ? std::string_view{} : impl_->deployment_id;
}

std::uint64_t PublishedLocalOverlay::generation() const noexcept {
    return impl_ == nullptr || !impl_->root.has_value()
               ? 0
               : impl_->root->generation();
}

std::uint64_t PublishedLocalOverlay::sequence_high_water() const noexcept {
    return impl_ == nullptr || !impl_->root.has_value()
               ? 0
               : impl_->root->sequence_high_water();
}

std::string_view PublishedLocalOverlay::root_bytes() const noexcept {
    return impl_ == nullptr ? std::string_view{} : impl_->root_bytes;
}

std::string_view PublishedLocalOverlay::root_sha256() const noexcept {
    return impl_ == nullptr || !impl_->root.has_value()
               ? std::string_view{}
               : impl_->root->checksum_sha256();
}

const ParsedOverlayActivationRoot *
PublishedLocalOverlay::active_root() const noexcept {
    return impl_ == nullptr || !impl_->root.has_value() ? nullptr
                                                        : &*impl_->root;
}

const ParsedLocalOverlayObject *
PublishedLocalOverlay::active_overlay() const noexcept {
    return impl_ == nullptr || !impl_->overlay.has_value() ? nullptr
                                                           : &*impl_->overlay;
}

const ParsedProfilingInputEnvelope *
PublishedLocalOverlay::active_profiling_input() const noexcept {
    return impl_ == nullptr || !impl_->profiling_input.has_value()
               ? nullptr
               : &*impl_->profiling_input;
}

std::string_view
PublishedLocalOverlay::active_baseline_observation_bytes() const noexcept {
    return impl_ == nullptr ? std::string_view{}
                            : impl_->baseline_observation_bytes;
}

std::string_view
PublishedLocalOverlay::active_workload_observation_bytes() const noexcept {
    return impl_ == nullptr ? std::string_view{}
                            : impl_->workload_observation_bytes;
}

std::string_view
PublishedLocalOverlay::active_release_observation_bytes() const noexcept {
    return impl_ == nullptr ? std::string_view{}
                            : impl_->release_observation_bytes;
}

bool LocalOverlayStoreResult::usable() const noexcept {
    return (status == LocalOverlayStoreStatus::Ready ||
            status == LocalOverlayStoreStatus::Activated ||
            status == LocalOverlayStoreStatus::Empty) &&
           authority.has_value() && authority->available();
}

LocalOverlayStore::LocalOverlayStore(
    std::unique_ptr<detail::DurableFileAdapter> adapter,
    LocalOverlayStoreLimits limits)
    : impl_(std::make_unique<Impl>(std::move(adapter), limits)) {}

LocalOverlayStore::LocalOverlayStore(LocalOverlayStore &&) noexcept = default;

LocalOverlayStore &
LocalOverlayStore::operator=(LocalOverlayStore &&) noexcept = default;

LocalOverlayStore::~LocalOverlayStore() = default;

std::filesystem::path LocalOverlayStore::namespace_path(
    const std::filesystem::path &cache_directory) {
    return cache_directory / local_overlay_namespace_directory;
}

LocalOverlayStore
LocalOverlayStore::native(std::filesystem::path cache_directory,
                          LocalOverlayStoreLimits limits) {
    return LocalOverlayStore(
        detail::make_platform_durable_file_adapter_in_fixed_namespace(
            cache_directory, local_overlay_namespace_directory),
        limits);
}

LocalOverlayStoreResult
LocalOverlayStore::snapshot(TrustedLocalOverlayReplayFloor floor) {
    if (!impl_) {
        return store_result(LocalOverlayStoreStatus::UnsupportedStorage);
    }
    if (!impl_->limits_are_usable()) {
        return store_result(LocalOverlayStoreStatus::LimitExceeded);
    }
    if (floor.root_.has_value() &&
        floor.root_->size() > impl_->limits.max_object_bytes) {
        return store_result(LocalOverlayStoreStatus::LimitExceeded);
    }
    std::lock_guard call_lock(impl_->calls);

    std::string identity;
    const auto acquired = impl_->acquire_identity(identity);
    if (acquired != LocalOverlayStoreStatus::Ready) {
        return store_result(acquired);
    }
    auto release_with_status = [&](LocalOverlayStoreStatus status) {
        return impl_->release_with_status(identity, status);
    };
    const auto retained_fence = detail::identity_fence(identity);
    bool root_present = false;
    const auto inspected = impl_->inspect_namespace(root_present);
    if (inspected != LocalOverlayStoreStatus::Ready) {
        if (inspected == LocalOverlayStoreStatus::RecoveryRequired &&
            !retained_fence) {
            detail::fence_identity(identity);
        }
        return release_with_status(
            retained_fence ? LocalOverlayStoreStatus::RecoveryRequired
                           : inspected);
    }
    const auto deployment = deployment_id_for_storage(identity);
    if (!deployment.has_value()) {
        return release_with_status(LocalOverlayStoreStatus::UnsupportedStorage);
    }

    auto loaded = impl_->load_graph(identity, *deployment, root_present);
    if (!loaded.graph.has_value()) {
        return release_with_status(
            retained_fence ? LocalOverlayStoreStatus::RecoveryRequired
                           : loaded.status);
    }
    const bool empty = loaded.graph->newest_first.empty();
    if ((empty && floor.root_.has_value()) ||
        (!empty && !floor.root_.has_value())) {
        return release_with_status(
            empty ? LocalOverlayStoreStatus::CorruptOrRollback
                  : LocalOverlayStoreStatus::ReplayFloorRequired);
    }
    if (!empty) {
        bool floor_seen = false;
        for (const auto &node : loaded.graph->newest_first) {
            if (node.root.canonical_bytes() == *floor.root_) {
                floor_seen = true;
                break;
            }
        }
        if (!floor_seen) {
            return release_with_status(
                LocalOverlayStoreStatus::CorruptOrRollback);
        }
    }
    if (!impl_->final_identity_and_root_match(
            identity, loaded.graph->root_bytes, !empty)) {
        return release_with_status(LocalOverlayStoreStatus::RecoveryRequired);
    }

    std::unique_ptr<PublishedLocalOverlay::Impl> published;
    if (empty) {
        published = std::make_unique<PublishedLocalOverlay::Impl>(
            loaded.graph->storage_identity, loaded.graph->deployment_id);
    } else {
        auto &active = loaded.graph->newest_first.front();
        published = std::make_unique<PublishedLocalOverlay::Impl>(
            loaded.graph->storage_identity, loaded.graph->deployment_id,
            loaded.graph->root_bytes, std::move(active.root),
            std::move(active.overlay), std::move(active.profiling_input),
            std::move(active.baseline_observation_bytes),
            std::move(active.workload_observation_bytes),
            std::move(active.release_observation_bytes));
    }
    const auto unlocked = impl_->release_lock();
    if (!unlocked.succeeded()) {
        detail::fence_identity(identity);
        return store_result(LocalOverlayStoreStatus::RecoveryRequired);
    }
    detail::clear_identity_fence(identity, retained_fence);
    return {empty ? LocalOverlayStoreStatus::Empty
                  : LocalOverlayStoreStatus::Ready,
            PublishedLocalOverlay(std::move(published))};
}

LocalOverlayStoreResult
LocalOverlayStore::activate(PublishedLocalOverlay &&expected,
                            LocalOverlayActivation activation) {
    auto expected_state = std::move(expected.impl_);
    if (!impl_) {
        return store_result(LocalOverlayStoreStatus::UnsupportedStorage);
    }
    if (!expected_state) {
        return store_result(LocalOverlayStoreStatus::CorruptOrRollback);
    }
    if (!impl_->limits_are_usable()) {
        return store_result(LocalOverlayStoreStatus::LimitExceeded);
    }
    std::lock_guard call_lock(impl_->calls);

    std::string identity;
    const auto acquired = impl_->acquire_identity(identity);
    if (acquired != LocalOverlayStoreStatus::Ready) {
        return store_result(acquired);
    }
    auto release_with_status = [&](LocalOverlayStoreStatus status) {
        return impl_->release_with_status(identity, status);
    };
    if (identity != expected_state->storage_identity) {
        const auto status =
            detail::same_directory_identity(identity,
                                            expected_state->storage_identity)
                ? LocalOverlayStoreStatus::UnsupportedStorage
                : LocalOverlayStoreStatus::DeploymentMismatch;
        return release_with_status(status);
    }
    if (detail::identity_is_fenced(identity)) {
        return release_with_status(LocalOverlayStoreStatus::RecoveryRequired);
    }
    const auto deployment = deployment_id_for_storage(identity);
    if (!deployment.has_value() || *deployment != expected_state->deployment_id) {
        return release_with_status(
            deployment.has_value()
                ? LocalOverlayStoreStatus::DeploymentMismatch
                : LocalOverlayStoreStatus::UnsupportedStorage);
    }
    bool root_present = false;
    const auto inspected = impl_->inspect_namespace(root_present);
    if (inspected != LocalOverlayStoreStatus::Ready) {
        if (inspected == LocalOverlayStoreStatus::RecoveryRequired) {
            detail::fence_identity(identity);
        }
        return release_with_status(inspected);
    }

    const auto current_root =
        impl_->adapter->read_root(impl_->limits.max_object_bytes);
    const bool expected_empty = !expected_state->root.has_value();
    const bool root_matches =
        expected_empty
            ? (!root_present && current_root.result.status ==
                                   detail::DurableFileStatus::NotFound)
            : (root_present && current_root.result.succeeded() &&
               !current_root.truncated &&
               current_root.bytes == expected_state->root_bytes);
    if (!root_matches) {
        if (current_root.truncated) {
            return release_with_status(LocalOverlayStoreStatus::LimitExceeded);
        }
        if (!current_root.result.succeeded() &&
            current_root.result.status != detail::DurableFileStatus::NotFound) {
            return release_with_status(read_failure_status(
                current_root, LocalOverlayStoreStatus::CorruptOrRollback));
        }
        return release_with_status(
            LocalOverlayStoreStatus::ConflictBeforeWrite);
    }

    auto loaded = impl_->load_graph(identity, *deployment, root_present);
    if (!loaded.graph.has_value()) {
        return release_with_status(loaded.status);
    }
    if (loaded.graph->newest_first.empty() != expected_empty) {
        return release_with_status(
            LocalOverlayStoreStatus::ConflictBeforeWrite);
    }
    if (loaded.graph->newest_first.size() >=
        impl_->limits.max_history_roots) {
        return release_with_status(LocalOverlayStoreStatus::LimitExceeded);
    }

    const std::uint64_t current_generation =
        expected_empty ? 0 : loaded.graph->newest_first.front().root.generation();
    const std::uint64_t current_high_water =
        expected_empty
            ? 0
            : loaded.graph->newest_first.front().root.sequence_high_water();
    if (current_generation == std::numeric_limits<std::uint64_t>::max()) {
        return release_with_status(LocalOverlayStoreStatus::LimitExceeded);
    }

    const ParsedProfilingInputEnvelope *selected_input = nullptr;
    const ParsedLocalOverlayObject *selected_overlay = nullptr;
    const std::string *selected_baseline_observation_bytes = nullptr;
    const std::string *selected_workload_observation_bytes = nullptr;
    const std::string *selected_release_observation_bytes = nullptr;
    OverlayRootTransition transition = OverlayRootTransition::Qualification;
    std::uint64_t next_high_water = current_high_water;

    if (activation.kind_ == LocalOverlayActivationKind::Qualification) {
        if (!activation.profiling_input_.has_value() ||
            !activation.overlay_.has_value() ||
            activation.rollback_overlay_sha256_.has_value() ||
            !activation.baseline_observation_bytes_.has_value() ||
            !activation.workload_observation_bytes_.has_value() ||
            !activation.release_observation_bytes_.has_value() ||
            current_high_water == std::numeric_limits<std::uint64_t>::max()) {
            return release_with_status(
                current_high_water ==
                        std::numeric_limits<std::uint64_t>::max()
                    ? LocalOverlayStoreStatus::LimitExceeded
                    : LocalOverlayStoreStatus::CorruptOrRollback);
        }
        selected_input = &*activation.profiling_input_;
        selected_overlay = &*activation.overlay_;
        selected_baseline_observation_bytes =
            &*activation.baseline_observation_bytes_;
        selected_workload_observation_bytes =
            &*activation.workload_observation_bytes_;
        selected_release_observation_bytes =
            &*activation.release_observation_bytes_;
        next_high_water = current_high_water + 1;
        if (selected_input->deployment_id() != *deployment ||
            selected_overlay->deployment_id() != *deployment) {
            return release_with_status(
                LocalOverlayStoreStatus::DeploymentMismatch);
        }
        if (selected_input->sequence() != next_high_water ||
            selected_overlay->sequence() != next_high_water ||
            selected_overlay->profiling_input_sha256() !=
                selected_input->checksum_sha256() ||
            selected_overlay->selector_sha256() !=
                selected_input->selector_sha256() ||
            selected_input->observed_at() > selected_overlay->qualified_at() ||
            selected_overlay->qualified_at() >=
                selected_input->fresh_until() ||
            selected_overlay->qualified_at() > activation.activated_at_ ||
            activation.activated_at_ >= selected_input->fresh_until() ||
            activation.activated_at_ >= selected_overlay->expires_at() ||
            !qualification_claims_are_safe(*selected_input,
                                           *selected_overlay)) {
            return release_with_status(
                LocalOverlayStoreStatus::CorruptOrRollback);
        }
        if (selected_baseline_observation_bytes->size() >
                impl_->limits.max_object_bytes ||
            selected_workload_observation_bytes->size() >
                impl_->limits.max_object_bytes ||
            selected_release_observation_bytes->size() >
                impl_->limits.max_object_bytes) {
            return release_with_status(LocalOverlayStoreStatus::LimitExceeded);
        }
        if (selected_baseline_observation_bytes->empty() ||
            selected_workload_observation_bytes->empty() ||
            selected_release_observation_bytes->empty()) {
            return release_with_status(LocalOverlayStoreStatus::MissingObject);
        }
        const auto baseline_sha256 =
            raw_sha256(*selected_baseline_observation_bytes);
        const auto workload_sha256 =
            raw_sha256(*selected_workload_observation_bytes);
        const auto release_sha256 =
            raw_sha256(*selected_release_observation_bytes);
        if (!baseline_sha256.has_value() || !workload_sha256.has_value() ||
            !release_sha256.has_value()) {
            return release_with_status(
                LocalOverlayStoreStatus::UnsupportedStorage);
        }
        if (*baseline_sha256 !=
                selected_input->baseline_observation_sha256() ||
            *workload_sha256 !=
                selected_input->workload_observation_sha256() ||
            *release_sha256 != selected_input->release_observation_sha256()) {
            return release_with_status(LocalOverlayStoreStatus::DigestMismatch);
        }
    } else {
        transition = OverlayRootTransition::Rollback;
        if (expected_empty || activation.profiling_input_.has_value() ||
            activation.overlay_.has_value() ||
            activation.baseline_observation_bytes_.has_value() ||
            activation.workload_observation_bytes_.has_value() ||
            activation.release_observation_bytes_.has_value() ||
            !activation.rollback_overlay_sha256_.has_value()) {
            return release_with_status(
                LocalOverlayStoreStatus::CorruptOrRollback);
        }
        const auto &active = loaded.graph->newest_first.front();
        for (std::size_t index = 1;
             index < loaded.graph->newest_first.size(); ++index) {
            const auto &candidate = loaded.graph->newest_first[index];
            if (candidate.overlay.checksum_sha256() ==
                    *activation.rollback_overlay_sha256_ &&
                candidate.overlay.sequence() < current_high_water &&
                candidate.overlay.selector_sha256() ==
                    active.overlay.selector_sha256()) {
                selected_overlay = &candidate.overlay;
                selected_input = &candidate.profiling_input;
                selected_baseline_observation_bytes =
                    &candidate.baseline_observation_bytes;
                selected_workload_observation_bytes =
                    &candidate.workload_observation_bytes;
                selected_release_observation_bytes =
                    &candidate.release_observation_bytes;
                break;
            }
        }
        if (selected_overlay == nullptr || selected_input == nullptr ||
            activation.activated_at_ < selected_overlay->qualified_at() ||
            activation.activated_at_ >= selected_overlay->expires_at()) {
            return release_with_status(
                LocalOverlayStoreStatus::CorruptOrRollback);
        }
    }

    if (selected_input->canonical_bytes().size() >
            impl_->limits.max_object_bytes ||
        selected_overlay->canonical_bytes().size() >
            impl_->limits.max_object_bytes) {
        return release_with_status(LocalOverlayStoreStatus::LimitExceeded);
    }

    OverlayActivationRootDraft draft;
    draft.deployment_id = *deployment;
    draft.generation = current_generation + 1;
    if (!expected_empty) {
        draft.previous_root_sha256 =
            std::string(loaded.graph->newest_first.front()
                            .root.checksum_sha256());
    }
    draft.transition = transition;
    draft.selected_overlay_sha256 =
        std::string(selected_overlay->checksum_sha256());
    draft.selected_overlay_sequence = selected_overlay->sequence();
    draft.sequence_high_water = next_high_water;
    draft.selected_selector_sha256 =
        std::string(selected_overlay->selector_sha256());
    draft.selected_method_sha256 =
        std::string(selected_overlay->method_sha256());
    draft.decision_trace_sha256 = activation.decision_trace_sha256_;
    draft.activated_at = activation.activated_at_;
    draft.expires_at = std::string(selected_overlay->expires_at());
    auto sealed_root = seal_overlay_activation_root(std::move(draft));
    if (!sealed_root.accepted()) {
        return release_with_status(
            contract_failure_status(sealed_root.status));
    }
    if (sealed_root.candidate->canonical_bytes().size() >
        impl_->limits.max_object_bytes) {
        return release_with_status(LocalOverlayStoreStatus::LimitExceeded);
    }

    const auto provisional_fence = detail::fence_identity(identity);
    bool storage_effect = false;
    auto abort_mutation = [&](LocalOverlayStoreStatus status,
                              bool ambiguous) {
        const bool unchanged = impl_->final_identity_and_root_match(
            identity, expected_state->root_bytes, !expected_empty);
        const auto unlocked = impl_->release_lock();
        if (!ambiguous && !storage_effect && unchanged &&
            unlocked.succeeded()) {
            detail::clear_identity_fence(identity, provisional_fence);
            return store_result(status);
        }
        return store_result(LocalOverlayStoreStatus::RecoveryRequired);
    };
    auto publish_object = [&](std::string_view sha256,
                              std::string_view bytes)
        -> std::optional<LocalOverlayStoreResult> {
        const auto created =
            impl_->adapter->create_immutable_object(sha256, bytes);
        if (created.succeeded()) {
            storage_effect = true;
            return std::nullopt;
        }
        if (created.status == detail::DurableFileStatus::AlreadyExists) {
            const auto existing = impl_->adapter->read_immutable_object(
                sha256, impl_->limits.max_object_bytes);
            if (!existing.result.succeeded()) {
                return abort_mutation(
                    read_failure_status(existing,
                                        LocalOverlayStoreStatus::MissingObject),
                    existing.result.effect_may_have_occurred());
            }
            if (existing.truncated) {
                return abort_mutation(LocalOverlayStoreStatus::LimitExceeded,
                                      false);
            }
            if (existing.bytes != bytes) {
                return abort_mutation(LocalOverlayStoreStatus::DigestMismatch,
                                      false);
            }
            return std::nullopt;
        }
        return abort_mutation(LocalOverlayStoreStatus::UnsupportedStorage,
                              created.effect_may_have_occurred());
    };

    if (activation.kind_ == LocalOverlayActivationKind::Qualification) {
        if (auto failed = publish_object(
                selected_input->baseline_observation_sha256(),
                *selected_baseline_observation_bytes)) {
            return std::move(*failed);
        }
        if (auto failed = publish_object(
                selected_input->workload_observation_sha256(),
                *selected_workload_observation_bytes)) {
            return std::move(*failed);
        }
        if (auto failed = publish_object(
                selected_input->release_observation_sha256(),
                *selected_release_observation_bytes)) {
            return std::move(*failed);
        }
        if (auto failed = publish_object(selected_input->checksum_sha256(),
                                         selected_input->canonical_bytes())) {
            return std::move(*failed);
        }
        if (auto failed = publish_object(selected_overlay->checksum_sha256(),
                                         selected_overlay->canonical_bytes())) {
            return std::move(*failed);
        }
    }
    if (auto failed = publish_object(
            sealed_root.candidate->checksum_sha256(),
            sealed_root.candidate->canonical_bytes())) {
        return std::move(*failed);
    }

    const auto replaced = impl_->adapter->replace_root(
        sealed_root.candidate->canonical_bytes());
    if (!replaced.succeeded()) {
        return abort_mutation(LocalOverlayStoreStatus::UnsupportedStorage,
                              replaced.effect_may_have_occurred());
    }
    storage_effect = true;

    auto published_graph = impl_->load_graph(identity, *deployment, true);
    if (!published_graph.graph.has_value() ||
        published_graph.graph->root_bytes !=
            sealed_root.candidate->canonical_bytes() ||
        !impl_->final_identity_and_root_match(
            identity, sealed_root.candidate->canonical_bytes(), true)) {
        return release_with_status(LocalOverlayStoreStatus::RecoveryRequired);
    }

    auto &active = published_graph.graph->newest_first.front();
    auto published = std::make_unique<PublishedLocalOverlay::Impl>(
        published_graph.graph->storage_identity,
        published_graph.graph->deployment_id,
        published_graph.graph->root_bytes, std::move(active.root),
        std::move(active.overlay), std::move(active.profiling_input),
        std::move(active.baseline_observation_bytes),
        std::move(active.workload_observation_bytes),
        std::move(active.release_observation_bytes));
    const auto unlocked = impl_->release_lock();
    if (!unlocked.succeeded()) {
        return store_result(LocalOverlayStoreStatus::RecoveryRequired);
    }
    detail::clear_identity_fence(identity, provisional_fence);
    return {LocalOverlayStoreStatus::Activated,
            PublishedLocalOverlay(std::move(published))};
}

namespace detail {

#ifdef LEMONADE_RESIDENCY_DURABLE_TESTING
LocalOverlayStore LocalOverlayStoreTestFactory::make(
    std::unique_ptr<DurableFileAdapter> adapter,
    LocalOverlayStoreLimits limits) {
    return LocalOverlayStore(std::move(adapter), limits);
}

LocalOverlayStore make_local_overlay_store_for_test(
    std::unique_ptr<DurableFileAdapter> adapter,
    LocalOverlayStoreLimits limits) {
    return LocalOverlayStoreTestFactory::make(std::move(adapter), limits);
}
#endif

} // namespace detail

} // namespace lemon::residency
