#pragma once

#include "lemon/residency/local_overlay.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace lemon::residency {

inline constexpr std::string_view local_overlay_namespace_directory =
    "residency-local-overlay";

struct LocalOverlayStoreLimits {
    std::size_t max_object_bytes;
    std::size_t max_history_roots;
};

enum class LocalOverlayStoreStatus {
    Ready,
    Activated,
    Empty,
    ConflictBeforeWrite,
    ReplayFloorRequired,
    MissingObject,
    DigestMismatch,
    DeploymentMismatch,
    CorruptOrRollback,
    RecoveryRequired,
    UnsupportedStorage,
    LimitExceeded,
};

class TrustedLocalOverlayReplayFloor {
public:
    TrustedLocalOverlayReplayFloor() = delete;

    static TrustedLocalOverlayReplayFloor uninitialized();
    static TrustedLocalOverlayReplayFloor exact_root(std::string root);

private:
    explicit TrustedLocalOverlayReplayFloor(std::optional<std::string> root);

    std::optional<std::string> root_;

    friend class LocalOverlayStore;
};

enum class LocalOverlayActivationKind { Qualification, Rollback };

namespace detail {
#ifdef LEMONADE_RESIDENCY_DURABLE_TESTING
class LocalOverlayStoreTestFactory;
#endif
} // namespace detail

class LocalOverlayActivation {
public:
    LocalOverlayActivation() = delete;
    LocalOverlayActivation(const LocalOverlayActivation &) = default;
    LocalOverlayActivation &operator=(const LocalOverlayActivation &) = default;
    LocalOverlayActivation(LocalOverlayActivation &&) noexcept = default;
    LocalOverlayActivation &operator=(LocalOverlayActivation &&) noexcept = default;

    static LocalOverlayActivation
    unresolved_qualification(ParsedProfilingInputEnvelope profiling_input,
                             ParsedLocalOverlayObject overlay,
                             std::string baseline_observation_bytes,
                             std::string workload_observation_bytes,
                             std::string release_observation_bytes,
                             std::string decision_trace_sha256,
                             std::string activated_at);
    static LocalOverlayActivation rollback(std::string overlay_sha256,
                                           std::string decision_trace_sha256,
                                           std::string activated_at);

    LocalOverlayActivationKind kind() const noexcept;

private:
    LocalOverlayActivation(
        LocalOverlayActivationKind kind,
        std::optional<ParsedProfilingInputEnvelope> profiling_input,
        std::optional<ParsedLocalOverlayObject> overlay,
        std::optional<std::string> rollback_overlay_sha256,
        std::optional<std::string> baseline_observation_bytes,
        std::optional<std::string> workload_observation_bytes,
        std::optional<std::string> release_observation_bytes,
        std::string decision_trace_sha256, std::string activated_at);

    LocalOverlayActivationKind kind_;
    std::optional<ParsedProfilingInputEnvelope> profiling_input_;
    std::optional<ParsedLocalOverlayObject> overlay_;
    std::optional<std::string> rollback_overlay_sha256_;
    std::optional<std::string> baseline_observation_bytes_;
    std::optional<std::string> workload_observation_bytes_;
    std::optional<std::string> release_observation_bytes_;
    std::string decision_trace_sha256_;
    std::string activated_at_;
#ifdef LEMONADE_RESIDENCY_DURABLE_TESTING
    bool qualification_resolved_for_test_ = false;
#endif

    friend class LocalOverlayStore;
#ifdef LEMONADE_RESIDENCY_DURABLE_TESTING
    friend class detail::LocalOverlayStoreTestFactory;
#endif
};

class LocalOverlayStore;

namespace detail {
class DurableFileAdapter;
} // namespace detail

class PublishedLocalOverlay {
public:
    PublishedLocalOverlay() = delete;
    PublishedLocalOverlay(const PublishedLocalOverlay &) = delete;
    PublishedLocalOverlay &operator=(const PublishedLocalOverlay &) = delete;
    PublishedLocalOverlay(PublishedLocalOverlay &&) noexcept;
    PublishedLocalOverlay &operator=(PublishedLocalOverlay &&) noexcept;
    ~PublishedLocalOverlay();

    bool available() const noexcept;
    bool empty() const noexcept;
    std::string_view deployment_id() const noexcept;
    std::uint64_t generation() const noexcept;
    std::uint64_t sequence_high_water() const noexcept;
    std::string_view root_bytes() const noexcept;
    std::string_view root_sha256() const noexcept;
    const ParsedOverlayActivationRoot *active_root() const noexcept;
    const ParsedLocalOverlayObject *active_overlay() const noexcept;
    const ParsedProfilingInputEnvelope *active_profiling_input() const noexcept;
    std::string_view active_baseline_observation_bytes() const noexcept;
    std::string_view active_workload_observation_bytes() const noexcept;
    std::string_view active_release_observation_bytes() const noexcept;

private:
    friend class LocalOverlayStore;
    class Impl;
    explicit PublishedLocalOverlay(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

struct LocalOverlayStoreResult {
    LocalOverlayStoreStatus status = LocalOverlayStoreStatus::CorruptOrRollback;
    std::optional<PublishedLocalOverlay> authority;

    bool usable() const noexcept;
};

class LocalOverlayStore {
public:
    LocalOverlayStore() = delete;
    LocalOverlayStore(const LocalOverlayStore &) = delete;
    LocalOverlayStore &operator=(const LocalOverlayStore &) = delete;
    LocalOverlayStore(LocalOverlayStore &&) noexcept;
    LocalOverlayStore &operator=(LocalOverlayStore &&) noexcept;
    ~LocalOverlayStore();

    static std::filesystem::path
    namespace_path(const std::filesystem::path &cache_directory);
    static LocalOverlayStore native(std::filesystem::path cache_directory,
                                    LocalOverlayStoreLimits limits);

    LocalOverlayStoreResult snapshot(TrustedLocalOverlayReplayFloor floor);
    LocalOverlayStoreResult activate(PublishedLocalOverlay &&expected,
                                     LocalOverlayActivation activation);

private:
#ifdef LEMONADE_RESIDENCY_DURABLE_TESTING
    friend class detail::LocalOverlayStoreTestFactory;
#endif
    class Impl;
    explicit LocalOverlayStore(std::unique_ptr<detail::DurableFileAdapter> adapter,
                               LocalOverlayStoreLimits limits);

    std::unique_ptr<Impl> impl_;
};

} // namespace lemon::residency
