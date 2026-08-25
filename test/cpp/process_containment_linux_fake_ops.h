#pragma once

#ifdef __linux__

#include <cstddef>

namespace lemon::utils::internal::testing {

struct ProcessContainmentLinuxFakeSnapshot {
    std::size_t filesystem_checks = 0;
    std::size_t identity_checks = 0;
    std::size_t leaf_creations = 0;
    std::size_t leaf_removals = 0;
    std::size_t clone_calls = 0;
    std::size_t clone_contract_failures = 0;
    std::size_t pidfd_signals = 0;
    std::size_t pidfd_waits = 0;
    std::size_t cgroup_kill_writes = 0;
    std::size_t procs_rewinds = 0;
    std::size_t pidfd_polls = 0;
    std::size_t sleep_calls = 0;
    std::size_t fake_clock_elapsed_ms = 0;
};

bool reset_process_containment_linux_fake() noexcept;
void fail_next_leaf_open() noexcept;
void fail_retained_scope_identity() noexcept;
void fail_scope_name_binding() noexcept;
void hold_child_before_exec() noexcept;
void change_membership_on_procs_rewind(std::size_t ordinal) noexcept;
void duplicate_member_on_procs_rewind(std::size_t ordinal) noexcept;
void corrupt_identity_on_proc_stat_open(std::size_t ordinal) noexcept;
void report_pidfd_exit_on_poll(std::size_t ordinal) noexcept;
void rewrite_proc_stat_comm() noexcept;
void force_scope_populated(bool enabled) noexcept;
void force_next_events_populated() noexcept;
void enable_fake_clock() noexcept;
void disable_fake_clock() noexcept;
bool process_containment_linux_fake_drained() noexcept;
ProcessContainmentLinuxFakeSnapshot
process_containment_linux_fake_snapshot() noexcept;

} // namespace lemon::utils::internal::testing

#endif
