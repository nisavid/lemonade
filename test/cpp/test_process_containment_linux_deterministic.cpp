#include <lemon/utils/process_manager.h>

#include "process_containment_linux_fake_ops.h"
#include "process_containment_linux_test_support.h"

#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <fcntl.h>
#include <unistd.h>

int run_process_containment_linux_deterministic_race_tests();

namespace {

using namespace std::chrono_literals;
using lemon::utils::ProcessContainmentRequest;
using lemon::utils::ProcessContainmentStartResult;
using lemon::utils::ProcessContainmentStatus;
using lemon::utils::ProcessManager;
using lemon::utils::internal::testing::fail_next_leaf_open;
using lemon::utils::internal::testing::fail_retained_scope_identity;
using lemon::utils::internal::testing::fail_scope_name_binding;
using lemon::utils::internal::testing::force_next_events_populated;
using lemon::utils::internal::testing::hold_child_before_exec;
using lemon::utils::internal::testing::process_containment_linux_fake_drained;
using lemon::utils::internal::testing::process_containment_linux_fake_snapshot;
using process_containment_test::TemporaryDirectory;
using process_containment_test::TestState;
using process_containment_test::begin_scenario;
using process_containment_test::nonce;
using process_containment_test::operation_control;

class ClosedStandardDescriptors {
public:
    ClosedStandardDescriptors() {
        for (std::size_t index = 0; index < backups_.size(); ++index) {
            backups_[index] =
                ::fcntl(static_cast<int>(index), F_DUPFD_CLOEXEC, 3);
            if (backups_[index] < 0) {
                restore();
                return;
            }
        }
        for (std::size_t index = 0; index < backups_.size(); ++index) {
            if (::close(static_cast<int>(index)) != 0) {
                restore();
                return;
            }
        }
        closed_ = true;
    }

    ClosedStandardDescriptors(const ClosedStandardDescriptors &) = delete;
    ClosedStandardDescriptors &
    operator=(const ClosedStandardDescriptors &) = delete;
    ~ClosedStandardDescriptors() { restore(); }

    bool closed() const noexcept { return closed_; }

