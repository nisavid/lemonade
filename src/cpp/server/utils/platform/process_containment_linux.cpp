#include "process_containment_linux_ops.h"
#include "process_containment_platform.h"

#ifdef __linux__

#include <lemon/utils/aixlog.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <poll.h>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <linux/capability.h>
#include <linux/limits.h>
#include <linux/magic.h>
#include <linux/sched.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace lemon::utils::internal {
namespace {

constexpr std::size_t kMaxControlBytes = 1024U * 1024U;
constexpr std::size_t kMaxDiagnosticBytes = 256U;
constexpr std::size_t kMaxMembers = 1024U;
constexpr std::size_t kMaxOwnerScopeBytes = 512U;
constexpr std::size_t kMaxSpawnBytes = 16U * 1024U * 1024U;
constexpr auto kMaxKillTimeout = std::chrono::seconds(30);
constexpr std::string_view kLeafPrefix = "lemonade-profile-";

const ProcessContainmentLinuxOps &linux_ops() noexcept {
    return process_lifetime_process_containment_linux_ops();
}

int linux_close(int fd) noexcept {
    const auto &ops = linux_ops();
    return ops.close_fd(ops.context, fd);
}

int linux_duplicate(int fd, int minimum) noexcept {
    const auto &ops = linux_ops();
    return ops.duplicate_fd(ops.context, fd, minimum);
}

int linux_dup3(int source, int destination, int flags) noexcept {
    const auto &ops = linux_ops();
    return ops.duplicate_fd_to(ops.context, source, destination, flags);
}

int linux_dup2(int source, int destination) noexcept {
    const auto &ops = linux_ops();
    return ops.duplicate_fd_to_legacy(ops.context, source, destination);
}

off_t linux_seek(int fd, off_t offset, int whence) noexcept {
    const auto &ops = linux_ops();
    return ops.seek_fd(ops.context, fd, offset, whence);
}

ssize_t linux_read(int fd, void *buffer, std::size_t size) noexcept {
    const auto &ops = linux_ops();
    return ops.read_fd(ops.context, fd, buffer, size);
}

ssize_t linux_write(int fd, const void *buffer, std::size_t size) noexcept {
    const auto &ops = linux_ops();
    return ops.write_fd(ops.context, fd, buffer, size);
}

int linux_open(const char *path, int flags) noexcept {
    const auto &ops = linux_ops();
    return ops.open_path(ops.context, path, flags);
}

int linux_openat(int directory_fd, const char *path, int flags) noexcept {
    const auto &ops = linux_ops();
    return ops.open_at(ops.context, directory_fd, path, flags);
}

int linux_mkdirat(int directory_fd, const char *path, mode_t mode) noexcept {
    const auto &ops = linux_ops();
    return ops.make_directory_at(ops.context, directory_fd, path, mode);
}

int linux_unlinkat(int directory_fd, const char *path, int flags) noexcept {
    const auto &ops = linux_ops();
    return ops.remove_directory_at(ops.context, directory_fd, path, flags);
}

int linux_fstat(int fd, struct stat *value) noexcept {
    const auto &ops = linux_ops();
    return ops.stat_fd(ops.context, fd, value);
}

int linux_fstatat(int directory_fd, const char *path, struct stat *value,
                  int flags) noexcept {
    const auto &ops = linux_ops();
    return ops.stat_at(ops.context, directory_fd, path, value, flags);
}

int linux_fstatfs(int fd, struct statfs *value) noexcept {
    const auto &ops = linux_ops();
    return ops.stat_filesystem(ops.context, fd, value);
}

int linux_statx(int fd, const char *path, int flags, unsigned int mask,
                struct statx *value) noexcept {
    const auto &ops = linux_ops();
    return ops.stat_extended(ops.context, fd, path, flags, mask, value);
}

DIR *linux_fdopendir(int fd) noexcept {
    const auto &ops = linux_ops();
    return ops.open_directory_stream(ops.context, fd);
}

dirent *linux_readdir(DIR *directory) noexcept {
    const auto &ops = linux_ops();
    return ops.read_directory(ops.context, directory);
}

int linux_closedir(DIR *directory) noexcept {
    const auto &ops = linux_ops();
    return ops.close_directory(ops.context, directory);
}

int linux_pipe2(int descriptors[2], int flags) noexcept {
    const auto &ops = linux_ops();
    return ops.make_pipe(ops.context, descriptors, flags);
}

int linux_eventfd(unsigned int initial_value, int flags) noexcept {
    const auto &ops = linux_ops();
    return ops.make_event(ops.context, initial_value, flags);
}

int linux_poll(struct pollfd *descriptors, nfds_t count,
               int timeout_ms) noexcept {
    const auto &ops = linux_ops();
    return ops.poll_fds(ops.context, descriptors, count, timeout_ms);
}

int linux_chdir(const char *path) noexcept {
    const auto &ops = linux_ops();
    return ops.change_directory(ops.context, path);
}

int linux_execve(const char *path, char *const arguments[],
                 char *const environment[]) noexcept {
    const auto &ops = linux_ops();
    return ops.execute(ops.context, path, arguments, environment);
}

std::size_t linux_confstr(int name, char *buffer, std::size_t size) noexcept {
    const auto &ops = linux_ops();
    return ops.configuration_string(ops.context, name, buffer, size);
}

pid_t linux_getpid() noexcept {
    const auto &ops = linux_ops();
    return ops.process_id(ops.context);
}

pid_t linux_getppid() noexcept {
    const auto &ops = linux_ops();
    return ops.parent_process_id(ops.context);
}

int linux_sigaction(int signal_number, const struct sigaction *action,
                    struct sigaction *previous) noexcept {
    const auto &ops = linux_ops();
    return ops.signal_action(ops.context, signal_number, action, previous);
}

[[noreturn]] void linux_exit_child(int status) noexcept {
    const auto &ops = linux_ops();
    ops.exit_child(ops.context, status);
    __builtin_unreachable();
}

long linux_gettid() noexcept {
    const auto &ops = linux_ops();
    return ops.get_thread_id(ops.context);
}

long linux_set_signal_mask(int operation, const void *set, void *previous,
                           std::size_t size) noexcept {
    const auto &ops = linux_ops();
    return ops.set_signal_mask(ops.context, operation, set, previous, size);
}

long linux_set_process_control(int operation, unsigned long argument2,
                               unsigned long argument3,
                               unsigned long argument4,
                               unsigned long argument5) noexcept {
    const auto &ops = linux_ops();
    return ops.set_process_control(ops.context, operation, argument2, argument3,
                                   argument4, argument5);
}

long linux_get_capabilities(void *header, void *data) noexcept {
    const auto &ops = linux_ops();
    return ops.get_capabilities(ops.context, header, data);
}

long linux_set_capabilities(const void *header, const void *data) noexcept {
    const auto &ops = linux_ops();
    return ops.set_capabilities(ops.context, header, data);
}

long linux_clone_process(struct clone_args *arguments,
                         std::size_t size) noexcept {
    const auto &ops = linux_ops();
    return ops.clone_process(ops.context, arguments, size);
}

int linux_open_pidfd(pid_t pid, unsigned int flags) noexcept {
    const auto &ops = linux_ops();
    return ops.open_pidfd(ops.context, pid, flags);
}

int linux_signal_pidfd(int pidfd, int signal_number,
                       const siginfo_t *information,
                       unsigned int flags) noexcept {
    const auto &ops = linux_ops();
    return ops.signal_pidfd(ops.context, pidfd, signal_number, information,
                            flags);
}

int linux_wait_pidfd(int pidfd, siginfo_t *information, int options) noexcept {
    const auto &ops = linux_ops();
    return ops.wait_pidfd(ops.context, pidfd, information, options);
}

std::chrono::steady_clock::time_point linux_now() noexcept {
    const auto &ops = linux_ops();
    return ops.steady_now(ops.context);
}

void linux_sleep_until(std::chrono::steady_clock::time_point deadline) noexcept {
    const auto &ops = linux_ops();
    ops.sleep_until(ops.context, deadline);
}

enum class ChildFailureStage : std::uint32_t {
    ParentDeathSignal = 1,
    WorkingDirectory = 2,
    Output = 3,
    Capability = 4,
    Exec = 5,
    SignalState = 6,
};

struct ChildFailure {
    std::uint32_t magic;
    std::uint32_t stage;
    std::int32_t error;
};

constexpr std::uint32_t kChildFailureMagic = 0x4c435046U;

std::string errno_diagnostic(std::string_view operation, int error);

class ScopedFd {
public:
    ScopedFd() = default;
    explicit ScopedFd(int fd) noexcept : fd_(fd) {}
    ScopedFd(const ScopedFd &) = delete;
    ScopedFd &operator=(const ScopedFd &) = delete;
    ScopedFd(ScopedFd &&other) noexcept : fd_(other.release()) {}
    ScopedFd &operator=(ScopedFd &&other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }
    ~ScopedFd() { reset(); }

    int get() const noexcept { return fd_; }
    explicit operator bool() const noexcept { return fd_ >= 0; }
    int release() noexcept {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }
    void reset(int fd = -1) noexcept {
        if (fd_ >= 0) {
            (void)linux_close(fd_);
        }
        fd_ = fd;
    }

private:
    int fd_ = -1;
};

bool normalize_internal_fd(ScopedFd &fd, std::string &diagnostic) {
    if (!fd) {
        diagnostic = "internal descriptor is unavailable";
        return false;
    }
    if (fd.get() >= 3) {
        return true;
    }
    int duplicate;
    do {
        duplicate = linux_duplicate(fd.get(), 3);
    } while (duplicate < 0 && errno == EINTR);
    if (duplicate < 0) {
        diagnostic = errno_diagnostic("cannot normalize internal descriptor",
                                      errno);
        return false;
    }
    fd.reset(duplicate);
    return true;
}

class LeafCleanupGuard {
public:
    LeafCleanupGuard(ScopedFd root_fd, const std::string &leaf_name) noexcept
        : root_fd_(std::move(root_fd)), leaf_name_(&leaf_name) {}
    LeafCleanupGuard(const LeafCleanupGuard &) = delete;
    LeafCleanupGuard &operator=(const LeafCleanupGuard &) = delete;
    ~LeafCleanupGuard() {
        if (armed_) {
            int result = linux_unlinkat(root_fd_.get(), leaf_name_->c_str(),
                                        AT_REMOVEDIR);
            if (result < 0 && errno == EINTR) {
                (void)linux_unlinkat(root_fd_.get(), leaf_name_->c_str(),
                                     AT_REMOVEDIR);
            }
        }
    }
    void disarm() noexcept { armed_ = false; }

private:
    ScopedFd root_fd_;
    const std::string *leaf_name_;
    bool armed_ = true;
};

std::string bounded(std::string value) {
    if (value.size() > kMaxDiagnosticBytes) {
        value.resize(kMaxDiagnosticBytes);
    }
    return value;
}

std::string errno_diagnostic(std::string_view operation, int error) {
    return bounded(std::string(operation) + ": " +
                   std::error_code(error, std::generic_category()).message());
}

template <typename Result>
Result failed(ProcessContainmentStatus status, std::string diagnostic) {
    Result result;
    result.status = status;
    result.diagnostic = bounded(std::move(diagnostic));
    return result;
}

bool has_nul(std::string_view value) {
    return value.find('\0') != std::string_view::npos;
}

bool has_control(std::string_view value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char character) {
        return character < 0x20U || character == 0x7fU;
    });
}

bool valid_nonce(std::string_view value) {
    return value.size() == 64U &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

bool valid_boot_id(std::string_view value) {
    if (value.size() != 36U) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 8U || index == 13U || index == 18U || index == 23U) {
            if (value[index] != '-') {
                return false;
            }
            continue;
        }
        const unsigned char character =
            static_cast<unsigned char>(value[index]);
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return true;
}

std::string trim_ascii_whitespace(std::string value) {
    const auto whitespace = [](unsigned char character) {
        return character == ' ' || character == '\t' || character == '\n' ||
               character == '\r' || character == '\f' || character == '\v';
    };
    const auto first = std::find_if_not(value.begin(), value.end(), whitespace);
    const auto last = std::find_if_not(value.rbegin(), value.rend(), whitespace).base();
    if (first >= last) {
        return {};
    }
    return std::string(first, last);
}

bool read_bounded_fd(int fd, std::size_t maximum, std::string &output,
                     std::string &diagnostic) {
    while (linux_seek(fd, 0, SEEK_SET) < 0) {
        if (errno == EINTR) {
            continue;
        }
        diagnostic = errno_diagnostic("control rewind failed", errno);
        return false;
    }

    output.clear();
    std::array<char, 4096> buffer{};
    while (true) {
        const ssize_t count = linux_read(fd, buffer.data(), buffer.size());
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            diagnostic = errno_diagnostic("control read failed", errno);
            return false;
        }
        if (count == 0) {
            return true;
        }
        const std::size_t amount = static_cast<std::size_t>(count);
        if (amount > maximum - std::min(maximum, output.size())) {
            diagnostic = "control content exceeds the bounded read limit";
            return false;
        }
        output.append(buffer.data(), amount);
    }
}

