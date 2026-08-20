#include "platform/durable_file_adapter.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace lemon::residency::detail {

namespace {

constexpr std::string_view journal_name = "journal.jsonl";
constexpr std::string_view root_name = "authority-root.json";
constexpr std::string_view lock_name = "authority.lock";
constexpr std::string_view journal_stage_name = ".journal.jsonl.stage";
constexpr std::string_view root_stage_name = ".authority-root.json.stage";
constexpr std::size_t preflight_collision_attempts = 8;
std::atomic<std::uint64_t> preflight_sequence{0};

struct PreflightNames {
    std::string probe;
    std::string stage;
};

PreflightNames next_preflight_names() {
    const auto sequence =
        preflight_sequence.fetch_add(1, std::memory_order_relaxed);
    auto probe = ".durability-probe." +
                 std::to_string(static_cast<std::uint64_t>(::getpid())) + "." +
                 std::to_string(sequence);
    return {probe, probe + ".stage"};
}

DurableFileResult make_result(DurableFileStatus status) {
    return {status, {}};
}

DurableFileResult make_errno_result(DurableFileStatus status,
                                    int error_number) {
    return {status, std::strerror(error_number)};
}

DurableFileResult open_error_result(int error_number) {
    if (error_number == EINTR) {
        return make_errno_result(DurableFileStatus::Interrupted, error_number);
    }
    if (error_number == ENOENT) {
        return make_errno_result(DurableFileStatus::NotFound, error_number);
    }
    if (error_number == EEXIST) {
        return make_errno_result(DurableFileStatus::AlreadyExists,
                                 error_number);
    }
    if (error_number == ELOOP || error_number == EISDIR ||
        error_number == ENOTDIR) {
        return make_errno_result(DurableFileStatus::Unsupported, error_number);
    }
    return make_errno_result(DurableFileStatus::FailedBeforeEffect,
                             error_number);
}

DurableFileResult close_open_descriptor(int descriptor,
                                        DurableFileResult operation_result) {
    if (::close(descriptor) != 0 && operation_result.succeeded()) {
        return make_errno_result(DurableFileStatus::EffectMayHaveOccurred,
                                 errno);
    }
    return operation_result;
}

void close_best_effort(int descriptor) {
    if (descriptor >= 0) {
        static_cast<void>(::close(descriptor));
    }
}

class PosixDurableFileChannel final : public DurableFileChannel {
public:
    explicit PosixDurableFileChannel(int descriptor)
        : descriptor_(descriptor) {}

    ~PosixDurableFileChannel() override { close_best_effort(descriptor_); }

    DurableWriteResult write_some(std::string_view bytes) override {
        const auto request = std::min(
            bytes.size(), static_cast<std::size_t>(
                              std::numeric_limits<ssize_t>::max()));
        const auto written = ::write(descriptor_, bytes.data(), request);
        if (written < 0) {
            if (errno == EINTR) {
                return {make_errno_result(DurableFileStatus::Interrupted,
                                          errno),
                        0};
            }
            return {make_errno_result(DurableFileStatus::FailedBeforeEffect,
                                      errno),
                    0};
        }
        return {make_result(DurableFileStatus::Succeeded),
                static_cast<std::size_t>(written)};
    }

    DurableFileResult flush() override {
        if (::fsync(descriptor_) != 0) {
            if (errno == EINTR) {
                return make_errno_result(DurableFileStatus::Interrupted,
                                         errno);
            }
            return make_errno_result(
                DurableFileStatus::EffectMayHaveOccurred, errno);
        }
        return make_result(DurableFileStatus::Succeeded);
    }

    DurableFileResult truncate(std::size_t bytes) override {
        if (bytes > static_cast<std::size_t>(
                        std::numeric_limits<off_t>::max())) {
            return make_result(DurableFileStatus::FailedBeforeEffect);
        }
        if (::ftruncate(descriptor_, static_cast<off_t>(bytes)) != 0) {
            if (errno == EINTR) {
                return make_errno_result(DurableFileStatus::Interrupted,
                                         errno);
            }
            return make_errno_result(
                DurableFileStatus::EffectMayHaveOccurred, errno);
        }
        return make_result(DurableFileStatus::Succeeded);
    }

    DurableFileResult close() override {
        const auto descriptor = std::exchange(descriptor_, -1);
        if (::close(descriptor) != 0) {
            return make_errno_result(
                DurableFileStatus::EffectMayHaveOccurred, errno);
        }
        return make_result(DurableFileStatus::Succeeded);
    }

private:
    int descriptor_;
};

class PosixDurableReadChannel final : public DurableReadChannel {
public:
    explicit PosixDurableReadChannel(int descriptor)
        : descriptor_(descriptor) {}

    ~PosixDurableReadChannel() override { close_best_effort(descriptor_); }

    DurableReadChunkResult read_some(std::size_t max_bytes) override {
        const auto request =
            std::min(max_bytes, std::size_t(2147483647));
        std::string buffer(request, '\0');
        const auto transferred =
            ::read(descriptor_, buffer.data(), request);
        if (transferred < 0) {
            if (errno == EINTR) {
                return {make_errno_result(DurableFileStatus::Interrupted,
                                          errno),
                        {}, false};
            }
            return {make_errno_result(DurableFileStatus::FailedBeforeEffect,
                                      errno),
                    {}, false};
        }
        buffer.resize(static_cast<std::size_t>(transferred));
        return {make_result(DurableFileStatus::Succeeded), std::move(buffer),
                transferred == 0};
    }

