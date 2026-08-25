#pragma once

#ifdef __linux__

#include <chrono>
#include <cstddef>
#include <cstdint>

#include <dirent.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/types.h>

struct clone_args;
struct statx;

namespace lemon::utils::internal {

struct ProcessContainmentLinuxOps {
    void *context;

    int (*close_fd)(void *context, int fd) noexcept;
    int (*duplicate_fd)(void *context, int fd, int minimum) noexcept;
    int (*duplicate_fd_to)(void *context, int source, int destination,
                           int flags) noexcept;
    int (*duplicate_fd_to_legacy)(void *context, int source,
                                  int destination) noexcept;
    off_t (*seek_fd)(void *context, int fd, off_t offset,
                     int whence) noexcept;
    ssize_t (*read_fd)(void *context, int fd, void *buffer,
                       std::size_t size) noexcept;
    ssize_t (*write_fd)(void *context, int fd, const void *buffer,
                        std::size_t size) noexcept;

    int (*open_path)(void *context, const char *path, int flags) noexcept;
    int (*open_at)(void *context, int directory_fd, const char *path,
                   int flags) noexcept;
    int (*make_directory_at)(void *context, int directory_fd,
                             const char *path, mode_t mode) noexcept;
    int (*remove_directory_at)(void *context, int directory_fd,
                               const char *path, int flags) noexcept;
    int (*stat_fd)(void *context, int fd, struct stat *value) noexcept;
    int (*stat_at)(void *context, int directory_fd, const char *path,
                   struct stat *value, int flags) noexcept;
    int (*stat_filesystem)(void *context, int fd,
                           struct statfs *value) noexcept;
    int (*stat_extended)(void *context, int fd, const char *path, int flags,
                         unsigned int mask, struct statx *value) noexcept;

    DIR *(*open_directory_stream)(void *context, int fd) noexcept;
    struct dirent *(*read_directory)(void *context, DIR *directory) noexcept;
    int (*close_directory)(void *context, DIR *directory) noexcept;

    int (*make_pipe)(void *context, int descriptors[2], int flags) noexcept;
    int (*make_event)(void *context, unsigned int initial_value,
                      int flags) noexcept;
    int (*poll_fds)(void *context, struct pollfd *descriptors,
                    nfds_t count, int timeout_ms) noexcept;

    int (*change_directory)(void *context, const char *path) noexcept;
    int (*execute)(void *context, const char *path, char *const arguments[],
                   char *const environment[]) noexcept;
    std::size_t (*configuration_string)(void *context, int name, char *buffer,
                                        std::size_t size) noexcept;
    pid_t (*process_id)(void *context) noexcept;
    pid_t (*parent_process_id)(void *context) noexcept;
    int (*signal_action)(void *context, int signal_number,
                         const struct sigaction *action,
                         struct sigaction *previous) noexcept;
    void (*exit_child)(void *context, int status) noexcept;

    long (*get_thread_id)(void *context) noexcept;
    long (*set_signal_mask)(void *context, int operation, const void *set,
                            void *previous, std::size_t size) noexcept;
    long (*set_process_control)(void *context, int operation,
                                unsigned long argument2,
                                unsigned long argument3,
                                unsigned long argument4,
                                unsigned long argument5) noexcept;
    long (*get_capabilities)(void *context, void *header, void *data) noexcept;
    long (*set_capabilities)(void *context, const void *header,
                             const void *data) noexcept;
    long (*clone_process)(void *context, struct clone_args *arguments,
                          std::size_t size) noexcept;
    int (*open_pidfd)(void *context, pid_t pid, unsigned int flags) noexcept;
    int (*signal_pidfd)(void *context, int pidfd, int signal_number,
                        const siginfo_t *information,
                        unsigned int flags) noexcept;
    int (*wait_pidfd)(void *context, int pidfd, siginfo_t *information,
                      int options) noexcept;

    std::chrono::steady_clock::time_point (*steady_now)(
        void *context) noexcept;
    void (*sleep_until)(void *context,
                        std::chrono::steady_clock::time_point deadline) noexcept;
};

// The provider and its context have process lifetime. Test providers must drain
// all asynchronous rollback work before resetting scenario state.
const ProcessContainmentLinuxOps &
process_lifetime_process_containment_linux_ops() noexcept;

} // namespace lemon::utils::internal

#endif
