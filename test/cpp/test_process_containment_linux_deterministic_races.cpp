#include <lemon/utils/process_manager.h>

#include "process_containment_linux_fake_ops.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

#include <unistd.h>

namespace {

using namespace std::chrono_literals;
using lemon::utils::ProcessContainmentOperationControl;
using lemon::utils::ProcessContainmentPrepareResult;
using lemon::utils::ProcessContainmentRequest;
using lemon::utils::ProcessContainmentStartResult;
using lemon::utils::ProcessContainmentStatus;
using lemon::utils::ProcessManager;
using lemon::utils::internal::testing::change_membership_on_procs_rewind;
using lemon::utils::internal::testing::corrupt_identity_on_proc_stat_open;
using lemon::utils::internal::testing::disable_fake_clock;
using lemon::utils::internal::testing::duplicate_member_on_procs_rewind;
using lemon::utils::internal::testing::enable_fake_clock;
using lemon::utils::internal::testing::fail_retained_scope_identity;
using lemon::utils::internal::testing::force_scope_populated;
using lemon::utils::internal::testing::process_containment_linux_fake_snapshot;
using lemon::utils::internal::testing::report_pidfd_exit_on_poll;
using lemon::utils::internal::testing::reset_process_containment_linux_fake;
using lemon::utils::internal::testing::rewrite_proc_stat_comm;

struct TestState {
    int failures = 0;