bool read_bounded_fd_until(
    int fd, std::size_t maximum,
    std::chrono::steady_clock::time_point deadline, std::string &output,
    std::string &diagnostic) {
    while (linux_seek(fd, 0, SEEK_SET) < 0) {
        if (errno == EINTR && linux_now() < deadline) {
            continue;
        }
        diagnostic = errno == EINTR
                         ? "control rewind exceeded the operation deadline"
                         : errno_diagnostic("control rewind failed", errno);
        return false;
    }
    output.clear();
    std::array<char, 4096> buffer{};
    while (linux_now() < deadline) {
        const ssize_t count = linux_read(fd, buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            diagnostic = errno_diagnostic("control read failed", errno);
            return false;
        }
        if (count == 0) {
            return true;
        }
        const std::size_t amount = static_cast<std::size_t>(count);
        if (amount > maximum - std::min(maximum, output.size())) {
            diagnostic = "control content exceeds the bounded read limit";
            return false;
        }
        output.append(buffer.data(), amount);
    }
    diagnostic = "control read exceeded the operation deadline";
    return false;
}

ScopedFd open_control(int directory_fd, const char *name, int flags,
                      std::string &diagnostic) {
    int fd;
    do {
        fd = linux_openat(directory_fd, name,
                          flags | O_CLOEXEC | O_NOFOLLOW);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        diagnostic = errno_diagnostic(std::string("cannot open ") + name, errno);
    }
    ScopedFd result(fd);
    if (result && !normalize_internal_fd(result, diagnostic)) {
        return {};
    }
    return result;
}

bool read_control(int directory_fd, const char *name, std::string &output,
                  std::string &diagnostic) {
    ScopedFd fd = open_control(directory_fd, name, O_RDONLY, diagnostic);
    return fd && read_bounded_fd(fd.get(), kMaxControlBytes, output, diagnostic);
}

bool write_single_control_until(
    int fd, std::string_view value,
    std::chrono::steady_clock::time_point deadline,
    std::string &diagnostic) {
    while (linux_now() < deadline) {
        const ssize_t count = linux_write(fd, value.data(), value.size());
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            diagnostic = errno_diagnostic("control write failed", errno);
            return false;
        }
        if (static_cast<std::size_t>(count) != value.size()) {
            diagnostic = "control write was incomplete";
            return false;
        }
        return true;
    }
    diagnostic = "control write exceeded the operation deadline";
    return false;
}

void report_child_failure(int fd, ChildFailureStage stage, int error) noexcept {
    const ChildFailure failure{kChildFailureMagic,
                               static_cast<std::uint32_t>(stage), error};
    const auto *bytes = reinterpret_cast<const unsigned char *>(&failure);
    std::size_t written = 0;
    while (written < sizeof(failure)) {
        const ssize_t count =
            linux_write(fd, bytes + written, sizeof(failure) - written);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            break;
        }
        written += static_cast<std::size_t>(count);
    }
}

enum class ExecHandshakeOutcome {
    Executed,
    ChildFailure,
    Cancelled,
    TimedOut,
    Failed,
};

std::optional<ProcessContainmentStatus> operation_control_failure(
    const ProcessContainmentOperationControl &control,
    std::string &diagnostic) {
    if (control.cancellation &&
        control.cancellation->load(std::memory_order_acquire)) {
        diagnostic = "process containment operation cancelled";
        return ProcessContainmentStatus::Cancelled;
    }
    if (linux_now() >= control.deadline) {
        diagnostic = "process containment operation deadline expired";
        return ProcessContainmentStatus::TimedOut;
    }
    return std::nullopt;
}

std::optional<ProcessContainmentStatus> acquire_operation_lock(
    const ProcessContainmentOperationControl &control,
    std::unique_lock<std::timed_mutex> &lock, std::string &diagnostic) {
    while (true) {
        if (const auto failure = operation_control_failure(control, diagnostic)) {
            return failure;
        }
        const auto remaining = control.deadline - linux_now();
        auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
        if (wait <= std::chrono::milliseconds::zero()) {
            wait = std::chrono::milliseconds(1);
        }
        wait = std::min(wait, std::chrono::milliseconds(5));
        if (lock.try_lock_for(wait)) {
            if (const auto failure =
                    operation_control_failure(control, diagnostic)) {
                return failure;
            }
            return std::nullopt;
        }
    }
}

ExecHandshakeOutcome read_exec_handshake(
    int fd, ChildFailure &failure,
    const ProcessContainmentOperationControl &control,
    std::string &diagnostic) {
    auto *bytes = reinterpret_cast<unsigned char *>(&failure);
    std::size_t received = 0;
    while (received < sizeof(failure)) {
        if (const auto failure_status =
                operation_control_failure(control, diagnostic)) {
            return *failure_status == ProcessContainmentStatus::Cancelled
                       ? ExecHandshakeOutcome::Cancelled
                       : ExecHandshakeOutcome::TimedOut;
        }

        const auto remaining = control.deadline - linux_now();
        auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(remaining);
        if (wait <= std::chrono::milliseconds::zero()) {
            wait = std::chrono::milliseconds(1);
        }
        wait = std::min(wait, std::chrono::milliseconds(10));
        pollfd descriptor{fd, POLLIN | POLLHUP, 0};
        const int poll_result =
            linux_poll(&descriptor, 1, static_cast<int>(wait.count()));
        if (poll_result < 0 && errno == EINTR) {
            continue;
        }
        if (poll_result < 0) {
            diagnostic = errno_diagnostic("exec handshake poll failed", errno);
            return ExecHandshakeOutcome::Failed;
        }
        if (poll_result == 0) {
            continue;
        }
        if (const auto failure_status =
                operation_control_failure(control, diagnostic)) {
            return *failure_status == ProcessContainmentStatus::Cancelled
                       ? ExecHandshakeOutcome::Cancelled
                       : ExecHandshakeOutcome::TimedOut;
        }
        if ((descriptor.revents & POLLNVAL) != 0) {
            diagnostic = "exec handshake descriptor became invalid";
            return ExecHandshakeOutcome::Failed;
        }

        const ssize_t count =
            linux_read(fd, bytes + received, sizeof(failure) - received);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            diagnostic = errno_diagnostic("exec handshake read failed", errno);
            return ExecHandshakeOutcome::Failed;
        }
        if (count == 0) {
            if (const auto failure_status =
                    operation_control_failure(control, diagnostic)) {
                return *failure_status == ProcessContainmentStatus::Cancelled
                           ? ExecHandshakeOutcome::Cancelled
                           : ExecHandshakeOutcome::TimedOut;
            }
            if (received != 0U) {
                diagnostic = "exec handshake ended with a partial failure record";
                return ExecHandshakeOutcome::Failed;
            }
            return ExecHandshakeOutcome::Executed;
        }
        received += static_cast<std::size_t>(count);
        if (const auto failure_status =
                operation_control_failure(control, diagnostic)) {
            return *failure_status == ProcessContainmentStatus::Cancelled
                       ? ExecHandshakeOutcome::Cancelled
                       : ExecHandshakeOutcome::TimedOut;
        }
    }
    if (failure.magic != kChildFailureMagic) {
        diagnostic = "exec handshake returned an invalid failure record";
        return ExecHandshakeOutcome::Failed;
    }
    return ExecHandshakeOutcome::ChildFailure;
}

bool create_cloexec_pipe(int descriptors[2], std::string &diagnostic) {
    int result;
    do {
        result = linux_pipe2(descriptors, O_CLOEXEC);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        diagnostic = errno_diagnostic("pipe creation failed", errno);
        descriptors[0] = -1;
        descriptors[1] = -1;
        return false;
    }
    return true;
}

bool dup2_nointr(int source, int destination) noexcept {
    int result;
    do {
        result = linux_dup2(source, destination);
    } while (result < 0 && errno == EINTR);
    return result >= 0;
}

bool signal_pidfd(int pidfd, int &error) noexcept {
#ifdef SYS_pidfd_send_signal
    const int result = linux_signal_pidfd(pidfd, SIGKILL, nullptr, 0U);
    if (result == 0 || errno == ESRCH) {
        return true;
    }
    error = errno;
    return false;
#else
    (void)pidfd;
    error = ENOSYS;
    return false;
#endif
}

enum class PidfdReapState {
    Running,
    Reaped,
    Ambiguous,
};

struct RollbackCompletion {
    std::atomic<bool> reaped{false};
};

PidfdReapState reap_pidfd_once(int pidfd) noexcept {
#ifdef SYS_waitid
    siginfo_t information{};
    const int result =
        linux_wait_pidfd(pidfd, &information, WEXITED | WNOHANG);
    if (result == 0) {
        return information.si_pid == 0 ? PidfdReapState::Running
                                       : PidfdReapState::Reaped;
    }
    return errno == ECHILD ? PidfdReapState::Reaped
                           : PidfdReapState::Ambiguous;
#else
    (void)pidfd;
    return PidfdReapState::Ambiguous;
#endif
}

class RollbackReaper {
public:
    class Reservation {
    public:
        Reservation() = default;
        Reservation(const Reservation &) = delete;
        Reservation &operator=(const Reservation &) = delete;
        Reservation(Reservation &&other) noexcept
            : owner_(other.owner_), index_(other.index_) {
            other.owner_ = nullptr;
        }
        Reservation &operator=(Reservation &&other) noexcept {
            if (this != &other) {
                cancel();
                owner_ = other.owner_;
                index_ = other.index_;
                other.owner_ = nullptr;
            }
            return *this;
        }
        ~Reservation() { cancel(); }

        explicit operator bool() const noexcept { return owner_ != nullptr; }
        void publish(ScopedFd pidfd,
                     std::shared_ptr<RollbackCompletion> completion) noexcept {
            if (owner_ == nullptr) {
                return;
            }
            owner_->publish(index_, pidfd.release(), std::move(completion));
            owner_ = nullptr;
        }
        void cancel() noexcept {
            if (owner_ != nullptr) {
                owner_->cancel(index_);
                owner_ = nullptr;
            }
        }

    private:
        Reservation(RollbackReaper *owner, std::size_t index) noexcept
            : owner_(owner), index_(index) {}

        RollbackReaper *owner_ = nullptr;
        std::size_t index_ = 0;
        friend class RollbackReaper;
    };

    RollbackReaper() {
        int descriptor = linux_eventfd(0U, EFD_CLOEXEC | EFD_NONBLOCK);
        if (descriptor < 0) {
            throw std::system_error(errno, std::generic_category(),
                                    "rollback reaper eventfd");
        }
        event_fd_.reset(descriptor);
        std::string diagnostic;
        if (!normalize_internal_fd(event_fd_, diagnostic)) {
            throw std::system_error(EMFILE, std::generic_category(), diagnostic);
        }
        worker_ = std::thread([this]() noexcept { run(); });
    }

    RollbackReaper(const RollbackReaper &) = delete;
    RollbackReaper &operator=(const RollbackReaper &) = delete;

    ~RollbackReaper() {
        stopping_.store(true, std::memory_order_release);
        wake();
        if (worker_.joinable()) {
            worker_.join();
        }
        for (auto &slot : slots_) {
            if (slot.state.load(std::memory_order_acquire) == 2) {
                (void)linux_close(slot.pidfd);
            }
        }
    }

    Reservation reserve() noexcept {
        for (std::size_t index = 0; index < slots_.size(); ++index) {
            int expected = 0;
            if (slots_[index].state.compare_exchange_strong(
                    expected, 1, std::memory_order_acq_rel)) {
                return Reservation(this, index);
            }
        }
        return {};
    }

private:
    struct Slot {
        std::atomic<int> state{0};
        int pidfd = -1;
        std::shared_ptr<RollbackCompletion> completion;
    };

    void publish(std::size_t index, int pidfd,
                 std::shared_ptr<RollbackCompletion> completion) noexcept {
        Slot &slot = slots_[index];
        slot.pidfd = pidfd;
        slot.completion = std::move(completion);
        slot.state.store(2, std::memory_order_release);
        wake();
    }

    void cancel(std::size_t index) noexcept {
        slots_[index].state.store(0, std::memory_order_release);
    }

    void wake() noexcept {
        const std::uint64_t value = 1U;
        (void)linux_write(event_fd_.get(), &value, sizeof(value));
    }

    void run() noexcept {
        while (!stopping_.load(std::memory_order_acquire)) {
            pollfd descriptor{event_fd_.get(), POLLIN, 0};
            int result;
            do {
                result = linux_poll(&descriptor, 1, 10);
            } while (result < 0 && errno == EINTR &&
                     !stopping_.load(std::memory_order_acquire));
            if (result > 0 && (descriptor.revents & POLLIN) != 0) {
                std::uint64_t value = 0;
                while (linux_read(event_fd_.get(), &value, sizeof(value)) < 0 &&
                       errno == EINTR) {
                }
            }
            for (auto &slot : slots_) {
                if (slot.state.load(std::memory_order_acquire) != 2) {
                    continue;
                }
                int signal_error = 0;
                (void)signal_pidfd(slot.pidfd, signal_error);
                if (reap_pidfd_once(slot.pidfd) == PidfdReapState::Reaped) {
                    slot.completion->reaped.store(true,
                                                  std::memory_order_release);
                    (void)linux_close(slot.pidfd);
                    slot.pidfd = -1;
                    slot.completion.reset();
                    slot.state.store(0, std::memory_order_release);
                }
            }
        }
    }

    std::array<Slot, 1024> slots_{};
    ScopedFd event_fd_;
    std::atomic<bool> stopping_{false};
    std::thread worker_;
};

RollbackReaper &rollback_reaper() {
    static RollbackReaper reaper;
    return reaper;
}

