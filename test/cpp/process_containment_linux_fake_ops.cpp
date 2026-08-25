#include "process_containment_linux_fake_ops.h"

#include "platform/process_containment_linux_ops.h"

#ifdef __linux__

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include <fcntl.h>
#include <linux/magic.h>
#include <linux/sched.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace lemon::utils::internal {
namespace {

struct FakeContext {
    FakeContext() {
        for (auto &role : fd_roles) {
            role.store(0, std::memory_order_relaxed);
        }
    }

    std::atomic<bool> fail_leaf_open{false};
    std::atomic<bool> fail_scope_identity{false};
    std::atomic<bool> fail_scope_name_binding{false};
    std::atomic<bool> hold_child_before_exec{false};
    std::atomic<bool> weird_proc_stat_comm{false};
    std::atomic<bool> force_populated{false};
    std::atomic<bool> force_events_populated_once{false};
    std::atomic<bool> fake_clock_enabled{false};
    std::atomic<std::size_t> filesystem_checks{0};
    std::atomic<std::size_t> identity_checks{0};
    std::atomic<std::size_t> leaf_creations{0};
    std::atomic<std::size_t> leaf_removals{0};
    std::atomic<std::size_t> clone_calls{0};
    std::atomic<std::size_t> clone_contract_failures{0};
    std::atomic<std::size_t> pidfd_signals{0};
    std::atomic<std::size_t> pidfd_waits{0};
    std::atomic<std::size_t> cgroup_kill_writes{0};
    std::atomic<std::size_t> procs_rewinds{0};
    std::atomic<std::size_t> change_membership_rewind{0};
    std::atomic<std::size_t> duplicate_member_rewind{0};
    std::atomic<std::size_t> proc_stat_opens{0};
    std::atomic<std::size_t> corrupt_identity_open{0};
    std::atomic<std::size_t> pidfd_polls{0};
    std::atomic<std::size_t> report_pidfd_exit_poll{0};
    std::atomic<std::size_t> sleep_calls{0};
    std::atomic<std::int64_t> fake_clock_origin_ns{0};
    std::atomic<std::int64_t> fake_clock_now_ns{0};
    std::atomic<int> scope_fd{-1};
    std::atomic<pid_t> direct_pid{-1};
    std::array<std::atomic<int>, 4096> fd_roles{};
};

enum class FdRole : int {
    None,
    Procs,
    Events,
    Kill,
    Pidfd,
    ProcStat,
    ProcStatCorrupt,
};

FakeContext &fake_context() noexcept {
    static FakeContext *context = new FakeContext;
    return *context;
}

FakeContext &context(void *value) noexcept {
    return *static_cast<FakeContext *>(value);
}

bool is_leaf_name(const char *path) noexcept {
    constexpr char prefix[] = "lemonade-profile-";
    return path != nullptr &&
           std::strncmp(path, prefix, sizeof(prefix) - 1U) == 0;
}

bool is_proc_stat_path(const char *path) noexcept {
    if (path == nullptr || std::strncmp(path, "/proc/", 6U) != 0) {
        return false;
    }
    const std::size_t length = std::strlen(path);
    return length > 11U && std::strcmp(path + length - 5U, "/stat") == 0;
}

void rewrite_comm(char *buffer, std::size_t size) noexcept {
    char *const open = static_cast<char *>(std::memchr(buffer, '(', size));
    if (open == nullptr) {
        return;
    }
    char *close = nullptr;
    for (std::size_t index = size; index > 0U; --index) {
        if (buffer[index - 1U] == ')') {
            close = buffer + index - 1U;
            break;
        }
    }
    if (close != nullptr && close - open >= 4) {
        open[1] = 'a';
        open[2] = ')';
        open[3] = '(';
    }
}

void corrupt_start_time(char *buffer, std::size_t size) noexcept {
    char *close = nullptr;
    for (std::size_t index = size; index > 0U; --index) {
        if (buffer[index - 1U] == ')') {
            close = buffer + index - 1U;
            break;
        }
    }
    if (close == nullptr) {
        return;
    }
    char *cursor = close + 1;
    char *const end = buffer + size;
    for (std::size_t field = 0; field < 20U; ++field) {
        while (cursor < end && *cursor == ' ') {
            ++cursor;
        }
        if (cursor == end) {
            return;
        }
        if (field == 19U) {
            *cursor = *cursor == '1' ? '2' : '1';
            return;
        }
        while (cursor < end && *cursor != ' ' && *cursor != '\n') {
            ++cursor;
        }
    }
}

FdRole fd_role(FakeContext &state, int fd) noexcept {
    if (fd < 0 || static_cast<std::size_t>(fd) >= state.fd_roles.size()) {
        return FdRole::None;
    }
    return static_cast<FdRole>(
        state.fd_roles[static_cast<std::size_t>(fd)].load(
            std::memory_order_acquire));
}

void set_fd_role(FakeContext &state, int fd, FdRole role) noexcept {
    if (fd < 0 || static_cast<std::size_t>(fd) >= state.fd_roles.size()) {
        return;
    }
    state.fd_roles[static_cast<std::size_t>(fd)].store(
        static_cast<int>(role), std::memory_order_release);
}

FdRole control_role(const char *path) noexcept {
    if (std::strcmp(path, "cgroup.procs") == 0) {
        return FdRole::Procs;
    }
    if (std::strcmp(path, "cgroup.events") == 0) {
        return FdRole::Events;
    }
    if (std::strcmp(path, "cgroup.kill") == 0) {
        return FdRole::Kill;
    }
    return FdRole::None;
}

bool child_has_exited(pid_t pid) noexcept {
    if (pid <= 0) {
        return true;
    }
    siginfo_t information{};
    if (::waitid(P_PID, static_cast<id_t>(pid), &information,
                 WEXITED | WNOHANG | WNOWAIT) == 0) {
        return information.si_pid != 0;
    }
    return errno == ECHILD;
}

bool rewrite_control(int scope_fd, const char *name,
                     std::string_view content) noexcept {
    if (scope_fd < 0) {
        return false;
    }
    const int fd = ::openat(scope_fd, name,
                            O_WRONLY | O_TRUNC | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    const bool written =
        content.empty() ||
        ::write(fd, content.data(), content.size()) ==
            static_cast<ssize_t>(content.size());
    const int saved_error = errno;
    (void)::close(fd);
    errno = saved_error;
    return written;
}

void refresh_membership(FakeContext &state) noexcept {
    const pid_t pid = state.direct_pid.load(std::memory_order_acquire);
    const int scope_fd = state.scope_fd.load(std::memory_order_acquire);
    if (pid <= 0 ||
        (child_has_exited(pid) &&
         !state.force_populated.load(std::memory_order_acquire))) {
        (void)rewrite_control(scope_fd, "cgroup.procs", {});
        (void)rewrite_control(scope_fd, "cgroup.events", "populated 0\n");
        return;
    }
    char member[32]{};
    const int size = std::snprintf(member, sizeof(member), "%d\n", pid);
    if (size > 0) {
        (void)rewrite_control(
            scope_fd, "cgroup.procs",
            std::string_view(member, static_cast<std::size_t>(size)));
        (void)rewrite_control(scope_fd, "cgroup.events", "populated 1\n");
    }
}

bool create_control(int directory_fd, const char *name,
                    const char *content) noexcept {
    const int fd = ::openat(directory_fd, name,
                            O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0640);
    if (fd < 0) {
        return false;
    }
    const std::size_t size = std::strlen(content);
    const bool written = size == 0U ||
                         ::write(fd, content, size) ==
                             static_cast<ssize_t>(size);
    const int saved_error = errno;
    (void)::close(fd);
    errno = saved_error;
    return written;
}

int close_fd(void *opaque, int fd) noexcept {
    FakeContext &state = context(opaque);
    const int result = ::close(fd);
    if (result == 0) {
        set_fd_role(state, fd, FdRole::None);
        int expected = fd;
        (void)state.scope_fd.compare_exchange_strong(
            expected, -1, std::memory_order_acq_rel);
    }
    return result;
}
int duplicate_fd(void *opaque, int fd, int minimum) noexcept {
    const int duplicate = ::fcntl(fd, F_DUPFD_CLOEXEC, minimum);
    if (duplicate >= 0) {
        set_fd_role(context(opaque), duplicate, fd_role(context(opaque), fd));
    }
    return duplicate;
}
int duplicate_fd_to(void *opaque, int source, int destination,
                    int flags) noexcept {
    const int result = ::dup3(source, destination, flags);
    if (result >= 0) {
        set_fd_role(context(opaque), destination,
                    fd_role(context(opaque), source));
    }
    return result;
}
int duplicate_fd_to_legacy(void *opaque, int source,
                           int destination) noexcept {
    const int result = ::dup2(source, destination);
    if (result >= 0) {
        set_fd_role(context(opaque), destination,
                    fd_role(context(opaque), source));
    }
    return result;
}
off_t seek_fd(void *opaque, int fd, off_t offset, int whence) noexcept {
    FakeContext &state = context(opaque);
    const FdRole role = fd_role(state, fd);
    if (offset == 0 && whence == SEEK_SET &&
        (role == FdRole::Procs || role == FdRole::Events)) {
        refresh_membership(state);
        if (role == FdRole::Procs) {
            const std::size_t rewind =
                state.procs_rewinds.fetch_add(1, std::memory_order_acq_rel) + 1U;
            if (rewind == state.change_membership_rewind.load(
                              std::memory_order_acquire)) {
                (void)rewrite_control(
                    state.scope_fd.load(std::memory_order_acquire),
                    "cgroup.procs", {});
            } else if (rewind == state.duplicate_member_rewind.load(
                                     std::memory_order_acquire)) {
                const pid_t pid = state.direct_pid.load(std::memory_order_acquire);
                char members[64]{};
                const int length =
                    std::snprintf(members, sizeof(members), "%d\n%d\n", pid, pid);
                if (length > 0) {
                    (void)rewrite_control(
                        state.scope_fd.load(std::memory_order_acquire),
                        "cgroup.procs",
                        std::string_view(members,
                                         static_cast<std::size_t>(length)));
                }
            }
        } else if (state.force_events_populated_once.exchange(
                       false, std::memory_order_acq_rel)) {
            (void)rewrite_control(
                state.scope_fd.load(std::memory_order_acquire), "cgroup.events",
                "populated 1\n");
        }
    }
    return ::lseek(fd, offset, whence);
}
ssize_t read_fd(void *opaque, int fd, void *buffer, std::size_t size) noexcept {
    const ssize_t result = ::read(fd, buffer, size);
    if (result <= 0) {
        return result;
    }
    const FdRole role = fd_role(context(opaque), fd);
    if (role == FdRole::ProcStat || role == FdRole::ProcStatCorrupt) {
        if (context(opaque).weird_proc_stat_comm.load(std::memory_order_acquire)) {
            rewrite_comm(static_cast<char *>(buffer),
                         static_cast<std::size_t>(result));
        }
        if (role == FdRole::ProcStatCorrupt) {
            corrupt_start_time(static_cast<char *>(buffer),
                               static_cast<std::size_t>(result));
        }
    }
    return result;
}
ssize_t write_fd(void *opaque, int fd, const void *buffer,
                 std::size_t size) noexcept {
    FakeContext &state = context(opaque);
    if (fd_role(state, fd) == FdRole::Kill) {
        state.cgroup_kill_writes.fetch_add(1, std::memory_order_relaxed);
        const pid_t pid = state.direct_pid.load(std::memory_order_acquire);
        if (pid > 0) {
            (void)::kill(pid, SIGKILL);
        }
        return static_cast<ssize_t>(size);
    }
    return ::write(fd, buffer, size);
}
int open_path(void *opaque, const char *path, int flags) noexcept {
    FakeContext &state = context(opaque);
    const int fd = ::open(path, flags);
    if (fd >= 0) {
        FdRole role = FdRole::None;
        if (is_proc_stat_path(path)) {
            const std::size_t ordinal =
                state.proc_stat_opens.fetch_add(1, std::memory_order_acq_rel) + 1U;
            role = ordinal == state.corrupt_identity_open.load(
                                  std::memory_order_acquire)
                       ? FdRole::ProcStatCorrupt
                       : FdRole::ProcStat;
        }
        set_fd_role(state, fd, role);
    }
    return fd;
}
int open_at(void *opaque, int directory_fd, const char *path,
            int flags) noexcept {
    FakeContext &state = context(opaque);
    if ((flags & O_DIRECTORY) != 0 && is_leaf_name(path) &&
        state.fail_leaf_open.exchange(false, std::memory_order_acq_rel)) {
        errno = EACCES;
        return -1;
    }
    const int fd = ::openat(directory_fd, path, flags);
    if (fd >= 0) {
        const FdRole role = control_role(path);
        set_fd_role(state, fd, role);
        if (role != FdRole::None) {
            state.scope_fd.store(directory_fd, std::memory_order_release);
        }
    }
    return fd;
}
int make_directory_at(void *opaque, int directory_fd, const char *path,
                      mode_t mode) noexcept {
    if (::mkdirat(directory_fd, path, mode) != 0) {
        return -1;
    }
    const int leaf_fd = ::openat(directory_fd, path,
                                 O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (leaf_fd < 0 || !create_control(leaf_fd, "cgroup.type", "domain\n") ||
        !create_control(leaf_fd, "cgroup.subtree_control", "") ||
        !create_control(leaf_fd, "cgroup.max.depth", "") ||
        !create_control(leaf_fd, "cgroup.procs", "") ||
        !create_control(leaf_fd, "cgroup.kill", "") ||
        !create_control(leaf_fd, "cgroup.events", "populated 0\n")) {
        const int saved_error = errno == 0 ? EIO : errno;
        if (leaf_fd >= 0) {
            (void)::close(leaf_fd);
        }
        errno = saved_error;
        return -1;
    }
    (void)::close(leaf_fd);
    context(opaque).leaf_creations.fetch_add(1, std::memory_order_relaxed);
    return 0;
}
int remove_directory_at(void *opaque, int directory_fd, const char *path,
                        int flags) noexcept {
    if ((flags & AT_REMOVEDIR) != 0) {
        const int leaf_fd = ::openat(directory_fd, path,
                                     O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (leaf_fd >= 0) {
            constexpr const char *controls[] = {
                "cgroup.type", "cgroup.subtree_control", "cgroup.max.depth",
                "cgroup.procs", "cgroup.kill", "cgroup.events"};
            for (const char *control : controls) {
                (void)::unlinkat(leaf_fd, control, 0);
            }
            (void)::close(leaf_fd);
        }
    }
    const int result = ::unlinkat(directory_fd, path, flags);
    if (result == 0 && is_leaf_name(path)) {
        context(opaque).leaf_removals.fetch_add(1,
                                                std::memory_order_relaxed);
    }
    return result;
}
int stat_fd(void *, int fd, struct stat *value) noexcept {
    return ::fstat(fd, value);
}
int stat_at(void *opaque, int directory_fd, const char *path, struct stat *value,
            int flags) noexcept {
    if (is_leaf_name(path) &&
        context(opaque).fail_scope_name_binding.exchange(
            false, std::memory_order_acq_rel)) {
        errno = ENOENT;
        return -1;
    }
    return ::fstatat(directory_fd, path, value, flags);
}
int stat_filesystem(void *opaque, int, struct statfs *value) noexcept {
    std::memset(value, 0, sizeof(*value));
    value->f_type = CGROUP2_SUPER_MAGIC;
    context(opaque).filesystem_checks.fetch_add(1,
                                                std::memory_order_relaxed);
    return 0;
}
int stat_extended(void *opaque, int fd, const char *path, int flags,
                  unsigned int mask, struct statx *value) noexcept {
    if (::statx(fd, path, flags, mask, value) != 0) {
        return -1;
    }
    value->stx_mask |= STATX_MNT_ID;
    value->stx_mnt_id = 4242U;
    if (path != nullptr && path[0] == '\0' &&
        context(opaque).fail_scope_identity.exchange(
            false, std::memory_order_acq_rel)) {
        ++value->stx_mnt_id;
    }
    context(opaque).identity_checks.fetch_add(1,
                                              std::memory_order_relaxed);
    return 0;
}
DIR *open_directory_stream(void *, int fd) noexcept { return ::fdopendir(fd); }
struct dirent *read_directory(void *, DIR *directory) noexcept {
    return ::readdir(directory);
}
int close_directory(void *, DIR *directory) noexcept {
    return ::closedir(directory);
}
int make_pipe(void *opaque, int descriptors[2], int flags) noexcept {
    const int result = ::pipe2(descriptors, flags);
    if (result == 0) {
        set_fd_role(context(opaque), descriptors[0], FdRole::None);
        set_fd_role(context(opaque), descriptors[1], FdRole::None);
    }
    return result;
}
int make_event(void *opaque, unsigned int initial_value, int flags) noexcept {
    const int fd = ::eventfd(initial_value, flags);
    if (fd >= 0) {
        set_fd_role(context(opaque), fd, FdRole::None);
    }
    return fd;
}
int poll_fds(void *opaque, struct pollfd *descriptors, nfds_t count,
             int timeout_ms) noexcept {
    FakeContext &state = context(opaque);
    if (count == 1U && fd_role(state, descriptors[0].fd) == FdRole::Pidfd) {
        const std::size_t ordinal =
            state.pidfd_polls.fetch_add(1, std::memory_order_acq_rel) + 1U;
        if (ordinal == state.report_pidfd_exit_poll.load(
                           std::memory_order_acquire)) {
            descriptors[0].revents = POLLIN;
            return 1;
        }
    }
    return ::poll(descriptors, count, timeout_ms);
}
int change_directory(void *, const char *path) noexcept { return ::chdir(path); }
int execute(void *opaque, const char *path, char *const arguments[],
            char *const environment[]) noexcept {
    if (context(opaque).hold_child_before_exec.load(
            std::memory_order_acquire)) {
        while (true) {
            (void)::pause();
        }
    }
    return ::execve(path, arguments, environment);
}
std::size_t configuration_string(void *, int name, char *buffer,
                                 std::size_t size) noexcept {
    return ::confstr(name, buffer, size);
}
pid_t process_id(void *) noexcept { return ::getpid(); }
pid_t parent_process_id(void *) noexcept { return ::getppid(); }
int signal_action(void *, int signal_number, const struct sigaction *action,
                  struct sigaction *previous) noexcept {
    return ::sigaction(signal_number, action, previous);
}
void exit_child(void *, int status) noexcept { ::_exit(status); }
long get_thread_id(void *) noexcept { return ::syscall(SYS_gettid); }
long set_signal_mask(void *, int operation, const void *set, void *previous,
                     std::size_t size) noexcept {
    return ::syscall(SYS_rt_sigprocmask, operation, set, previous, size);
}
long set_process_control(void *, int operation, unsigned long argument2,
                         unsigned long argument3, unsigned long argument4,
                         unsigned long argument5) noexcept {
    return ::syscall(SYS_prctl, operation, argument2, argument3, argument4,
                     argument5);
}
long get_capabilities(void *, void *header, void *data) noexcept {
    return ::syscall(SYS_capget, header, data);
}
long set_capabilities(void *, const void *header, const void *data) noexcept {
    return ::syscall(SYS_capset, header, data);
}
long clone_process(void *opaque, struct clone_args *arguments,
                   std::size_t size) noexcept {
    FakeContext &state = context(opaque);
    state.clone_calls.fetch_add(1, std::memory_order_relaxed);
    if (size != sizeof(*arguments) ||
        arguments->flags != (CLONE_INTO_CGROUP | CLONE_PIDFD) ||
        arguments->cgroup == 0U || arguments->pidfd == 0U ||
        arguments->exit_signal != SIGCHLD) {
        state.clone_contract_failures.fetch_add(1, std::memory_order_relaxed);
        errno = EINVAL;
        return -1;
    }

    const pid_t pid = ::fork();
    if (pid <= 0) {
        return pid;
    }
    const int pidfd =
        static_cast<int>(::syscall(SYS_pidfd_open, pid, 0U));
    if (pidfd < 0) {
        const int saved_error = errno;
        (void)::kill(pid, SIGKILL);
        (void)::waitpid(pid, nullptr, 0);
        errno = saved_error;
        return -1;
    }
    *reinterpret_cast<int *>(
        static_cast<std::uintptr_t>(arguments->pidfd)) = pidfd;
    set_fd_role(state, pidfd, FdRole::Pidfd);
    state.scope_fd.store(static_cast<int>(arguments->cgroup),
                         std::memory_order_release);
    state.direct_pid.store(pid, std::memory_order_release);
    refresh_membership(state);
    return pid;
}
int open_pidfd(void *opaque, pid_t pid, unsigned int flags) noexcept {
    const int fd = static_cast<int>(::syscall(SYS_pidfd_open, pid, flags));
    if (fd >= 0) {
        set_fd_role(context(opaque), fd, FdRole::Pidfd);
    }
    return fd;
}
int signal_pidfd(void *opaque, int pidfd, int signal_number,
                 const siginfo_t *information, unsigned int flags) noexcept {
    context(opaque).pidfd_signals.fetch_add(1, std::memory_order_relaxed);
    return static_cast<int>(::syscall(SYS_pidfd_send_signal, pidfd,
                                      signal_number, information, flags));
}
int wait_pidfd(void *opaque, int pidfd, siginfo_t *information,
               int options) noexcept {
    context(opaque).pidfd_waits.fetch_add(1, std::memory_order_relaxed);
    constexpr int kPidfdIdType = 3;
    return static_cast<int>(::syscall(SYS_waitid, kPidfdIdType, pidfd,
                                      information, options, nullptr));
}
std::chrono::steady_clock::time_point steady_now(void *) noexcept {
    FakeContext &state = fake_context();
    if (!state.fake_clock_enabled.load(std::memory_order_acquire)) {
        return std::chrono::steady_clock::now();
    }
    return std::chrono::steady_clock::time_point(
        std::chrono::nanoseconds(
            state.fake_clock_now_ns.load(std::memory_order_acquire)));
}
void sleep_until(void *opaque,
                 std::chrono::steady_clock::time_point deadline) noexcept {
    FakeContext &state = context(opaque);
    if (!state.fake_clock_enabled.load(std::memory_order_acquire)) {
        std::this_thread::sleep_until(deadline);
        return;
    }
    state.sleep_calls.fetch_add(1, std::memory_order_relaxed);
    const auto requested = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               deadline.time_since_epoch())
                               .count();
    std::int64_t observed =
        state.fake_clock_now_ns.load(std::memory_order_acquire);
    while (observed < requested &&
           !state.fake_clock_now_ns.compare_exchange_weak(
               observed, requested, std::memory_order_acq_rel)) {
    }
}

const ProcessContainmentLinuxOps kFakeOps{
    &fake_context(),
    close_fd,
    duplicate_fd,
    duplicate_fd_to,
    duplicate_fd_to_legacy,
    seek_fd,
    read_fd,
    write_fd,
    open_path,
    open_at,
    make_directory_at,
    remove_directory_at,
    stat_fd,
    stat_at,
    stat_filesystem,
    stat_extended,
    open_directory_stream,
    read_directory,
    close_directory,
    make_pipe,
    make_event,
    poll_fds,
    change_directory,
    execute,
    configuration_string,
    process_id,
    parent_process_id,
    signal_action,
    exit_child,
    get_thread_id,
    set_signal_mask,
    set_process_control,
    get_capabilities,
    set_capabilities,
    clone_process,
    open_pidfd,
    signal_pidfd,
    wait_pidfd,
    steady_now,
    sleep_until,
};

} // namespace

const ProcessContainmentLinuxOps &
process_lifetime_process_containment_linux_ops() noexcept {
    return kFakeOps;
}

namespace testing {

bool reset_process_containment_linux_fake() noexcept {
    FakeContext &state = fake_context();
    if (!process_containment_linux_fake_drained()) {
        return false;
    }
    state.fail_leaf_open.store(false, std::memory_order_release);
    state.fail_scope_identity.store(false, std::memory_order_release);
    state.fail_scope_name_binding.store(false, std::memory_order_release);
    state.hold_child_before_exec.store(false, std::memory_order_release);
    state.weird_proc_stat_comm.store(false, std::memory_order_release);
    state.force_populated.store(false, std::memory_order_release);
    state.force_events_populated_once.store(false, std::memory_order_release);
    state.fake_clock_enabled.store(false, std::memory_order_release);
    state.filesystem_checks.store(0, std::memory_order_release);
    state.identity_checks.store(0, std::memory_order_release);
    state.leaf_creations.store(0, std::memory_order_release);
    state.leaf_removals.store(0, std::memory_order_release);
    state.clone_calls.store(0, std::memory_order_release);
    state.clone_contract_failures.store(0, std::memory_order_release);
    state.pidfd_signals.store(0, std::memory_order_release);
    state.pidfd_waits.store(0, std::memory_order_release);
    state.cgroup_kill_writes.store(0, std::memory_order_release);
    state.procs_rewinds.store(0, std::memory_order_release);
    state.change_membership_rewind.store(0, std::memory_order_release);
    state.duplicate_member_rewind.store(0, std::memory_order_release);
    state.proc_stat_opens.store(0, std::memory_order_release);
    state.corrupt_identity_open.store(0, std::memory_order_release);
    state.pidfd_polls.store(0, std::memory_order_release);
    state.report_pidfd_exit_poll.store(0, std::memory_order_release);
    state.sleep_calls.store(0, std::memory_order_release);
    state.fake_clock_origin_ns.store(0, std::memory_order_release);
    state.fake_clock_now_ns.store(0, std::memory_order_release);
    state.scope_fd.store(-1, std::memory_order_release);
    state.direct_pid.store(-1, std::memory_order_release);
    for (auto &role : state.fd_roles) {
        role.store(static_cast<int>(FdRole::None),
                   std::memory_order_release);
    }
    return true;
}

void fail_next_leaf_open() noexcept {
    fake_context().fail_leaf_open.store(true, std::memory_order_release);
}

void fail_retained_scope_identity() noexcept {
    fake_context().fail_scope_identity.store(true, std::memory_order_release);
}

void fail_scope_name_binding() noexcept {
    fake_context().fail_scope_name_binding.store(true,
                                                  std::memory_order_release);
}

void hold_child_before_exec() noexcept {
    fake_context().hold_child_before_exec.store(true,
                                                std::memory_order_release);
}

void change_membership_on_procs_rewind(std::size_t ordinal) noexcept {
    FakeContext &state = fake_context();
    state.procs_rewinds.store(0, std::memory_order_release);
    state.change_membership_rewind.store(ordinal, std::memory_order_release);
    state.duplicate_member_rewind.store(0, std::memory_order_release);
}

void duplicate_member_on_procs_rewind(std::size_t ordinal) noexcept {
    FakeContext &state = fake_context();
    state.procs_rewinds.store(0, std::memory_order_release);
    state.change_membership_rewind.store(0, std::memory_order_release);
    state.duplicate_member_rewind.store(ordinal, std::memory_order_release);
}

void corrupt_identity_on_proc_stat_open(std::size_t ordinal) noexcept {
    FakeContext &state = fake_context();
    state.proc_stat_opens.store(0, std::memory_order_release);
    state.corrupt_identity_open.store(ordinal, std::memory_order_release);
}

void report_pidfd_exit_on_poll(std::size_t ordinal) noexcept {
    FakeContext &state = fake_context();
    state.pidfd_polls.store(0, std::memory_order_release);
    state.report_pidfd_exit_poll.store(ordinal, std::memory_order_release);
}

void rewrite_proc_stat_comm() noexcept {
    fake_context().weird_proc_stat_comm.store(true, std::memory_order_release);
}

void force_scope_populated(bool enabled) noexcept {
    fake_context().force_populated.store(enabled, std::memory_order_release);
}

void force_next_events_populated() noexcept {
    fake_context().force_events_populated_once.store(true,
                                                     std::memory_order_release);
}

void enable_fake_clock() noexcept {
    FakeContext &state = fake_context();
    const auto origin = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    state.fake_clock_origin_ns.store(origin, std::memory_order_release);
    state.fake_clock_now_ns.store(origin, std::memory_order_release);
    state.sleep_calls.store(0, std::memory_order_release);
    state.fake_clock_enabled.store(true, std::memory_order_release);
}

void disable_fake_clock() noexcept {
    fake_context().fake_clock_enabled.store(false, std::memory_order_release);
}

bool process_containment_linux_fake_drained() noexcept {
    FakeContext &state = fake_context();
    const pid_t pid = state.direct_pid.load(std::memory_order_acquire);
    bool child_drained = pid <= 0;
    if (!child_drained) {
        siginfo_t information{};
        if (::waitid(P_PID, static_cast<id_t>(pid), &information,
                     WEXITED | WNOHANG | WNOWAIT) != 0 && errno == ECHILD) {
            state.direct_pid.store(-1, std::memory_order_release);
            child_drained = true;
        }
    }
    if (!child_drained) {
        return false;
    }
    for (const auto &role : state.fd_roles) {
        if (static_cast<FdRole>(role.load(std::memory_order_acquire)) ==
            FdRole::Pidfd) {
            return false;
        }
    }
    return true;
}

ProcessContainmentLinuxFakeSnapshot
process_containment_linux_fake_snapshot() noexcept {
    FakeContext &state = fake_context();
    const auto elapsed_ns = std::max<std::int64_t>(
        0, state.fake_clock_now_ns.load(std::memory_order_acquire) -
               state.fake_clock_origin_ns.load(std::memory_order_acquire));
    return {
        state.filesystem_checks.load(std::memory_order_acquire),
        state.identity_checks.load(std::memory_order_acquire),
        state.leaf_creations.load(std::memory_order_acquire),
        state.leaf_removals.load(std::memory_order_acquire),
        state.clone_calls.load(std::memory_order_acquire),
        state.clone_contract_failures.load(std::memory_order_acquire),
        state.pidfd_signals.load(std::memory_order_acquire),
        state.pidfd_waits.load(std::memory_order_acquire),
        state.cgroup_kill_writes.load(std::memory_order_acquire),
        state.procs_rewinds.load(std::memory_order_acquire),
        state.pidfd_polls.load(std::memory_order_acquire),
        state.sleep_calls.load(std::memory_order_acquire),
        static_cast<std::size_t>(elapsed_ns / 1000000),
    };
}

} // namespace testing
} // namespace lemon::utils::internal

#endif
