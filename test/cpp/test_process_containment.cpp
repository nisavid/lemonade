#include <lemon/utils/process_containment.h>
#include <lemon/utils/process_manager.h>

#include "platform/process_containment_platform.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace lemon::utils {

struct ProcessContainmentTestHook {
    static PreparedProcessContainment make(
        std::unique_ptr<internal::ProcessContainmentState> state) {
        return PreparedProcessContainment(std::move(state));
    }
};

} // namespace lemon::utils

namespace {

using namespace std::chrono_literals;
using lemon::utils::PreparedProcessContainment;
using lemon::utils::ProcessBirthIdentity;
using lemon::utils::ProcessContainmentIdentity;
using lemon::utils::ProcessContainmentOperationResult;
using lemon::utils::ProcessContainmentPrepareResult;
using lemon::utils::ProcessContainmentSnapshot;
using lemon::utils::ProcessContainmentSnapshotResult;
using lemon::utils::ProcessContainmentOperationControl;
using lemon::utils::ProcessContainmentStartResult;
using lemon::utils::ProcessContainmentStatus;
using lemon::utils::ProcessContainmentTestHook;
using lemon::utils::ProcessHandle;
using lemon::utils::ProcessManager;

using Environment = std::vector<std::pair<std::string, std::string>>;
using LegacyStartProcess = ProcessHandle (*)(
    const std::string &, const std::vector<std::string> &,
    const std::string &, bool, bool, const Environment &);

static_assert(std::is_trivially_copyable_v<ProcessHandle>);
static_assert(std::is_standard_layout_v<ProcessHandle>);
static_assert(std::is_copy_constructible_v<ProcessHandle>);
static_assert(std::is_copy_assignable_v<ProcessHandle>);
static_assert(std::is_same_v<decltype(ProcessHandle::handle), void *>);
static_assert(std::is_same_v<decltype(ProcessHandle::pid), int>);
static_assert(std::is_same_v<
              decltype(static_cast<LegacyStartProcess>(
                  &ProcessManager::start_process)),
              LegacyStartProcess>);

using ContainedStartProcess = ProcessContainmentStartResult (*)(
    PreparedProcessContainment &, const std::string &,
    const std::vector<std::string> &, const ProcessContainmentOperationControl &,
    const std::string &, bool, bool, const Environment &);

static_assert(std::is_same_v<
              decltype(static_cast<ContainedStartProcess>(
                  &ProcessManager::start_process_contained)),
              ContainedStartProcess>);

static_assert(!std::is_default_constructible_v<PreparedProcessContainment>);
static_assert(!std::is_copy_constructible_v<PreparedProcessContainment>);
static_assert(!std::is_copy_assignable_v<PreparedProcessContainment>);
static_assert(std::is_nothrow_move_constructible_v<PreparedProcessContainment>);
static_assert(!std::is_move_assignable_v<PreparedProcessContainment>);

struct TestState {
    int failures = 0;