class ChildRollbackGuard {
public:
    ChildRollbackGuard(pid_t pid, ScopedFd pidfd, ScopedFd pending_pidfd,
                       std::shared_ptr<RollbackCompletion> completion,
                       RollbackReaper::Reservation reservation,
                       pid_t &pending_pid, ScopedFd &state_pending_pidfd,
                       std::shared_ptr<RollbackCompletion>
                           &state_pending_completion) noexcept
        : pid_(pid), pidfd_(std::move(pidfd)),
          pending_pidfd_(std::move(pending_pidfd)),
          completion_(std::move(completion)),
          reservation_(std::move(reservation)), pending_pid_(pending_pid),
          state_pending_pidfd_(state_pending_pidfd),
          state_pending_completion_(state_pending_completion) {}
    ChildRollbackGuard(const ChildRollbackGuard &) = delete;
    ChildRollbackGuard &operator=(const ChildRollbackGuard &) = delete;
    ~ChildRollbackGuard() {
        if (!armed_) {
            return;
        }
        int signal_error = 0;
        (void)signal_pidfd(pidfd_.get(), signal_error);
        if (reap_pidfd_once(pidfd_.get()) == PidfdReapState::Reaped) {
            reservation_.cancel();
            return;
        }
        pending_pid_ = pid_;
        state_pending_pidfd_ = std::move(pending_pidfd_);
        state_pending_completion_ = completion_;
        reservation_.publish(std::move(pidfd_), std::move(completion_));
    }
    ScopedFd commit() noexcept {
        armed_ = false;
        reservation_.cancel();
        return std::move(pidfd_);
    }
    int pidfd() const noexcept { return pidfd_.get(); }
    bool normalize_pidfd(std::string &diagnostic) {
        if (normalize_internal_fd(pidfd_, diagnostic)) {
            return true;
        }
        if (pending_pidfd_) {
            pidfd_.reset();
            pidfd_ = std::move(pending_pidfd_);
            diagnostic.clear();
            return true;
        }
        return false;
    }

private:
    pid_t pid_;
    ScopedFd pidfd_;
    ScopedFd pending_pidfd_;
    std::shared_ptr<RollbackCompletion> completion_;
    RollbackReaper::Reservation reservation_;
    pid_t &pending_pid_;
    ScopedFd &state_pending_pidfd_;
    std::shared_ptr<RollbackCompletion> &state_pending_completion_;
    bool armed_ = true;
};

bool validate_spawn_inputs(
    const std::string &executable, const std::vector<std::string> &args,
    const std::string &working_dir,
    const std::vector<std::pair<std::string, std::string>> &env_vars,
    std::string &diagnostic) {
    if (executable.empty() || executable.size() > PATH_MAX ||
        has_nul(executable) || has_control(executable)) {
        diagnostic = "executable path is empty, oversized, or contains controls";
        return false;
    }
    if (working_dir.size() > PATH_MAX || has_nul(working_dir) ||
        has_control(working_dir)) {
        diagnostic = "working directory is oversized or contains controls";
        return false;
    }
    if (args.size() > kMaxMembers || env_vars.size() > kMaxMembers) {
        diagnostic = "spawn input contains too many arguments or environment entries";
        return false;
    }
    std::size_t total = 0U;
    const auto add_size = [&](std::size_t amount) {
        if (total > kMaxSpawnBytes || amount > kMaxSpawnBytes - total) {
            return false;
        }
        total += amount;
        return true;
    };
    if (!add_size(executable.size()) || !add_size(1U) ||
        !add_size(working_dir.size()) || !add_size(1U)) {
        diagnostic = "spawn input exceeds the bounded byte limit";
        return false;
    }
    for (const auto &argument : args) {
        if (has_nul(argument) || !add_size(argument.size()) || !add_size(1U)) {
            diagnostic = "spawn argument is invalid or exceeds the byte limit";
            return false;
        }
    }
    for (const auto &entry : env_vars) {
        if (entry.first.empty() || entry.first.find('=') != std::string::npos ||
            has_nul(entry.first) || has_control(entry.first) ||
            has_nul(entry.second) || !add_size(entry.first.size()) ||
            !add_size(entry.second.size()) || !add_size(2U)) {
            diagnostic = "spawn environment entry is invalid or exceeds the byte limit";
            return false;
        }
    }
    return true;
}

struct CapabilitySnapshot {
    __user_cap_header_struct header{};
    std::array<__user_cap_data_struct, 2> data{};
    bool preserve_sys_resource = false;
};

bool capture_capabilities(CapabilitySnapshot &snapshot,
                          std::string &diagnostic) {
    snapshot.header.version = _LINUX_CAPABILITY_VERSION_3;
    snapshot.header.pid = 0;
#ifdef SYS_capget
    if (linux_get_capabilities(&snapshot.header, snapshot.data.data()) != 0) {
        diagnostic = errno_diagnostic("capability snapshot failed", errno);
        return false;
    }
#else
    diagnostic = "capget is unavailable on this Linux build";
    return false;
#endif
    constexpr std::uint32_t mask = 1U << (CAP_SYS_RESOURCE % 32);
    constexpr std::size_t word = CAP_SYS_RESOURCE / 32;
    snapshot.preserve_sys_resource =
        (snapshot.data[word].effective & mask) != 0U;
    if (snapshot.preserve_sys_resource) {
        snapshot.data[word].inheritable |= mask;
    }
    return true;
}

bool apply_child_capabilities(const CapabilitySnapshot &snapshot,
                              int &error) noexcept {
    if (!snapshot.preserve_sys_resource) {
        return true;
    }
#ifdef SYS_capset
    if (linux_set_capabilities(&snapshot.header, snapshot.data.data()) != 0) {
        error = errno;
        return false;
    }
#else
    error = ENOSYS;
    return false;
#endif
    if (linux_set_process_control(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE,
                                  CAP_SYS_RESOURCE, 0, 0) != 0) {
        error = errno;
        return false;
    }
    return true;
}

bool snapshot_environment(
    const std::vector<std::pair<std::string, std::string>> &overrides,
    std::vector<std::string> &environment, std::string &effective_path,
    std::string &diagnostic) {
    environment.clear();
    std::size_t total = 0U;
    for (char **entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
        if (environment.size() == kMaxMembers) {
            diagnostic = "process environment exceeds the bounded entry limit";
            return false;
        }
        const std::string_view value(*entry);
        if (value.size() > kMaxSpawnBytes - std::min(total, kMaxSpawnBytes)) {
            diagnostic = "process environment exceeds the bounded byte limit";
            return false;
        }
        total += value.size();
        environment.emplace_back(value);
    }

    for (const auto &override_value : overrides) {
        const std::string prefix = override_value.first + "=";
        const std::string replacement = prefix + override_value.second;
        auto first = environment.end();
        for (auto current = environment.begin(); current != environment.end();) {
            if (current->rfind(prefix, 0) != 0) {
                ++current;
                continue;
            }
            if (first == environment.end()) {
                *current = replacement;
                first = current;
                ++current;
            } else {
                current = environment.erase(current);
            }
        }
        if (first == environment.end()) {
            environment.push_back(replacement);
        }
    }

    total = 0U;
    if (environment.size() > kMaxMembers) {
        diagnostic = "process environment exceeds the bounded entry limit";
        return false;
    }
    for (const auto &entry : environment) {
        if (entry.size() + 1U >
            kMaxSpawnBytes - std::min(total, kMaxSpawnBytes)) {
            diagnostic = "process environment exceeds the bounded byte limit";
            return false;
        }
        total += entry.size() + 1U;
    }

    bool found_path = false;
    for (const auto &entry : environment) {
        if (entry.rfind("PATH=", 0) == 0) {
            effective_path = entry.substr(5U);
            found_path = true;
            break;
        }
    }
    if (!found_path) {
        const std::size_t size = linux_confstr(_CS_PATH, nullptr, 0);
        if (size == 0U) {
            diagnostic = "default executable search path is unavailable";
            return false;
        }
        std::vector<char> value(size);
        if (linux_confstr(_CS_PATH, value.data(), value.size()) == 0U) {
            diagnostic = "default executable search path changed during capture";
            return false;
        }
        effective_path.assign(value.data());
    }
    return true;
}

bool build_exec_candidates(const std::string &executable,
                           std::string_view effective_path,
                           std::vector<std::string> &candidates,
                           std::string &diagnostic) {
    candidates.clear();
    if (executable.find('/') != std::string::npos) {
        candidates.push_back(executable);
        return true;
    }

    std::size_t cursor = 0;
    std::size_t total = 0U;
    while (cursor <= effective_path.size()) {
        if (candidates.size() == kMaxMembers) {
            diagnostic = "executable search path exceeds the bounded entry limit";
            return false;
        }
        const std::size_t separator = effective_path.find(':', cursor);
        const std::size_t end =
            separator == std::string_view::npos ? effective_path.size()
                                                : separator;
        const std::string_view directory =
            effective_path.substr(cursor, end - cursor);
        std::string candidate;
        if (directory.empty()) {
            candidate = executable;
        } else {
            if (directory.size() > kMaxSpawnBytes - executable.size() - 1U) {
                diagnostic = "executable search candidate exceeds the byte limit";
                return false;
            }
            candidate.reserve(directory.size() + executable.size() + 1U);
            candidate.append(directory);
            candidate.push_back('/');
            candidate.append(executable);
        }
        if (candidate.size() + 1U >
            kMaxSpawnBytes - std::min(total, kMaxSpawnBytes)) {
            diagnostic = "executable search candidates exceed the byte limit";
            return false;
        }
        total += candidate.size() + 1U;
        candidates.push_back(std::move(candidate));
        if (separator == std::string_view::npos) {
            break;
        }
        cursor = separator + 1U;
    }
    return !candidates.empty();
}

constexpr std::size_t kKernelSignalCount = 64U;
constexpr std::size_t kSignalWordBits = sizeof(unsigned long) * 8U;
using KernelSignalMask =
    std::array<unsigned long, kKernelSignalCount / kSignalWordBits>;
static_assert(kKernelSignalCount % kSignalWordBits == 0U);

class CloneSignalMaskGuard {
public:
    bool block(std::string &diagnostic) noexcept {
#ifdef SYS_rt_sigprocmask
        KernelSignalMask blocked;
        blocked.fill(~0UL);
        if (linux_set_signal_mask(SIG_SETMASK, blocked.data(),
                                  previous_mask_.data(), sizeof(blocked)) != 0) {
            diagnostic = errno_diagnostic("cannot block signals around clone3",
                                          errno);
            return false;
        }
        active_ = true;
        return true;
#else
        diagnostic = "rt_sigprocmask is unavailable on this Linux build";
        return false;
#endif
    }

    bool restore(std::string &diagnostic) noexcept {
        if (!active_) {
            return true;
        }
#ifdef SYS_rt_sigprocmask
        if (linux_set_signal_mask(SIG_SETMASK, previous_mask_.data(), nullptr,
                                  sizeof(previous_mask_)) != 0) {
            diagnostic = errno_diagnostic(
                "cannot restore parent signal mask after clone3", errno);
            return false;
        }
        active_ = false;
        return true;
#else
        diagnostic = "rt_sigprocmask is unavailable on this Linux build";
        return false;
#endif
    }

    const KernelSignalMask &previous_mask() const noexcept {
        return previous_mask_;
    }

    ~CloneSignalMaskGuard() {
        if (active_) {
#ifdef SYS_rt_sigprocmask
            (void)linux_set_signal_mask(SIG_SETMASK, previous_mask_.data(),
                                        nullptr, sizeof(previous_mask_));
#endif
        }
    }

private:
    KernelSignalMask previous_mask_{};
    bool active_ = false;
};

bool reset_child_signal_dispositions(int &error) noexcept {
    struct sigaction default_action {};
    default_action.sa_handler = SIG_DFL;
    for (int signal_number = 1; signal_number < NSIG; ++signal_number) {
        if (signal_number == SIGKILL || signal_number == SIGSTOP) {
            continue;
        }
        struct sigaction inherited_action {};
        if (linux_sigaction(signal_number, nullptr, &inherited_action) != 0) {
            if (errno == EINVAL) {
                continue;
            }
            error = errno;
            return false;
        }
        if (inherited_action.sa_handler == SIG_DFL ||
            inherited_action.sa_handler == SIG_IGN) {
            continue;
        }
        if (linux_sigaction(signal_number, &default_action, nullptr) != 0) {
            error = errno;
            return false;
        }
    }
    return true;
}

bool restore_child_signal_mask(const KernelSignalMask &mask,
                               int &error) noexcept {
#ifdef SYS_rt_sigprocmask
    if (linux_set_signal_mask(SIG_SETMASK, mask.data(), nullptr,
                              sizeof(mask)) != 0) {
        error = errno;
        return false;
    }
    return true;
#else
    (void)mask;
    error = ENOSYS;
    return false;
#endif
}

pid_t clone_into_cgroup(int cgroup_fd, int &pidfd,
                        std::string &diagnostic) {
#ifdef SYS_clone3
    clone_args arguments{};
    arguments.flags = CLONE_INTO_CGROUP | CLONE_PIDFD;
    arguments.pidfd = static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(&pidfd));
    arguments.exit_signal = SIGCHLD;
    arguments.cgroup = static_cast<decltype(arguments.cgroup)>(
        static_cast<unsigned int>(cgroup_fd));
    long result;
    do {
        result = linux_clone_process(&arguments, sizeof(arguments));
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        diagnostic = errno_diagnostic(
            "atomic cgroup process creation is unavailable", errno);
        return -1;
    }
    return static_cast<pid_t>(result);
