#include "process_containment_linux_ops.h"

#ifdef __linux__

#include <cerrno>

#include <fcntl.h>
#include <linux/sched.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>

namespace lemon::utils::internal {
namespace {

int close_fd(void *, int fd) noexcept { return ::close(fd); }
int duplicate_fd(void *, int fd, int minimum) noexcept {
    return ::fcntl(fd, F_DUPFD_CLOEXEC, minimum);
}
int duplicate_fd_to(void *, int source, int destination, int flags) noexcept {
    return ::dup3(source, destination, flags);
}
int duplicate_fd_to_legacy(void *, int source, int destination) noexcept {
    return ::dup2(source, destination);
}
off_t seek_fd(void *, int fd, off_t offset, int whence) noexcept {
    return ::lseek(fd, offset, whence);
}
ssize_t read_fd(void *, int fd, void *buffer, std::size_t size) noexcept {
    return ::read(fd, buffer, size);
}
ssize_t write_fd(void *, int fd, const void *buffer, std::size_t size) noexcept {
    return ::write(fd, buffer, size);
}
int open_path(void *, const char *path, int flags) noexcept {
    return ::open(path, flags);
}
int open_at(void *, int directory_fd, const char *path, int flags) noexcept {
    return ::openat(directory_fd, path, flags);
}
int make_directory_at(void *, int directory_fd, const char *path,
                      mode_t mode) noexcept {
    return ::mkdirat(directory_fd, path, mode);
}
int remove_directory_at(void *, int directory_fd, const char *path,
                        int flags) noexcept {
    return ::unlinkat(directory_fd, path, flags);
}
int stat_fd(void *, int fd, struct stat *value) noexcept {
    return ::fstat(fd, value);
}
int stat_at(void *, int directory_fd, const char *path, struct stat *value,
            int flags) noexcept {
    return ::fstatat(directory_fd, path, value, flags);
}
int stat_filesystem(void *, int fd, struct statfs *value) noexcept {
    return ::fstatfs(fd, value);
}
int stat_extended(void *, int fd, const char *path, int flags,
                  unsigned int mask, struct statx *value) noexcept {
    return ::statx(fd, path, flags, mask, value);
}
DIR *open_directory_stream(void *, int fd) noexcept { return ::fdopendir(fd); }
struct dirent *read_directory(void *, DIR *directory) noexcept {
    return ::readdir(directory);
}
int close_directory(void *, DIR *directory) noexcept {
    return ::closedir(directory);
}
int make_pipe(void *, int descriptors[2], int flags) noexcept {
    return ::pipe2(descriptors, flags);
}
int make_event(void *, unsigned int initial_value, int flags) noexcept {
    return ::eventfd(initial_value, flags);
}
int poll_fds(void *, struct pollfd *descriptors, nfds_t count,
             int timeout_ms) noexcept {
    return ::poll(descriptors, count, timeout_ms);
}
int change_directory(void *, const char *path) noexcept { return ::chdir(path); }
int execute(void *, const char *path, char *const arguments[],
            char *const environment[]) noexcept {
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
#ifdef SYS_capget
    return ::syscall(SYS_capget, header, data);
#else
    errno = ENOSYS;
    return -1;
#endif
}
long set_capabilities(void *, const void *header, const void *data) noexcept {
#ifdef SYS_capset
    return ::syscall(SYS_capset, header, data);
#else
    errno = ENOSYS;
    return -1;
#endif
}
long clone_process(void *, struct clone_args *arguments,
                   std::size_t size) noexcept {
#ifdef SYS_clone3
    return ::syscall(SYS_clone3, arguments, size);
#else
    errno = ENOSYS;
    return -1;
#endif
}
int open_pidfd(void *, pid_t pid, unsigned int flags) noexcept {
#ifdef SYS_pidfd_open
    return static_cast<int>(::syscall(SYS_pidfd_open, pid, flags));
#else
    errno = ENOSYS;
    return -1;
#endif
}
int signal_pidfd(void *, int pidfd, int signal_number,
                 const siginfo_t *information, unsigned int flags) noexcept {
#ifdef SYS_pidfd_send_signal
    return static_cast<int>(::syscall(SYS_pidfd_send_signal, pidfd,
                                      signal_number, information, flags));
#else
    errno = ENOSYS;
    return -1;
#endif
}
int wait_pidfd(void *, int pidfd, siginfo_t *information,
               int options) noexcept {
#ifdef SYS_waitid
    constexpr int kPidfdIdType = 3;
    return static_cast<int>(::syscall(SYS_waitid, kPidfdIdType, pidfd,
                                      information, options, nullptr));
#else
    errno = ENOSYS;
    return -1;
#endif
}
std::chrono::steady_clock::time_point steady_now(void *) noexcept {
    return std::chrono::steady_clock::now();
}
void sleep_until(void *,
                 std::chrono::steady_clock::time_point deadline) noexcept {
    std::this_thread::sleep_until(deadline);
}

constexpr ProcessContainmentLinuxOps kProductionOps{
    nullptr,
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
    return kProductionOps;
}

} // namespace lemon::utils::internal

#endif