    DurableFileResult close() override {
        const auto descriptor = std::exchange(descriptor_, -1);
        if (::close(descriptor) != 0) {
            return make_errno_result(
                DurableFileStatus::EffectMayHaveOccurred, errno);
        }
        return make_result(DurableFileStatus::Succeeded);
    }

private:
    int descriptor_;
};

DurableFileResult lock_file_retrying_interrupts(int lock_fd) {
    class LockCall final : public DurableInterruptibleCall {
    public:
        explicit LockCall(int lock_fd) : lock_fd_(lock_fd) {}

        DurableFileResult attempt() override {
            if (::flock(lock_fd_, LOCK_EX) != 0) {
                if (errno == EINTR) {
                    return make_errno_result(DurableFileStatus::Interrupted,
                                             errno);
                }
                return make_errno_result(
                    DurableFileStatus::FailedBeforeEffect, errno);
            }
            return make_result(DurableFileStatus::Succeeded);
        }

    private:
        int lock_fd_;
    };
    LockCall call(lock_fd);
    return retry_interrupted(call);
}

DurableFileResult unlock_file_retrying_interrupts(int lock_fd) {
    class UnlockCall final : public DurableInterruptibleCall {
    public:
        explicit UnlockCall(int lock_fd) : lock_fd_(lock_fd) {}

        DurableFileResult attempt() override {
            if (::flock(lock_fd_, LOCK_UN) != 0) {
                if (errno == EINTR) {
                    return make_errno_result(DurableFileStatus::Interrupted,
                                             errno);
                }
                return make_errno_result(
                    DurableFileStatus::EffectMayHaveOccurred, errno);
            }
            return make_result(DurableFileStatus::Succeeded);
        }

    private:
        int lock_fd_;
    };
    UnlockCall call(lock_fd);
    return retry_interrupted(call);
}

DurableFileResult lock_directory_retrying_interrupts(int directory_fd) {
    class DirectoryLockCall final : public DurableInterruptibleCall {
    public:
        explicit DirectoryLockCall(int directory_fd)
            : directory_fd_(directory_fd) {}

        DurableFileResult attempt() override {
            if (::flock(directory_fd_, LOCK_EX) != 0) {
                if (errno == EINTR) {
                    return make_errno_result(DurableFileStatus::Interrupted,
                                             errno);
                }
                return make_errno_result(
                    DurableFileStatus::FailedBeforeEffect, errno);
            }
            return make_result(DurableFileStatus::Succeeded);
        }

    private:
        int directory_fd_;
    };
    DirectoryLockCall call(directory_fd);
    return retry_interrupted(call);
}

DurableFileResult unlock_directory_retrying_interrupts(int directory_fd) {
    class DirectoryUnlockCall final : public DurableInterruptibleCall {
    public:
        explicit DirectoryUnlockCall(int directory_fd)
            : directory_fd_(directory_fd) {}

        DurableFileResult attempt() override {
            if (::flock(directory_fd_, LOCK_UN) != 0) {
                if (errno == EINTR) {
                    return make_errno_result(DurableFileStatus::Interrupted,
                                             errno);
                }
                return make_errno_result(
                    DurableFileStatus::EffectMayHaveOccurred, errno);
            }
            return make_result(DurableFileStatus::Succeeded);
        }

    private:
        int directory_fd_;
    };
    DirectoryUnlockCall call(directory_fd);
    return retry_interrupted(call);
}

DurableFileResult sync_directory_retrying_interrupts(int directory_fd) {
    class DirectorySyncCall final : public DurableInterruptibleCall {
    public:
        explicit DirectorySyncCall(int directory_fd)
            : directory_fd_(directory_fd) {}

        DurableFileResult attempt() override {
            if (::fsync(directory_fd_) != 0) {
                if (errno == EINTR) {
                    return make_errno_result(DurableFileStatus::Interrupted,
                                             errno);
                }
                return make_errno_result(
                    DurableFileStatus::EffectMayHaveOccurred, errno);
            }
            return make_result(DurableFileStatus::Succeeded);
        }

    private:
        int directory_fd_;
    };
    DirectorySyncCall call(directory_fd);
    return retry_interrupted(call);
}

struct PosixFileIdentity {
    dev_t device;
    ino_t inode;
};

DurableFileResult capture_regular_identity(
    int descriptor, PosixFileIdentity &identity) {
    struct stat information {};
    if (::fstat(descriptor, &information) != 0) {
        return make_errno_result(DurableFileStatus::FailedBeforeEffect,
                                 errno);
    }
    if (!S_ISREG(information.st_mode) || information.st_nlink != 1) {
        return make_result(DurableFileStatus::Unsupported);
    }
    identity = {information.st_dev, information.st_ino};
    return make_result(DurableFileStatus::Succeeded);
}

DurableFileResult post_create_failure(DurableFileResult failure) {
    failure.status = DurableFileStatus::EffectMayHaveOccurred;
    return failure;
}

bool same_file_identity(const PosixFileIdentity &left,
                        const PosixFileIdentity &right) {
    return left.device == right.device && left.inode == right.inode;
}

DurableFileResult verify_replaced_target(
    int directory_fd, std::string_view target_name,
    const PosixFileIdentity &expected_identity,
    std::string_view expected_bytes) {
    int descriptor;
    do {
        descriptor = ::openat(directory_fd, target_name.data(),
                              O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0) {
        return make_errno_result(DurableFileStatus::EffectMayHaveOccurred,
                                 errno);
    }
    struct stat information {};
    if (::fstat(descriptor, &information) != 0) {
        const auto error_number = errno;
        return close_open_descriptor(
            descriptor,
            make_errno_result(DurableFileStatus::EffectMayHaveOccurred,
                              error_number));
    }
    if (!S_ISREG(information.st_mode) || information.st_nlink != 1 ||
        information.st_dev != expected_identity.device ||
        information.st_ino != expected_identity.inode) {
        return close_open_descriptor(
            descriptor,
            make_result(DurableFileStatus::EffectMayHaveOccurred));
    }
    PosixDurableReadChannel channel(descriptor);
    const auto observed = read_bounded_close(channel, expected_bytes.size());
    if (!observed.result.succeeded()) {
        return {DurableFileStatus::EffectMayHaveOccurred,
                observed.result.diagnostic};
    }
    const auto observed_bytes =
        std::string_view(observed.bytes.data(), observed.bytes.size());
    if (observed.truncated || observed_bytes != expected_bytes) {
        return make_result(DurableFileStatus::EffectMayHaveOccurred);
    }
    return make_result(DurableFileStatus::Succeeded);
}

DurableFileResult clear_verified_stage(int descriptor) {
    while (::ftruncate(descriptor, 0) != 0) {
        if (errno == EINTR) {
            continue;
        }
        return make_errno_result(DurableFileStatus::EffectMayHaveOccurred,
                                 errno);
    }
    return make_result(DurableFileStatus::Succeeded);
}

DurableFileResult verify_owned_probe(
    int directory_fd, std::string_view name,
    const PosixFileIdentity &expected_identity) {
    struct stat information {};
    if (::fstatat(directory_fd, name.data(), &information,
                  AT_SYMLINK_NOFOLLOW) != 0) {
        return make_errno_result(DurableFileStatus::EffectMayHaveOccurred,
                                 errno);
    }
    const PosixFileIdentity observed{information.st_dev,
                                     information.st_ino};
    if (!S_ISREG(information.st_mode) || information.st_nlink != 1 ||
        !same_file_identity(observed, expected_identity)) {
        return make_result(DurableFileStatus::EffectMayHaveOccurred);
    }
    return make_result(DurableFileStatus::Succeeded);
}

DurableFileResult remove_owned_probe(
    int directory_fd, std::string_view name,
    const PosixFileIdentity &expected_identity) {
    const auto verified =
        verify_owned_probe(directory_fd, name, expected_identity);
    if (!verified.succeeded()) {
        return verified;
    }
    if (::unlinkat(directory_fd, name.data(), 0) != 0) {
        return make_errno_result(DurableFileStatus::EffectMayHaveOccurred,
                                 errno);
    }
    return make_result(DurableFileStatus::Succeeded);
}

class PosixDurableFileAdapter final : public DurableFileAdapter {
public:
    explicit PosixDurableFileAdapter(int directory_fd)
        : directory_fd_(directory_fd) {}

#ifdef LEMONADE_RESIDENCY_DURABLE_TESTING
    PosixDurableFileAdapter(int directory_fd,
                            DurablePreflightTestFault preflight_test_fault)
        : directory_fd_(directory_fd),
          preflight_test_fault_(preflight_test_fault) {}
#endif

    ~PosixDurableFileAdapter() override {
        close_best_effort(lock_fd_);
        close_best_effort(directory_fd_);
    }

    DurableFileResult lock_authority() override {
        if (directory_fd_ < 0 || lock_fd_ >= 0 || directory_lock_held_) {
            return make_result(DurableFileStatus::Unsupported);
        }
        const auto directory_lock_result =
            lock_directory_retrying_interrupts(directory_fd_);
        if (!directory_lock_result.succeeded()) {
            return directory_lock_result;
        }
        directory_lock_held_ = true;

        int opened_fd;
        do {
            opened_fd = ::openat(directory_fd_, lock_name.data(),
                                 O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW,
                                 0600);
        } while (opened_fd < 0 && errno == EINTR);
        if (opened_fd < 0) {
            const auto error_number = errno;
            return release_directory_after_failed_lock(
                open_error_result(error_number));
        }
        lock_fd_ = opened_fd;
        const auto lock_result = lock_file_retrying_interrupts(lock_fd_);
        if (!lock_result.succeeded()) {
            return close_unlocked_after_failed_lock(lock_result);
        }

        struct stat opened_information {};
        if (::fstat(lock_fd_, &opened_information) != 0) {
            const auto error_number = errno;
            return abandon_failed_lock(make_errno_result(
                DurableFileStatus::Unsupported, error_number));
        }
        struct stat current_information {};
        if (::fstatat(directory_fd_, lock_name.data(), &current_information,
                      AT_SYMLINK_NOFOLLOW) != 0) {
            const auto error_number = errno;
            return abandon_failed_lock(make_errno_result(
                DurableFileStatus::Unsupported, error_number));
        }
        if (!S_ISREG(opened_information.st_mode) ||
            !S_ISREG(current_information.st_mode) ||
            opened_information.st_nlink == 0 ||
            current_information.st_nlink == 0 ||
            opened_information.st_dev != current_information.st_dev ||
            opened_information.st_ino != current_information.st_ino) {
            return abandon_failed_lock(
                make_result(DurableFileStatus::Unsupported));
        }
        return make_result(DurableFileStatus::Succeeded);
    }

    DurableIdentityResult authority_identity() override {
        if (directory_fd_ < 0 || lock_fd_ < 0 || !directory_lock_held_) {
            return {make_result(DurableFileStatus::Unsupported), {}};
        }
        struct stat directory_information {};
        if (::fstat(directory_fd_, &directory_information) != 0) {
            return {make_errno_result(DurableFileStatus::Unsupported, errno),
                    {}};
        }
        struct stat lock_information {};
        if (::fstat(lock_fd_, &lock_information) != 0) {
            return {make_errno_result(DurableFileStatus::Unsupported, errno),
                    {}};
        }
        struct stat current_lock_information {};
        if (::fstatat(directory_fd_, lock_name.data(),
                      &current_lock_information, AT_SYMLINK_NOFOLLOW) != 0) {
            return {make_errno_result(DurableFileStatus::Unsupported, errno),
                    {}};
        }
        if (!S_ISDIR(directory_information.st_mode) ||
            !S_ISREG(lock_information.st_mode) ||
            !S_ISREG(current_lock_information.st_mode) ||
            lock_information.st_nlink == 0 ||
            current_lock_information.st_nlink == 0 ||
            lock_information.st_dev != current_lock_information.st_dev ||
            lock_information.st_ino != current_lock_information.st_ino) {
            return {make_result(DurableFileStatus::Unsupported), {}};
        }
        return {make_result(DurableFileStatus::Succeeded),
                "directory:" +
                    std::to_string(
                        static_cast<std::uintmax_t>(directory_information.st_dev)) +
                    ":" +
                    std::to_string(
                        static_cast<std::uintmax_t>(directory_information.st_ino)) +
                    "/lock:" +
                    std::to_string(
                        static_cast<std::uintmax_t>(lock_information.st_dev)) +
                    ":" +
                    std::to_string(
                        static_cast<std::uintmax_t>(lock_information.st_ino))};
    }

    DurableFileResult preflight_capabilities() override {
        if (directory_fd_ < 0 || lock_fd_ < 0) {
            return make_result(DurableFileStatus::Unsupported);
        }
        for (const auto name : {journal_name, root_name, lock_name,
                                journal_stage_name, root_stage_name}) {
            struct stat information {};
            if (::fstatat(directory_fd_, name.data(), &information,
                          AT_SYMLINK_NOFOLLOW) != 0) {
                if (errno == ENOENT) {
                    continue;
                }
                return make_errno_result(DurableFileStatus::Unsupported,
                                         errno);
            }
            if (!S_ISREG(information.st_mode) ||
                (name != lock_name && information.st_nlink != 1) ||
                (name == lock_name && information.st_nlink == 0)) {
                return make_result(DurableFileStatus::Unsupported);
            }
        }

        bool scratch_create_succeeded = false;
        const auto terminal_failure = [&](DurableFileResult failure) {
            if (scratch_create_succeeded && !failure.succeeded()) {
                failure.status = DurableFileStatus::EffectMayHaveOccurred;
            }
            return failure;
        };
        for (std::size_t attempt = 0;
             attempt < preflight_collision_attempts; ++attempt) {
            const auto names = next_preflight_names();
            PosixFileIdentity probe_identity {};
            PosixFileIdentity stage_identity {};
            bool probe_owned = false;
            bool stage_owned = false;
            const auto cleanup_owned = [&]() {
                auto result = make_result(DurableFileStatus::Succeeded);
                if (stage_owned) {
                    const auto removed = remove_owned_probe(
                        directory_fd_, names.stage, stage_identity);
                    if (removed.succeeded()) {
                        stage_owned = false;
                    } else {
                        result = removed;
                    }
                }
                if (probe_owned) {
                    const auto removed = remove_owned_probe(
                        directory_fd_, names.probe, probe_identity);
                    if (removed.succeeded()) {
                        probe_owned = false;
                    } else if (result.succeeded()) {
                        result = removed;
                    }
                }
                return result;
            };
            const auto fail_after_cleanup =
                [&](DurableFileResult failure) {
                    const auto cleanup = cleanup_owned();
                    return terminal_failure(cleanup.succeeded() ? failure
                                                                : cleanup);
                };

            int probe_fd;
            do {
                probe_fd = ::openat(directory_fd_, names.probe.c_str(),
                                    O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC |
                                        O_NOFOLLOW,
                                    0600);
            } while (probe_fd < 0 && errno == EINTR);
            if (probe_fd < 0) {
                if (errno == EEXIST) {
                    continue;
                }
                return terminal_failure(open_error_result(errno));
            }
            scratch_create_succeeded = true;
            auto probe_safe =
                capture_regular_identity(probe_fd, probe_identity);
#ifdef LEMONADE_RESIDENCY_DURABLE_TESTING
            probe_safe = apply_created_identity_test_fault(
                DurablePreflightTestFault::ProbeIdentityCaptureFailureOnce,
                std::move(probe_safe));
#endif
            if (!probe_safe.succeeded()) {
                return terminal_failure(close_open_descriptor(
                    probe_fd, post_create_failure(probe_safe)));
            }
            probe_owned = true;
            PosixDurableFileChannel probe_channel(probe_fd);
            auto flush_probe =
                write_flush_close(probe_channel, "durability-probe");
            if (!flush_probe.succeeded()) {
                const auto cleanup = cleanup_owned();
                if (!cleanup.succeeded()) {
                    return terminal_failure(cleanup);
                }
                flush_probe.status =
                    DurableFileStatus::EffectMayHaveOccurred;
                return flush_probe;
            }

            int truncate_fd;
            do {
                truncate_fd = ::openat(directory_fd_, names.probe.c_str(),
                                       O_RDWR | O_CLOEXEC | O_NOFOLLOW);
            } while (truncate_fd < 0 && errno == EINTR);
            if (truncate_fd < 0) {
                return fail_after_cleanup(open_error_result(errno));
            }
            PosixFileIdentity truncate_identity {};
            const auto truncate_safe =
                capture_regular_identity(truncate_fd, truncate_identity);
            if (!truncate_safe.succeeded() ||
                !same_file_identity(truncate_identity, probe_identity)) {
                const auto result = close_open_descriptor(
                    truncate_fd,
                    truncate_safe.succeeded()
                        ? make_result(DurableFileStatus::EffectMayHaveOccurred)
                        : post_create_failure(truncate_safe));
                return fail_after_cleanup(result);
            }
            PosixDurableFileChannel truncate_channel(truncate_fd);
            auto truncate_probe =
                truncate_flush_close(truncate_channel, 0);
            if (!truncate_probe.succeeded()) {
                const auto cleanup = cleanup_owned();
                if (!cleanup.succeeded()) {
                    return terminal_failure(cleanup);
                }
                truncate_probe.status =
                    DurableFileStatus::EffectMayHaveOccurred;
                return truncate_probe;
            }

#ifdef LEMONADE_RESIDENCY_DURABLE_TESTING
            const auto collision = seed_stage_collision_for_test(names.stage);
            if (!collision.succeeded()) {
                return fail_after_cleanup(collision);
            }
#endif
            int stage_fd;
            do {
                stage_fd = ::openat(directory_fd_, names.stage.c_str(),
                                    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC |
                                        O_NOFOLLOW,
                                    0600);
            } while (stage_fd < 0 && errno == EINTR);
            if (stage_fd < 0) {
                const auto error_number = errno;
                const auto cleanup = cleanup_owned();
                if (!cleanup.succeeded()) {
                    return terminal_failure(cleanup);
                }
                if (error_number == EEXIST) {
                    continue;
                }
                return terminal_failure(open_error_result(error_number));
            }
            scratch_create_succeeded = true;
            auto stage_safe =
                capture_regular_identity(stage_fd, stage_identity);
#ifdef LEMONADE_RESIDENCY_DURABLE_TESTING
            stage_safe = apply_created_identity_test_fault(
                DurablePreflightTestFault::StageIdentityCaptureFailureOnce,
                std::move(stage_safe));
#endif
            if (!stage_safe.succeeded()) {
                return fail_after_cleanup(
                    close_open_descriptor(stage_fd,
                                          post_create_failure(stage_safe)));
            }
            stage_owned = true;
            PosixDurableFileChannel stage_channel(stage_fd);
            auto replace_probe =
                write_flush_close(stage_channel, "replacement-probe");
            if (!replace_probe.succeeded()) {
                const auto cleanup = cleanup_owned();
                if (!cleanup.succeeded()) {
                    return terminal_failure(cleanup);
                }
                replace_probe.status =
                    DurableFileStatus::EffectMayHaveOccurred;
                return replace_probe;
            }
            const auto stage_current = verify_owned_probe(
                directory_fd_, names.stage, stage_identity);
            const auto probe_current = verify_owned_probe(
                directory_fd_, names.probe, probe_identity);
            if (!stage_current.succeeded() || !probe_current.succeeded()) {
                return fail_after_cleanup(
                    !stage_current.succeeded() ? stage_current
                                               : probe_current);
            }
            const auto replaced =
                ::renameat(directory_fd_, names.stage.c_str(), directory_fd_,
                           names.probe.c_str());
            if (replaced != 0) {
                return fail_after_cleanup(make_errno_result(
                    DurableFileStatus::EffectMayHaveOccurred, errno));
            }
            probe_identity = stage_identity;
            probe_owned = true;
            stage_owned = false;
            const auto cleanup = cleanup_owned();
            if (!cleanup.succeeded()) {
                return terminal_failure(cleanup);
            }
            const auto namespace_result =
                sync_directory_retrying_interrupts(directory_fd_);
            if (!namespace_result.succeeded()) {
                return terminal_failure(namespace_result);
            }
            return make_result(DurableFileStatus::Succeeded);
        }
        return terminal_failure(
            make_errno_result(DurableFileStatus::AlreadyExists, EEXIST));
    }

    DurableFixedNamespaceResult inspect_fixed_namespace() override {
        DurableFixedNamespaceResult inspected{
            make_result(DurableFileStatus::Succeeded), false, false, false,
            false, false};
        const std::array<std::pair<std::string_view, bool *>, 5> entries{
            std::pair{journal_name, &inspected.journal_present},
            std::pair{root_name, &inspected.root_present},
            std::pair{journal_stage_name, &inspected.journal_stage_present},
            std::pair{root_stage_name, &inspected.root_stage_present},
            std::pair{lock_name, &inspected.lock_present},
        };
        for (const auto &[name, present] : entries) {
            struct stat information {};
            if (::fstatat(directory_fd_, name.data(), &information,
                          AT_SYMLINK_NOFOLLOW) != 0) {
                if (errno == ENOENT) {
                    continue;
                }
                inspected.result =
                    make_errno_result(DurableFileStatus::Unsupported, errno);
                return inspected;
            }
            if (!S_ISREG(information.st_mode) ||
                (name != lock_name && information.st_nlink != 1) ||
                (name == lock_name && information.st_nlink == 0)) {
                inspected.result =
                    make_result(DurableFileStatus::Unsupported);
                return inspected;
            }
            *present = true;
        }
        return inspected;
    }

    DurableReadResult read_root(std::size_t max_bytes) override {
        int descriptor;
        do {
            descriptor = ::openat(directory_fd_, root_name.data(),
                                  O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        } while (descriptor < 0 && errno == EINTR);
        if (descriptor < 0) {
            return {open_error_result(errno), {}, false};
        }
        struct stat information {};
        if (::fstat(descriptor, &information) != 0) {
            return {close_open_descriptor(
                        descriptor,
                        make_errno_result(DurableFileStatus::Unsupported,
                                          errno)),
                    {}, false};
        }
        if (!S_ISREG(information.st_mode) || information.st_nlink != 1) {
            return {close_open_descriptor(
                        descriptor,
                        make_result(DurableFileStatus::Unsupported)),
                    {}, false};
        }
        PosixDurableReadChannel channel(descriptor);
        return read_bounded_close(channel, max_bytes);
    }

    DurableReadResult read_journal(std::size_t max_bytes) override {
        int descriptor;
        do {
            descriptor = ::openat(directory_fd_, journal_name.data(),
                                  O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        } while (descriptor < 0 && errno == EINTR);
        if (descriptor < 0) {
            return {open_error_result(errno), {}, false};
        }
        struct stat information {};
        if (::fstat(descriptor, &information) != 0) {
            return {close_open_descriptor(
                        descriptor,
                        make_errno_result(DurableFileStatus::Unsupported,
                                          errno)),
                    {}, false};
        }
        if (!S_ISREG(information.st_mode) || information.st_nlink != 1) {
            return {close_open_descriptor(
                        descriptor,
                        make_result(DurableFileStatus::Unsupported)),
                    {}, false};
        }
        PosixDurableReadChannel channel(descriptor);
        return read_bounded_close(channel, max_bytes);
    }

    DurableFileResult create_journal(std::string_view bytes) override {
        int descriptor;
        do {
            descriptor = ::openat(directory_fd_, journal_name.data(),
                                  O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC |
                                      O_NOFOLLOW,
                                  0600);
        } while (descriptor < 0 && errno == EINTR);
        if (descriptor < 0) {
            return open_error_result(errno);
        }
        struct stat information {};
        if (::fstat(descriptor, &information) != 0) {
            return close_open_descriptor(
                descriptor,
                make_errno_result(DurableFileStatus::EffectMayHaveOccurred,
                                  errno));
        }
        if (!S_ISREG(information.st_mode) || information.st_nlink != 1) {
            return close_open_descriptor(
                descriptor,
                make_result(DurableFileStatus::EffectMayHaveOccurred));
        }
        PosixDurableFileChannel channel(descriptor);
        const auto persisted = write_flush_close(channel, bytes);
        if (!persisted.succeeded() &&
            !persisted.effect_may_have_occurred()) {
            return {DurableFileStatus::EffectMayHaveOccurred,
                    persisted.diagnostic};
        }
        return persisted;
    }

    DurableFileResult append_journal(std::string_view bytes) override {
        int descriptor;
        do {
            descriptor = ::openat(directory_fd_, journal_name.data(),
                                  O_WRONLY | O_APPEND | O_CLOEXEC |
                                      O_NOFOLLOW);
        } while (descriptor < 0 && errno == EINTR);
        if (descriptor < 0) {
            return open_error_result(errno);
        }
        struct stat information {};
        if (::fstat(descriptor, &information) != 0) {
            return close_open_descriptor(
                descriptor,
                make_errno_result(DurableFileStatus::FailedBeforeEffect,
                                  errno));
        }
        if (!S_ISREG(information.st_mode) || information.st_nlink != 1) {
            return close_open_descriptor(
                descriptor,
                make_result(DurableFileStatus::Unsupported));
        }
        PosixDurableFileChannel channel(descriptor);
        return write_flush_close(channel, bytes);
    }

    DurableFileResult truncate_journal(std::size_t bytes) override {
        int descriptor;
        do {
            descriptor = ::openat(directory_fd_, journal_name.data(),
                                  O_RDWR | O_CLOEXEC | O_NOFOLLOW);
        } while (descriptor < 0 && errno == EINTR);
        if (descriptor < 0) {
            return open_error_result(errno);
        }
        struct stat information {};
        if (::fstat(descriptor, &information) != 0) {
            return close_open_descriptor(
                descriptor,
                make_errno_result(DurableFileStatus::FailedBeforeEffect,
                                  errno));
        }
        if (!S_ISREG(information.st_mode) || information.st_nlink != 1) {
            return close_open_descriptor(
                descriptor,
                make_result(DurableFileStatus::Unsupported));
        }
        PosixDurableFileChannel channel(descriptor);
        return truncate_flush_close(channel, bytes);
    }

    DurableFileResult replace_journal(std::string_view bytes) override {
        int descriptor;
        do {
            descriptor = ::openat(directory_fd_, journal_stage_name.data(),
                                  O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW,
                                  0600);
        } while (descriptor < 0 && errno == EINTR);
        if (descriptor < 0) {
            return open_error_result(errno);
        }
        struct stat information {};
        if (::fstat(descriptor, &information) != 0) {
            return close_open_descriptor(
                descriptor,
                make_errno_result(DurableFileStatus::EffectMayHaveOccurred,
                                  errno));
        }
        if (!S_ISREG(information.st_mode) || information.st_nlink != 1) {
            return close_open_descriptor(
                descriptor,
                make_result(DurableFileStatus::EffectMayHaveOccurred));
        }
        const PosixFileIdentity staged_identity{information.st_dev,
                                                information.st_ino};
        const auto cleared = clear_verified_stage(descriptor);
        if (!cleared.succeeded()) {
            return close_open_descriptor(descriptor, cleared);
        }
        PosixDurableFileChannel channel(descriptor);
        const auto staged = write_flush_close(channel, bytes);
        if (!staged.succeeded() && !staged.effect_may_have_occurred()) {
            return {DurableFileStatus::EffectMayHaveOccurred,
                    staged.diagnostic};
        }
        if (!staged.succeeded()) {
            return staged;
        }
        if (::renameat(directory_fd_, journal_stage_name.data(), directory_fd_,
                       journal_name.data()) != 0) {
            return make_errno_result(
                DurableFileStatus::EffectMayHaveOccurred, errno);
        }
        const auto namespace_result =
            sync_directory_retrying_interrupts(directory_fd_);
        if (!namespace_result.succeeded()) {
            return namespace_result;
        }
        return verify_replaced_target(directory_fd_, journal_name,
                                      staged_identity, bytes);
    }

    DurableFileResult replace_root(std::string_view bytes) override {
        int descriptor;
        do {
            descriptor = ::openat(directory_fd_, root_stage_name.data(),
                                  O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW,
                                  0600);
        } while (descriptor < 0 && errno == EINTR);
        if (descriptor < 0) {
            return open_error_result(errno);
        }
        struct stat information {};
        if (::fstat(descriptor, &information) != 0) {
            return close_open_descriptor(
                descriptor,
                make_errno_result(DurableFileStatus::EffectMayHaveOccurred,
                                  errno));
        }
        if (!S_ISREG(information.st_mode) || information.st_nlink != 1) {
            return close_open_descriptor(
                descriptor,
                make_result(DurableFileStatus::EffectMayHaveOccurred));
        }
        const PosixFileIdentity staged_identity{information.st_dev,
                                                information.st_ino};
        const auto cleared = clear_verified_stage(descriptor);
        if (!cleared.succeeded()) {
            return close_open_descriptor(descriptor, cleared);
        }
        PosixDurableFileChannel channel(descriptor);
        const auto staged = write_flush_close(channel, bytes);
        if (!staged.succeeded() && !staged.effect_may_have_occurred()) {
            return {DurableFileStatus::EffectMayHaveOccurred,
                    staged.diagnostic};
        }
        if (!staged.succeeded()) {
            return staged;
        }
        if (::renameat(directory_fd_, root_stage_name.data(), directory_fd_,
                       root_name.data()) != 0) {
            return make_errno_result(
                DurableFileStatus::EffectMayHaveOccurred, errno);
        }
        const auto namespace_result =
            sync_directory_retrying_interrupts(directory_fd_);
        if (!namespace_result.succeeded()) {
            return namespace_result;
        }
        return verify_replaced_target(directory_fd_, root_name,
                                      staged_identity, bytes);
    }

    DurableFileResult unlock_authority() override {
        if (lock_fd_ < 0 || !directory_lock_held_) {
            return make_result(DurableFileStatus::Unsupported);
        }
        const auto descriptor = std::exchange(lock_fd_, -1);
        const auto unlock_result =
            unlock_file_retrying_interrupts(descriptor);
        const auto close_result = ::close(descriptor);
        const auto close_error_number = close_result == 0 ? 0 : errno;
        const auto directory_unlock_result =
            unlock_directory_retrying_interrupts(directory_fd_);
        if (!directory_unlock_result.succeeded()) {
            return directory_unlock_result;
        }
        directory_lock_held_ = false;
        if (!unlock_result.succeeded() && close_result != 0) {
            return make_result(DurableFileStatus::EffectMayHaveOccurred);
        }
        if (!unlock_result.succeeded()) {
            return unlock_result;
        }
        if (close_result != 0) {
            return make_errno_result(
                DurableFileStatus::EffectMayHaveOccurred,
                close_error_number);
        }
        return make_result(DurableFileStatus::Succeeded);
    }

private:
    DurableFileResult release_directory_after_failed_lock(
        DurableFileResult failure) {
        const auto directory_unlock_result =
            unlock_directory_retrying_interrupts(directory_fd_);
        if (!directory_unlock_result.succeeded()) {
            return directory_unlock_result;
        }
        directory_lock_held_ = false;
        return failure;
    }

    DurableFileResult close_unlocked_after_failed_lock(
        DurableFileResult failure) {
        const auto descriptor = std::exchange(lock_fd_, -1);
        if (::close(descriptor) != 0) {
            const auto error_number = errno;
            failure = make_errno_result(
                DurableFileStatus::EffectMayHaveOccurred, error_number);
        }
        return release_directory_after_failed_lock(std::move(failure));
    }

    DurableFileResult abandon_failed_lock(DurableFileResult failure) {
        const auto descriptor = std::exchange(lock_fd_, -1);
        const auto unlock_result =
            unlock_file_retrying_interrupts(descriptor);
        const auto close_result = ::close(descriptor);
        const auto close_error_number = close_result == 0 ? 0 : errno;
        const auto directory_unlock_result =
            unlock_directory_retrying_interrupts(directory_fd_);
        if (!directory_unlock_result.succeeded()) {
            return directory_unlock_result;
        }
        directory_lock_held_ = false;
        if (!unlock_result.succeeded() && close_result != 0) {
            return make_result(DurableFileStatus::EffectMayHaveOccurred);
        }
        if (!unlock_result.succeeded()) {
            return unlock_result;
        }
        if (close_result != 0) {
            return make_errno_result(
                DurableFileStatus::EffectMayHaveOccurred,
                close_error_number);
        }
        return failure;
    }

    int directory_fd_;
    int lock_fd_ = -1;
    bool directory_lock_held_ = false;
#ifdef LEMONADE_RESIDENCY_DURABLE_TESTING
    DurableFileResult seed_stage_collision_for_test(
        const std::string &name) noexcept {
        if (preflight_test_fault_ !=
            DurablePreflightTestFault::StageCreateCollisionExhaustion) {
            return make_result(DurableFileStatus::Succeeded);
        }
        int descriptor;
        do {
            descriptor = ::openat(directory_fd_, name.c_str(),
                                  O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC |
                                      O_NOFOLLOW,
                                  0600);
        } while (descriptor < 0 && errno == EINTR);
        if (descriptor < 0) {
            return open_error_result(errno);
        }
        PosixDurableFileChannel channel(descriptor);
        const auto written = write_flush_close(
            channel, "caller-owned-stage-collision");
        if (!written.succeeded()) {
            return written;
        }
        return sync_directory_retrying_interrupts(directory_fd_);
    }

    DurableFileResult apply_created_identity_test_fault(
        DurablePreflightTestFault phase,
        DurableFileResult captured) noexcept {
        if (captured.succeeded() && preflight_test_fault_ == phase &&
            !preflight_test_fault_taken_) {
            preflight_test_fault_taken_ = true;
            return make_result(DurableFileStatus::FailedBeforeEffect);
        }
        return captured;
    }
    std::optional<DurablePreflightTestFault> preflight_test_fault_;
    bool preflight_test_fault_taken_ = false;
#endif
};

} // namespace

std::unique_ptr<DurableFileAdapter>
make_platform_durable_file_adapter(const std::filesystem::path &directory) {
    int directory_fd;
    do {
        directory_fd = ::open(directory.c_str(),
                              O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
    } while (directory_fd < 0 && errno == EINTR);
    return std::make_unique<PosixDurableFileAdapter>(directory_fd);
}

#ifdef LEMONADE_RESIDENCY_DURABLE_TESTING
std::unique_ptr<DurableFileAdapter>
make_platform_durable_file_adapter_for_test(
    const std::filesystem::path &directory, DurablePreflightTestFault fault) {
    int directory_fd;
    do {
        directory_fd = ::open(directory.c_str(),
                              O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
    } while (directory_fd < 0 && errno == EINTR);
    return std::make_unique<PosixDurableFileAdapter>(directory_fd, fault);
}
#endif

} // namespace lemon::residency::detail