#else
    (void)cgroup_fd;
    (void)pidfd;
    diagnostic = "clone3 atomic cgroup process creation is unavailable";
    return -1;
#endif
}

bool read_boot_id(std::string &boot_id, std::string &diagnostic) {
    int raw_fd;
    do {
        raw_fd = linux_open("/proc/sys/kernel/random/boot_id",
                            O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    } while (raw_fd < 0 && errno == EINTR);
    ScopedFd fd(raw_fd);
    if (!fd) {
        diagnostic = errno_diagnostic("cannot open boot identity", errno);
        return false;
    }
    if (!read_bounded_fd(fd.get(), 128U, boot_id, diagnostic)) {
        return false;
    }
    boot_id = trim_ascii_whitespace(std::move(boot_id));
    if (!valid_boot_id(boot_id)) {
        diagnostic = "boot identity is malformed";
        return false;
    }
    return true;
}

bool statx_identity(int fd, std::uint64_t &mount_id, std::uint64_t &device,
                    std::uint64_t &inode, std::string &diagnostic) {
    struct statx value {};
    int result;
    do {
        result = linux_statx(fd, "", AT_EMPTY_PATH | AT_STATX_SYNC_AS_STAT,
                             STATX_BASIC_STATS | STATX_MNT_ID, &value);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        diagnostic = errno_diagnostic("statx identity failed", errno);
        return false;
    }
    if ((value.stx_mask & (STATX_TYPE | STATX_INO | STATX_MNT_ID)) !=
        (STATX_TYPE | STATX_INO | STATX_MNT_ID)) {
        diagnostic = "statx did not supply the required cgroup identity";
        return false;
    }
    if ((value.stx_mode & S_IFMT) != S_IFDIR || value.stx_ino == 0U ||
        value.stx_mnt_id == 0U) {
        diagnostic = "cgroup directory identity is invalid";
        return false;
    }

    struct stat legacy {};
    if (linux_fstat(fd, &legacy) != 0) {
        diagnostic = errno_diagnostic("fstat identity failed", errno);
        return false;
    }
    const dev_t encoded = makedev(value.stx_dev_major, value.stx_dev_minor);
    if (!S_ISDIR(legacy.st_mode) || legacy.st_ino != value.stx_ino ||
        legacy.st_dev != encoded) {
        diagnostic = "statx and fstat cgroup identities disagree";
        return false;
    }

    mount_id = value.stx_mnt_id;
    device = static_cast<std::uint64_t>(encoded);
    inode = value.stx_ino;
    return true;
}

bool verify_cgroup2(int fd, std::string &diagnostic) {
    struct statfs filesystem {};
    if (linux_fstatfs(fd, &filesystem) != 0) {
        diagnostic = errno_diagnostic("cgroup filesystem validation failed", errno);
        return false;
    }
    if (static_cast<unsigned long>(filesystem.f_type) !=
        static_cast<unsigned long>(CGROUP2_SUPER_MAGIC)) {
        diagnostic = "delegated root is not on a cgroup-v2 filesystem";
        return false;
    }
    return true;
}

bool has_child_directories(int fd, std::string &diagnostic) {
    int duplicate;
    do {
        duplicate =
            linux_openat(fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    } while (duplicate < 0 && errno == EINTR);
    if (duplicate < 0) {
        diagnostic = errno_diagnostic("cannot inspect cgroup descendants", errno);
        return true;
    }
    DIR *directory = linux_fdopendir(duplicate);
    if (directory == nullptr) {
        const int error = errno;
        (void)linux_close(duplicate);
        diagnostic = errno_diagnostic("cannot inspect cgroup descendants", error);
        return true;
    }

    bool found = false;
    int readdir_error = 0;
    while (true) {
        errno = 0;
        dirent *entry = linux_readdir(directory);
        if (entry == nullptr) {
            readdir_error = errno;
            break;
        }
        const std::string_view name(entry->d_name);
        if (name == "." || name == "..") {
            continue;
        }
        if (entry->d_type == DT_DIR) {
            found = true;
            break;
        }
        if (entry->d_type == DT_UNKNOWN) {
            struct stat value {};
            if (linux_fstatat(fd, entry->d_name, &value,
                              AT_SYMLINK_NOFOLLOW) != 0) {
                diagnostic = errno_diagnostic("cannot stat cgroup descendant", errno);
                found = true;
                break;
            }
            if (S_ISDIR(value.st_mode)) {
                found = true;
                break;
            }
        }
    }
    if (readdir_error != 0 && !found) {
        diagnostic = errno_diagnostic("cannot read cgroup directory",
                                      readdir_error);
        found = true;
    }
    (void)linux_closedir(directory);
    return found;
}

bool parse_populated(std::string_view text, bool &populated,
                     std::string &diagnostic) {
    bool found = false;
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        const std::size_t end = text.find('\n', cursor);
        const std::size_t length =
            (end == std::string_view::npos ? text.size() : end) - cursor;
        const std::string_view line = text.substr(cursor, length);
        if (line.rfind("populated ", 0) == 0) {
            if (found || (line != "populated 0" && line != "populated 1")) {
                diagnostic = "cgroup.events has malformed populated state";
                return false;
            }
            found = true;
            populated = line.back() == '1';
        }
        if (end == std::string_view::npos) {
            break;
        }
        cursor = end + 1U;
    }
    if (!found) {
        diagnostic = "cgroup.events omits populated state";
        return false;
    }
    return true;
}

bool validate_domain_controls(int fd, bool require_empty,
                              std::string &diagnostic) {
    std::string value;
    if (!read_control(fd, "cgroup.type", value, diagnostic) ||
        trim_ascii_whitespace(std::move(value)) != "domain") {
        if (diagnostic.empty()) {
            diagnostic = "cgroup is not a non-threaded domain";
        }
        return false;
    }
    if (!read_control(fd, "cgroup.subtree_control", value, diagnostic)) {
        return false;
    }
    if (!trim_ascii_whitespace(std::move(value)).empty()) {
        diagnostic = "cgroup has enabled subtree controllers";
        return false;
    }
    if (!read_control(fd, "cgroup.max.depth", value, diagnostic)) {
        return false;
    }
    if (trim_ascii_whitespace(std::move(value)) != "0") {
        diagnostic = "cgroup descendant depth is not locked to zero";
        return false;
    }
    if (has_child_directories(fd, diagnostic)) {
        if (diagnostic.empty()) {
            diagnostic = "cgroup has descendant cgroups";
        }
        return false;
    }
    if (!require_empty) {
        return true;
    }
    if (!read_control(fd, "cgroup.procs", value, diagnostic)) {
        return false;
    }
    if (!trim_ascii_whitespace(std::move(value)).empty()) {
        diagnostic = "cgroup has direct process members";
        return false;
    }
    if (!read_control(fd, "cgroup.events", value, diagnostic)) {
        return false;
    }
    bool populated = false;
    if (!parse_populated(value, populated, diagnostic) || populated) {
        if (diagnostic.empty()) {
            diagnostic = "cgroup is populated";
        }
        return false;
    }
    return true;
}

bool parse_pid_list(std::string_view text, std::vector<int> &members,
                    std::string &diagnostic) {
    members.clear();
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        while (cursor < text.size() &&
               std::strchr(" \t\r\n\f\v", text[cursor]) != nullptr) {
            ++cursor;
        }
        if (cursor == text.size()) {
            break;
        }
        const std::size_t start = cursor;
        while (cursor < text.size() && text[cursor] >= '0' &&
               text[cursor] <= '9') {
            ++cursor;
        }
        if (start == cursor ||
            (cursor < text.size() &&
             std::strchr(" \t\r\n\f\v", text[cursor]) == nullptr)) {
            diagnostic = "cgroup.procs contains a malformed process id";
            return false;
        }
        int pid = 0;
        const auto conversion =
            std::from_chars(text.data() + start, text.data() + cursor, pid);
        if (conversion.ec != std::errc() || conversion.ptr != text.data() + cursor ||
            pid <= 0) {
            diagnostic = "cgroup.procs contains an invalid process id";
            return false;
        }
        if (members.size() == kMaxMembers) {
            diagnostic = "cgroup membership exceeds the bounded member limit";
            return false;
        }
        members.push_back(pid);
    }
    std::sort(members.begin(), members.end());
    if (std::adjacent_find(members.begin(), members.end()) != members.end()) {
        diagnostic = "cgroup.procs repeated a process id during one read";
        return false;
    }
    return true;
}

bool read_members(int fd, std::vector<int> &members, std::string &diagnostic) {
    std::string text;
    return read_bounded_fd(fd, kMaxControlBytes, text, diagnostic) &&
           parse_pid_list(text, members, diagnostic);
}

bool read_members_until(int fd, std::vector<int> &members,
                        std::chrono::steady_clock::time_point deadline,
                        std::string &diagnostic) {
    std::string text;
    return read_bounded_fd_until(fd, kMaxControlBytes, deadline, text,
                                 diagnostic) &&
           parse_pid_list(text, members, diagnostic);
}

ScopedFd open_pidfd(int pid, std::string &diagnostic) {
#ifdef SYS_pidfd_open
    int fd;
    do {
        fd = linux_open_pidfd(pid, 0U);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        diagnostic = errno_diagnostic("pidfd_open failed", errno);
    }
    ScopedFd result(fd);
    if (result && !normalize_internal_fd(result, diagnostic)) {
        return {};
    }
    return result;
#else
    (void)pid;
    diagnostic = "pidfd_open is unavailable on this Linux build";
    return {};
#endif
}

enum class PidfdState {
    Alive,
    Exited,
    Failed,
};