    void require(bool condition, const char *message) {
        if (condition) {
            return;
        }
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
};

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        std::string pattern = "/tmp/lemonade-containment-race-XXXXXX";
        if (char *created = ::mkdtemp(pattern.data())) {
            path_ = created;
        }
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path &path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

struct RunningScenario {
    ProcessContainmentPrepareResult prepared;
    ProcessContainmentStartResult started;
};

ProcessContainmentOperationControl operation_control() {
    return {std::chrono::steady_clock::now() + 5s, {}};
}

std::string nonce(char value) { return std::string(64U, value); }

RunningScenario start_running_scenario(TestState &state,
                                       const TemporaryDirectory &root,
                                       const char *owner_scope_id,
                                       char nonce_value) {
    auto prepared = ProcessManager::prepare_process_containment(
        ProcessContainmentRequest{root.path(), owner_scope_id,
                                  nonce(nonce_value)},
        operation_control());
    RunningScenario scenario{std::move(prepared), {}};
    state.require(scenario.prepared.succeeded(),
                  "race scenario containment preparation succeeds");
    if (!scenario.prepared.succeeded()) {
        return scenario;
    }
    scenario.started = ProcessManager::start_process_contained(
        *scenario.prepared.containment, "/bin/sleep", {"60"},
        operation_control());
    state.require(scenario.started.succeeded(),
                  "race scenario contained child starts");
    return scenario;
}

void reap_and_release(TestState &state, RunningScenario &scenario) {
    if (!scenario.prepared.containment) {
        return;
    }
    if (scenario.started.process) {
        if (ProcessManager::reap_process(*scenario.started.process) < 0) {
            ProcessManager::kill_process(*scenario.started.process);
        }
    }
    auto released = ProcessManager::release_process_containment(
        *scenario.prepared.containment, operation_control());
    if (!released.succeeded() && scenario.started.process) {
        (void)ProcessManager::kill_process_containment(
            *scenario.prepared.containment, 2s);
        released = ProcessManager::release_process_containment(
            *scenario.prepared.containment, operation_control());
    }
    state.require(released.succeeded(),
                  "race scenario releases its drained containment");
}

void kill_reap_and_release(TestState &state, RunningScenario &scenario) {
    if (!scenario.prepared.containment || !scenario.started.process) {
        return;
    }
    const auto killed = ProcessManager::kill_process_containment(
        *scenario.prepared.containment, 2s);
    state.require(killed.succeeded(),
                  "race scenario cleanup drains the containment");
    reap_and_release(state, scenario);
}

using OrdinalInjection = void (*)(std::size_t);

void test_snapshot_rejection(TestState &state, const char *owner_scope_id,
                             char nonce_value, OrdinalInjection inject,
                             std::size_t ordinal,
                             ProcessContainmentStatus expected_status,
                             const char *rejection_message) {
    if (!reset_process_containment_linux_fake()) {
        state.require(false,
                      "snapshot scenario starts without retained fake state");
        return;
    }
    TemporaryDirectory root;
    auto scenario =
        start_running_scenario(state, root, owner_scope_id, nonce_value);
    if (!scenario.started.succeeded()) {
        return;
    }

    inject(ordinal);
    const auto rejected = ProcessManager::snapshot_process_containment(
        *scenario.prepared.containment, operation_control());
    state.require(rejected.status == expected_status, rejection_message);

    const auto stable = ProcessManager::snapshot_process_containment(
        *scenario.prepared.containment, operation_control());
    state.require(stable.succeeded() && stable.snapshot->generation == 2U,
                  "failed snapshot consumes one generation before stable retry");

    kill_reap_and_release(state, scenario);
}

void test_snapshot_rejects_final_membership_churn(TestState &state) {
    test_snapshot_rejection(
        state, "deterministic/final-membership-churn", 'f',
        change_membership_on_procs_rewind, 3U,
        ProcessContainmentStatus::MembershipChanged,
        "snapshot rejects membership change on its final confirmation read");
}

void test_snapshot_rejects_duplicate_pid(TestState &state) {
    test_snapshot_rejection(
        state, "deterministic/duplicate-pid", '0',
        duplicate_member_on_procs_rewind, 1U,
        ProcessContainmentStatus::MembershipChanged,
        "snapshot rejects a duplicate PID within one raw membership read");
}

void test_snapshot_rejects_identity_change(TestState &state) {
    test_snapshot_rejection(
        state, "deterministic/identity-change", '1',
        corrupt_identity_on_proc_stat_open, 2U,
        ProcessContainmentStatus::IdentityChanged,
        "snapshot rejects a changed start time during identity confirmation");
}

void test_snapshot_rejects_final_pidfd_exit(TestState &state) {
    test_snapshot_rejection(
        state, "deterministic/final-pidfd-exit", '2',
        report_pidfd_exit_on_poll, 3U,
        ProcessContainmentStatus::IdentityChanged,
        "snapshot rejects exit after final membership equality");
}

void test_proc_stat_parser_uses_the_final_comm_delimiter(TestState &state) {
    if (!reset_process_containment_linux_fake()) {
        state.require(false,
                      "proc-stat scenario starts without retained fake state");
        return;
    }
    TemporaryDirectory root;
    auto scenario = start_running_scenario(
        state, root, "deterministic/proc-stat-comm", '3');
    if (!scenario.started.succeeded()) {
        return;
    }

    rewrite_proc_stat_comm();
    const auto snapshot = ProcessManager::snapshot_process_containment(
        *scenario.prepared.containment, operation_control());
    state.require(snapshot.succeeded() && snapshot.snapshot->members.size() == 1U &&
                      snapshot.snapshot->members.front() ==
                          *scenario.started.direct_child_identity,
                  "process stat parsing tolerates closing delimiters in comm");

    kill_reap_and_release(state, scenario);
}

void test_kill_cleans_after_retained_identity_anomaly(TestState &state) {
    if (!reset_process_containment_linux_fake()) {
        state.require(false,
                      "retained-identity scenario starts without fake state");
        return;
    }
    TemporaryDirectory root;
    auto scenario = start_running_scenario(
        state, root, "deterministic/retained-identity", '4');
    if (!scenario.started.succeeded()) {
        return;
    }

    fail_retained_scope_identity();
    const auto killed = ProcessManager::kill_process_containment(
        *scenario.prepared.containment, 2s);
    const auto observed = process_containment_linux_fake_snapshot();
    state.require(killed.status == ProcessContainmentStatus::ScopeChanged,
                  "kill reports retained-scope identity change after cleanup");
    state.require(observed.pidfd_signals >= 1U &&
                      observed.cgroup_kill_writes >= 1U,
                  "retained-scope anomaly cannot gate exact cleanup authority");

    reap_and_release(state, scenario);
}

void test_kill_cleans_live_direct_child_missing_from_scope(TestState &state) {
    if (!reset_process_containment_linux_fake()) {
        state.require(false,
                      "direct-escape scenario starts without retained fake state");
        return;
    }
    TemporaryDirectory root;
    auto scenario = start_running_scenario(
        state, root, "deterministic/direct-escape", '6');
    if (!scenario.started.succeeded()) {
        return;
    }

    change_membership_on_procs_rewind(1U);
    const auto killed = ProcessManager::kill_process_containment(
        *scenario.prepared.containment, 2s);
    const auto observed = process_containment_linux_fake_snapshot();
    state.require(killed.status == ProcessContainmentStatus::MembershipChanged,
                  "kill reports a live direct child missing from its scope");
    state.require(observed.pidfd_signals >= 1U &&
                      observed.cgroup_kill_writes >= 1U,
                  "direct-child escape cannot gate exact cleanup authority");

    reap_and_release(state, scenario);
}

void test_repeated_kill_respects_one_deadline(TestState &state) {
    if (!reset_process_containment_linux_fake()) {
        state.require(false,
                      "kill-deadline scenario starts without retained fake state");
        return;
    }
    TemporaryDirectory root;
    auto scenario = start_running_scenario(
        state, root, "deterministic/kill-deadline", '5');
    if (!scenario.started.succeeded()) {
        return;
    }

    force_scope_populated(true);
    enable_fake_clock();
    const auto timed_out = ProcessManager::kill_process_containment(
        *scenario.prepared.containment, 25ms);
    const auto timed_out_observed =
        process_containment_linux_fake_snapshot();
    disable_fake_clock();
    force_scope_populated(false);

    state.require(timed_out.status == ProcessContainmentStatus::TimedOut,
                  "kill reports timeout while the scope remains populated");
    state.require(timed_out_observed.cgroup_kill_writes >= 2U,
                  "kill retries cgroup.kill while population remains");
    state.require(timed_out_observed.sleep_calls >= 1U &&
                      timed_out_observed.fake_clock_elapsed_ms <= 25U,
                  "kill polling never sleeps beyond its original deadline");

    const auto repeated = ProcessManager::kill_process_containment(
        *scenario.prepared.containment, 2s);
    state.require(repeated.succeeded(),
                  "kill can be retried after a bounded timeout");

    reap_and_release(state, scenario);
}

} // namespace

int run_process_containment_linux_deterministic_race_tests() {
    TestState state;
    test_snapshot_rejects_final_membership_churn(state);
    test_snapshot_rejects_duplicate_pid(state);
    test_snapshot_rejects_identity_change(state);
    test_snapshot_rejects_final_pidfd_exit(state);
    test_proc_stat_parser_uses_the_final_comm_delimiter(state);
    test_kill_cleans_after_retained_identity_anomaly(state);
    test_kill_cleans_live_direct_child_missing_from_scope(state);
    test_repeated_kill_respects_one_deadline(state);
    return state.failures;
}
