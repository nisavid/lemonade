#include "linux_amd_gtt_point_observation_source.h"

#include "lemon/utils/process_manager.h"

#include <linux/magic.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

namespace lemon::residency::internal {

namespace {

constexpr auto maximum_read_duration = std::chrono::seconds(30);
constexpr std::size_t maximum_sysfs_attribute_bytes = 64 * 1024;
constexpr std::size_t maximum_fdinfo_bytes = 64 * 1024;
constexpr std::size_t maximum_fdinfo_descriptors_per_read = 8192;
constexpr std::size_t maximum_fdinfo_descriptors_per_pass =
    maximum_fdinfo_descriptors_per_read / 2;

enum class AttemptStatus {
    Active,
    Cancelled,
    Unavailable,
};

class ReadAttempt {
public:
    ReadAttempt(std::chrono::steady_clock::time_point deadline,
                const std::function<std::chrono::steady_clock::time_point()>
                    &monotonic_now,
                const ProfilingCancellationCheck &should_abort)
        : deadline_(deadline), monotonic_now_(monotonic_now),
          should_abort_(should_abort),
          cancellation_(std::make_shared<std::atomic<bool>>(false)) {}

    bool checkpoint() noexcept {
        if (status_ != AttemptStatus::Active) return false;
        try {
            if (should_abort_ && should_abort_()) {
                status_ = AttemptStatus::Cancelled;
                cancellation_->store(true, std::memory_order_release);
                return false;
            }
            if (!monotonic_now_ || monotonic_now_() >= deadline_) {
                status_ = AttemptStatus::Unavailable;
                return false;
            }
            return true;
        } catch (...) {
            status_ = AttemptStatus::Unavailable;
            return false;
        }
    }

    void fail() noexcept {
        if (status_ == AttemptStatus::Active) {
            status_ = AttemptStatus::Unavailable;
        }
    }

    void cancel() noexcept {
        status_ = AttemptStatus::Cancelled;
        cancellation_->store(true, std::memory_order_release);
    }

    AttemptStatus status() const noexcept { return status_; }

    lemon::utils::ProcessContainmentOperationControl operation_control()
        const {
        return {deadline_, cancellation_};
    }

private:
    std::chrono::steady_clock::time_point deadline_;
    const std::function<std::chrono::steady_clock::time_point()>
        &monotonic_now_;
    const ProfilingCancellationCheck &should_abort_;
    std::shared_ptr<std::atomic<bool>> cancellation_;
    AttemptStatus status_ = AttemptStatus::Active;
};

ProfilingRawReadResult unavailable_result() {
    return {ProfilingSourceError::Unavailable,
            {},
            {},
            "linux AMD GTT point observation is unavailable"};
}

ProfilingRawReadResult cancelled_result() {
    return {ProfilingSourceError::Cancelled,
            {},
            {},
            "linux AMD GTT point observation was cancelled"};
}

class ScopedFd {
public:
    ScopedFd() = default;
    explicit ScopedFd(int fd) noexcept : fd_(fd) {}
    ScopedFd(const ScopedFd &) = delete;
    ScopedFd &operator=(const ScopedFd &) = delete;
    ScopedFd(ScopedFd &&other) noexcept : fd_(other.release()) {}
    ScopedFd &operator=(ScopedFd &&other) noexcept {
        if (this != &other) reset(other.release());
        return *this;
    }
    ~ScopedFd() {
        if (fd_ >= 0) ::close(fd_);
    }

    int get() const noexcept { return fd_; }
    explicit operator bool() const noexcept { return fd_ >= 0; }

    int release() noexcept {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }

    void reset(int fd = -1) noexcept {
        if (fd_ >= 0) ::close(fd_);
        fd_ = fd;
    }

private:
    int fd_ = -1;
};

class ScopedDirectory {
public:
    explicit ScopedDirectory(DIR *directory) noexcept
        : directory_(directory) {}
    ScopedDirectory(const ScopedDirectory &) = delete;
    ScopedDirectory &operator=(const ScopedDirectory &) = delete;
    ~ScopedDirectory() {
        if (directory_ != nullptr) ::closedir(directory_);
    }

    DIR *get() const noexcept { return directory_; }

private:
    DIR *directory_ = nullptr;
};

struct DirectoryIdentity {
    dev_t device{};
    ino_t inode{};