PidfdState pidfd_state(
    int fd, std::string &diagnostic,
    std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::time_point::max()) {
    pollfd descriptor{fd, POLLIN, 0};
    int result;
    do {
        result = linux_poll(&descriptor, 1, 0);
    } while (result < 0 && errno == EINTR && linux_now() < deadline);
    if (result < 0) {
        diagnostic = errno == EINTR
                         ? "pidfd poll exceeded the operation deadline"
                         : errno_diagnostic("pidfd poll failed", errno);
        return PidfdState::Failed;
    }
    if (result != 0 && (descriptor.revents & POLLNVAL) != 0) {
        diagnostic = "pidfd became invalid";
        return PidfdState::Failed;
    }
    if (result != 0 &&
        (descriptor.revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
        return PidfdState::Exited;
    }
    return PidfdState::Alive;
}

bool read_process_identity(int pid, const std::string &boot_id,
                           ProcessBirthIdentity &identity,
                           std::string &diagnostic) {
    const std::string path = "/proc/" + std::to_string(pid) + "/stat";
    int raw_fd;
    do {
        raw_fd = linux_open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    } while (raw_fd < 0 && errno == EINTR);
    ScopedFd fd(raw_fd);
    if (!fd) {
        diagnostic = errno_diagnostic("cannot open process stat", errno);
        return false;
    }
    std::string text;
    if (!read_bounded_fd(fd.get(), 65536U, text, diagnostic)) {
        return false;
    }

    const std::size_t open_parenthesis = text.find('(');
    const std::size_t close_parenthesis = text.rfind(')');
    if (open_parenthesis == std::string::npos || close_parenthesis == std::string::npos ||
        open_parenthesis == 0U || close_parenthesis <= open_parenthesis ||
        close_parenthesis + 1U >= text.size()) {
        diagnostic = "process stat record is malformed";
        return false;
    }

    std::string_view pid_field(text.data(), open_parenthesis);
    while (!pid_field.empty() && pid_field.back() == ' ') {
        pid_field.remove_suffix(1);
    }
    int parsed_pid = 0;
    const auto pid_conversion = std::from_chars(
        pid_field.data(), pid_field.data() + pid_field.size(), parsed_pid);
    if (pid_conversion.ec != std::errc() ||
        pid_conversion.ptr != pid_field.data() + pid_field.size() ||
        parsed_pid != pid) {
        diagnostic = "process stat pid does not match the requested process";
        return false;
    }

    std::string_view suffix(text.data() + close_parenthesis + 1U,
                            text.size() - close_parenthesis - 1U);
    std::array<std::string_view, 20> fields{};
    std::size_t count = 0;
    std::size_t cursor = 0;
    while (cursor < suffix.size() && count < fields.size()) {
        while (cursor < suffix.size() && suffix[cursor] == ' ') {
            ++cursor;
        }
        if (cursor == suffix.size()) {
            break;
        }
        const std::size_t start = cursor;
        while (cursor < suffix.size() && suffix[cursor] != ' ' &&
               suffix[cursor] != '\n') {
            ++cursor;
        }
        fields[count++] = suffix.substr(start, cursor - start);
    }
    if (count < fields.size() || fields[0].size() != 1U) {
        diagnostic = "process stat omits field 22";
        return false;
    }
    std::uint64_t start_time = 0;
    const auto time_conversion = std::from_chars(
        fields[19].data(), fields[19].data() + fields[19].size(), start_time);
    if (time_conversion.ec != std::errc() ||
        time_conversion.ptr != fields[19].data() + fields[19].size() ||
        start_time == 0U) {
        diagnostic = "process stat has an invalid field 22";
        return false;
    }
    identity = ProcessBirthIdentity{boot_id, pid, start_time};
    return true;
}

struct CapturedMember {
    ProcessBirthIdentity identity;
    ScopedFd pidfd;
};

class LinuxProcessContainmentState final : public ProcessContainmentState {
public:
    LinuxProcessContainmentState(ProcessContainmentIdentity identity,
                                 std::string leaf_name, ScopedFd root_fd,
                                 ScopedFd scope_fd, ScopedFd procs_read_fd,
                                 ScopedFd procs_write_fd, ScopedFd kill_fd,
                                 ScopedFd events_fd) noexcept
        : identity_(std::move(identity)), leaf_name_(std::move(leaf_name)),
          root_fd_(std::move(root_fd)), scope_fd_(std::move(scope_fd)),
          procs_read_fd_(std::move(procs_read_fd)),
          procs_write_fd_(std::move(procs_write_fd)),
          kill_fd_(std::move(kill_fd)), events_fd_(std::move(events_fd)) {}

    bool active() const noexcept override { return active_.load(); }

    ProcessContainmentStartResult start(
        const std::string &executable, const std::vector<std::string> &args,
        const ProcessContainmentOperationControl &control,
        const std::string &working_dir, bool inherit_output,
        bool filter_health_logs,
        const std::vector<std::pair<std::string, std::string>> &env_vars) override {
        std::string diagnostic;
        std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
        if (const auto failure =
                acquire_operation_lock(control, lock, diagnostic)) {
            return failed<ProcessContainmentStartResult>(
                *failure, std::move(diagnostic));
        }
        if (!active_.load()) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::Closed, "process containment is closed");
        }
        if (!resolve_pending_cleanup()) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::StillPopulated,
                "a prior failed child is still awaiting contained cleanup");
        }
        if (started_) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::InvalidRequest,
                "process containment already started a direct child");
        }

        if (!validate_spawn_inputs(executable, args, working_dir, env_vars,
                                   diagnostic)) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::InvalidRequest,
                std::move(diagnostic));
        }
        if (inherit_output && filter_health_logs) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::InvalidRequest,
                "contained profiling processes do not support filtered output");
        }
        if (const auto failure_status =
                operation_control_failure(control, diagnostic)) {
            return failed<ProcessContainmentStartResult>(
                *failure_status, std::move(diagnostic));
        }
        if (!revalidate_scope(diagnostic)) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::ScopeChanged, std::move(diagnostic));
        }
        std::vector<int> initial_members;
        if (!read_members(procs_read_fd_.get(), initial_members, diagnostic)) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::MembershipChanged,
                std::move(diagnostic));
        }
        if (!initial_members.empty()) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::ScopeNotEmpty,
                "containment scope must be empty before start");
        }
        if (const auto failure_status =
                operation_control_failure(control, diagnostic)) {
            return failed<ProcessContainmentStartResult>(
                *failure_status, std::move(diagnostic));
        }

        std::vector<std::string> environment;
        std::string effective_path;
        if (!snapshot_environment(env_vars, environment, effective_path,
                                  diagnostic)) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::SpawnFailed, std::move(diagnostic));
        }
        std::vector<std::string> exec_candidates;
        if (!build_exec_candidates(executable, effective_path, exec_candidates,
                                   diagnostic)) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::SpawnFailed, std::move(diagnostic));
        }

        std::vector<char *> argv;
        argv.reserve(args.size() + 2U);
        argv.push_back(const_cast<char *>(executable.c_str()));
        for (const auto &argument : args) {
            argv.push_back(const_cast<char *>(argument.c_str()));
        }
        argv.push_back(nullptr);

        std::vector<char *> envp;
        envp.reserve(environment.size() + 1U);
        for (auto &entry : environment) {
            envp.push_back(entry.data());
        }
        envp.push_back(nullptr);

        std::vector<char *> shell_argv;
        shell_argv.reserve(args.size() + 3U);
        shell_argv.push_back(const_cast<char *>("/bin/sh"));
        shell_argv.push_back(nullptr);

        std::vector<const char *> exec_candidate_paths;
        exec_candidate_paths.reserve(exec_candidates.size());
        for (const auto &candidate : exec_candidates) {
            exec_candidate_paths.push_back(candidate.c_str());
        }
        char **const child_argv = argv.data();
        char **const child_envp = envp.data();
        char **const child_shell_argv = shell_argv.data();
        const char *const child_working_dir = working_dir.c_str();
        const char *const *const child_exec_candidates =
            exec_candidate_paths.data();
        const std::size_t child_exec_candidate_count =
            exec_candidate_paths.size();
        for (const auto &argument : args) {
            shell_argv.push_back(const_cast<char *>(argument.c_str()));
        }
        shell_argv.push_back(nullptr);

        CapabilitySnapshot capabilities;
        if (!capture_capabilities(capabilities, diagnostic)) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::SpawnFailed, std::move(diagnostic));
        }

        if (inherit_output) {
            std::string command_line = executable;
            for (const auto &argument : args) {
                command_line += " " + argument;
            }
            LOG(DEBUG, "ProcessManager")
                << "Starting contained process"
                << (filter_health_logs ? " with filtered output: "
                                       : " with inherited output: ")
                << command_line << std::endl;
        }

        if (const auto failure_status =
                operation_control_failure(control, diagnostic)) {
            return failed<ProcessContainmentStartResult>(
                *failure_status, std::move(diagnostic));
        }

        int status_pipe[2] = {-1, -1};
        if (!create_cloexec_pipe(status_pipe, diagnostic)) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::SpawnFailed, std::move(diagnostic));
        }
        ScopedFd status_read_fd(status_pipe[0]);
        ScopedFd status_write_fd(status_pipe[1]);
        if (!normalize_internal_fd(status_read_fd, diagnostic) ||
            !normalize_internal_fd(status_write_fd, diagnostic)) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::SpawnFailed, std::move(diagnostic));
        }

        ScopedFd null_fd;
        if (!inherit_output) {
            int descriptor;
            do {
                descriptor = linux_open("/dev/null", O_WRONLY | O_CLOEXEC);
            } while (descriptor < 0 && errno == EINTR);
            null_fd.reset(descriptor);
            if (!null_fd || !normalize_internal_fd(null_fd, diagnostic)) {
                return failed<ProcessContainmentStartResult>(
                    ProcessContainmentStatus::SpawnFailed,
                    diagnostic.empty()
                        ? errno_diagnostic("cannot open null output sink", errno)
                        : std::move(diagnostic));
            }
        }

        RollbackReaper::Reservation reaper_reservation;
        std::shared_ptr<RollbackCompletion> rollback_completion;
        try {
            reaper_reservation = rollback_reaper().reserve();
            rollback_completion = std::make_shared<RollbackCompletion>();
        } catch (const std::exception &error) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::SpawnFailed,
                bounded(std::string("rollback reaper initialization failed: ") +
                        error.what()));
        }
        if (!reaper_reservation) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::SpawnFailed,
                "rollback reaper has no free bounded ownership slot");
        }
        int pending_descriptor;
        do {
            pending_descriptor = linux_open("/dev/null", O_RDONLY | O_CLOEXEC);
        } while (pending_descriptor < 0 && errno == EINTR);
        ScopedFd pending_pidfd_slot(pending_descriptor);
        if (!pending_pidfd_slot ||
            !normalize_internal_fd(pending_pidfd_slot, diagnostic)) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::SpawnFailed,
                diagnostic.empty()
                    ? errno_diagnostic("cannot reserve rollback pidfd", errno)
                    : std::move(diagnostic));
        }
        if (const auto failure_status =
                operation_control_failure(control, diagnostic)) {
            return failed<ProcessContainmentStartResult>(
                *failure_status, std::move(diagnostic));
        }

        if (!revalidate_scope(diagnostic)) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::ScopeChanged,
                std::move(diagnostic));
        }
        std::vector<int> preclone_members;
        if (!read_members(procs_read_fd_.get(), preclone_members, diagnostic)) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::MembershipChanged,
                std::move(diagnostic));
        }
        if (!preclone_members.empty()) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::ScopeNotEmpty,
                "containment scope became populated before clone3");
        }
        if (const auto failure_status =
                operation_control_failure(control, diagnostic)) {
            return failed<ProcessContainmentStartResult>(
                *failure_status, std::move(diagnostic));
        }

        const bool parent_thread_is_process_leader =
            static_cast<pid_t>(linux_gettid()) == linux_getpid();
        const pid_t parent_process_pid = linux_getpid();
        CloneSignalMaskGuard signal_mask_guard;
        if (!signal_mask_guard.block(diagnostic)) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::SpawnFailed, std::move(diagnostic));
        }
        if (const auto failure_status =
                operation_control_failure(control, diagnostic)) {
            return failed<ProcessContainmentStartResult>(
                *failure_status, std::move(diagnostic));
        }
        int clone_pidfd = -1;
        const pid_t pid =
            clone_into_cgroup(scope_fd_.get(), clone_pidfd, diagnostic);
        std::string parent_mask_diagnostic;
        const bool parent_mask_restored =
            pid == 0 || signal_mask_guard.restore(parent_mask_diagnostic);
        if (pid < 0) {
            if (!parent_mask_restored) {
                diagnostic = bounded(parent_mask_diagnostic + "; " + diagnostic);
            }
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::SpawnFailed, std::move(diagnostic));
        }

        if (pid == 0) {
            (void)linux_close(status_read_fd.get());

            int signal_error = 0;
            if (!reset_child_signal_dispositions(signal_error)) {
                report_child_failure(status_write_fd.get(),
                                     ChildFailureStage::SignalState,
                                     signal_error);
                linux_exit_child(127);
            }
            if (parent_thread_is_process_leader &&
                linux_set_process_control(PR_SET_PDEATHSIG, SIGTERM, 0, 0,
                                          0) != 0) {
                report_child_failure(status_write_fd.get(),
                                     ChildFailureStage::ParentDeathSignal, errno);
                linux_exit_child(127);
            }
            if (parent_thread_is_process_leader &&
                linux_getppid() != parent_process_pid) {
                report_child_failure(status_write_fd.get(),
                                     ChildFailureStage::ParentDeathSignal,
                                     ESRCH);
                linux_exit_child(127);
            }
            if (!restore_child_signal_mask(signal_mask_guard.previous_mask(),
                                           signal_error)) {
                report_child_failure(status_write_fd.get(),
                                     ChildFailureStage::SignalState,
                                     signal_error);
                linux_exit_child(127);
            }
            if (child_working_dir[0] != '\0' &&
                linux_chdir(child_working_dir) != 0) {
                report_child_failure(status_write_fd.get(),
                                     ChildFailureStage::WorkingDirectory, errno);
                linux_exit_child(127);
            }

            if (!inherit_output) {
                if (!dup2_nointr(null_fd.get(), STDOUT_FILENO) ||
                    !dup2_nointr(null_fd.get(), STDERR_FILENO)) {
                    report_child_failure(status_write_fd.get(),
                                         ChildFailureStage::Output, errno);
                    linux_exit_child(127);
                }
                (void)linux_close(null_fd.get());
            }

            int capability_error = 0;
            if (!apply_child_capabilities(capabilities, capability_error)) {
                report_child_failure(status_write_fd.get(),
                                     ChildFailureStage::Capability,
                                     capability_error);
                linux_exit_child(127);
            }
            int exec_error = ENOENT;
            bool saw_access_denied = false;
            for (std::size_t candidate_index = 0;
                 candidate_index < child_exec_candidate_count;
                 ++candidate_index) {
                const char *const candidate =
                    child_exec_candidates[candidate_index];
                linux_execve(candidate, child_argv, child_envp);
                exec_error = errno;
                if (exec_error == ENOEXEC) {
                    child_shell_argv[1] = const_cast<char *>(candidate);
                    linux_execve("/bin/sh", child_shell_argv, child_envp);
                    exec_error = errno;
                    break;
                }
                if (exec_error == EACCES) {
                    saw_access_denied = true;
                    continue;
                }
                if (exec_error != ENOENT && exec_error != ENOTDIR &&
                    exec_error != ESTALE && exec_error != ENODEV &&
                    exec_error != ETIMEDOUT) {
                    break;
                }
            }
            if (saw_access_denied &&
                (exec_error == ENOENT || exec_error == ENOTDIR ||
                 exec_error == ESTALE || exec_error == ENODEV ||
                 exec_error == ETIMEDOUT)) {
                exec_error = EACCES;
            }
            report_child_failure(status_write_fd.get(), ChildFailureStage::Exec,
                                 exec_error);
            linux_exit_child(127);
        }

        status_write_fd.reset();
        null_fd.reset();
        int duplicate_result;
        int duplicate_error = 0;
        do {
            duplicate_result =
                linux_dup3(clone_pidfd, pending_pidfd_slot.get(), O_CLOEXEC);
            duplicate_error = duplicate_result < 0 ? errno : 0;
        } while (duplicate_result < 0 && duplicate_error == EINTR &&
                 !operation_control_failure(control, diagnostic));
        if (duplicate_result < 0) {
            pending_pidfd_slot.reset();
        }
        ChildRollbackGuard rollback(
            pid, ScopedFd(clone_pidfd), std::move(pending_pidfd_slot),
            std::move(rollback_completion),
            std::move(reaper_reservation), pending_cleanup_pid_,
            pending_cleanup_pidfd_, pending_cleanup_completion_);
        if (!parent_mask_restored) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::SpawnFailed,
                std::move(parent_mask_diagnostic));
        }
        if (clone_pidfd < 0) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::IdentityUnavailable,
                "clone3 did not return the required pidfd");
        }
        if (!rollback.normalize_pidfd(diagnostic)) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::IdentityUnavailable,
                std::move(diagnostic));
        }
        if (duplicate_result < 0) {
            if (const auto failure_status =
                    operation_control_failure(control, diagnostic)) {
                return failed<ProcessContainmentStartResult>(
                    *failure_status, std::move(diagnostic));
            }
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::IdentityUnavailable,
                errno_diagnostic("cannot duplicate rollback pidfd",
                                 duplicate_error));
        }
        const int direct_pidfd = rollback.pidfd();

        ChildFailure child_failure{};
        const ExecHandshakeOutcome handshake = read_exec_handshake(
            status_read_fd.get(), child_failure, control, diagnostic);
        status_read_fd.reset();
        if (handshake != ExecHandshakeOutcome::Executed) {
            if (diagnostic.empty()) {
                diagnostic = errno_diagnostic(
                    "contained child setup failed", child_failure.error);
            }
            ProcessContainmentStatus status = ProcessContainmentStatus::SpawnFailed;
            if (handshake == ExecHandshakeOutcome::Cancelled) {
                status = ProcessContainmentStatus::Cancelled;
            } else if (handshake == ExecHandshakeOutcome::TimedOut) {
                status = ProcessContainmentStatus::TimedOut;
            } else if (handshake == ExecHandshakeOutcome::ChildFailure &&
                       child_failure.stage ==
                           static_cast<std::uint32_t>(ChildFailureStage::Exec)) {
                status = ProcessContainmentStatus::ExecFailed;
            }
            return failed<ProcessContainmentStartResult>(status,
                                                         std::move(diagnostic));
        }

        ProcessBirthIdentity direct_identity;
        if (!read_process_identity(pid, identity_.boot_id, direct_identity,
                                   diagnostic) ||
            pidfd_state(direct_pidfd, diagnostic) != PidfdState::Alive) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::IdentityUnavailable,
                std::move(diagnostic));
        }
        std::vector<int> before;
        std::vector<int> after;
        if (!read_members(procs_read_fd_.get(), before, diagnostic) ||
            !std::binary_search(before.begin(), before.end(), pid) ||
            !read_members(procs_read_fd_.get(), after, diagnostic) ||
            before != after) {
            if (diagnostic.empty()) {
                diagnostic = "direct child membership was not stable after exec";
            }
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::MembershipChanged,
                std::move(diagnostic));
        }
        ProcessBirthIdentity confirmation;
        if (!read_process_identity(pid, identity_.boot_id, confirmation,
                                   diagnostic) ||
            confirmation != direct_identity ||
            pidfd_state(direct_pidfd, diagnostic) != PidfdState::Alive) {
            if (diagnostic.empty()) {
                diagnostic = "direct child identity changed after exec";
            }
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::IdentityChanged,
                std::move(diagnostic));
        }
        std::vector<int> final_members;
        if (!read_members(procs_read_fd_.get(), final_members, diagnostic) ||
            final_members != after ||
            !std::binary_search(final_members.begin(), final_members.end(), pid) ||
            pidfd_state(direct_pidfd, diagnostic) != PidfdState::Alive) {
            if (diagnostic.empty()) {
                diagnostic =
                    "direct child membership changed before start commit";
            }
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::MembershipChanged,
                std::move(diagnostic));
        }
        if (!revalidate_scope_identity(diagnostic)) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::ScopeChanged,
                std::move(diagnostic));
        }
        if (const auto failure_status =
                operation_control_failure(control, diagnostic)) {
            return failed<ProcessContainmentStartResult>(
                *failure_status, std::move(diagnostic));
        }

        std::vector<int> commit_members;
        ProcessBirthIdentity commit_identity;
        if (!read_members(procs_read_fd_.get(), commit_members, diagnostic) ||
            commit_members != final_members ||
            !std::binary_search(commit_members.begin(), commit_members.end(),
                                pid)) {
            if (diagnostic.empty()) {
                diagnostic =
                    "direct child changed during final start confirmation";
            }
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::MembershipChanged,
                std::move(diagnostic));
        }
        if (!read_process_identity(pid, identity_.boot_id, commit_identity,
                                   diagnostic) ||
            commit_identity != direct_identity ||
            pidfd_state(direct_pidfd, diagnostic) != PidfdState::Alive) {
            if (diagnostic.empty()) {
                diagnostic =
                    "direct child identity changed before start commit";
            }
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::IdentityChanged,
                std::move(diagnostic));
        }
        if (!revalidate_scope_identity(diagnostic)) {
            return failed<ProcessContainmentStartResult>(
                ProcessContainmentStatus::ScopeChanged,
                std::move(diagnostic));
        }
        if (const auto failure_status =
                operation_control_failure(control, diagnostic)) {
            return failed<ProcessContainmentStartResult>(
                *failure_status, std::move(diagnostic));
        }

        ProcessContainmentStartResult result;
        result.status = ProcessContainmentStatus::Success;
        result.process = ProcessHandle{nullptr, pid};
        result.direct_child_identity = direct_identity;
        if (inherit_output) {
            LOG(INFO, "ProcessManager")
                << "Contained process started successfully, PID: " << pid
                << std::endl;
        }
        direct_identity_ = direct_identity;
        direct_pidfd_ = rollback.commit();
        started_ = true;
        return result;
    }

    ProcessContainmentSnapshotResult
    snapshot(const ProcessContainmentOperationControl &control) override {
        std::string diagnostic;
        std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
        if (const auto failure =
                acquire_operation_lock(control, lock, diagnostic)) {
            return failed<ProcessContainmentSnapshotResult>(
                *failure, std::move(diagnostic));
        }
        if (!active_.load()) {
            return failed<ProcessContainmentSnapshotResult>(
                ProcessContainmentStatus::Closed, "process containment is closed");
        }
        if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
            return failed<ProcessContainmentSnapshotResult>(
                ProcessContainmentStatus::Failed,
                "process containment snapshot generation overflowed");
        }
        const std::uint64_t snapshot_generation = ++generation_;
        if (!revalidate_scope(diagnostic)) {
            return failed<ProcessContainmentSnapshotResult>(
                ProcessContainmentStatus::ScopeChanged, std::move(diagnostic));
        }

        std::vector<int> before;
        if (!read_members(procs_read_fd_.get(), before, diagnostic)) {
            return failed<ProcessContainmentSnapshotResult>(
                ProcessContainmentStatus::MembershipChanged, std::move(diagnostic));
        }

        std::vector<CapturedMember> captured;
        captured.reserve(before.size());
        for (const int pid : before) {
            if (const auto failure =
                    operation_control_failure(control, diagnostic)) {
                return failed<ProcessContainmentSnapshotResult>(
                    *failure, std::move(diagnostic));
            }
            CapturedMember member;
            member.pidfd = open_pidfd(pid, diagnostic);
            if (!member.pidfd ||
                !read_process_identity(pid, identity_.boot_id, member.identity,
                                       diagnostic) ||
                pidfd_state(member.pidfd.get(), diagnostic) !=
                    PidfdState::Alive) {
                return failed<ProcessContainmentSnapshotResult>(
                    ProcessContainmentStatus::IdentityUnavailable,
                    std::move(diagnostic));
            }
            captured.push_back(std::move(member));
        }

        std::vector<int> after;
        if (!read_members(procs_read_fd_.get(), after, diagnostic) || before != after) {
            if (diagnostic.empty()) {
                diagnostic = "cgroup membership changed during snapshot";
            }
            return failed<ProcessContainmentSnapshotResult>(
                ProcessContainmentStatus::MembershipChanged, std::move(diagnostic));
        }

        std::vector<ProcessBirthIdentity> identities;
        identities.reserve(captured.size());
        for (auto &member : captured) {
            if (const auto failure =
                    operation_control_failure(control, diagnostic)) {
                return failed<ProcessContainmentSnapshotResult>(
                    *failure, std::move(diagnostic));
            }
            ProcessBirthIdentity confirmation;
            if (!read_process_identity(member.identity.pid, identity_.boot_id,
                                       confirmation, diagnostic) ||
                confirmation != member.identity ||
                pidfd_state(member.pidfd.get(), diagnostic) !=
                    PidfdState::Alive) {
                if (diagnostic.empty()) {
                    diagnostic = "process identity changed during snapshot";
                }
                return failed<ProcessContainmentSnapshotResult>(
                    ProcessContainmentStatus::IdentityChanged,
                    std::move(diagnostic));
            }
            identities.push_back(std::move(member.identity));
        }

        std::vector<int> final_members;
        if (!read_members(procs_read_fd_.get(), final_members, diagnostic) ||
            final_members != after) {
            if (diagnostic.empty()) {
                diagnostic =
                    "cgroup membership changed after identity confirmation";
            }
            return failed<ProcessContainmentSnapshotResult>(
                ProcessContainmentStatus::MembershipChanged,
                std::move(diagnostic));
        }
        for (const auto &member : captured) {
            if (const auto failure =
                    operation_control_failure(control, diagnostic)) {
                return failed<ProcessContainmentSnapshotResult>(
                    *failure, std::move(diagnostic));
            }
            if (pidfd_state(member.pidfd.get(), diagnostic) !=
                PidfdState::Alive) {
                if (diagnostic.empty()) {
                    diagnostic =
                        "process exited after final membership confirmation";
                }
                return failed<ProcessContainmentSnapshotResult>(
                    ProcessContainmentStatus::IdentityChanged,
                    std::move(diagnostic));
            }
        }
        ProcessContainmentStatus direct_status =
            ProcessContainmentStatus::Success;
        if (!validate_live_direct_membership(final_members, direct_status,
                                             diagnostic)) {
            return failed<ProcessContainmentSnapshotResult>(
                direct_status, std::move(diagnostic));
        }

        if (!revalidate_scope(diagnostic)) {
            return failed<ProcessContainmentSnapshotResult>(
                ProcessContainmentStatus::ScopeChanged, std::move(diagnostic));
        }
        if (const auto failure =
                operation_control_failure(control, diagnostic)) {
            return failed<ProcessContainmentSnapshotResult>(
                *failure, std::move(diagnostic));
        }

        ProcessContainmentSnapshotResult result;
        result.status = ProcessContainmentStatus::Success;
        result.snapshot = ProcessContainmentSnapshot{
            identity_, snapshot_generation, std::move(identities)};
        return result;
    }

    ProcessContainmentOperationResult
    kill(std::chrono::milliseconds timeout) override {
        using Clock = std::chrono::steady_clock;
        const auto started = linux_now();
        if (timeout <= std::chrono::milliseconds::zero() ||
            timeout >
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    kMaxKillTimeout)) {
            return failed<ProcessContainmentOperationResult>(
                ProcessContainmentStatus::InvalidRequest,
                "kill timeout must be positive and no greater than 30 seconds");
        }
        const auto timeout_duration =
            std::chrono::duration_cast<Clock::duration>(timeout);
        if (timeout_duration > Clock::time_point::max() - started) {
            return failed<ProcessContainmentOperationResult>(
                ProcessContainmentStatus::InvalidRequest,
                "kill timeout cannot be represented by the monotonic clock");
        }
        const auto deadline = started + timeout_duration;
        std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
        if (!lock.try_lock_until(deadline) || linux_now() >= deadline) {
            return failed<ProcessContainmentOperationResult>(
                ProcessContainmentStatus::TimedOut,
                "kill timed out waiting for containment state access");
        }
        if (!active_.load()) {
            return failed<ProcessContainmentOperationResult>(
                ProcessContainmentStatus::Closed, "process containment is closed");
        }
        std::string diagnostic;
        std::optional<ProcessContainmentStatus> cleanup_anomaly;
        std::string cleanup_anomaly_diagnostic;
        const auto record_cleanup_anomaly =
            [&](ProcessContainmentStatus status, std::string message) {
                if (!cleanup_anomaly) {
                    cleanup_anomaly = status;
                    cleanup_anomaly_diagnostic = bounded(std::move(message));
                }
            };
        const auto validate_direct_for_cleanup =
            [&](const std::vector<int> &observed_members) {
                ProcessContainmentStatus status =
                    ProcessContainmentStatus::Success;
                std::string validation_diagnostic;
                if (!validate_live_direct_membership(
                        observed_members, status, validation_diagnostic,
                        deadline)) {
                    record_cleanup_anomaly(status,
                                           std::move(validation_diagnostic));
                }
            };
        const auto signal_direct_for_cleanup = [&]() {
            if (!direct_pidfd_) {
                return true;
            }
            if (linux_now() >= deadline) {
                return false;
            }
            std::string state_diagnostic;
            const PidfdState state =
                pidfd_state(direct_pidfd_.get(), state_diagnostic, deadline);
            if (state == PidfdState::Exited) {
                return true;
            }
            if (state == PidfdState::Failed) {
                record_cleanup_anomaly(
                    ProcessContainmentStatus::IdentityUnavailable,
                    std::move(state_diagnostic));
            }
            if (linux_now() >= deadline) {
                return false;
            }
            int signal_error = 0;
            if (!signal_pidfd(direct_pidfd_.get(), signal_error)) {
                record_cleanup_anomaly(
                    ProcessContainmentStatus::Failed,
                    errno_diagnostic("direct-child pidfd cleanup signal failed",
                                     signal_error));
            }
            return false;
        };
        const auto service_pending_cleanup = [&]() {
            if (pending_cleanup_pid_ <= 0) {
                return true;
            }
            if (linux_now() >= deadline) {
                return false;
            }
            if (resolve_pending_cleanup()) {
                return true;
            }
            if (!pending_cleanup_pidfd_) {
                return false;
            }
            std::string state_diagnostic;
            const PidfdState state =
                pidfd_state(pending_cleanup_pidfd_.get(), state_diagnostic,
                            deadline);
            if (state == PidfdState::Exited) {
                return resolve_pending_cleanup();
            }
            if (state == PidfdState::Failed) {
                record_cleanup_anomaly(
                    ProcessContainmentStatus::IdentityUnavailable,
                    std::move(state_diagnostic));
            }
            if (linux_now() >= deadline) {
                return false;
            }
            int signal_error = 0;
            if (!signal_pidfd(pending_cleanup_pidfd_.get(), signal_error)) {
                record_cleanup_anomaly(
                    ProcessContainmentStatus::Failed,
                    errno_diagnostic(
                        "failed-child pidfd cleanup signal failed",
                        signal_error));
            }
            return false;
        };
        if (!revalidate_retained_scope_identity(diagnostic)) {
            record_cleanup_anomaly(ProcessContainmentStatus::ScopeChanged,
                                   std::move(diagnostic));
            diagnostic.clear();
        }
        std::string name_diagnostic;
        if (!revalidate_scope_name_binding(name_diagnostic)) {
            record_cleanup_anomaly(ProcessContainmentStatus::ScopeChanged,
                                   std::move(name_diagnostic));
        }
        if (linux_now() >= deadline) {
            return failed<ProcessContainmentOperationResult>(
                ProcessContainmentStatus::TimedOut,
                "kill deadline expired while validating containment identity");
        }
        std::vector<int> members;
        if (!read_members_until(procs_read_fd_.get(), members, deadline,
                                diagnostic)) {
            if (linux_now() >= deadline) {
                return failed<ProcessContainmentOperationResult>(
                    ProcessContainmentStatus::TimedOut,
                    std::move(diagnostic));
            }
            record_cleanup_anomaly(ProcessContainmentStatus::MembershipChanged,
                                   std::move(diagnostic));
            members.clear();
        }
        validate_direct_for_cleanup(members);
        if (linux_now() >= deadline) {
            return failed<ProcessContainmentOperationResult>(
                ProcessContainmentStatus::TimedOut,
                "kill deadline expired before cleanup began");
        }
        (void)signal_direct_for_cleanup();
        (void)service_pending_cleanup();
        if (linux_now() >= deadline) {
            return failed<ProcessContainmentOperationResult>(
                ProcessContainmentStatus::TimedOut,
                "kill deadline expired before cleanup began");
        }
        if (!write_single_control_until(kill_fd_.get(), "1", deadline,
                                        diagnostic)) {
            return failed<ProcessContainmentOperationResult>(
                linux_now() >= deadline ? ProcessContainmentStatus::TimedOut
                                         : ProcessContainmentStatus::Failed,
                std::move(diagnostic));
        }

        while (linux_now() < deadline) {
            std::string events;
            bool populated = true;
            if (!read_bounded_fd_until(events_fd_.get(), kMaxControlBytes,
                                       deadline, events, diagnostic) ||
                !parse_populated(events, populated, diagnostic)) {
                return failed<ProcessContainmentOperationResult>(
                    linux_now() >= deadline
                        ? ProcessContainmentStatus::TimedOut
                        : ProcessContainmentStatus::Failed,
                    std::move(diagnostic));
            }
            std::vector<int> observed_members;
            if (!read_members_until(procs_read_fd_.get(), observed_members,
                                    deadline, diagnostic)) {
                return failed<ProcessContainmentOperationResult>(
                    linux_now() >= deadline
                        ? ProcessContainmentStatus::TimedOut
                        : ProcessContainmentStatus::MembershipChanged,
                    std::move(diagnostic));
            }
            validate_direct_for_cleanup(observed_members);
            const bool direct_resolved = signal_direct_for_cleanup();
            const bool pending_resolved = service_pending_cleanup();

            if (!populated && observed_members.empty() && direct_resolved &&
                pending_resolved) {
                if (!revalidate_retained_scope_identity(diagnostic)) {
                    record_cleanup_anomaly(
                        ProcessContainmentStatus::ScopeChanged,
                        std::move(diagnostic));
                    diagnostic.clear();
                }
                std::string confirmation_name_diagnostic;
                if (!revalidate_scope_name_binding(
                        confirmation_name_diagnostic)) {
                    record_cleanup_anomaly(
                        ProcessContainmentStatus::ScopeChanged,
                        std::move(confirmation_name_diagnostic));
                }
                if (linux_now() >= deadline) {
                    break;
                }
                std::string confirmation_events;
                bool confirmation_populated = true;
                std::vector<int> confirmation_members;
                if (!read_bounded_fd_until(
                        events_fd_.get(), kMaxControlBytes, deadline,
                        confirmation_events, diagnostic) ||
                    !parse_populated(confirmation_events,
                                     confirmation_populated, diagnostic) ||
                    !read_members_until(procs_read_fd_.get(),
                                        confirmation_members, deadline,
                                        diagnostic)) {
                    return failed<ProcessContainmentOperationResult>(
                        linux_now() >= deadline
                            ? ProcessContainmentStatus::TimedOut
                            : ProcessContainmentStatus::Failed,
                        std::move(diagnostic));
                }
                validate_direct_for_cleanup(confirmation_members);
                const bool confirmation_direct_resolved =
                    signal_direct_for_cleanup();
                const bool confirmation_pending_resolved =
                    service_pending_cleanup();
                if (!confirmation_populated && confirmation_members.empty() &&
                    confirmation_direct_resolved &&
                    confirmation_pending_resolved && linux_now() < deadline) {
                    if (cleanup_anomaly) {
                        return failed<ProcessContainmentOperationResult>(
                            *cleanup_anomaly,
                            std::move(cleanup_anomaly_diagnostic));
                    }
                    return ProcessContainmentOperationResult{
                        ProcessContainmentStatus::Success, {}};
                }
            }

            if (linux_now() >= deadline) {
                break;
            }
            if (!write_single_control_until(kill_fd_.get(), "1", deadline,
                                            diagnostic)) {
                return failed<ProcessContainmentOperationResult>(
                    linux_now() >= deadline
                        ? ProcessContainmentStatus::TimedOut
                        : ProcessContainmentStatus::Failed,
                    std::move(diagnostic));
            }
            const auto next_poll =
                std::min(deadline, linux_now() + std::chrono::milliseconds(10));
            linux_sleep_until(next_poll);
        }
        return failed<ProcessContainmentOperationResult>(
            ProcessContainmentStatus::TimedOut,
            "cgroup remained populated through the kill deadline");
    }

    ProcessContainmentOperationResult
    release(const ProcessContainmentOperationControl &control) override {
        std::string diagnostic;
        std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
        if (const auto failure =
                acquire_operation_lock(control, lock, diagnostic)) {
            return failed<ProcessContainmentOperationResult>(
                *failure, std::move(diagnostic));
        }
        if (!active_.load()) {
            return failed<ProcessContainmentOperationResult>(
                ProcessContainmentStatus::Closed, "process containment is closed");
        }
        if (!resolve_pending_cleanup()) {
            return failed<ProcessContainmentOperationResult>(
                ProcessContainmentStatus::StillPopulated,
                "a failed child is still awaiting contained cleanup");
        }
        if (const auto failure =
                operation_control_failure(control, diagnostic)) {
            return failed<ProcessContainmentOperationResult>(
                *failure, std::move(diagnostic));
        }
        if (!revalidate_scope(diagnostic)) {
            return failed<ProcessContainmentOperationResult>(
                ProcessContainmentStatus::ScopeChanged, std::move(diagnostic));
        }
        std::vector<int> members;
        if (!read_members(procs_read_fd_.get(), members, diagnostic)) {
            return failed<ProcessContainmentOperationResult>(
                ProcessContainmentStatus::MembershipChanged,
                std::move(diagnostic));
        }
        if (const auto failure =
                operation_control_failure(control, diagnostic)) {
            return failed<ProcessContainmentOperationResult>(
                *failure, std::move(diagnostic));
        }
        ProcessContainmentStatus direct_status =
            ProcessContainmentStatus::Success;
        if (!validate_live_direct_membership(members, direct_status,
                                             diagnostic)) {
            return failed<ProcessContainmentOperationResult>(
                direct_status, std::move(diagnostic));
        }
        if (!members.empty()) {
            return failed<ProcessContainmentOperationResult>(
                ProcessContainmentStatus::ScopeNotEmpty,
                "cgroup must be empty before release");
        }
        std::string events;
        bool populated = true;
        if (!read_bounded_fd(events_fd_.get(), kMaxControlBytes, events,
                             diagnostic) ||
            !parse_populated(events, populated, diagnostic)) {
            return failed<ProcessContainmentOperationResult>(
                ProcessContainmentStatus::Failed, std::move(diagnostic));
        }
        if (populated) {
            return failed<ProcessContainmentOperationResult>(
                ProcessContainmentStatus::ScopeNotEmpty,
                "cgroup reports populated before release");
        }
        if (const auto failure =
                operation_control_failure(control, diagnostic)) {
            return failed<ProcessContainmentOperationResult>(
                *failure, std::move(diagnostic));
        }
        if (!revalidate_scope(diagnostic)) {
            return failed<ProcessContainmentOperationResult>(
                ProcessContainmentStatus::ScopeChanged,
                std::move(diagnostic));
        }
        std::vector<int> confirmation_members;
        if (!read_members(procs_read_fd_.get(), confirmation_members,
                          diagnostic)) {
            return failed<ProcessContainmentOperationResult>(
                ProcessContainmentStatus::MembershipChanged,
                std::move(diagnostic));
        }
        if (!confirmation_members.empty()) {
            return failed<ProcessContainmentOperationResult>(
                ProcessContainmentStatus::ScopeNotEmpty,
                "cgroup became populated before release");
        }
        if (!validate_live_direct_membership(
                confirmation_members, direct_status, diagnostic)) {
            return failed<ProcessContainmentOperationResult>(
                direct_status, std::move(diagnostic));
        }
        std::string confirmation_events;
        bool confirmation_populated = true;
        if (!read_bounded_fd(events_fd_.get(), kMaxControlBytes,
                             confirmation_events, diagnostic) ||
            !parse_populated(confirmation_events, confirmation_populated,
                             diagnostic)) {
            return failed<ProcessContainmentOperationResult>(
                ProcessContainmentStatus::Failed, std::move(diagnostic));
        }
        if (confirmation_populated) {
            return failed<ProcessContainmentOperationResult>(
                ProcessContainmentStatus::ScopeNotEmpty,
                "cgroup became populated before release");
        }
        if (!revalidate_scope_identity(diagnostic)) {
            return failed<ProcessContainmentOperationResult>(
                ProcessContainmentStatus::ScopeChanged,
                std::move(diagnostic));
        }
        if (const auto failure =
                operation_control_failure(control, diagnostic)) {
            return failed<ProcessContainmentOperationResult>(
                *failure, std::move(diagnostic));
        }
        if (linux_unlinkat(root_fd_.get(), leaf_name_.c_str(), AT_REMOVEDIR) !=
            0) {
            const int error = errno;
            return failed<ProcessContainmentOperationResult>(
                error == EBUSY || error == ENOTEMPTY
                    ? ProcessContainmentStatus::ScopeNotEmpty
                    : ProcessContainmentStatus::Failed,
                errno_diagnostic("cgroup release failed", error));
        }
        active_.store(false);
        direct_pidfd_.reset();
        direct_identity_.reset();
        pending_cleanup_pid_ = -1;
        pending_cleanup_pidfd_.reset();
        pending_cleanup_completion_.reset();
        events_fd_.reset();
        kill_fd_.reset();
        procs_write_fd_.reset();
        procs_read_fd_.reset();
        scope_fd_.reset();
        root_fd_.reset();
        return ProcessContainmentOperationResult{ProcessContainmentStatus::Success,
                                                 {}};
    }

