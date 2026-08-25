#pragma once

#include "lemon/utils/process_handle.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lemon::utils {

class ProcessManager;
struct ProcessContainmentTestHook;

namespace internal {
class ProcessContainmentState;
} // namespace internal

enum class ProcessContainmentStatus {
    Success,
    Unsupported,
    InvalidRequest,
    InvalidDelegation,
    ScopeNotEmpty,
    ScopeChanged,
    Cancelled,
    SpawnFailed,
    JoinFailed,
    ExecFailed,
    IdentityUnavailable,
    IdentityChanged,
    MembershipChanged,
    StillPopulated,
    TimedOut,
    Closed,
    Failed,
};

struct ProcessBirthIdentity {
    std::string boot_id;
    int pid = -1;
    std::uint64_t start_time_ticks = 0;

    bool operator==(const ProcessBirthIdentity &other) const noexcept;
    bool operator!=(const ProcessBirthIdentity &other) const noexcept;
    bool operator<(const ProcessBirthIdentity &other) const noexcept;
};

struct ProcessContainmentIdentity {
    std::string boot_id;
    std::uint64_t mount_id = 0;
    std::uint64_t device = 0;
    std::uint64_t inode = 0;
    std::string owner_scope_id;
    std::string nonce_sha256;

    bool operator==(const ProcessContainmentIdentity &other) const noexcept;
    bool operator!=(const ProcessContainmentIdentity &other) const noexcept;
};

struct ProcessContainmentRequest {
    std::filesystem::path delegated_root;
    std::string owner_scope_id;
    std::string nonce_sha256;
};

struct ProcessContainmentOperationControl {
    std::chrono::steady_clock::time_point deadline;
    std::shared_ptr<const std::atomic<bool>> cancellation;
};

struct ProcessContainmentSnapshot {
    ProcessContainmentIdentity identity;
    std::uint64_t generation = 0;
    std::vector<ProcessBirthIdentity> members;
};

class PreparedProcessContainment {
public:
    PreparedProcessContainment() = delete;
    PreparedProcessContainment(const PreparedProcessContainment &) = delete;
    PreparedProcessContainment &
    operator=(const PreparedProcessContainment &) = delete;
    PreparedProcessContainment(PreparedProcessContainment &&) noexcept;
    PreparedProcessContainment &
    operator=(PreparedProcessContainment &&) noexcept = delete;
    ~PreparedProcessContainment();

    bool active() const noexcept;

private:
    explicit PreparedProcessContainment(
        std::unique_ptr<internal::ProcessContainmentState> state) noexcept;

    std::unique_ptr<internal::ProcessContainmentState> state_;

    friend class ProcessManager;
    friend struct ProcessContainmentTestHook;
};

struct ProcessContainmentPrepareResult {
    ProcessContainmentStatus status = ProcessContainmentStatus::Failed;
    std::string diagnostic;
    std::optional<PreparedProcessContainment> containment;

    bool succeeded() const noexcept;
};

struct ProcessContainmentStartResult {
    ProcessContainmentStatus status = ProcessContainmentStatus::Failed;
    std::string diagnostic;
    std::optional<ProcessHandle> process;
    std::optional<ProcessBirthIdentity> direct_child_identity;

    bool succeeded() const noexcept;
};

struct ProcessContainmentSnapshotResult {
    ProcessContainmentStatus status = ProcessContainmentStatus::Failed;
    std::string diagnostic;
    std::optional<ProcessContainmentSnapshot> snapshot;

    bool succeeded() const noexcept;
};

struct ProcessContainmentOperationResult {
    ProcessContainmentStatus status = ProcessContainmentStatus::Failed;
    std::string diagnostic;

    bool succeeded() const noexcept;
};

} // namespace lemon::utils