    bool operator==(const DirectoryIdentity &other) const noexcept {
        return device == other.device && inode == other.inode;
    }
    bool operator!=(const DirectoryIdentity &other) const noexcept {
        return !(*this == other);
    }
};

std::optional<DirectoryIdentity> directory_identity(int fd) {
    struct stat status {};
    if (::fstat(fd, &status) != 0 || !S_ISDIR(status.st_mode)) {
        return std::nullopt;
    }
    return DirectoryIdentity{status.st_dev, status.st_ino};
}

std::optional<long> filesystem_magic(int fd) {
    struct statfs status {};
    if (::fstatfs(fd, &status) != 0) return std::nullopt;
    return status.f_type;
}

ScopedFd open_directory(const std::filesystem::path &path) {
    return ScopedFd(
        ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
}

ScopedFd open_directory_at(int parent_fd, const char *name) {
    return ScopedFd(::openat(parent_fd, name,
                             O_RDONLY | O_DIRECTORY | O_CLOEXEC |
                                 O_NOFOLLOW));
}

bool canonical_pci_bdf(std::string_view value) {
    if (value.size() != 12 || value[4] != ':' || value[7] != ':' ||
        value[10] != '.') {
        return false;
    }
    const auto lower_hex = [](char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
    };
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 4 || index == 7 || index == 10) continue;
        if (!lower_hex(value[index])) return false;
    }
    return (value[8] == '0' || value[8] == '1') &&
           value[11] >= '0' && value[11] <= '7';
}

bool path_is_usable(const std::filesystem::path &path) {
    return !path.empty() && path.is_absolute() &&
           path.native().find('\0') == std::string::npos;
}

bool canonical_decimal(std::string_view value) {
    if (value.empty()) return false;
    if (value.size() > 1 && value.front() == '0') return false;
    return std::all_of(value.begin(), value.end(), [](char character) {
        return character >= '0' && character <= '9';
    });
}

std::optional<std::uint64_t> parse_canonical_u64(std::string_view value) {
    if (!canonical_decimal(value)) return std::nullopt;
    std::uint64_t result = 0;
    const auto parsed =
        std::from_chars(value.data(), value.data() + value.size(), result, 10);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return result;
}

std::string_view strip_one_trailing_newline(std::string_view value) {
    if (!value.empty() && value.back() == '\n') value.remove_suffix(1);
    return value;
}

std::optional<std::string> read_bounded_file_at(
    int directory_fd, std::string_view name, std::size_t maximum_bytes,
    ReadAttempt &attempt) {
    if (!attempt.checkpoint() || name.empty() ||
        name.find('\0') != std::string_view::npos) {
        attempt.fail();
        return std::nullopt;
    }
    const std::string owned_name(name);
    ScopedFd fd(::openat(directory_fd, owned_name.c_str(),
                         O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (!fd || !attempt.checkpoint()) {
        attempt.fail();
        return std::nullopt;
    }
    struct stat status {};
    if (::fstat(fd.get(), &status) != 0 || !S_ISREG(status.st_mode)) {
        attempt.fail();
        return std::nullopt;
    }

    std::string bytes;
    bytes.reserve(std::min<std::size_t>(maximum_bytes, 4096));
    char buffer[4096];
    while (true) {
        if (!attempt.checkpoint()) return std::nullopt;
        const ssize_t count = ::read(fd.get(), buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EINTR) continue;
            attempt.fail();
            return std::nullopt;
        }
        if (count == 0) break;
        const auto size = static_cast<std::size_t>(count);
        if (bytes.size() > maximum_bytes ||
            size > maximum_bytes - bytes.size()) {
            attempt.fail();
            return std::nullopt;
        }
        bytes.append(buffer, size);
        if (bytes.size() > maximum_bytes) {
            attempt.fail();
            return std::nullopt;
        }
    }
    if (!attempt.checkpoint()) return std::nullopt;
    return bytes;
}

bool directory_still_named(
    const std::filesystem::path &path, int retained_fd,
    const DirectoryIdentity &retained_identity, long expected_magic,
    ReadAttempt &attempt) {
    if (!attempt.checkpoint()) return false;
    auto named = open_directory(path);
    if (!named || !attempt.checkpoint()) {
        attempt.fail();
        return false;
    }
    const auto named_identity = directory_identity(named.get());
    const auto current_retained_identity = directory_identity(retained_fd);
    const auto named_magic = filesystem_magic(named.get());
    const auto retained_magic = filesystem_magic(retained_fd);
    if (!named_identity || !current_retained_identity || !named_magic ||
        !retained_magic || *named_identity != retained_identity ||
        *current_retained_identity != retained_identity ||
        *named_magic != expected_magic ||
        *retained_magic != expected_magic) {
        attempt.fail();
        return false;
    }
    return attempt.checkpoint();
}

bool uevent_matches(std::string_view bytes, std::string_view drm_pdev) {
    std::map<std::string, std::string> fields;
    std::size_t start = 0;
    while (start < bytes.size()) {
        const std::size_t end = bytes.find('\n', start);
        const std::size_t length =
            (end == std::string_view::npos ? bytes.size() : end) - start;
        const auto line = bytes.substr(start, length);
        if (line.empty()) return false;
        const std::size_t separator = line.find('=');
        if (separator == std::string_view::npos || separator == 0 ||
            separator + 1 >= line.size() ||
            separator != line.rfind('=')) {
            return false;
        }
        const auto key = line.substr(0, separator);
        if (!std::all_of(key.begin(), key.end(), [](char character) {
                return (character >= 'A' && character <= 'Z') ||
                       (character >= '0' && character <= '9') ||
                       character == '_';
            })) {
            return false;
        }
        if (!fields.emplace(std::string(key),
                            std::string(line.substr(separator + 1)))
                 .second) {
            return false;
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    const auto driver = fields.find("DRIVER");
    const auto slot = fields.find("PCI_SLOT_NAME");
    return driver != fields.end() && driver->second == "amdgpu" &&
           slot != fields.end() && slot->second == drm_pdev;
}

std::string_view trim_horizontal(std::string_view value) {
    while (!value.empty() &&
           (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() &&
           (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
    }
    return value;
}

std::optional<std::uint64_t> parse_fdinfo_bytes(std::string_view value) {
    value = trim_horizontal(value);
    std::size_t digit_count = 0;
    while (digit_count < value.size() && value[digit_count] >= '0' &&
           value[digit_count] <= '9') {
        ++digit_count;
    }
    if (digit_count == 0) return std::nullopt;
    const auto amount = parse_canonical_u64(value.substr(0, digit_count));
    if (!amount) return std::nullopt;
    if (digit_count < value.size() && value[digit_count] != ' ' &&
        value[digit_count] != '\t') {
        return std::nullopt;
    }
    const auto unit = trim_horizontal(value.substr(digit_count));
    std::uint64_t multiplier = 1;
    if (unit == "KiB") {
        multiplier = 1024;
    } else if (unit == "MiB") {
        multiplier = 1024 * 1024;
    } else if (!unit.empty()) {
        return std::nullopt;
    }
    if (*amount > std::numeric_limits<std::uint64_t>::max() / multiplier) {
        return std::nullopt;
    }
    return *amount * multiplier;
}

struct ClientUsage {
    std::uint64_t resident_gtt = 0;
    std::uint64_t shared_gtt = 0;

    bool operator==(const ClientUsage &other) const noexcept {
        return resident_gtt == other.resident_gtt &&
               shared_gtt == other.shared_gtt;
    }
};

using ClientKey = std::pair<std::string, std::uint64_t>;
using ClientMap = std::map<ClientKey, ClientUsage>;

struct MemberDescriptorSet {
    lemon::utils::ProcessBirthIdentity member;
    std::vector<std::string> descriptors;

    bool operator==(const MemberDescriptorSet &other) const noexcept {
        return member == other.member && descriptors == other.descriptors;
    }
};

struct FdinfoPass {
    ClientMap clients;
    std::vector<MemberDescriptorSet> descriptor_sets;

    bool operator==(const FdinfoPass &other) const noexcept {
        return clients == other.clients &&
               descriptor_sets == other.descriptor_sets;
    }
};

enum class FdinfoClassification {
    Ignored,
    Target,
};

struct ParsedFdinfo {
    FdinfoClassification classification = FdinfoClassification::Ignored;
    ClientKey key;
    ClientUsage usage;
};

std::optional<ParsedFdinfo> parse_fdinfo(std::string_view bytes,
                                         std::string_view target_pdev) {
    std::map<std::string, std::string> fields;
    static constexpr std::array<std::string_view, 6> relevant_keys{
        "drm-driver",       "drm-pdev",       "drm-client-id",
        "drm-resident-gtt", "drm-memory-gtt", "drm-shared-gtt",
    };
    bool saw_drm_key = false;
    std::size_t start = 0;
    while (start < bytes.size()) {
        const std::size_t end = bytes.find('\n', start);
        const std::size_t length =
            (end == std::string_view::npos ? bytes.size() : end) - start;
        const auto line = bytes.substr(start, length);
        const auto trimmed_line = trim_horizontal(line);
        if (trimmed_line.rfind("drm-", 0) == 0 &&
            trimmed_line.size() != line.size()) {
            return std::nullopt;
        }
        if (line.rfind("drm-", 0) == 0) {
            saw_drm_key = true;
            const std::size_t separator = line.find(':');
            if (separator == std::string_view::npos || separator == 0) {
                return std::nullopt;
            }
            const auto key = line.substr(0, separator);
            if (!std::all_of(key.begin(), key.end(), [](char character) {
                    return (character >= 'a' && character <= 'z') ||
                           (character >= '0' && character <= '9') ||
                           character == '-';
                })) {
                return std::nullopt;
            }
            if (std::find(relevant_keys.begin(), relevant_keys.end(), key) !=
                relevant_keys.end()) {
                const auto value = trim_horizontal(line.substr(separator + 1));
                if (value.empty() ||
                    !fields.emplace(std::string(key), std::string(value))
                         .second) {
                    return std::nullopt;
                }
            }
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }

    if (fields.empty()) {
        if (saw_drm_key) return std::nullopt;
        return ParsedFdinfo{};
    }
    const auto driver = fields.find("drm-driver");
    const auto pdev = fields.find("drm-pdev");
    if (pdev == fields.end() || !canonical_pci_bdf(pdev->second)) {
        return std::nullopt;
    }
    if (pdev->second != target_pdev) return ParsedFdinfo{};
    if (driver == fields.end() || driver->second != "amdgpu") {
        return std::nullopt;
    }

    const auto client = fields.find("drm-client-id");
    const auto resident = fields.find("drm-resident-gtt");
    const auto legacy = fields.find("drm-memory-gtt");
    const auto shared = fields.find("drm-shared-gtt");
    if (client == fields.end() ||
        (resident == fields.end() && legacy == fields.end()) ||
        shared == fields.end()) {
        return std::nullopt;
    }
    const auto client_id = parse_canonical_u64(client->second);
    const auto resident_bytes =
        resident == fields.end()
            ? std::optional<std::uint64_t>{}
            : parse_fdinfo_bytes(resident->second);
    const auto legacy_bytes =
        legacy == fields.end() ? std::optional<std::uint64_t>{}
                               : parse_fdinfo_bytes(legacy->second);
    const auto shared_bytes = parse_fdinfo_bytes(shared->second);
    if (!client_id || (!resident_bytes && resident != fields.end()) ||
        (!legacy_bytes && legacy != fields.end()) || !shared_bytes ||
        *shared_bytes != 0 ||
        (resident_bytes && legacy_bytes &&
         *resident_bytes != *legacy_bytes)) {
        return std::nullopt;
    }
    return ParsedFdinfo{
        FdinfoClassification::Target,
        {pdev->second, *client_id},
        {resident_bytes ? *resident_bytes : *legacy_bytes, *shared_bytes},
    };
}

std::optional<std::uint64_t> parse_process_start_time(
    std::string_view bytes, int expected_pid) {
    bytes = strip_one_trailing_newline(bytes);
    if (bytes.find('\n') != std::string_view::npos ||
        bytes.find('\r') != std::string_view::npos) {
        return std::nullopt;
    }
    const std::string prefix = std::to_string(expected_pid) + " (";
    if (bytes.rfind(prefix, 0) != 0) return std::nullopt;
    const std::size_t closing = bytes.rfind(") ");
    if (closing == std::string_view::npos || closing < prefix.size()) {
        return std::nullopt;
    }
    std::string_view fields = bytes.substr(closing + 2);
    std::size_t field_index = 0;
    while (!fields.empty()) {
        const std::size_t separator = fields.find(' ');
        const auto field = fields.substr(0, separator);
        if (field.empty()) return std::nullopt;
        if (field_index == 19) return parse_canonical_u64(field);
        ++field_index;
        if (separator == std::string_view::npos) break;
        fields.remove_prefix(separator + 1);
    }
    return std::nullopt;
}

struct DirectoryListing {
    DirectoryIdentity identity;
    std::vector<std::string> names;
};

std::optional<DirectoryListing> list_numeric_fdinfo_entries(
    int process_fd, std::size_t maximum_entries, ReadAttempt &attempt) {
    if (!attempt.checkpoint()) return std::nullopt;
    auto fdinfo = open_directory_at(process_fd, "fdinfo");
    if (!fdinfo || !attempt.checkpoint()) {
        attempt.fail();
        return std::nullopt;
    }
    const auto identity = directory_identity(fdinfo.get());
    if (!identity) {
        attempt.fail();
        return std::nullopt;
    }
    const int raw_fd = fdinfo.release();
    DIR *const raw_directory = ::fdopendir(raw_fd);
    if (raw_directory == nullptr) {
        ::close(raw_fd);
        attempt.fail();
        return std::nullopt;
    }
    ScopedDirectory directory(raw_directory);
    std::vector<std::pair<std::uint64_t, std::string>> entries;
    while (true) {
        if (!attempt.checkpoint()) return std::nullopt;
        errno = 0;
        dirent *const entry = ::readdir(directory.get());
        if (entry == nullptr) {
            if (errno != 0) attempt.fail();
            break;
        }
        const std::string_view name(entry->d_name);
        if (name == "." || name == "..") continue;
        const auto number = parse_canonical_u64(name);
        if (!number) {
            attempt.fail();
            return std::nullopt;
        }
        if (entries.size() >= maximum_entries) {
            attempt.fail();
            return std::nullopt;
        }
        entries.emplace_back(*number, std::string(name));
    }
    if (attempt.status() != AttemptStatus::Active) return std::nullopt;
    std::sort(entries.begin(), entries.end());
    if (std::adjacent_find(
            entries.begin(), entries.end(),
            [](const auto &left, const auto &right) {
                return left.first == right.first;
            }) != entries.end()) {
        attempt.fail();
        return std::nullopt;
    }
    DirectoryListing result;
    result.identity = *identity;
    result.names.reserve(entries.size());
    for (auto &entry : entries) result.names.push_back(std::move(entry.second));
    if (!attempt.checkpoint()) return std::nullopt;
    return result;
}

bool process_identity_matches(
    int process_fd, const lemon::utils::ProcessBirthIdentity &identity,
    ReadAttempt &attempt) {
    const auto stat = read_bounded_file_at(process_fd, "stat", 4096, attempt);
    if (!stat) return false;
    const auto start_time = parse_process_start_time(*stat, identity.pid);
    if (!start_time || *start_time != identity.start_time_ticks) {
        attempt.fail();
        return false;
    }
    return true;
}

bool merge_client(ClientMap &clients, const ParsedFdinfo &parsed,
                  ReadAttempt &attempt) {
    if (parsed.classification == FdinfoClassification::Ignored) return true;
    const auto inserted = clients.emplace(parsed.key, parsed.usage);
    if (!inserted.second && !(inserted.first->second == parsed.usage)) {
        attempt.fail();
        return false;
    }
    if (clients.size() > 1) {
        attempt.fail();
        return false;
    }
    return true;
}

bool read_member_fdinfo(
    int proc_root_fd, const lemon::utils::ProcessBirthIdentity &member,
    std::string_view target_pdev, ClientMap &clients,
    std::vector<MemberDescriptorSet> &descriptor_sets,
    std::size_t maximum_descriptors,
    ReadAttempt &attempt) {
    if (member.pid <= 0 || member.start_time_ticks == 0 ||
        !attempt.checkpoint()) {
        attempt.fail();
        return false;
    }
    const std::string pid = std::to_string(member.pid);
    auto process = open_directory_at(proc_root_fd, pid.c_str());
    if (!process || !attempt.checkpoint()) {
        attempt.fail();
        return false;
    }
    if (!process_identity_matches(process.get(), member, attempt)) {
        return false;
    }
    const auto before = list_numeric_fdinfo_entries(
        process.get(), maximum_descriptors, attempt);
    if (!before) return false;

    auto fdinfo = open_directory_at(process.get(), "fdinfo");
    if (!fdinfo || !attempt.checkpoint()) {
        attempt.fail();
        return false;
    }
    const auto read_identity = directory_identity(fdinfo.get());
    if (!read_identity || *read_identity != before->identity) {
        attempt.fail();
        return false;
    }
    for (const auto &name : before->names) {
        const auto bytes = read_bounded_file_at(
            fdinfo.get(), name, maximum_fdinfo_bytes, attempt);
        if (!bytes) return false;
        const auto parsed = parse_fdinfo(*bytes, target_pdev);
        if (!parsed || !merge_client(clients, *parsed, attempt)) return false;
    }

    const auto after = list_numeric_fdinfo_entries(
        process.get(), before->names.size(), attempt);
    if (!after || after->identity != before->identity ||
        after->names != before->names ||
        !process_identity_matches(process.get(), member, attempt)) {
        attempt.fail();
        return false;
    }
    descriptor_sets.push_back(MemberDescriptorSet{member, before->names});
    return attempt.checkpoint();
}

std::optional<FdinfoPass> read_fdinfo_pass(
    int proc_root_fd,
    const lemon::utils::ProcessContainmentSnapshot &snapshot,
    std::string_view target_pdev, ReadAttempt &attempt) {
    FdinfoPass pass;
    std::size_t remaining_descriptors =
        maximum_fdinfo_descriptors_per_pass;
    for (const auto &member : snapshot.members) {
        if (member.boot_id != snapshot.identity.boot_id ||
            !read_member_fdinfo(proc_root_fd, member, target_pdev,
                                pass.clients, pass.descriptor_sets,
                                remaining_descriptors, attempt)) {
            attempt.fail();
            return std::nullopt;
        }
        const auto member_descriptor_count =
            pass.descriptor_sets.back().descriptors.size();
        if (member_descriptor_count > remaining_descriptors) {
            attempt.fail();
            return std::nullopt;
        }
        remaining_descriptors -= member_descriptor_count;
    }
    if (!attempt.checkpoint()) return std::nullopt;
    return pass;
}

std::optional<std::uint64_t> read_global_gtt(int sysfs_device_fd,
                                             ReadAttempt &attempt) {
    const auto bytes = read_bounded_file_at(
        sysfs_device_fd, "mem_info_gtt_used",
        maximum_sysfs_attribute_bytes, attempt);
    if (!bytes) return std::nullopt;
    const auto value = strip_one_trailing_newline(*bytes);
    if (value.find('\n') != std::string_view::npos) {
        attempt.fail();
        return std::nullopt;
    }
    const auto parsed = parse_canonical_u64(value);
    if (!parsed) attempt.fail();
    return parsed;
}

std::optional<lemon::utils::ProcessContainmentSnapshot> take_snapshot(
    lemon::utils::PreparedProcessContainment &containment,
    ReadAttempt &attempt) {
    if (!attempt.checkpoint()) return std::nullopt;
    auto result = lemon::utils::ProcessManager::snapshot_process_containment(
        containment, attempt.operation_control());
    if (result.status == lemon::utils::ProcessContainmentStatus::Cancelled) {
        attempt.cancel();
        return std::nullopt;
    }
    if (!result.succeeded() || !result.snapshot) {
        attempt.fail();
        return std::nullopt;
    }
    if (!attempt.checkpoint()) return std::nullopt;
    return std::move(*result.snapshot);
}

} // namespace

class LinuxAmdGttPointObservationSource::Impl {
public:
    Impl(LinuxAmdGttPointObservationSourceBinding binding,
         lemon::utils::PreparedProcessContainment &process_containment,
         TestEnvironment environment)
        : binding_(std::move(binding)),
          process_containment_(process_containment),
          environment_(std::move(environment)) {
        initialize();
    }

    ProfilingRawReadResult
    read(const ProfilingRawReadRequest &request,
         const ProfilingCancellationCheck &should_abort) {
        try {
            if (!initialized_ || !request_is_valid(request)) {
                return unavailable_result();
            }
            if (should_abort && should_abort()) return cancelled_result();
            if (!environment_.monotonic_now) return unavailable_result();
            const auto started = environment_.monotonic_now();
            const auto duration =
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    binding_.max_read_duration);
            if (duration <= std::chrono::steady_clock::duration::zero() ||
                started.time_since_epoch() >
                    std::chrono::steady_clock::time_point::max()
                            .time_since_epoch() -
                        duration) {
                return unavailable_result();
            }
            ReadAttempt attempt(started + duration,
                                environment_.monotonic_now, should_abort);
            if (!attempt.checkpoint()) return result_for(attempt.status());
            std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
            if (!lock.owns_lock()) return unavailable_result();
            if (!process_containment_.active() || !proc_root_identity_ ||
                !sysfs_device_identity_) {
                return unavailable_result();
            }
            if (!directory_still_named(
                    environment_.proc_root, proc_root_fd_.get(),
                    *proc_root_identity_, expected_proc_filesystem_magic_,
                    attempt) ||
                !validate_device(attempt)) {
                return result_for(attempt.status());
            }

            const auto opening =
                take_snapshot(process_containment_, attempt);
            if (!opening ||
                opening->identity != binding_.process_containment_identity ||
                opening->generation == 0) {
                attempt.fail();
                return result_for(attempt.status());
            }
            if (!notify_boundary(ReadBoundary::AfterOpeningSnapshot,
                                 attempt)) {
                return result_for(attempt.status());
            }
            const auto first_pass = read_fdinfo_pass(
                proc_root_fd_.get(), *opening, binding_.drm_pdev, attempt);
            if (!first_pass) return result_for(attempt.status());
            if (!notify_boundary(ReadBoundary::AfterFirstFdinfoPass,
                                 attempt)) {
                return result_for(attempt.status());
            }
            const auto global =
                read_global_gtt(sysfs_device_fd_.get(), attempt);
            if (!global) return result_for(attempt.status());
            if (!notify_boundary(ReadBoundary::AfterGlobalPoint, attempt)) {
                return result_for(attempt.status());
            }
            const auto second_pass = read_fdinfo_pass(
                proc_root_fd_.get(), *opening, binding_.drm_pdev, attempt);
            if (!second_pass) return result_for(attempt.status());
            if (!notify_boundary(ReadBoundary::AfterSecondFdinfoPass,
                                 attempt)) {
                return result_for(attempt.status());
            }

            const auto closing =
                take_snapshot(process_containment_, attempt);
            if (!closing) return result_for(attempt.status());
            if (!notify_boundary(ReadBoundary::AfterClosingSnapshot,
                                 attempt)) {
                return result_for(attempt.status());
            }
            if (closing->identity !=
                    binding_.process_containment_identity ||
                closing->identity != opening->identity ||
                closing->members != opening->members ||
                closing->generation <= opening->generation ||
                !(*first_pass == *second_pass)) {
                attempt.fail();
                return result_for(attempt.status());
            }
            const std::uint64_t target =
                second_pass->clients.empty()
                    ? 0
                    : second_pass->clients.begin()->second.resident_gtt;
            if (*global < target || !attempt.checkpoint()) {
                if (attempt.status() == AttemptStatus::Active) attempt.fail();
                return result_for(attempt.status());
            }
            if (!directory_still_named(
                    environment_.proc_root, proc_root_fd_.get(),
                    *proc_root_identity_, expected_proc_filesystem_magic_,
                    attempt) ||
                !validate_device(attempt)) {
                return result_for(attempt.status());
            }

            return {ProfilingSourceError::None,
                    {{std::string(linux_amd_gtt_point_sensor_id),
                      std::nullopt, *global, closing->generation},
                     {std::string(linux_amd_gtt_point_sensor_id),
                      owner_binding_->owner_scope_id, target,
                      closing->generation}},
                    *owner_scope_set_sha256_,
                    {}};
        } catch (...) {
            return unavailable_result();
        }
    }

private:
    void initialize() noexcept {
        try {
            if (!path_is_usable(binding_.sysfs_device_directory) ||
                !path_is_usable(environment_.proc_root) ||
                !canonical_pci_bdf(binding_.drm_pdev) ||
                binding_.max_read_duration <=
                    std::chrono::milliseconds::zero() ||
                binding_.max_read_duration >
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        maximum_read_duration)) {
                return;
            }

            owner_binding_ = profiling_owner_scope_binding(
                binding_.process_containment_identity);
            if (!owner_binding_) return;
            owner_scope_set_sha256_ =
                profiling_owner_scope_set_sha256({*owner_binding_});
            if (!owner_scope_set_sha256_) return;

            proc_root_fd_ = open_directory(environment_.proc_root);
            sysfs_device_fd_ =
                open_directory(binding_.sysfs_device_directory);
            if (!proc_root_fd_ || !sysfs_device_fd_) return;
            proc_root_identity_ = directory_identity(proc_root_fd_.get());
            sysfs_device_identity_ =
                directory_identity(sysfs_device_fd_.get());
            const auto proc_magic = filesystem_magic(proc_root_fd_.get());
            const auto sysfs_magic =
                filesystem_magic(sysfs_device_fd_.get());
            const long expected_proc_magic =
                environment_.expected_proc_filesystem_magic.value_or(
                    static_cast<long>(PROC_SUPER_MAGIC));
            const long expected_sysfs_magic =
                environment_.expected_sysfs_filesystem_magic.value_or(
                    static_cast<long>(SYSFS_MAGIC));
            if (!proc_root_identity_ || !sysfs_device_identity_ ||
                !proc_magic || !sysfs_magic ||
                *proc_magic != expected_proc_magic ||
                *sysfs_magic != expected_sysfs_magic) {
                return;
            }
            expected_proc_filesystem_magic_ = expected_proc_magic;
            expected_sysfs_filesystem_magic_ = expected_sysfs_magic;
            initialized_ = true;
        } catch (...) {
            initialized_ = false;
        }
    }

    bool request_is_valid(const ProfilingRawReadRequest &request) const {
        return owner_binding_ && owner_scope_set_sha256_ &&
               request.sensor_ids.size() == 1 &&
               request.sensor_ids[0] == linux_amd_gtt_point_sensor_id &&
               request.owner_scope_ids.size() == 1 &&
               request.owner_scope_ids[0] == owner_binding_->owner_scope_id &&
               request.owner_scope_set_sha256 == *owner_scope_set_sha256_;
    }

    static ProfilingRawReadResult result_for(AttemptStatus status) {
        return status == AttemptStatus::Cancelled ? cancelled_result()
                                                  : unavailable_result();
    }

    bool notify_boundary(ReadBoundary boundary, ReadAttempt &attempt) {
        if (environment_.on_read_boundary) {
            environment_.on_read_boundary(boundary);
        }
        return attempt.checkpoint();
    }

    bool validate_device(ReadAttempt &attempt) {
        if (!directory_still_named(
                binding_.sysfs_device_directory, sysfs_device_fd_.get(),
                *sysfs_device_identity_, expected_sysfs_filesystem_magic_,
                attempt)) {
            return false;
        }
        const auto vendor = read_bounded_file_at(
            sysfs_device_fd_.get(), "vendor", 32, attempt);
        const auto uevent = read_bounded_file_at(
            sysfs_device_fd_.get(), "uevent",
            maximum_sysfs_attribute_bytes, attempt);
        if (!vendor || !uevent ||
            strip_one_trailing_newline(*vendor) != "0x1002" ||
            !uevent_matches(*uevent, binding_.drm_pdev)) {
            attempt.fail();
            return false;
        }
        return attempt.checkpoint();
    }

    LinuxAmdGttPointObservationSourceBinding binding_;
    lemon::utils::PreparedProcessContainment &process_containment_;
    TestEnvironment environment_;
    std::optional<ProfilingOwnerScopeBinding> owner_binding_;
    std::optional<std::string> owner_scope_set_sha256_;
    ScopedFd proc_root_fd_;
    ScopedFd sysfs_device_fd_;
    std::optional<DirectoryIdentity> proc_root_identity_;
    std::optional<DirectoryIdentity> sysfs_device_identity_;
    long expected_proc_filesystem_magic_ = 0;
    long expected_sysfs_filesystem_magic_ = 0;
    bool initialized_ = false;
    std::mutex mutex_;
};

LinuxAmdGttPointObservationSource::LinuxAmdGttPointObservationSource(
    LinuxAmdGttPointObservationSourceBinding binding,
    lemon::utils::PreparedProcessContainment &process_containment)
    : LinuxAmdGttPointObservationSource(
          std::move(binding), process_containment,
          TestEnvironment{
              "/proc",
              [] { return std::chrono::steady_clock::now(); },
              static_cast<long>(PROC_SUPER_MAGIC),
              static_cast<long>(SYSFS_MAGIC),
              {},
          }) {}

LinuxAmdGttPointObservationSource::LinuxAmdGttPointObservationSource(
    LinuxAmdGttPointObservationSourceBinding binding,
    lemon::utils::PreparedProcessContainment &process_containment,
    TestEnvironment environment)
    : impl_(std::make_unique<Impl>(std::move(binding), process_containment,
                                   std::move(environment))) {}

LinuxAmdGttPointObservationSource::~LinuxAmdGttPointObservationSource() =
    default;

ProfilingRawReadResult LinuxAmdGttPointObservationSource::read(
    const ProfilingRawReadRequest &request,
    const ProfilingCancellationCheck &should_abort) {
    try {
        return impl_->read(request, should_abort);
    } catch (...) {
        return {ProfilingSourceError::Unavailable,
                {},
                {},
                "linux AMD GTT point observation is unavailable"};
    }
}

} // namespace lemon::residency::internal