    void restore() noexcept {
        for (std::size_t index = 0; index < backups_.size(); ++index) {
            if (backups_[index] >= 0) {
                (void)::dup2(backups_[index], static_cast<int>(index));
                (void)::close(backups_[index]);
                backups_[index] = -1;
            }
        }
        closed_ = false;
    }

private:
    std::array<int, 3> backups_{{-1, -1, -1}};
    bool closed_ = false;
};

bool wait_for_fake_drain() {
    for (int attempt = 0; attempt < 500; ++attempt) {
        if (process_containment_linux_fake_drained()) {
            return true;
        }
        std::this_thread::sleep_for(2ms);
    }
    return false;
}

void test_prepare_uses_production_state_machine(TestState &state) {
    TemporaryDirectory root;
    if (!begin_scenario(state, root)) {
        return;
    }
    {
        auto result = ProcessManager::prepare_process_containment(
            ProcessContainmentRequest{root.path(), "deterministic/prepare",
                                      nonce('a')},
            operation_control());
        state.require(result.succeeded(),
                      "public prepare reaches the Linux production state machine");
    }
    const auto observed = process_containment_linux_fake_snapshot();
    state.require(observed.filesystem_checks >= 2U &&
                      observed.identity_checks == 1U &&
                      observed.leaf_creations == 1U,
                  "prepare dispatches filesystem and identity boundaries through fake ops");
}

void test_prepare_failure_rolls_back_leaf(TestState &state) {
    TemporaryDirectory root;
    if (!begin_scenario(state, root)) {
        return;
    }
    fail_next_leaf_open();
    const auto result = ProcessManager::prepare_process_containment(
        ProcessContainmentRequest{root.path(), "deterministic/rollback",
                                  nonce('b')},
        operation_control());
    const auto observed = process_containment_linux_fake_snapshot();
    state.require(result.status == ProcessContainmentStatus::InvalidDelegation,
                  "post-mkdir failure preserves its typed prepare status");
    state.require(observed.leaf_creations == 1U &&
                      observed.leaf_removals == 1U,
                  "post-mkdir failure closes descriptors and removes the leaf");
}

void test_full_lifecycle_and_scope_anomaly_cleanup(TestState &state) {
    TemporaryDirectory root;
    if (!begin_scenario(state, root)) {
        return;
    }
    auto prepared = ProcessManager::prepare_process_containment(
        ProcessContainmentRequest{root.path(), "deterministic/lifecycle",
                                  nonce('c')},
        operation_control());
    state.require(prepared.succeeded(), "lifecycle prepare succeeds");
    if (!prepared.succeeded()) {
        return;
    }

    auto &containment = *prepared.containment;
    auto started = ProcessManager::start_process_contained(
        containment, "/bin/sleep", {"60"}, operation_control(), "", false,
        true);
    state.require(started.succeeded(),
                  "inherit=false filter=true remains a contained-start no-op");
    if (!started.succeeded()) {
        return;
    }

    const auto populated_release = ProcessManager::release_process_containment(
        containment, operation_control());
    state.require(populated_release.status ==
                      ProcessContainmentStatus::ScopeNotEmpty &&
                      containment.active(),
                  "release refuses a live populated scope without closing it");

    const auto populated = ProcessManager::snapshot_process_containment(
        containment, operation_control());
    state.require(populated.succeeded() &&
                      populated.snapshot->members.size() == 1U &&
                      populated.snapshot->members.front() ==
                          *started.direct_child_identity,
                  "snapshot returns the strong identity of the stable member");

    fail_scope_name_binding();
    const auto killed =
        ProcessManager::kill_process_containment(containment, 2s);
    state.require(killed.status == ProcessContainmentStatus::ScopeChanged,
                  "kill reports a name-binding anomaly after exact cleanup");
    (void)ProcessManager::reap_process(*started.process);

    const auto released = ProcessManager::release_process_containment(
        containment, operation_control());
    state.require(released.succeeded() && !containment.active(),
                  "drained scope releases through the production lifecycle");

    const auto observed = process_containment_linux_fake_snapshot();
    state.require(observed.clone_calls == 1U &&
                      observed.clone_contract_failures == 0U,
                  "clone uses CLONE_INTO_CGROUP and CLONE_PIDFD atomically");
    state.require(observed.pidfd_signals >= 1U &&
                      observed.cgroup_kill_writes >= 1U,
                  "scope anomaly still uses exact pidfd and retained kill authority");
    state.require(process_containment_linux_fake_drained(),
                  "successful child has no ownership left after caller reap");
}

void test_exec_failure_transfers_exact_reaping(TestState &state) {
    TemporaryDirectory root;
    if (!begin_scenario(state, root)) {
        return;
    }
    auto prepared = ProcessManager::prepare_process_containment(
        ProcessContainmentRequest{root.path(), "deterministic/exec-failure",
                                  nonce('d')},
        operation_control());
    state.require(prepared.succeeded(), "exec-failure prepare succeeds");
    if (!prepared.succeeded()) {
        return;
    }

    auto &containment = *prepared.containment;
    const auto started = ProcessManager::start_process_contained(
        containment, "/definitely/not/a/lemonade/executable", {},
        operation_control());
    state.require(started.status == ProcessContainmentStatus::ExecFailed &&
                      !started.process && !started.direct_child_identity,
                  "ENOENT handshake returns ExecFailed without leaking a handle");
    state.require(process_containment_linux_fake_snapshot().cgroup_kill_writes ==
                      0U,
                  "failed-start rollback does not kill contaminating members");

    bool released = false;
    for (int attempt = 0; attempt < 200 && !released; ++attempt) {
        const auto result = ProcessManager::release_process_containment(
            containment, operation_control());
        released = result.succeeded();
        if (!released) {
            std::this_thread::sleep_for(2ms);
        }
    }
    state.require(released && !containment.active(),
                  "global rollback reaper publishes completion for retryable release");
    const auto observed = process_containment_linux_fake_snapshot();
    state.require(observed.pidfd_signals >= 1U && observed.pidfd_waits >= 1U &&
                      wait_for_fake_drain(),
                  "failed child is signaled and reaped only through exact pidfd authority");
}

void test_exec_handshake_cancellation_is_bounded(TestState &state) {
    TemporaryDirectory root;
    if (!begin_scenario(state, root)) {
        return;
    }
    auto prepared = ProcessManager::prepare_process_containment(
        ProcessContainmentRequest{root.path(), "deterministic/cancel",
                                  nonce('e')},
        operation_control());
    state.require(prepared.succeeded(), "cancellation prepare succeeds");
    if (!prepared.succeeded()) {
        return;
    }

    auto &containment = *prepared.containment;
    hold_child_before_exec();
    auto cancellation = std::make_shared<std::atomic<bool>>(false);
    std::thread cancel([cancellation] {
        std::this_thread::sleep_for(20ms);
        cancellation->store(true, std::memory_order_release);
    });
    const auto began = std::chrono::steady_clock::now();
    const auto started = ProcessManager::start_process_contained(
        containment, "/bin/sleep", {"60"},
        {std::chrono::steady_clock::now() + 2s, cancellation});
    const auto elapsed = std::chrono::steady_clock::now() - began;
    cancel.join();
    state.require(started.status == ProcessContainmentStatus::Cancelled &&
                      elapsed < 1s,
                  "child-held exec handshake observes cancellation promptly");

    bool released = false;
    for (int attempt = 0; attempt < 200 && !released; ++attempt) {
        const auto result = ProcessManager::release_process_containment(
            containment, operation_control());
        released = result.succeeded();
        if (!released) {
            std::this_thread::sleep_for(2ms);
        }
    }
    state.require(released && wait_for_fake_drain(),
                  "cancelled child and global reaper are fully drained");
}

void test_exec_handshake_timeout_is_bounded(TestState &state) {
    TemporaryDirectory root;
    if (!begin_scenario(state, root)) {
        return;
    }
    auto prepared = ProcessManager::prepare_process_containment(
        ProcessContainmentRequest{root.path(), "deterministic/timeout",
                                  nonce('f')},
        operation_control());
    state.require(prepared.succeeded(), "timeout prepare succeeds");
    if (!prepared.succeeded()) {
        return;
    }

    auto &containment = *prepared.containment;
    hold_child_before_exec();
    const auto began = std::chrono::steady_clock::now();
    const auto started = ProcessManager::start_process_contained(
        containment, "/bin/sleep", {"60"},
        {std::chrono::steady_clock::now() + 30ms, {}});
    const auto elapsed = std::chrono::steady_clock::now() - began;
    state.require(started.status == ProcessContainmentStatus::TimedOut &&
                      elapsed < 1s,
                  "child-held exec handshake observes its deadline promptly");

    bool released = false;
    for (int attempt = 0; attempt < 500 && !released; ++attempt) {
        released = ProcessManager::release_process_containment(
                       containment, operation_control())
                       .succeeded();
        if (!released) {
            std::this_thread::sleep_for(2ms);
        }
    }
    state.require(released && wait_for_fake_drain(),
                  "timed-out child and global reaper are fully drained");
}

void test_closed_standard_descriptors_are_normalized(TestState &state) {
    TemporaryDirectory root;
    if (!begin_scenario(state, root)) {
        return;
    }
    auto prepared = ProcessManager::prepare_process_containment(
        ProcessContainmentRequest{root.path(), "deterministic/closed-stdio",
                                  nonce('0')},
        operation_control());
    state.require(prepared.succeeded(), "closed-stdio prepare succeeds");
    if (!prepared.succeeded()) {
        return;
    }

    auto &containment = *prepared.containment;
    ProcessContainmentStartResult started;
    bool descriptors_closed = false;
    {
        ClosedStandardDescriptors descriptors;
        descriptors_closed = descriptors.closed();
        if (descriptors_closed) {
            started = ProcessManager::start_process_contained(
                containment, "/bin/sleep", {"60"}, operation_control(), "",
                false, true);
        }
        descriptors.restore();
    }
    state.require(descriptors_closed,
                  "test fixture closes all three standard descriptors");
    state.require(started.succeeded(),
                  "internal descriptors are normalized above closed stdio slots");
    if (!started.succeeded()) {
        return;
    }

    const auto killed =
        ProcessManager::kill_process_containment(containment, 2s);
    state.require(killed.succeeded(), "closed-stdio child drains normally");
    (void)ProcessManager::reap_process(*started.process);
    const auto released = ProcessManager::release_process_containment(
        containment, operation_control());
    state.require(released.succeeded() && wait_for_fake_drain(),
                  "closed-stdio lifecycle releases all owned descriptors");
}

void test_release_revalidates_empty_scope(TestState &state) {
    TemporaryDirectory root;
    if (!begin_scenario(state, root)) {
        return;
    }
    auto prepared = ProcessManager::prepare_process_containment(
        ProcessContainmentRequest{root.path(), "deterministic/release",
                                  nonce('1')},
        operation_control());
    state.require(prepared.succeeded(), "release prepare succeeds");
    if (!prepared.succeeded()) {
        return;
    }

    auto &containment = *prepared.containment;
    fail_retained_scope_identity();
    const auto identity_changed = ProcessManager::release_process_containment(
        containment, operation_control());
    state.require(identity_changed.status ==
                      ProcessContainmentStatus::ScopeChanged &&
                      containment.active(),
                  "release retains its lease when retained identity changes");

    fail_scope_name_binding();
    const auto name_changed = ProcessManager::release_process_containment(
        containment, operation_control());
    state.require(name_changed.status == ProcessContainmentStatus::ScopeChanged &&
                      containment.active(),
                  "release retains its lease when the leaf name changes");

    force_next_events_populated();
    const auto became_populated = ProcessManager::release_process_containment(
        containment, operation_control());
    state.require(became_populated.status ==
                      ProcessContainmentStatus::ScopeNotEmpty &&
                      containment.active(),
                  "release refuses a late populated event without unlinking");

    const auto released = ProcessManager::release_process_containment(
        containment, operation_control());
    state.require(released.succeeded() && !containment.active(),
                  "release retries successfully after transient validation faults");
}

} // namespace

int main() {
    TestState state;
    test_prepare_uses_production_state_machine(state);
    test_prepare_failure_rolls_back_leaf(state);
    test_full_lifecycle_and_scope_anomaly_cleanup(state);
    test_exec_failure_transfers_exact_reaping(state);
    test_exec_handshake_cancellation_is_bounded(state);
    test_exec_handshake_timeout_is_bounded(state);
    test_closed_standard_descriptors_are_normalized(state);
    test_release_revalidates_empty_scope(state);
    state.failures += run_process_containment_linux_deterministic_race_tests();
    if (state.failures != 0) {
        std::cerr << state.failures << " deterministic containment test(s) failed\n";
        return 1;
    }
    std::cout << "Deterministic Linux process containment tests passed\n";
    return 0;
}