    void require(bool condition, const char *message) {
        if (condition) return;
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
};

struct StartArguments {
    std::string executable;
    std::vector<std::string> args;
    std::string working_dir;
    bool inherit_output = false;
    bool filter_health_logs = false;
    Environment env_vars;
    std::chrono::steady_clock::time_point setup_deadline;
    bool cancellation_requested = false;
};

struct FakeControl {
    bool active = true;
    bool close_on_successful_release = true;
    bool throw_from_start = false;
    bool throw_from_snapshot = false;
    bool throw_from_kill = false;
    bool throw_from_release = false;
    int destructor_count = 0;
    std::vector<std::string> operations;
    std::optional<StartArguments> start_arguments;
    std::vector<std::chrono::milliseconds> kill_timeouts;
    std::deque<ProcessContainmentStartResult> start_results;
    std::deque<ProcessContainmentSnapshotResult> snapshot_results;
    std::deque<ProcessContainmentOperationResult> kill_results;
    std::deque<ProcessContainmentOperationResult> release_results;
};

class FakeProcessContainmentState final
    : public lemon::utils::internal::ProcessContainmentState {
public:
    explicit FakeProcessContainmentState(std::shared_ptr<FakeControl> control)
        : control_(std::move(control)) {}

    ~FakeProcessContainmentState() override { ++control_->destructor_count; }

    bool active() const noexcept override { return control_->active; }

    ProcessContainmentStartResult start(
        const std::string &executable,
        const std::vector<std::string> &args,
        const ProcessContainmentOperationControl &operation_control,
        const std::string &working_dir,
        bool inherit_output,
        bool filter_health_logs,
        const Environment &env_vars) override {
        control_->operations.push_back("start");
        control_->start_arguments = StartArguments{
            executable, args, working_dir, inherit_output, filter_health_logs,
            env_vars, operation_control.deadline,
            operation_control.cancellation &&
                operation_control.cancellation->load(std::memory_order_acquire)};
        if (control_->throw_from_start) {
            throw std::runtime_error("injected start exception");
        }
        if (control_->start_results.empty()) {
            return {ProcessContainmentStatus::Failed,
                    "unexpected start call", std::nullopt, std::nullopt};
        }
        auto result = std::move(control_->start_results.front());
        control_->start_results.pop_front();
        return result;
    }

    ProcessContainmentSnapshotResult
    snapshot(const ProcessContainmentOperationControl &) override {
        control_->operations.push_back("snapshot");
        if (control_->throw_from_snapshot) {
            throw std::runtime_error("injected snapshot exception");
        }
        if (control_->snapshot_results.empty()) {
            return {ProcessContainmentStatus::Failed,
                    "unexpected snapshot call", std::nullopt};
        }
        auto result = std::move(control_->snapshot_results.front());
        control_->snapshot_results.pop_front();
        return result;
    }

    ProcessContainmentOperationResult
    kill(std::chrono::milliseconds timeout) override {
        control_->operations.push_back("kill");
        control_->kill_timeouts.push_back(timeout);
        if (control_->throw_from_kill) {
            throw std::runtime_error("injected kill exception");
        }
        if (control_->kill_results.empty()) {
            return {ProcessContainmentStatus::Failed,
                    "unexpected kill call"};
        }
        auto result = std::move(control_->kill_results.front());
        control_->kill_results.pop_front();
        return result;
    }

    ProcessContainmentOperationResult
    release(const ProcessContainmentOperationControl &) override {
        control_->operations.push_back("release");
        if (control_->throw_from_release) {
            throw std::runtime_error("injected release exception");
        }
        if (control_->release_results.empty()) {
            return {ProcessContainmentStatus::Failed,
                    "unexpected release call"};
        }
        auto result = std::move(control_->release_results.front());
        control_->release_results.pop_front();
        if (result.succeeded() && control_->close_on_successful_release) {
            control_->active = false;
        }
        return result;
    }

private:
    std::shared_ptr<FakeControl> control_;
};

PreparedProcessContainment
make_prepared(const std::shared_ptr<FakeControl> &control) {
    return ProcessContainmentTestHook::make(
        std::make_unique<FakeProcessContainmentState>(control));
}

ProcessContainmentOperationControl operation_control() {
    return {std::chrono::steady_clock::now() + 5s, {}};
}

#if defined(_WIN32) || defined(__APPLE__)
void test_platform_reports_unsupported_containment(TestState &state) {
    const auto result = ProcessManager::prepare_process_containment(
        {}, operation_control());
    state.require(result.status == ProcessContainmentStatus::Unsupported &&
                      !result.succeeded() && !result.containment,
                  "unsupported platforms return no containment authority");
}
#endif

ProcessBirthIdentity birth(
    std::string boot_id, int pid, std::uint64_t start_time_ticks) {
    return {std::move(boot_id), pid, start_time_ticks};
}

ProcessContainmentIdentity containment_identity() {
    return {"boot-alpha", 19, 23, 29, "owner/model-alpha",
            std::string(64, 'a')};
}

ProcessContainmentStartResult successful_start() {
    return {ProcessContainmentStatus::Success,
            {},
            ProcessHandle{nullptr, 4312},
            birth("boot-alpha", 4312, 997)};
}

ProcessContainmentSnapshotResult snapshot_result(
    std::uint64_t generation, std::vector<ProcessBirthIdentity> members) {
    return {ProcessContainmentStatus::Success,
            {},
            ProcessContainmentSnapshot{
                containment_identity(), generation, std::move(members)}};
}

void test_result_helpers_are_fail_closed(TestState &state) {
    ProcessContainmentPrepareResult prepare;
    ProcessContainmentStartResult start;
    ProcessContainmentSnapshotResult snapshot;
    ProcessContainmentOperationResult operation;

    state.require(!prepare.succeeded(),
                  "a default prepare result is not successful");
    state.require(!start.succeeded(),
                  "a default start result is not successful");
    state.require(!snapshot.succeeded(),
                  "a default snapshot result is not successful");
    state.require(!operation.succeeded(),
                  "a default operation result is not successful");

    ProcessContainmentPrepareResult unsupported{
        ProcessContainmentStatus::Unsupported,
        "process containment is unsupported on this platform",
        std::nullopt};
    state.require(!unsupported.succeeded() &&
                      unsupported.status ==
                          ProcessContainmentStatus::Unsupported &&
                      !unsupported.containment.has_value(),
                  "unsupported containment cannot carry success authority");

    auto valid_control = std::make_shared<FakeControl>();
    ProcessContainmentPrepareResult complete_prepare{
        ProcessContainmentStatus::Success,
        {},
        std::optional<PreparedProcessContainment>{
            make_prepared(valid_control)}};
    state.require(complete_prepare.succeeded(),
                  "prepare success requires an active prepared containment");

    auto inactive_control = std::make_shared<FakeControl>();
    inactive_control->active = false;
    ProcessContainmentPrepareResult inactive_prepare{
        ProcessContainmentStatus::Success,
        {},
        std::optional<PreparedProcessContainment>{
            make_prepared(inactive_control)}};
    state.require(!inactive_prepare.succeeded(),
                  "prepare success rejects an inactive containment");

    auto failed_control = std::make_shared<FakeControl>();
    ProcessContainmentPrepareResult failed_prepare{
        ProcessContainmentStatus::InvalidDelegation,
        {},
        std::optional<PreparedProcessContainment>{
            make_prepared(failed_control)}};
    state.require(!failed_prepare.succeeded(),
                  "a prepared payload cannot override a failing status");

    start.status = ProcessContainmentStatus::Success;
    start.process = ProcessHandle{nullptr, 41};
    state.require(!start.succeeded(),
                  "start success requires a direct-child birth identity");
    start.direct_child_identity = birth("boot", 41, 43);
    state.require(start.succeeded(),
                  "a complete successful start result is successful");
    start.status = ProcessContainmentStatus::IdentityChanged;
    state.require(!start.succeeded(),
                  "start payloads cannot override a failing status");

    snapshot.status = ProcessContainmentStatus::Success;
    state.require(!snapshot.succeeded(),
                  "snapshot success requires a complete snapshot");
    snapshot.snapshot = ProcessContainmentSnapshot{
        containment_identity(), 1, {}};
    state.require(snapshot.succeeded(),
                  "a complete successful snapshot result is successful");
    snapshot.status = ProcessContainmentStatus::MembershipChanged;
    state.require(!snapshot.succeeded(),
                  "snapshot payloads cannot override a failing status");

    operation.status = ProcessContainmentStatus::Success;
    state.require(operation.succeeded(),
                  "operation success is explicit");
    operation.status = ProcessContainmentStatus::TimedOut;
    state.require(!operation.succeeded(),
                  "operation failure is fail closed");
}

void test_identity_and_snapshot_contract(TestState &state) {
    const ProcessBirthIdentity first = birth("boot-a", 90, 10);
    const ProcessBirthIdentity same = birth("boot-a", 90, 10);
    const ProcessBirthIdentity other_boot = birth("boot-b", 90, 10);
    const ProcessBirthIdentity other_pid = birth("boot-a", 91, 10);
    const ProcessBirthIdentity other_birth = birth("boot-a", 90, 11);

    state.require(first == same && !(first != same),
                  "equal process identities bind every field");
    state.require(first != other_boot && first != other_pid &&
                      first != other_birth,
                  "boot, pid, and birth token each participate in identity");

    const std::vector<ProcessBirthIdentity> unsorted_members{
        birth("boot-b", 3, 8), birth("boot-a", 7, 5),
        birth("boot-a", 7, 4), birth("boot-a", 2, 9)};
    auto members = unsorted_members;
    std::sort(members.begin(), members.end());
    const std::vector<ProcessBirthIdentity> expected{
        birth("boot-a", 2, 9), birth("boot-a", 7, 4),
        birth("boot-a", 7, 5), birth("boot-b", 3, 8)};
    state.require(members == expected,
                  "membership ordering is canonical across strong identities");

    const ProcessContainmentIdentity identity = containment_identity();
    ProcessContainmentIdentity changed = identity;
    ++changed.mount_id;
    state.require(identity != changed,
                  "containment identity binds the delegated mount");
    changed = identity;
    ++changed.device;
    state.require(identity != changed,
                  "containment identity binds the backing device");
    changed = identity;
    ++changed.inode;
    state.require(identity != changed,
                  "containment identity binds the leaf inode");
    changed = identity;
    changed.boot_id = "boot-beta";
    state.require(identity != changed,
                  "containment identity binds the boot identity");
    changed = identity;
    changed.owner_scope_id = "owner/model-beta";
    state.require(identity != changed,
                  "containment identity binds the owner scope");
    changed = identity;
    changed.nonce_sha256 = std::string(64, 'b');
    state.require(identity != changed,
                  "containment identity binds the preparation nonce");

    const std::vector<ProcessBirthIdentity> unsorted_snapshot_members{
        birth("boot-alpha", 73, 8), birth("boot-alpha", 71, 5),
        birth("boot-alpha", 69, 4), birth("boot-alpha", 67, 9)};
    const std::vector<ProcessBirthIdentity> sorted_snapshot_members{
        birth("boot-alpha", 67, 9), birth("boot-alpha", 69, 4),
        birth("boot-alpha", 71, 5), birth("boot-alpha", 73, 8)};
    auto control = std::make_shared<FakeControl>();
    control->snapshot_results.push_back(
        snapshot_result(1, unsorted_snapshot_members));
    control->snapshot_results.push_back(snapshot_result(2, {}));
    auto prepared = make_prepared(control);

    const auto populated =
        ProcessManager::snapshot_process_containment(prepared,
                                                     operation_control());
    const auto empty = ProcessManager::snapshot_process_containment(
        prepared, operation_control());
    state.require(populated.succeeded() && populated.snapshot->generation == 1 &&
                      populated.snapshot->identity == identity &&
                      populated.snapshot->members == sorted_snapshot_members,
                  "a populated snapshot retains canonical strong membership");
    state.require(empty.succeeded() && empty.snapshot->generation == 2 &&
                      empty.snapshot->members.empty(),
                  "a later membership generation can prove the scope empty");

    auto duplicate_control = std::make_shared<FakeControl>();
    const auto duplicate_identity = birth("boot-alpha", 79, 11);
    duplicate_control->snapshot_results.push_back(
        snapshot_result(1, {duplicate_identity, duplicate_identity}));
    auto duplicate_prepared = make_prepared(duplicate_control);
    const auto duplicate =
        ProcessManager::snapshot_process_containment(duplicate_prepared,
                                                     operation_control());
    state.require(duplicate.status ==
                          ProcessContainmentStatus::MembershipChanged &&
                      !duplicate.snapshot.has_value() &&
                      duplicate_prepared.active(),
                  "duplicate strong identities invalidate the whole snapshot");

    auto recycled_control = std::make_shared<FakeControl>();
    recycled_control->snapshot_results.push_back(snapshot_result(
        1, {birth("boot-alpha", 83, 13), birth("boot-alpha", 83, 17)}));
    auto recycled_prepared = make_prepared(recycled_control);
    const auto recycled = ProcessManager::snapshot_process_containment(
        recycled_prepared, operation_control());
    state.require(recycled.status ==
                          ProcessContainmentStatus::MembershipChanged &&
                      !recycled.snapshot.has_value() && recycled_prepared.active(),
                  "one PID cannot carry two birth identities in one snapshot");
}

void test_start_forwards_arguments_and_lifecycle_order(TestState &state) {
    auto control = std::make_shared<FakeControl>();
    const std::vector<ProcessBirthIdentity> members{
        birth("boot-alpha", 4312, 997)};
    control->start_results.push_back(successful_start());
    control->snapshot_results.push_back(snapshot_result(1, members));
    control->kill_results.push_back(
        {ProcessContainmentStatus::Success, {}});
    control->snapshot_results.push_back(snapshot_result(2, {}));
    control->release_results.push_back(
        {ProcessContainmentStatus::Success, {}});
    auto prepared = make_prepared(control);

    const Environment env{{"ALPHA", "one"}, {"BETA", "two words"}};
    const auto control_deadline = std::chrono::steady_clock::now() + 5s;
    const auto started = ProcessManager::start_process_contained(
        prepared, "/opt/lemon/bin/backend", {"--port", "8123"},
        {control_deadline, {}}, "/var/lib/lemon", true,
        false, env);
    const auto populated =
        ProcessManager::snapshot_process_containment(prepared,
                                                     operation_control());
    const auto killed =
        ProcessManager::kill_process_containment(prepared, 275ms);
    const auto empty = ProcessManager::snapshot_process_containment(
        prepared, operation_control());
    const auto released =
        ProcessManager::release_process_containment(prepared,
                                                    operation_control());

    state.require(started.succeeded() && started.process->pid == 4312 &&
                      *started.direct_child_identity == members.front(),
                  "contained start returns its process and strong direct-child identity");
    state.require(control->start_arguments.has_value(),
                  "contained start reaches the prepared platform state");
    if (control->start_arguments) {
        const auto &actual = *control->start_arguments;
        state.require(actual.executable == "/opt/lemon/bin/backend" &&
                          actual.args ==
                              std::vector<std::string>{"--port", "8123"} &&
                          actual.working_dir == "/var/lib/lemon" &&
                          actual.inherit_output && !actual.filter_health_logs &&
                          actual.env_vars == env &&
                          actual.setup_deadline == control_deadline &&
                          !actual.cancellation_requested,
                      "contained start forwards every launch and control input exactly");
    }
    state.require(populated.succeeded() &&
                      populated.snapshot->generation == 1 &&
                      killed.succeeded() && empty.succeeded() &&
                      empty.snapshot->generation == 2 && released.succeeded(),
                  "the prepared containment supports the complete explicit lifecycle");
    state.require(control->operations ==
                      std::vector<std::string>{
                          "start", "snapshot", "kill", "snapshot", "release"},
                  "containment operations execute in caller order without hidden effects");
    state.require(control->kill_timeouts ==
                      std::vector<std::chrono::milliseconds>{275ms},
                  "the caller-selected kill deadline reaches the platform state exactly");
    state.require(!prepared.active(),
                  "successful empty release closes the prepared containment");

    const std::size_t calls_before_closed_checks = control->operations.size();
    const auto closed_start = ProcessManager::start_process_contained(
        prepared, "ignored", {}, operation_control());
    const auto closed_snapshot =
        ProcessManager::snapshot_process_containment(prepared,
                                                     operation_control());
    const auto closed_kill =
        ProcessManager::kill_process_containment(prepared, 1ms);
    const auto closed_release =
        ProcessManager::release_process_containment(prepared,
                                                    operation_control());
    state.require(closed_start.status == ProcessContainmentStatus::Closed &&
                      closed_snapshot.status == ProcessContainmentStatus::Closed &&
                      closed_kill.status == ProcessContainmentStatus::Closed &&
                      closed_release.status == ProcessContainmentStatus::Closed,
                  "every operation fails closed after release");
    state.require(control->operations.size() == calls_before_closed_checks,
                  "closed containment never dispatches another platform effect");
}

void test_operation_control_fails_before_platform_dispatch(TestState &state) {
    {
        auto control = std::make_shared<FakeControl>();
        auto prepared = make_prepared(control);
        const auto result = ProcessManager::start_process_contained(
            prepared, "backend", {}, operation_control(), {}, true, true);
        state.require(result.status == ProcessContainmentStatus::InvalidRequest &&
                          control->operations.empty() && prepared.active(),
                      "contained start rejects filtered inherited output without an owned pump");
    }

    {
        auto control = std::make_shared<FakeControl>();
        control->start_results.push_back(successful_start());
        auto prepared = make_prepared(control);
        const auto result = ProcessManager::start_process_contained(
            prepared, "backend", {}, operation_control(), {}, false, true);
        state.require(result.succeeded() &&
                          control->operations ==
                              std::vector<std::string>{"start"} &&
                          control->start_arguments.has_value() &&
                          !control->start_arguments->inherit_output &&
                          control->start_arguments->filter_health_logs,
                      "filter selection remains a no-op when output is not inherited");
    }

    {
        auto control = std::make_shared<FakeControl>();
        auto prepared = make_prepared(control);
        const auto result = ProcessManager::start_process_contained(
            prepared, "backend", {},
            {std::chrono::steady_clock::now() - 1ms, {}});
        state.require(result.status == ProcessContainmentStatus::TimedOut &&
                          control->operations.empty() && prepared.active(),
                      "an expired setup deadline prevents contained start");
    }

    {
        auto control = std::make_shared<FakeControl>();
        auto prepared = make_prepared(control);
        const auto result = ProcessManager::start_process_contained(
            prepared, "backend", {},
            {std::chrono::steady_clock::now() + 5s,
             std::make_shared<std::atomic<bool>>(true)});
        state.require(result.status == ProcessContainmentStatus::Cancelled &&
                          control->operations.empty() && prepared.active(),
                      "pre-dispatch cancellation prevents contained start");
    }

    {
        auto control = std::make_shared<FakeControl>();
        auto prepared = make_prepared(control);
        const auto result = ProcessManager::start_process_contained(
            prepared, "backend", {},
            {std::chrono::steady_clock::now() + 1h, {}});
        state.require(result.status == ProcessContainmentStatus::InvalidRequest &&
                          control->operations.empty() && prepared.active(),
                      "an unbounded operation horizon is rejected");
    }

    {
        auto control = std::make_shared<FakeControl>();
        auto prepared = make_prepared(control);
        const auto snapshot = ProcessManager::snapshot_process_containment(
            prepared,
            {std::chrono::steady_clock::now() + 5s,
             std::make_shared<std::atomic<bool>>(true)});
        const auto release = ProcessManager::release_process_containment(
            prepared,
            {std::chrono::steady_clock::now() - 1ms, {}});
        state.require(snapshot.status == ProcessContainmentStatus::Cancelled &&
                          release.status == ProcessContainmentStatus::TimedOut &&
                          control->operations.empty() && prepared.active(),
                      "snapshot and release controls fail before platform dispatch");
    }

    const auto prepare = ProcessManager::prepare_process_containment(
        {}, {std::chrono::steady_clock::now() + 5s,
             std::make_shared<std::atomic<bool>>(true)});
    state.require(prepare.status == ProcessContainmentStatus::Cancelled &&
                      !prepare.containment.has_value(),
                  "cancelled preparation performs no platform mutation");

    auto control = std::make_shared<FakeControl>();
    auto prepared = make_prepared(control);
    const auto unbounded_kill = ProcessManager::kill_process_containment(
        prepared, std::chrono::hours(1));
    state.require(unbounded_kill.status ==
                          ProcessContainmentStatus::InvalidRequest &&
                      control->operations.empty() && prepared.active(),
                  "an unbounded cleanup timeout is rejected before dispatch");
}

void test_start_and_snapshot_errors_are_preserved(TestState &state) {
    const std::vector<ProcessContainmentStatus> start_errors{
        ProcessContainmentStatus::SpawnFailed,
        ProcessContainmentStatus::JoinFailed,
        ProcessContainmentStatus::ExecFailed,
        ProcessContainmentStatus::IdentityUnavailable,
        ProcessContainmentStatus::IdentityChanged,
        ProcessContainmentStatus::MembershipChanged};

    for (const auto expected : start_errors) {
        auto control = std::make_shared<FakeControl>();
        control->start_results.push_back(
            {expected, "injected contained-start failure", std::nullopt,
             std::nullopt});
        auto prepared = make_prepared(control);
        const auto result = ProcessManager::start_process_contained(
            prepared, "backend", {"--serve"}, operation_control());
        state.require(!result.succeeded() && result.status == expected &&
                          result.diagnostic ==
                              "injected contained-start failure" &&
                          !result.process.has_value() &&
                          !result.direct_child_identity.has_value(),
                      "contained-start failures preserve their typed cause");
        state.require(control->operations ==
                          std::vector<std::string>{"start"},
                      "a failed contained start dispatches no later lifecycle operation");
    }

    const std::vector<ProcessContainmentStatus> snapshot_errors{
        ProcessContainmentStatus::IdentityUnavailable,
        ProcessContainmentStatus::IdentityChanged,
        ProcessContainmentStatus::MembershipChanged,
        ProcessContainmentStatus::Failed};
    for (const auto expected : snapshot_errors) {
        auto control = std::make_shared<FakeControl>();
        control->snapshot_results.push_back(
            {expected, "injected snapshot failure", std::nullopt});
        auto prepared = make_prepared(control);
        const auto result =
            ProcessManager::snapshot_process_containment(prepared,
                                                         operation_control());
        state.require(!result.succeeded() && result.status == expected &&
                          result.diagnostic == "injected snapshot failure" &&
                          !result.snapshot.has_value(),
                      "snapshot failures preserve identity and membership causes");
    }
}

void test_wrapper_rejects_malformed_platform_results(TestState &state) {
    {
        auto control = std::make_shared<FakeControl>();
        control->start_results.push_back(
            {ProcessContainmentStatus::Success,
             {},
             ProcessHandle{nullptr, 51},
             std::nullopt});
        control->start_results.push_back(
            {ProcessContainmentStatus::Success,
             {},
             std::nullopt,
             birth("boot", 52, 53)});
        control->start_results.push_back(
            {ProcessContainmentStatus::ExecFailed,
             "platform attached payloads to a failed start",
             ProcessHandle{nullptr, 54},
             birth("boot", 54, 55)});
        auto prepared = make_prepared(control);

        const auto missing_identity = ProcessManager::start_process_contained(
            prepared, "backend", {}, operation_control());
        const auto missing_process = ProcessManager::start_process_contained(
            prepared, "backend", {}, operation_control());
        const auto failed_with_payload =
            ProcessManager::start_process_contained(prepared, "backend", {},
                                                    operation_control());

        state.require(
            missing_identity.status == ProcessContainmentStatus::Failed &&
                !missing_identity.process.has_value() &&
                !missing_identity.direct_child_identity.has_value() &&
                !missing_identity.diagnostic.empty(),
            "incomplete start success is normalized and stripped of authority");
        state.require(
            missing_process.status == ProcessContainmentStatus::Failed &&
                !missing_process.process.has_value() &&
                !missing_process.direct_child_identity.has_value() &&
                !missing_process.diagnostic.empty(),
            "either missing start-success payload fails closed");
        state.require(
            failed_with_payload.status != ProcessContainmentStatus::Success &&
                !failed_with_payload.process.has_value() &&
                !failed_with_payload.direct_child_identity.has_value(),
            "failed start results cannot leak process authority payloads");
        state.require(prepared.active(),
                      "malformed start results preserve containment for cleanup");
    }

    {
        auto control = std::make_shared<FakeControl>();
        control->snapshot_results.push_back(
            {ProcessContainmentStatus::Success, {}, std::nullopt});
        control->snapshot_results.push_back(
            {ProcessContainmentStatus::MembershipChanged,
             "platform attached a snapshot to failure",
             ProcessContainmentSnapshot{
                 containment_identity(), 1, {birth("boot", 61, 67)}}});
        auto prepared = make_prepared(control);

        const auto incomplete =
            ProcessManager::snapshot_process_containment(prepared,
                                                         operation_control());
        const auto failed_with_payload =
            ProcessManager::snapshot_process_containment(prepared,
                                                         operation_control());

        state.require(incomplete.status == ProcessContainmentStatus::Failed &&
                          !incomplete.snapshot.has_value() &&
                          !incomplete.diagnostic.empty(),
                      "incomplete snapshot success is normalized to failure");
        state.require(
            failed_with_payload.status != ProcessContainmentStatus::Success &&
                !failed_with_payload.snapshot.has_value(),
            "failed snapshot results cannot leak membership authority");
        state.require(prepared.active(),
                      "malformed snapshots preserve containment for cleanup");
    }

    {
        auto control = std::make_shared<FakeControl>();
        control->close_on_successful_release = false;
        control->release_results.push_back(
            {ProcessContainmentStatus::Success, {}});
        auto prepared = make_prepared(control);

        const auto result =
            ProcessManager::release_process_containment(prepared,
                                                        operation_control());
        state.require(result.status == ProcessContainmentStatus::Failed &&
                          !result.diagnostic.empty() && prepared.active(),
                      "release success is rejected while the platform state remains active");
    }
}

void test_platform_exceptions_fail_closed_and_remain_retryable(
    TestState &state) {
    {
        auto control = std::make_shared<FakeControl>();
        control->throw_from_start = true;
        auto prepared = make_prepared(control);
        ProcessContainmentStartResult result;
        bool escaped = false;
        try {
            result = ProcessManager::start_process_contained(
                prepared, "backend", {"--serve"}, operation_control());
        } catch (...) {
            escaped = true;
        }
        state.require(!escaped &&
                          result.status == ProcessContainmentStatus::Failed &&
                          !result.process.has_value() &&
                          !result.direct_child_identity.has_value() &&
                          !result.diagnostic.empty() && prepared.active(),
                      "start exceptions become retryable failure results");
    }

    {
        auto control = std::make_shared<FakeControl>();
        control->throw_from_snapshot = true;
        auto prepared = make_prepared(control);
        ProcessContainmentSnapshotResult result;
        bool escaped = false;
        try {
            result = ProcessManager::snapshot_process_containment(
                prepared, operation_control());
        } catch (...) {
            escaped = true;
        }
        state.require(!escaped &&
                          result.status == ProcessContainmentStatus::Failed &&
                          !result.snapshot.has_value() &&
                          !result.diagnostic.empty() && prepared.active(),
                      "snapshot exceptions become retryable failure results");
    }

    {
        auto control = std::make_shared<FakeControl>();
        control->throw_from_kill = true;
        auto prepared = make_prepared(control);
        ProcessContainmentOperationResult result;
        bool escaped = false;
        try {
            result =
                ProcessManager::kill_process_containment(prepared, 37ms);
        } catch (...) {
            escaped = true;
        }
        state.require(!escaped &&
                          result.status == ProcessContainmentStatus::Failed &&
                          !result.diagnostic.empty() && prepared.active() &&
                          control->kill_timeouts ==
                              std::vector<std::chrono::milliseconds>{37ms},
                      "kill exceptions become retryable failure results");
    }

    {
        auto control = std::make_shared<FakeControl>();
        control->throw_from_release = true;
        auto prepared = make_prepared(control);
        ProcessContainmentOperationResult result;
        bool escaped = false;
        try {
            result = ProcessManager::release_process_containment(
                prepared, operation_control());
        } catch (...) {
            escaped = true;
        }
        state.require(!escaped &&
                          result.status == ProcessContainmentStatus::Failed &&
                          !result.diagnostic.empty() && prepared.active(),
                      "release exceptions become retryable failure results");
    }
}

void test_release_requires_empty_and_kill_is_retryable(TestState &state) {
    auto control = std::make_shared<FakeControl>();
    control->release_results.push_back(
        {ProcessContainmentStatus::ScopeNotEmpty,
         "process containment is still populated"});
    control->kill_results.push_back(
        {ProcessContainmentStatus::TimedOut,
         "timed out waiting for process containment to become empty"});
    control->kill_results.push_back(
        {ProcessContainmentStatus::Failed, "cgroup.kill write failed"});
    control->kill_results.push_back(
        {ProcessContainmentStatus::Success, {}});
    control->snapshot_results.push_back(snapshot_result(1, {}));
    control->release_results.push_back(
        {ProcessContainmentStatus::Success, {}});
    auto prepared = make_prepared(control);

    const auto nonempty =
        ProcessManager::release_process_containment(prepared,
                                                    operation_control());
    state.require(nonempty.status == ProcessContainmentStatus::ScopeNotEmpty &&
                      prepared.active(),
                  "release refuses a populated scope and leaves it retryable");

    const auto timed_out =
        ProcessManager::kill_process_containment(prepared, 17ms);
    const auto failed =
        ProcessManager::kill_process_containment(prepared, 29ms);
    const auto killed =
        ProcessManager::kill_process_containment(prepared, 41ms);
    state.require(timed_out.status == ProcessContainmentStatus::TimedOut &&
                      failed.status == ProcessContainmentStatus::Failed &&
                      killed.succeeded() && prepared.active(),
                  "kill timeout and I/O failure retain authority for an explicit retry");
    state.require(control->kill_timeouts ==
                      std::vector<std::chrono::milliseconds>{17ms, 29ms, 41ms},
                  "each kill retry retains its exact bounded deadline");

    const auto empty = ProcessManager::snapshot_process_containment(
        prepared, operation_control());
    const auto released =
        ProcessManager::release_process_containment(prepared,
                                                    operation_control());
    state.require(empty.succeeded() && empty.snapshot->members.empty() &&
                      released.succeeded() && !prepared.active(),
                  "only an empty snapshot followed by release closes the scope");
    state.require(control->operations ==
                      std::vector<std::string>{
                          "release", "kill", "kill", "kill", "snapshot",
                          "release"},
                  "release, kill retries, empty proof, and close stay explicit");
}

void test_move_and_destruction_never_kill(TestState &state) {
    auto control = std::make_shared<FakeControl>();
    {
        auto original = make_prepared(control);
        auto moved = std::move(original);
        state.require(!original.active() && moved.active(),
                      "move transfers the sole active containment owner");
    }

    state.require(control->destructor_count == 1,
                  "the platform state has exactly one owner after a move");
    state.require(control->operations.empty() &&
                      control->kill_timeouts.empty(),
                  "destruction performs no implicit kill or release");
}

} // namespace

int main() {
    TestState state;
#if defined(_WIN32) || defined(__APPLE__)
    test_platform_reports_unsupported_containment(state);
#endif
    test_result_helpers_are_fail_closed(state);
    test_identity_and_snapshot_contract(state);
    test_start_forwards_arguments_and_lifecycle_order(state);
    test_operation_control_fails_before_platform_dispatch(state);
    test_start_and_snapshot_errors_are_preserved(state);
    test_wrapper_rejects_malformed_platform_results(state);
    test_platform_exceptions_fail_closed_and_remain_retryable(state);
    test_release_requires_empty_and_kill_is_retryable(state);
    test_move_and_destruction_never_kill(state);

    if (state.failures == 0) {
        std::cout << "All process containment tests passed\n";
        return 0;
    }
    std::cerr << state.failures << " process containment test(s) failed\n";
    return 1;
}
