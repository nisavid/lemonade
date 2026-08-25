#include <lemon/utils/process_manager.h>
#include <lemon/utils/process_platform.h>

#include "platform/process_containment_platform.h"

#include <algorithm>
#include <optional>
#include <tuple>

namespace lemon {
namespace utils {

namespace {

constexpr auto kMaximumContainmentOperationHorizon =
    std::chrono::seconds(30);

struct OperationControlFailure {
    ProcessContainmentStatus status;
    std::string diagnostic;
};

std::optional<OperationControlFailure> operation_control_failure(
    const ProcessContainmentOperationControl &control) {
    if (control.cancellation &&
        control.cancellation->load(std::memory_order_acquire)) {
        return OperationControlFailure{
            ProcessContainmentStatus::Cancelled,
            "process containment operation cancelled"};
    }
    const auto now = std::chrono::steady_clock::now();
    if (control.deadline <= now) {
        return OperationControlFailure{
            ProcessContainmentStatus::TimedOut,
            "process containment operation deadline expired"};
    }
    if (control.deadline > now + kMaximumContainmentOperationHorizon) {
        return OperationControlFailure{
            ProcessContainmentStatus::InvalidRequest,
            "process containment operation deadline is too distant"};
    }
    return std::nullopt;
}

bool sha256_is_canonical(const std::string &value) {
    return value.size() == 64 &&
           std::all_of(value.begin(), value.end(), [](char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

bool birth_identity_is_valid(const ProcessBirthIdentity &identity) {
    return !identity.boot_id.empty() && identity.pid > 0 &&
           identity.start_time_ticks > 0;
}

bool containment_identity_is_valid(
    const ProcessContainmentIdentity &identity) {
    return !identity.boot_id.empty() && identity.mount_id > 0 &&
           identity.inode > 0 && !identity.owner_scope_id.empty() &&
           sha256_is_canonical(identity.nonce_sha256);
}

ProcessContainmentStartResult normalize_start_result(
    ProcessContainmentStartResult result) {
    if (result.status != ProcessContainmentStatus::Success) {
        result.process.reset();
        result.direct_child_identity.reset();
        return result;
    }
    if (!result.process || !result.direct_child_identity ||
        result.process->pid <= 0 ||
        result.direct_child_identity->pid != result.process->pid ||
        !birth_identity_is_valid(*result.direct_child_identity)) {
        return {ProcessContainmentStatus::Failed,
                "platform returned incomplete contained process",
                std::nullopt, std::nullopt};
    }
    return result;
}

ProcessContainmentSnapshotResult normalize_snapshot_result(
    ProcessContainmentSnapshotResult result) {
    if (result.status != ProcessContainmentStatus::Success) {
        result.snapshot.reset();
        return result;
    }
    if (!result.snapshot || result.snapshot->generation == 0 ||
        !containment_identity_is_valid(result.snapshot->identity)) {
        return {ProcessContainmentStatus::Failed,
                "platform returned incomplete containment snapshot",
                std::nullopt};
    }

    auto &members = result.snapshot->members;
    if (std::any_of(members.begin(), members.end(),
                    [&](const ProcessBirthIdentity &identity) {
                        return !birth_identity_is_valid(identity) ||
                               identity.boot_id !=
                                   result.snapshot->identity.boot_id;
                    })) {
        return {ProcessContainmentStatus::IdentityChanged,
                "platform returned invalid containment membership",
                std::nullopt};
    }
    std::sort(members.begin(), members.end());
    if (std::adjacent_find(
            members.begin(), members.end(),
            [](const ProcessBirthIdentity &left,
               const ProcessBirthIdentity &right) {
                return left.boot_id == right.boot_id && left.pid == right.pid;
            }) != members.end()) {
        return {ProcessContainmentStatus::MembershipChanged,
                "platform returned conflicting containment membership",
                std::nullopt};
    }
    return result;
}

} // namespace

bool ProcessBirthIdentity::operator==(
    const ProcessBirthIdentity &other) const noexcept {
    return std::tie(boot_id, pid, start_time_ticks) ==
           std::tie(other.boot_id, other.pid, other.start_time_ticks);
}

bool ProcessBirthIdentity::operator!=(
    const ProcessBirthIdentity &other) const noexcept {
    return !(*this == other);
}

bool ProcessBirthIdentity::operator<(
    const ProcessBirthIdentity &other) const noexcept {
    return std::tie(boot_id, pid, start_time_ticks) <
           std::tie(other.boot_id, other.pid, other.start_time_ticks);
}

bool ProcessContainmentIdentity::operator==(
    const ProcessContainmentIdentity &other) const noexcept {
    return std::tie(boot_id, mount_id, device, inode, owner_scope_id,
                    nonce_sha256) ==
           std::tie(other.boot_id, other.mount_id, other.device, other.inode,
                    other.owner_scope_id, other.nonce_sha256);
}

bool ProcessContainmentIdentity::operator!=(
    const ProcessContainmentIdentity &other) const noexcept {
    return !(*this == other);
}

PreparedProcessContainment::PreparedProcessContainment(
    std::unique_ptr<internal::ProcessContainmentState> state) noexcept
    : state_(std::move(state)) {}

PreparedProcessContainment::PreparedProcessContainment(
    PreparedProcessContainment &&) noexcept = default;

PreparedProcessContainment::~PreparedProcessContainment() = default;

bool PreparedProcessContainment::active() const noexcept {
    return state_ && state_->active();
}

bool ProcessContainmentPrepareResult::succeeded() const noexcept {
    return status == ProcessContainmentStatus::Success &&
           containment.has_value() && containment->active();
}

bool ProcessContainmentStartResult::succeeded() const noexcept {
    return status == ProcessContainmentStatus::Success && process.has_value() &&
           direct_child_identity.has_value();
}

bool ProcessContainmentSnapshotResult::succeeded() const noexcept {
    return status == ProcessContainmentStatus::Success && snapshot.has_value();
}

bool ProcessContainmentOperationResult::succeeded() const noexcept {
    return status == ProcessContainmentStatus::Success;
}

ProcessHandle ProcessManager::start_process(
    const std::string& executable,
    const std::vector<std::string>& args,
    const std::string& working_dir,
    bool inherit_output,
    bool filter_health_logs,
    const std::vector<std::pair<std::string, std::string>>& env_vars) {

    auto platform = create_process_platform();
    return platform->spawn(executable, args, working_dir, inherit_output, filter_health_logs, env_vars);
}

ProcessContainmentPrepareResult ProcessManager::prepare_process_containment(
    const ProcessContainmentRequest &request,
    const ProcessContainmentOperationControl &control) {
    internal::ProcessContainmentPlatformPrepareResult platform_result;
    try {
        if (const auto failure = operation_control_failure(control)) {
            return {failure->status, failure->diagnostic, std::nullopt};
        }
        platform_result =
            internal::prepare_process_containment_platform(request, control);
    } catch (...) {
        return {ProcessContainmentStatus::Failed,
                "process containment preparation failed", std::nullopt};
    }

    ProcessContainmentPrepareResult result;
    result.status = platform_result.status;
    result.diagnostic = std::move(platform_result.diagnostic);
    if (!platform_result.succeeded()) {
        if (result.status == ProcessContainmentStatus::Success) {
            result.status = ProcessContainmentStatus::Failed;
            result.diagnostic = "platform returned incomplete containment";
        }
        return result;
    }

    auto containment = PreparedProcessContainment(
        std::move(platform_result.state));
    result.containment.emplace(std::move(containment));
    return result;
}

ProcessContainmentStartResult ProcessManager::start_process_contained(
    PreparedProcessContainment &containment,
    const std::string &executable,
    const std::vector<std::string> &args,
    const ProcessContainmentOperationControl &control,
    const std::string &working_dir,
    bool inherit_output,
    bool filter_health_logs,
    const std::vector<std::pair<std::string, std::string>> &env_vars) {
    if (!containment.active()) {
        return {ProcessContainmentStatus::Closed,
                "process containment is closed", std::nullopt, std::nullopt};
    }
    if (inherit_output && filter_health_logs) {
        return {ProcessContainmentStatus::InvalidRequest,
                "contained process start does not own a filtered output pump",
                std::nullopt, std::nullopt};
    }
    try {
        if (const auto failure = operation_control_failure(control)) {
            return {failure->status, failure->diagnostic, std::nullopt,
                    std::nullopt};
        }
        return normalize_start_result(containment.state_->start(
            executable, args, control, working_dir, inherit_output,
            filter_health_logs, env_vars));
    } catch (...) {
        return {ProcessContainmentStatus::Failed,
                "contained process start failed", std::nullopt,
                std::nullopt};
    }
}

ProcessContainmentSnapshotResult
ProcessManager::snapshot_process_containment(
    PreparedProcessContainment &containment,
    const ProcessContainmentOperationControl &control) {
    if (!containment.active()) {
        return {ProcessContainmentStatus::Closed,
                "process containment is closed", std::nullopt};
    }
    try {
        if (const auto failure = operation_control_failure(control)) {
            return {failure->status, failure->diagnostic, std::nullopt};
        }
        return normalize_snapshot_result(
            containment.state_->snapshot(control));
    } catch (...) {
        return {ProcessContainmentStatus::Failed,
                "process containment snapshot failed", std::nullopt};
    }
}

ProcessContainmentOperationResult ProcessManager::kill_process_containment(
    PreparedProcessContainment &containment,
    std::chrono::milliseconds timeout) {
    if (!containment.active()) {
        return {ProcessContainmentStatus::Closed,
                "process containment is closed"};
    }
    if (timeout <= std::chrono::milliseconds::zero()) {
        return {ProcessContainmentStatus::InvalidRequest,
                "process containment kill timeout must be positive"};
    }
    if (timeout > std::chrono::duration_cast<std::chrono::milliseconds>(
                      kMaximumContainmentOperationHorizon)) {
        return {ProcessContainmentStatus::InvalidRequest,
                "process containment kill timeout is too long"};
    }
    try {
        return containment.state_->kill(timeout);
    } catch (...) {
        return {ProcessContainmentStatus::Failed,
                "process containment kill failed"};
    }
}

ProcessContainmentOperationResult ProcessManager::release_process_containment(
    PreparedProcessContainment &containment,
    const ProcessContainmentOperationControl &control) {
    if (!containment.active()) {
        return {ProcessContainmentStatus::Closed,
                "process containment is closed"};
    }
    try {
        if (const auto failure = operation_control_failure(control)) {
            return {failure->status, failure->diagnostic};
        }
        auto result = containment.state_->release(control);
        if (result.status == ProcessContainmentStatus::Success &&
            containment.active()) {
            return {ProcessContainmentStatus::Failed,
                    "platform did not close process containment"};
        }
        return result;
    } catch (...) {
        return {ProcessContainmentStatus::Failed,
                "process containment release failed"};
    }
}

void ProcessManager::stop_process(ProcessHandle handle) {
    auto platform = create_process_platform();
    platform->terminate(handle);
}

bool ProcessManager::is_running(ProcessHandle handle) {
    auto platform = create_process_platform();
    return platform->is_running(handle);
}

int ProcessManager::get_exit_code(ProcessHandle handle) {
    auto platform = create_process_platform();
    return platform->get_exit_code(handle);
}

int ProcessManager::wait_for_exit(ProcessHandle handle, int timeout_seconds) {
    auto platform = create_process_platform();
    return platform->wait_for_exit(handle, timeout_seconds);
}

int ProcessManager::reap_process(ProcessHandle handle) {
    auto platform = create_process_platform();
    return platform->reap(handle);
}

std::string ProcessManager::read_output(ProcessHandle handle, int max_bytes) {
    // Note: This is a simplified version. Full implementation would need pipes
    // for stdout/stderr capture during process creation.
    return "";
}

int ProcessManager::run_process_with_output(
    const std::string& executable,
    const std::vector<std::string>& args,
    OutputLineCallback on_line,
    const std::string& working_dir,
    int timeout_seconds,
    bool capture_stderr) {

    auto platform = create_process_platform();
    return platform->run_with_output(executable, args, on_line, working_dir, timeout_seconds, capture_stderr);
}

void ProcessManager::kill_process(ProcessHandle handle) {
    auto platform = create_process_platform();
    platform->kill(handle);
}

void ProcessManager::terminate_process(ProcessHandle handle) {
    auto platform = create_process_platform();
    platform->terminate_without_cleanup(handle);
}

int ProcessManager::find_free_port(int start_port) {
    auto platform = create_process_platform();
    return platform->find_free_port(start_port);
}

int ProcessManager::run_command(const std::string& command, std::string& output, int timeout_seconds) {
    auto platform = create_process_platform();
    return platform->run_command(command, output, timeout_seconds);
}

} // namespace utils
} // namespace lemon