private:
    bool resolve_pending_cleanup() noexcept {
        if (pending_cleanup_pid_ <= 0) {
            return true;
        }
        if (pending_cleanup_completion_ &&
            pending_cleanup_completion_->reaped.load(
                std::memory_order_acquire)) {
            pending_cleanup_pid_ = -1;
            pending_cleanup_pidfd_.reset();
            pending_cleanup_completion_.reset();
            return true;
        }
        if (!pending_cleanup_pidfd_) {
            return false;
        }
        if (reap_pidfd_once(pending_cleanup_pidfd_.get()) ==
            PidfdReapState::Reaped) {
            if (pending_cleanup_completion_) {
                pending_cleanup_completion_->reaped.store(
                    true, std::memory_order_release);
            }
            pending_cleanup_pid_ = -1;
            pending_cleanup_pidfd_.reset();
            pending_cleanup_completion_.reset();
            return true;
        }
        return false;
    }

    bool validate_live_direct_membership(
        const std::vector<int> &members, ProcessContainmentStatus &status,
        std::string &diagnostic,
        std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::time_point::max()) const {
        if (!direct_identity_ || !direct_pidfd_) {
            return true;
        }
        const PidfdState state =
            pidfd_state(direct_pidfd_.get(), diagnostic, deadline);
        if (state == PidfdState::Exited) {
            return true;
        }
        if (state == PidfdState::Failed) {
            status = ProcessContainmentStatus::IdentityUnavailable;
            return false;
        }
        ProcessBirthIdentity current;
        if (!read_process_identity(direct_identity_->pid, identity_.boot_id,
                                   current, diagnostic)) {
            std::string state_diagnostic;
            const PidfdState confirmation =
                pidfd_state(direct_pidfd_.get(), state_diagnostic, deadline);
            if (confirmation == PidfdState::Exited) {
                diagnostic.clear();
                return true;
            }
            if (confirmation == PidfdState::Failed) {
                diagnostic = std::move(state_diagnostic);
            }
            status = ProcessContainmentStatus::IdentityUnavailable;
            return false;
        }
        if (current != *direct_identity_) {
            std::string state_diagnostic;
            const PidfdState confirmation =
                pidfd_state(direct_pidfd_.get(), state_diagnostic, deadline);
            if (confirmation == PidfdState::Exited) {
                diagnostic.clear();
                return true;
            }
            if (confirmation == PidfdState::Failed) {
                diagnostic = std::move(state_diagnostic);
                status = ProcessContainmentStatus::IdentityUnavailable;
                return false;
            }
            diagnostic = "direct child birth identity changed";
            status = ProcessContainmentStatus::IdentityChanged;
            return false;
        }
        if (!std::binary_search(members.begin(), members.end(),
                                direct_identity_->pid)) {
            std::string state_diagnostic;
            const PidfdState confirmation =
                pidfd_state(direct_pidfd_.get(), state_diagnostic, deadline);
            if (confirmation == PidfdState::Exited) {
                diagnostic.clear();
                return true;
            }
            if (confirmation == PidfdState::Failed) {
                diagnostic = std::move(state_diagnostic);
                status = ProcessContainmentStatus::IdentityUnavailable;
                return false;
            }
            diagnostic = "live direct child escaped the retained cgroup scope";
            status = ProcessContainmentStatus::MembershipChanged;
            return false;
        }
        return true;
    }

    bool revalidate_retained_scope_identity(std::string &diagnostic) const {
        std::string boot_id;
        if (!read_boot_id(boot_id, diagnostic) || boot_id != identity_.boot_id ||
            !verify_cgroup2(scope_fd_.get(), diagnostic)) {
            if (diagnostic.empty()) {
                diagnostic = "cgroup boot or filesystem identity changed";
            }
            return false;
        }
        std::uint64_t mount_id = 0;
        std::uint64_t device = 0;
        std::uint64_t inode = 0;
        if (!statx_identity(scope_fd_.get(), mount_id, device, inode, diagnostic) ||
            mount_id != identity_.mount_id || device != identity_.device ||
            inode != identity_.inode) {
            if (diagnostic.empty()) {
                diagnostic = "retained cgroup identity changed";
            }
            return false;
        }
        return true;
    }

    bool revalidate_scope_name_binding(std::string &diagnostic) const {
        struct stat named {};
        if (linux_fstatat(root_fd_.get(), leaf_name_.c_str(), &named,
                          AT_SYMLINK_NOFOLLOW) != 0 ||
            !S_ISDIR(named.st_mode) ||
            static_cast<std::uint64_t>(named.st_dev) != identity_.device ||
            static_cast<std::uint64_t>(named.st_ino) != identity_.inode) {
            if (diagnostic.empty()) {
                diagnostic = "cgroup leaf no longer resolves to the retained scope";
            }
            return false;
        }
        return true;
    }

    bool revalidate_scope_identity(std::string &diagnostic) const {
        return revalidate_retained_scope_identity(diagnostic) &&
               revalidate_scope_name_binding(diagnostic);
    }

    bool revalidate_scope(std::string &diagnostic) const {
        return revalidate_scope_identity(diagnostic) &&
               validate_domain_controls(scope_fd_.get(), false, diagnostic);
    }

    ProcessContainmentIdentity identity_;
    std::string leaf_name_;
    ScopedFd root_fd_;
    ScopedFd scope_fd_;
    ScopedFd procs_read_fd_;
    ScopedFd procs_write_fd_;
    ScopedFd kill_fd_;
    ScopedFd events_fd_;
    ScopedFd direct_pidfd_;
    std::optional<ProcessBirthIdentity> direct_identity_;
    pid_t pending_cleanup_pid_ = -1;
    ScopedFd pending_cleanup_pidfd_;
    std::shared_ptr<RollbackCompletion> pending_cleanup_completion_;
    std::atomic<bool> active_{true};
    bool started_ = false;
    std::uint64_t generation_ = 0;
    mutable std::timed_mutex mutex_;
};

bool validate_request(const ProcessContainmentRequest &request,
                      std::string &diagnostic) {
    const std::string root = request.delegated_root.native();
    if (root.empty() || root.size() > PATH_MAX ||
        !request.delegated_root.is_absolute() || has_nul(root) ||
        has_control(root)) {
        diagnostic = "delegated cgroup root must be an absolute control-free path";
        return false;
    }
    for (const auto &component : request.delegated_root) {
        if (component == "." || component == "..") {
            diagnostic = "delegated cgroup root must not contain dot components";
            return false;
        }
    }
    if (request.owner_scope_id.empty() ||
        request.owner_scope_id.size() > kMaxOwnerScopeBytes ||
        has_nul(request.owner_scope_id) || has_control(request.owner_scope_id)) {
        diagnostic = "owner scope id is empty, oversized, or contains controls";
        return false;
    }
    if (!valid_nonce(request.nonce_sha256)) {
        diagnostic = "nonce must be exactly 64 lowercase hexadecimal characters";
        return false;
    }
    return true;
}

} // namespace

ProcessContainmentPlatformPrepareResult
prepare_process_containment_platform(
    const ProcessContainmentRequest &request,
    const ProcessContainmentOperationControl &control) {
    std::string diagnostic;
    if (const auto failure = operation_control_failure(control, diagnostic)) {
        return failed<ProcessContainmentPlatformPrepareResult>(
            *failure, std::move(diagnostic));
    }
    if (!validate_request(request, diagnostic)) {
        return failed<ProcessContainmentPlatformPrepareResult>(
            ProcessContainmentStatus::InvalidRequest, std::move(diagnostic));
    }

    int raw_root_fd;
    do {
        raw_root_fd = linux_open(
            request.delegated_root.c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    } while (raw_root_fd < 0 && errno == EINTR);
    ScopedFd root_fd(raw_root_fd);
    if (!root_fd) {
        return failed<ProcessContainmentPlatformPrepareResult>(
            ProcessContainmentStatus::InvalidDelegation,
            errno_diagnostic("cannot open delegated cgroup root", errno));
    }
    if (!normalize_internal_fd(root_fd, diagnostic)) {
        return failed<ProcessContainmentPlatformPrepareResult>(
            ProcessContainmentStatus::InvalidDelegation,
            std::move(diagnostic));
    }
    if (!verify_cgroup2(root_fd.get(), diagnostic)) {
        return failed<ProcessContainmentPlatformPrepareResult>(
            ProcessContainmentStatus::InvalidDelegation, std::move(diagnostic));
    }
    if (const auto failure = operation_control_failure(control, diagnostic)) {
        return failed<ProcessContainmentPlatformPrepareResult>(
            *failure, std::move(diagnostic));
    }

    const std::string leaf_name =
        std::string(kLeafPrefix) + request.nonce_sha256;
    int cleanup_root_descriptor;
    do {
        cleanup_root_descriptor = linux_duplicate(root_fd.get(), 3);
    } while (cleanup_root_descriptor < 0 && errno == EINTR);
    ScopedFd cleanup_root_fd(cleanup_root_descriptor);
    if (!cleanup_root_fd) {
        return failed<ProcessContainmentPlatformPrepareResult>(
            ProcessContainmentStatus::InvalidDelegation,
            errno_diagnostic("cannot retain containment cleanup root", errno));
    }
    if (const auto failure = operation_control_failure(control, diagnostic)) {
        return failed<ProcessContainmentPlatformPrepareResult>(
            *failure, std::move(diagnostic));
    }
    if (linux_mkdirat(root_fd.get(), leaf_name.c_str(), 0750) != 0) {
        const int error = errno;
        return failed<ProcessContainmentPlatformPrepareResult>(
            error == EEXIST ? ProcessContainmentStatus::ScopeNotEmpty
                            : ProcessContainmentStatus::InvalidDelegation,
            errno_diagnostic("cannot create unique containment leaf", error));
    }
    LeafCleanupGuard cleanup_leaf(std::move(cleanup_root_fd), leaf_name);
    if (const auto failure = operation_control_failure(control, diagnostic)) {
        return failed<ProcessContainmentPlatformPrepareResult>(
            *failure, std::move(diagnostic));
    }

    int raw_scope_fd;
    do {
        raw_scope_fd = linux_openat(
            root_fd.get(), leaf_name.c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    } while (raw_scope_fd < 0 && errno == EINTR);
    ScopedFd scope_fd(raw_scope_fd);
    if (!scope_fd) {
        auto result = failed<ProcessContainmentPlatformPrepareResult>(
            ProcessContainmentStatus::InvalidDelegation,
            errno_diagnostic("cannot open containment leaf", errno));
        return result;
    }
    if (!normalize_internal_fd(scope_fd, diagnostic)) {
        return failed<ProcessContainmentPlatformPrepareResult>(
            ProcessContainmentStatus::InvalidDelegation,
            std::move(diagnostic));
    }
    ScopedFd max_depth_fd =
        open_control(scope_fd.get(), "cgroup.max.depth", O_WRONLY, diagnostic);
    if (!max_depth_fd ||
        !write_single_control_until(max_depth_fd.get(), "0", control.deadline,
                                    diagnostic)) {
        if (const auto failure = operation_control_failure(control, diagnostic)) {
            max_depth_fd.reset();
            scope_fd.reset();
            return failed<ProcessContainmentPlatformPrepareResult>(
                *failure, std::move(diagnostic));
        }
        auto result = failed<ProcessContainmentPlatformPrepareResult>(
            ProcessContainmentStatus::InvalidDelegation, std::move(diagnostic));
        max_depth_fd.reset();
        scope_fd.reset();
        return result;
    }
    max_depth_fd.reset();
    if (const auto failure = operation_control_failure(control, diagnostic)) {
        return failed<ProcessContainmentPlatformPrepareResult>(
            *failure, std::move(diagnostic));
    }
    if (!verify_cgroup2(scope_fd.get(), diagnostic) ||
        !validate_domain_controls(scope_fd.get(), true, diagnostic)) {
        auto result = failed<ProcessContainmentPlatformPrepareResult>(
            ProcessContainmentStatus::InvalidDelegation, std::move(diagnostic));
        scope_fd.reset();
        return result;
    }
    if (const auto failure = operation_control_failure(control, diagnostic)) {
        return failed<ProcessContainmentPlatformPrepareResult>(
            *failure, std::move(diagnostic));
    }

    std::string boot_id;
    std::uint64_t mount_id = 0;
    std::uint64_t device = 0;
    std::uint64_t inode = 0;
    if (!read_boot_id(boot_id, diagnostic) ||
        !statx_identity(scope_fd.get(), mount_id, device, inode, diagnostic)) {
        auto result = failed<ProcessContainmentPlatformPrepareResult>(
            ProcessContainmentStatus::IdentityUnavailable, std::move(diagnostic));
        scope_fd.reset();
        return result;
    }
    if (const auto failure = operation_control_failure(control, diagnostic)) {
        return failed<ProcessContainmentPlatformPrepareResult>(
            *failure, std::move(diagnostic));
    }

    ScopedFd procs_read_fd =
        open_control(scope_fd.get(), "cgroup.procs", O_RDONLY, diagnostic);
    ScopedFd procs_write_fd =
        open_control(scope_fd.get(), "cgroup.procs", O_WRONLY, diagnostic);
    ScopedFd kill_fd = open_control(scope_fd.get(), "cgroup.kill", O_WRONLY,
                                    diagnostic);
    ScopedFd events_fd = open_control(scope_fd.get(), "cgroup.events", O_RDONLY,
                                      diagnostic);
    if (!procs_read_fd || !procs_write_fd || !kill_fd || !events_fd) {
        auto result = failed<ProcessContainmentPlatformPrepareResult>(
            ProcessContainmentStatus::InvalidDelegation, std::move(diagnostic));
        events_fd.reset();
        kill_fd.reset();
        procs_write_fd.reset();
        procs_read_fd.reset();
        scope_fd.reset();
        return result;
    }
    if (const auto failure = operation_control_failure(control, diagnostic)) {
        return failed<ProcessContainmentPlatformPrepareResult>(
            *failure, std::move(diagnostic));
    }

    ProcessContainmentIdentity identity{
        std::move(boot_id), mount_id, device, inode, request.owner_scope_id,
        request.nonce_sha256};
    std::string state_leaf_name = leaf_name;
    auto state = std::make_unique<LinuxProcessContainmentState>(
        std::move(identity), std::move(state_leaf_name), std::move(root_fd),
        std::move(scope_fd),
        std::move(procs_read_fd), std::move(procs_write_fd), std::move(kill_fd),
        std::move(events_fd));
    if (const auto failure = operation_control_failure(control, diagnostic)) {
        state.reset();
        return failed<ProcessContainmentPlatformPrepareResult>(
            *failure, std::move(diagnostic));
    }
    cleanup_leaf.disarm();

    ProcessContainmentPlatformPrepareResult result;
    result.status = ProcessContainmentStatus::Success;
    result.state = std::move(state);
    return result;
}

} // namespace lemon::utils::internal

#endif
