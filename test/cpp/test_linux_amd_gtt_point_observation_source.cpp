#include <lemon/residency/profiling_provider.h>
#include <lemon/utils/process_containment.h>
#include <lemon/utils/process_manager.h>

#include "platform/linux_amd_gtt_point_observation_source.h"
#include "platform/process_containment_platform.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <stdlib.h>
#include <sys/inotify.h>
#include <sys/vfs.h>
#include <unistd.h>

namespace lemon::utils {

struct ProcessContainmentTestHook {
    static PreparedProcessContainment make(
        std::unique_ptr<internal::ProcessContainmentState> state) {
        return PreparedProcessContainment(std::move(state));
    }
};

} // namespace lemon::utils

namespace lemon::residency::internal {

struct LinuxAmdGttPointObservationSourceTestHook {
    using ReadBoundary = LinuxAmdGttPointObservationSource::ReadBoundary;

    static LinuxAmdGttPointObservationSource make(
        LinuxAmdGttPointObservationSourceBinding binding,
        utils::PreparedProcessContainment &containment,
        std::filesystem::path proc_root,
        std::function<std::chrono::steady_clock::time_point()> monotonic_now,
        std::optional<long> expected_proc_filesystem_magic,
        std::optional<long> expected_sysfs_filesystem_magic,
        std::function<void(ReadBoundary)> on_read_boundary = {}) {
        return LinuxAmdGttPointObservationSource(
            std::move(binding), containment,
            LinuxAmdGttPointObservationSource::TestEnvironment{
                std::move(proc_root), std::move(monotonic_now),
                expected_proc_filesystem_magic,
                expected_sysfs_filesystem_magic,
                std::move(on_read_boundary)});
    }
};

} // namespace lemon::residency::internal

namespace {

using namespace std::chrono_literals;
using lemon::residency::ClaimCompleteness;
using lemon::residency::ClaimFamily;
using lemon::residency::ClaimFamilyClosure;
using lemon::residency::ClaimUnit;
using lemon::residency::ProfilingCancellationCheck;
using lemon::residency::ProfilingCollectionClock;
using lemon::residency::ProfilingCollectionStatus;
using lemon::residency::ProfilingDerivationContract;
using lemon::residency::ProfilingObservationCollector;
using lemon::residency::ProfilingObservationSource;
using lemon::residency::ProfilingRawReadRequest;
using lemon::residency::ProfilingRawReadResult;
using lemon::residency::ProfilingSensorContract;
using lemon::residency::ProfilingSourceError;
using lemon::residency::ProfilingTransactionContext;
using lemon::residency::internal::LinuxAmdGttPointObservationSource;
using lemon::residency::internal::LinuxAmdGttPointObservationSourceBinding;
using lemon::residency::internal::LinuxAmdGttPointObservationSourceTestHook;
using lemon::residency::internal::linux_amd_gtt_point_sensor_id;
using lemon::residency::profiling_owner_scope_binding;
using lemon::residency::profiling_owner_scope_set_sha256;
using lemon::residency::profiling_derivation_contract_sha256;
using lemon::utils::PreparedProcessContainment;
using lemon::utils::ProcessBirthIdentity;
using lemon::utils::ProcessContainmentIdentity;
using lemon::utils::ProcessContainmentOperationControl;
using lemon::utils::ProcessContainmentOperationResult;
using lemon::utils::ProcessContainmentSnapshot;
using lemon::utils::ProcessContainmentSnapshotResult;
using lemon::utils::ProcessContainmentStartResult;
using lemon::utils::ProcessContainmentStatus;
using lemon::utils::ProcessContainmentTestHook;
using lemon::utils::ProcessManager;

using ReadBoundary = LinuxAmdGttPointObservationSourceTestHook::ReadBoundary;

static_assert(std::is_final_v<LinuxAmdGttPointObservationSource>);
static_assert(
    std::is_base_of_v<ProfilingObservationSource,
                      LinuxAmdGttPointObservationSource>);
static_assert(!std::is_copy_constructible_v<
              LinuxAmdGttPointObservationSource>);
static_assert(!std::is_copy_assignable_v<
              LinuxAmdGttPointObservationSource>);
static_assert(!std::is_move_constructible_v<
              LinuxAmdGttPointObservationSource>);
static_assert(!std::is_move_assignable_v<
              LinuxAmdGttPointObservationSource>);
static_assert(std::is_constructible_v<
              LinuxAmdGttPointObservationSource,
              LinuxAmdGttPointObservationSourceBinding,
              PreparedProcessContainment &>);
static_assert(linux_amd_gtt_point_sensor_id ==
              std::string_view{
                  "linux.amdgpu.mem_info_gtt_used.bytes.v1"});

constexpr std::string_view kExpectedContainmentDigest =
    "af6902e41989bbf69dfea988995f278c73234df08db107175ee70a3015925bf6";
constexpr std::string_view kExpectedOwnerSetDigest =
    "47d48a09485723ad082ab990cedb11c52d2c7e9572f8fbc0b9bf7edd8bbdc25f";
constexpr std::string_view kPdev = "0000:c6:00.0";
constexpr int kPid = 4312;

struct TestState {
    int failures = 0;

    void require(bool condition, const char *message) {
        if (condition) return;
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
};

[[noreturn]] void setup_failure(const char *message) {
    throw std::runtime_error(message);
}

std::function<void(ReadBoundary)> at_boundary(
    ReadBoundary wanted, std::function<void()> action) {
    return [wanted, action = std::move(action)](ReadBoundary observed) {
        if (observed == wanted) action();
    };
}

void write_file(const std::filesystem::path &path,
                std::string_view contents) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) setup_failure("could not create fixture directory");
    std::filesystem::remove_all(path, error);
    if (error) setup_failure("could not replace fixture path");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) setup_failure("could not open fixture file");
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!output) setup_failure("could not write fixture file");
}

class FileReadCounter {
public:
    FileReadCounter(const std::filesystem::path &directory,
                    std::string filename)
        : filename_(std::move(filename)),
          descriptor_(::inotify_init1(IN_CLOEXEC | IN_NONBLOCK)) {
        if (descriptor_ < 0 ||
            ::inotify_add_watch(descriptor_, directory.c_str(),
                                IN_OPEN | IN_CLOSE_NOWRITE |
                                    IN_CLOSE_WRITE) < 0) {
            if (descriptor_ >= 0) ::close(descriptor_);
            setup_failure("could not watch fixture file reads");
        }
    }

    FileReadCounter(const FileReadCounter &) = delete;
    FileReadCounter &operator=(const FileReadCounter &) = delete;

    ~FileReadCounter() { ::close(descriptor_); }

    std::size_t consume() const {
        std::size_t count = 0;
        alignas(inotify_event) std::array<char, 4096> buffer{};
        while (true) {
            const auto bytes = ::read(descriptor_, buffer.data(),
                                      buffer.size());
            if (bytes < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                setup_failure("could not read fixture file events");
            }
            if (bytes == 0) break;
            std::size_t offset = 0;
            const auto size = static_cast<std::size_t>(bytes);
            while (offset < size) {
                const auto *event = reinterpret_cast<const inotify_event *>(
                    buffer.data() + offset);
                if ((event->mask & IN_Q_OVERFLOW) != 0) {
                    setup_failure("fixture file event queue overflowed");
                }
                if ((event->mask & IN_CLOSE_NOWRITE) != 0 &&
                    event->len != 0 &&
                    std::string_view(event->name) == filename_) {
                    ++count;
                }
                offset += sizeof(inotify_event) +
                          static_cast<std::size_t>(event->len);
            }
        }
        return count;
    }

private:
    std::string filename_;
    int descriptor_ = -1;
};

long filesystem_magic(const std::filesystem::path &path) {
    struct statfs status {};
    if (::statfs(path.c_str(), &status) != 0) {
        setup_failure("could not identify fixture filesystem");
    }
    return status.f_type;
}

class TempTree {
public:
    TempTree() {
        std::error_code error;
        const auto temporary_root =
            std::filesystem::temp_directory_path(error);
        if (error || temporary_root.empty() ||
            !temporary_root.is_absolute()) {
            setup_failure("temporary directory root is unavailable");
        }
        std::string pattern =
            (temporary_root / "lemonade-amd-gtt-point-XXXXXX").string();
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        char *const created = ::mkdtemp(buffer.data());
        if (created == nullptr) {
            setup_failure("could not create isolated fixture directory");
        }
        root_ = std::filesystem::path(created);
    }

    TempTree(const TempTree &) = delete;
    TempTree &operator=(const TempTree &) = delete;

    ~TempTree() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    const std::filesystem::path &root() const { return root_; }

private:
    std::filesystem::path root_;
};

ProcessContainmentIdentity containment_identity() {
    return {"01234567-89ab-cdef-0123-456789abcdef", 19, 23, 29,
            "owner/model-alpha",
            std::string(64, 'a')};
}

ProcessBirthIdentity birth(int pid = kPid,
                           std::uint64_t start_time = 997) {
    return {"01234567-89ab-cdef-0123-456789abcdef", pid, start_time};
}

ProcessContainmentSnapshotResult successful_snapshot(
    const ProcessContainmentIdentity &identity,
    std::uint64_t generation,
    std::vector<ProcessBirthIdentity> members) {
    return {ProcessContainmentStatus::Success,
            {},
            ProcessContainmentSnapshot{
                identity, generation, std::move(members)}};
}

ProcessContainmentSnapshotResult failed_snapshot(
    ProcessContainmentStatus status,
    std::string diagnostic) {
    return {status, std::move(diagnostic), std::nullopt};
}

std::string process_stat(int pid, std::uint64_t start_time) {
    return std::to_string(pid) +
           " (backend worker) R 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 "
           "17 18 " +
           std::to_string(start_time) + " 0\n";
}

struct FakeContainmentControl {
    bool active = true;
    std::atomic<std::size_t> snapshot_calls{0};
    std::deque<ProcessContainmentSnapshotResult> snapshots;
    std::vector<ProcessContainmentOperationControl> snapshot_controls;
    std::mutex snapshots_mutex;
};

class FakeProcessContainmentState final
    : public lemon::utils::internal::ProcessContainmentState {
public:
    explicit FakeProcessContainmentState(
        std::shared_ptr<FakeContainmentControl> control)
        : control_(std::move(control)) {}

    bool active() const noexcept override { return control_->active; }

    ProcessContainmentStartResult start(
        const std::string &,
        const std::vector<std::string> &,
        const ProcessContainmentOperationControl &,
        const std::string &,
        bool,
        bool,
        const std::vector<std::pair<std::string, std::string>> &) override {
        return {ProcessContainmentStatus::Failed,
                "unexpected start", std::nullopt, std::nullopt};
    }

    ProcessContainmentSnapshotResult snapshot(
        const ProcessContainmentOperationControl &operation_control) override {
        std::lock_guard<std::mutex> lock(control_->snapshots_mutex);
        control_->snapshot_calls.fetch_add(1, std::memory_order_relaxed);
        control_->snapshot_controls.push_back(operation_control);
        if (control_->snapshots.empty()) {
            return {ProcessContainmentStatus::Failed,
                    "unexpected snapshot", std::nullopt};
        }
        auto result = std::move(control_->snapshots.front());
        control_->snapshots.pop_front();
        return result;
    }

    ProcessContainmentOperationResult
    kill(std::chrono::milliseconds) override {
        return {ProcessContainmentStatus::Failed, "unexpected kill"};
    }

    ProcessContainmentOperationResult release(
        const ProcessContainmentOperationControl &) override {
        return {ProcessContainmentStatus::Failed, "unexpected release"};
    }

private:
    std::shared_ptr<FakeContainmentControl> control_;
};

PreparedProcessContainment make_prepared(
    const std::shared_ptr<FakeContainmentControl> &control) {
    return ProcessContainmentTestHook::make(
        std::make_unique<FakeProcessContainmentState>(control));
}

std::string fdinfo(std::string_view resident,
                   std::string_view shared = "drm-shared-gtt:\t0 KiB\n",
                   std::string_view client = "7",
                   std::string_view pdev = kPdev,
                   std::string_view driver = "amdgpu") {
    std::string value =
        "pos:\t0\nflags:\t0100002\nmnt_id:\t24\n"
        "drm-driver:\t";
    value += driver;
    value += "\n"
        "drm-client-id:\t";
    value += client;
    value += "\ndrm-pdev:\t";
    value += pdev;
    value += '\n';
    value += resident;
    value += shared;
    return value;
}

class Scenario {
public:
    struct ReadOptions {
        std::optional<ProfilingRawReadRequest> request_override;
        ProfilingCancellationCheck cancellation_check = [] { return false; };
        std::optional<LinuxAmdGttPointObservationSourceBinding>
            source_binding;
        std::function<std::chrono::steady_clock::time_point()> monotonic_now;
        std::optional<long> expected_proc_filesystem_magic;
        std::optional<long> expected_sysfs_filesystem_magic;
        std::function<void(ReadBoundary)> on_read_boundary;
    };

    Scenario()
        : proc_root(tree.root() / "proc"),
          device_directory(tree.root() / "sys" / std::string(kPdev)),
          identity(containment_identity()),
          control(std::make_shared<FakeContainmentControl>()),
          containment(make_prepared(control)) {
        add_member(kPid, 997);
        std::error_code error;
        std::filesystem::create_directories(device_directory, error);
        if (error) setup_failure("could not create fake sysfs tree");
        write_file(device_directory / "vendor", "0x1002\n");
        write_file(device_directory / "uevent",
                   "DRIVER=amdgpu\nPCI_SLOT_NAME=0000:c6:00.0\n");
        write_file(device_directory / "mem_info_gtt_used", "16384\n");
        reset_snapshots();
        const auto owner = profiling_owner_scope_binding(identity);
        if (!owner) setup_failure("fixture identity is invalid");
        const auto owner_set = profiling_owner_scope_set_sha256({*owner});
        if (!owner_set) setup_failure("fixture owner set is invalid");
        request = {{std::string(linux_amd_gtt_point_sensor_id)},
                   {identity.owner_scope_id}, *owner_set};
    }

    LinuxAmdGttPointObservationSourceBinding binding() const {
        return {device_directory, std::string(kPdev), identity, 50ms};
    }

    void reset_snapshots(
        ProcessContainmentIdentity snapshot_identity =
            containment_identity(),
        std::vector<ProcessBirthIdentity> members = {birth()}) {
        control->snapshots.clear();
        control->snapshot_controls.clear();
        control->snapshot_calls.store(0, std::memory_order_relaxed);
        control->snapshots.push_back(successful_snapshot(
            snapshot_identity, 71, members));
        control->snapshots.push_back(successful_snapshot(
            snapshot_identity, 72, std::move(members)));
    }

    void add_member(int pid, std::uint64_t start_time) {
        std::error_code error;
        std::filesystem::create_directories(
            proc_root / std::to_string(pid) / "fdinfo", error);
        if (error) setup_failure("could not create fake proc member");
        write_file(proc_root / std::to_string(pid) / "stat",
                   process_stat(pid, start_time));
    }

    void write_fd(int fd, std::string_view contents, int pid = kPid) {
        write_file(proc_root / std::to_string(pid) / "fdinfo" /
                       std::to_string(fd),
                   contents);
    }

    void write_ignored_fds(std::size_t count, int pid = kPid) {
        if (count == 0) return;
        const auto directory =
            proc_root / std::to_string(pid) / "fdinfo";
        const auto first = directory / "3";
        write_file(first, "pos:\t0\nflags:\t0100002\n");
        for (std::size_t index = 1; index < count; ++index) {
            std::error_code error;
            const auto fd = 3U + static_cast<std::uint64_t>(index);
            std::filesystem::create_hard_link(
                first, directory / std::to_string(fd), error);
            if (error) setup_failure("could not link fixture fdinfo entry");
        }
    }

    ProfilingRawReadResult read(ReadOptions options) {
        if (!options.monotonic_now) {
            const auto base = std::chrono::steady_clock::now();
            options.monotonic_now = [base] { return base; };
        }
        if (!options.expected_proc_filesystem_magic) {
            options.expected_proc_filesystem_magic =
                filesystem_magic(proc_root);
        }
        if (!options.expected_sysfs_filesystem_magic) {
            options.expected_sysfs_filesystem_magic =
                filesystem_magic(device_directory);
        }
        auto source = LinuxAmdGttPointObservationSourceTestHook::make(
            options.source_binding ? std::move(*options.source_binding)
                                   : binding(),
            containment, proc_root, std::move(options.monotonic_now),
            options.expected_proc_filesystem_magic,
            options.expected_sysfs_filesystem_magic,
            std::move(options.on_read_boundary));
        const auto &read_request = options.request_override
                                       ? *options.request_override
                                       : request;
        return source.read(read_request, options.cancellation_check);
    }

    ProfilingRawReadResult read() { return read(ReadOptions{}); }

    ProfilingRawReadResult read(ProfilingRawReadRequest read_request) {
        ReadOptions options;
        options.request_override = std::move(read_request);
        return read(std::move(options));
    }

    TempTree tree;
    std::filesystem::path proc_root;
    std::filesystem::path device_directory;
    ProcessContainmentIdentity identity;
    std::shared_ptr<FakeContainmentControl> control;
    PreparedProcessContainment containment;
    ProfilingRawReadRequest request;
};

Scenario::ReadOptions read_options_at_boundary(
    ReadBoundary boundary, std::function<void()> action) {
    Scenario::ReadOptions options;
    options.on_read_boundary =
        at_boundary(boundary, std::move(action));
    return options;
}

void require_success(TestState &state,
                     const ProfilingRawReadResult &result,
                     std::uint64_t target,
                     const char *message,
                     std::uint64_t global = 16384) {
    const bool matches =
        result.error == ProfilingSourceError::None &&
        result.samples.size() == 2 &&
        result.samples[0].sensor_id == linux_amd_gtt_point_sensor_id &&
        !result.samples[0].owner_scope_id &&
        result.samples[0].value == global &&
        result.samples[0].source_generation == 72 &&
        result.samples[1].sensor_id == linux_amd_gtt_point_sensor_id &&
        result.samples[1].owner_scope_id ==
            std::optional<std::string>{"owner/model-alpha"} &&
        result.samples[1].value == target &&
        result.samples[1].source_generation == 72 &&
        result.owner_scope_set_sha256 == kExpectedOwnerSetDigest &&
        result.diagnostic.empty();
    state.require(matches, message);
}

void require_unavailable(TestState &state,
                         const ProfilingRawReadResult &result,
                         const std::filesystem::path &fixture_root,
                         const char *message) {
    const bool path_free =
        result.diagnostic.find(fixture_root.string()) == std::string::npos &&
        result.diagnostic.find("/proc/") == std::string::npos &&
        result.diagnostic.find("/sys/") == std::string::npos;
    state.require(
        result.error == ProfilingSourceError::Unavailable &&
            result.samples.empty() && result.owner_scope_set_sha256.empty() &&
            !result.diagnostic.empty() &&
            result.diagnostic.size() <=
                lemon::residency::max_local_overlay_diagnostic_bytes &&
            path_free,
        message);
}

void test_binding_identity(TestState &state) {
    const auto identity = containment_identity();
    const auto binding = profiling_owner_scope_binding(identity);
    state.require(binding.has_value() &&
                      binding->owner_scope_id == identity.owner_scope_id &&
                      binding->containment_identity_sha256 ==
                          kExpectedContainmentDigest,
                  "containment binding uses the exact canonical digest");
    if (!binding) return;
    const auto owner_set = profiling_owner_scope_set_sha256({*binding});
    state.require(owner_set.has_value() &&
                      *owner_set == kExpectedOwnerSetDigest,
                  "the exact one-owner request digest is canonical");
}

void test_binding_is_acquired_from_a_normalized_snapshot(TestState &state) {
    Scenario scenario;
    scenario.write_fd(3, fdinfo("drm-resident-gtt:\t4 KiB\n"));
    const auto acquired = ProcessManager::snapshot_process_containment(
        scenario.containment,
        {std::chrono::steady_clock::now() + 5s, {}});
    state.require(acquired.succeeded() && acquired.snapshot.has_value(),
                  "a caller acquires a normalized containment snapshot");
    if (!acquired.snapshot) return;

    const auto owner =
        profiling_owner_scope_binding(acquired.snapshot->identity);
    const auto owner_set =
        owner ? profiling_owner_scope_set_sha256({*owner}) : std::nullopt;
    state.require(owner.has_value() && owner_set.has_value(),
                  "the acquired identity derives the owner request binding");
    if (!owner || !owner_set) return;

    ProfilingRawReadRequest request{
        {std::string(linux_amd_gtt_point_sensor_id)},
        {owner->owner_scope_id},
        *owner_set,
    };
    LinuxAmdGttPointObservationSourceBinding source_binding{
        scenario.device_directory,
        std::string(kPdev),
        acquired.snapshot->identity,
        50ms,
    };
    scenario.reset_snapshots(acquired.snapshot->identity,
                             acquired.snapshot->members);
    Scenario::ReadOptions options;
    options.request_override = std::move(request);
    options.source_binding = std::move(source_binding);
    require_success(
        state, scenario.read(std::move(options)),
        4096,
        "the source consumes the concrete acquired identity without digest echo");
}

void test_request_and_basic_reads(TestState &state) {
    {
        Scenario scenario;
        scenario.write_fd(3, fdinfo("drm-resident-gtt:\t4 KiB\n"));
        require_success(state, scenario.read(), 4096,
                        "a stable owner client produces two exact samples");
    }
    {
        Scenario scenario;
        require_success(state, scenario.read(), 0,
                        "two complete empty passes prove target zero");
    }
    {
        Scenario scenario;
        scenario.reset_snapshots(
            scenario.identity, std::vector<ProcessBirthIdentity>{});
        write_file(scenario.device_directory / "mem_info_gtt_used", "0\n");
        require_success(
            state, scenario.read(), 0,
            "a stable genuinely empty containment returns physical and target zero",
            0);
    }
    {
        Scenario scenario;
        scenario.write_fd(3, fdinfo("drm-resident-gtt:\t4 KiB\n", ""));
        require_unavailable(state, scenario.read(), scenario.tree.root(),
                            "missing shared-GTT evidence fails closed");
    }
    {
        Scenario scenario;
        scenario.write_fd(
            3, fdinfo("drm-resident-gtt:\t4 KiB\n",
                      "drm-shared-gtt:\t1 KiB\n"));
        require_unavailable(state, scenario.read(), scenario.tree.root(),
                            "nonzero shared-GTT evidence fails closed");
    }

    const std::vector<std::function<void(ProfilingRawReadRequest &)>>
        request_mutations{
            [](auto &value) { value.sensor_ids.clear(); },
            [](auto &value) { value.sensor_ids.push_back(value.sensor_ids[0]); },
            [](auto &value) { value.sensor_ids.push_back("sensor/extra"); },
            [](auto &value) { value.sensor_ids[0] = "sensor/wrong"; },
            [](auto &value) { value.owner_scope_ids.clear(); },
            [](auto &value) {
                value.owner_scope_ids.push_back(value.owner_scope_ids[0]);
            },
            [](auto &value) {
                value.owner_scope_ids.push_back("owner/model-beta");
            },
            [](auto &value) {
                value.owner_scope_ids[0] = "owner/model-beta";
            },
            [](auto &value) {
                value.owner_scope_set_sha256 = std::string(64, 'f');
            },
        };
    for (const auto &mutate : request_mutations) {
        Scenario scenario;
        auto request = scenario.request;
        mutate(request);
        require_unavailable(state, scenario.read(std::move(request)),
                            scenario.tree.root(),
                            "non-exact requests fail closed and path-free");
        state.require(scenario.control->snapshot_calls == 0,
                      "invalid requests fail before containment access");
    }

    {
        Scenario scenario;
        auto other = scenario.identity;
        other.nonce_sha256 = std::string(64, 'b');
        const auto owner = profiling_owner_scope_binding(other);
        const auto owner_set =
            owner ? profiling_owner_scope_set_sha256({*owner}) : std::nullopt;
        if (!owner_set) setup_failure("alternate lease is invalid");
        auto request = scenario.request;
        request.owner_scope_set_sha256 = *owner_set;
        require_unavailable(
            state, scenario.read(std::move(request)), scenario.tree.root(),
            "another lease for the same owner cannot satisfy the request");
    }
    {
        Scenario scenario;
        auto other = scenario.identity;
        ++other.inode;
        scenario.reset_snapshots(other);
        require_unavailable(state, scenario.read(), scenario.tree.root(),
                            "a live lease identity mismatch fails closed");
    }
}

void test_fdinfo_units_aliases_and_cardinality(TestState &state) {
    struct SuccessCase {
        std::string contents;
        std::uint64_t expected;
        std::uint64_t global = 16384;
    };
    const std::vector<SuccessCase> success_cases{
        {fdinfo("drm-resident-gtt:\t4096\n"), 4096},
        {fdinfo("drm-resident-gtt:\t4 KiB\n"), 4096},
        {fdinfo("drm-resident-gtt:\t1 MiB\n"), 1048576, 2097152},
        {fdinfo("drm-memory-gtt:\t4 KiB\n"), 4096},
        {fdinfo("drm-resident-gtt:\t4096\n"
                "drm-memory-gtt:\t4 KiB\n"),
         4096},
    };
    for (const auto &test_case : success_cases) {
        Scenario scenario;
        scenario.write_fd(3, test_case.contents);
        if (test_case.global != 16384) {
            write_file(scenario.device_directory / "mem_info_gtt_used",
                       std::to_string(test_case.global) + "\n");
        }
        require_success(state, scenario.read(), test_case.expected,
                        "resident and legacy GTT units normalize to bytes",
                        test_case.global);
    }

    {
        Scenario scenario;
        const auto contents = fdinfo("drm-resident-gtt:\t4 KiB\n");
        scenario.write_fd(3, contents);
        scenario.write_fd(4, contents);
        require_success(state, scenario.read(), 4096,
                        "duplicate descriptors for one client count once");
    }
    {
        Scenario scenario;
        scenario.write_fd(3, fdinfo("drm-resident-gtt:\t4096\n"));
        scenario.write_fd(4, fdinfo("drm-memory-gtt:\t4 KiB\n"));
        require_success(
            state, scenario.read(), 4096,
            "duplicate client descriptors normalize resident and legacy forms");
    }
    {
        Scenario scenario;
        scenario.write_fd(3, fdinfo("drm-resident-gtt:\t4 KiB\n"));
        scenario.write_fd(4, fdinfo("drm-resident-gtt:\t8 KiB\n"));
        require_unavailable(
            state, scenario.read(), scenario.tree.root(),
            "duplicate client descriptors must report one coherent value");
    }
    {
        Scenario scenario;
        scenario.write_fd(3, fdinfo("drm-resident-gtt:\t4 KiB\n"));
        scenario.write_fd(4, fdinfo("drm-resident-gtt:\t4 KiB\n", ""));
        require_unavailable(
            state, scenario.read(), scenario.tree.root(),
            "missing shared evidence on one duplicate blocks the client");
    }
    {
        Scenario scenario;
        scenario.write_fd(3, fdinfo("drm-resident-gtt:\t4 KiB\n"));
        scenario.write_fd(
            4, fdinfo("drm-resident-gtt:\t4 KiB\n",
                      "drm-shared-gtt:\t1 KiB\n"));
        require_unavailable(
            state, scenario.read(), scenario.tree.root(),
            "nonzero shared evidence on one duplicate blocks the client");
    }
    {
        Scenario scenario;
        scenario.write_fd(3, fdinfo("drm-resident-gtt:\t4 KiB\n"));
        scenario.write_fd(4, fdinfo("drm-resident-gtt:\t4 KiB\n",
                                    "drm-shared-gtt:\t0 KiB\n", "8"));
        require_unavailable(state, scenario.read(), scenario.tree.root(),
                            "two distinct target clients fail closed");
    }

    const std::vector<std::string> failures{
        fdinfo("drm-resident-gtt:\t4096\n"
               "drm-memory-gtt:\t8 KiB\n"),
        fdinfo("drm-resident-gtt:\t4 KiB\n"
               "drm-resident-gtt:\t4 KiB\n"),
        fdinfo("drm-resident-gtt:\t+4 KiB\n"),
        fdinfo("drm-resident-gtt:\t-4 KiB\n"),
        fdinfo("drm-resident-gtt:\t4x KiB\n"),
        fdinfo("drm-resident-gtt:\t4 KB\n"),
        fdinfo("drm-resident-gtt:\t18446744073709551616\n"),
        fdinfo("drm-resident-gtt:\t18014398509481984 KiB\n"),
        fdinfo("drm-resident-gtt:\t4 KiB\n",
               "drm-shared-gtt:\t0 KiB\n"
               "drm-shared-gtt:\t0 KiB\n"),
        fdinfo("drm-resident-gtt:\t4 KiB\n",
               "drm-shared-gtt:\tzero KiB\n"),
    };
    for (const auto &contents : failures) {
        Scenario scenario;
        scenario.write_fd(3, contents);
        require_unavailable(
            state, scenario.read(), scenario.tree.root(),
            "duplicate, signed, junk, unit, and overflow input fails closed");
    }
}

void test_fdinfo_device_selection(TestState &state) {
    constexpr std::string_view other_pdev = "0000:c7:00.0";
    {
        Scenario scenario;
        scenario.write_fd(
            3, fdinfo("drm-resident-gtt:\t8 KiB\n",
                      "drm-shared-gtt:\t0 KiB\n", "7", other_pdev));
        require_success(state, scenario.read(), 0,
                        "a complete canonical other-device record is ignored");
    }
    {
        Scenario scenario;
        scenario.write_fd(3, fdinfo("drm-resident-gtt:\t4 KiB\n"));
        scenario.write_fd(
            4, fdinfo("drm-resident-gtt:\t8 KiB\n",
                      "drm-shared-gtt:\t0 KiB\n", "7", other_pdev));
        require_success(
            state, scenario.read(), 4096,
            "an equal client id on another device is not target deduplication");
    }
    {
        Scenario scenario;
        scenario.write_fd(
            3, fdinfo("drm-resident-gtt:\t4 KiB\n",
                      "drm-shared-gtt:\t0 KiB\n", "7", kPdev, "i915"));
        require_unavailable(
            state, scenario.read(), scenario.tree.root(),
            "the target pdev under a non-amdgpu record fails closed");
    }
    {
        Scenario scenario;
        scenario.write_fd(
            3,
            "drm-driver:\tamdgpu\n"
            "drm-pdev:\t0000:c6:00.0\n"
            "drm-client-id:\t7\n"
            "drm-resident-gtt\t4 KiB\n"
            "drm-shared-gtt:\t0 KiB\n");
        require_unavailable(
            state, scenario.read(), scenario.tree.root(),
            "a malformed target-device DRM record fails closed");
    }
    {
        Scenario scenario;
        scenario.write_fd(
            3, "pos:\t0\nflags:\t0100002\n"
               "drm-engine-render:\t100 ns\n");
        require_unavailable(
            state, scenario.read(), scenario.tree.root(),
            "an unclassifiable DRM-only record cannot prove target absence");
    }
    {
        Scenario scenario;
        scenario.write_fd(3, fdinfo("drm-resident-gtt:\t0\n", ""));
        require_unavailable(
            state, scenario.read(), scenario.tree.root(),
            "zero target residency still requires explicit shared evidence");
    }
}

void test_fdinfo_completeness_failures(TestState &state) {
    {
        Scenario scenario;
        scenario.write_fd(
            3, "pos:\t0\nflags:\t0100002\nmnt_id:\t24\n");
        require_success(
            state, scenario.read(), 0,
            "an ordinary non-DRM fdinfo record is ignored completely");
    }
    {
        Scenario scenario;
        std::error_code error;
        std::filesystem::remove_all(
            scenario.proc_root / std::to_string(kPid), error);
        if (error) setup_failure("could not remove fake member proc entry");
        require_unavailable(state, scenario.read(), scenario.tree.root(),
                            "a missing member proc entry cannot prove zero");
    }
    {
        Scenario scenario;
        const auto directory = scenario.proc_root / std::to_string(kPid) /
                               "fdinfo";
        write_file(directory, "not a directory\n");
        require_unavailable(state, scenario.read(), scenario.tree.root(),
                            "a failed fdinfo directory access blocks zero");
    }
    {
        Scenario scenario;
        const auto entry = scenario.proc_root / std::to_string(kPid) /
                           "fdinfo" / "3";
        std::error_code error;
        std::filesystem::create_directory(entry, error);
        if (error) setup_failure("could not create unreadable fdinfo entry");
        require_unavailable(state, scenario.read(), scenario.tree.root(),
                            "an unreadable fdinfo entry blocks zero");
    }
    {
        Scenario scenario;
        const auto entry = scenario.proc_root / std::to_string(kPid) /
                           "fdinfo" / "3";
        std::error_code error;
        std::filesystem::create_symlink(
            scenario.tree.root() / "missing-fdinfo", entry, error);
        if (error) setup_failure("could not create broken fdinfo entry");
        require_unavailable(state, scenario.read(), scenario.tree.root(),
                            "an fdinfo open failure blocks zero");
    }
    {
        Scenario scenario;
        scenario.write_fd(3, "drm-client-id:\t7\n");
        require_unavailable(state, scenario.read(), scenario.tree.root(),
                            "an incomplete DRM fdinfo record blocks zero");
    }
    {
        Scenario scenario;
        scenario.write_fd(3, std::string(1024 * 1024, 'x'));
        require_unavailable(state, scenario.read(), scenario.tree.root(),
                            "an overlong fdinfo record is bounded and rejected");
    }
}

void test_multiple_containment_members(TestState &state) {
    constexpr int second_pid = 4313;
    const std::vector<ProcessBirthIdentity> members{
        birth(), birth(second_pid, 998)};
    {
        Scenario scenario;
        scenario.add_member(second_pid, 998);
        scenario.reset_snapshots(scenario.identity, members);
        scenario.write_fd(3, fdinfo("drm-resident-gtt:\t4 KiB\n"));
        scenario.write_fd(4, fdinfo("drm-memory-gtt:\t4 KiB\n"),
                          second_pid);
        require_success(state, scenario.read(), 4096,
                        "one coherent client across two members counts once");
    }
    {
        Scenario scenario;
        scenario.add_member(second_pid, 998);
        scenario.reset_snapshots(scenario.identity, members);
        scenario.write_fd(3, fdinfo("drm-resident-gtt:\t4 KiB\n"));
        scenario.write_fd(
            4, fdinfo("drm-resident-gtt:\t4 KiB\n",
                      "drm-shared-gtt:\t0 KiB\n", "8"),
            second_pid);
        require_unavailable(state, scenario.read(), scenario.tree.root(),
                            "distinct clients across members fail closed");
    }
    {
        Scenario scenario;
        scenario.reset_snapshots(scenario.identity, members);
        require_unavailable(
            state, scenario.read(), scenario.tree.root(),
            "an inaccessible second containment member blocks known zero");
    }
    {
        Scenario scenario;
        scenario.add_member(second_pid, 998);
        scenario.reset_snapshots(
            scenario.identity, {birth(), birth(second_pid, 997)});
        require_unavailable(
            state, scenario.read(), scenario.tree.root(),
            "snapshot identity 997 cannot match proc start time 998");
    }
    {
        Scenario scenario;
        scenario.add_member(second_pid, 998);
        scenario.reset_snapshots(scenario.identity, members);
        require_unavailable(
            state,
            scenario.read(read_options_at_boundary(
                ReadBoundary::AfterGlobalPoint, [&scenario] {
                    write_file(scenario.proc_root / "4313" / "stat",
                               process_stat(4313, 999));
                })),
            scenario.tree.root(),
            "a member birth identity change between passes fails closed");
    }
}

void test_fdinfo_descriptor_limits(TestState &state) {
    constexpr std::size_t descriptors_per_pass = 4096;
    constexpr int second_pid = 4313;

    {
        Scenario scenario;
        scenario.write_ignored_fds(descriptors_per_pass);
        auto binding = scenario.binding();
        binding.max_read_duration = 30s;
        Scenario::ReadOptions options;
        options.source_binding = std::move(binding);
        require_success(
            state, scenario.read(std::move(options)), 0,
            "4096 descriptors per pass and 8192 per read are accepted");
    }
    {
        Scenario scenario;
        scenario.write_ignored_fds(descriptors_per_pass + 1);
        auto binding = scenario.binding();
        binding.max_read_duration = 30s;
        Scenario::ReadOptions options;
        options.source_binding = std::move(binding);
        require_unavailable(
            state, scenario.read(std::move(options)), scenario.tree.root(),
            "a 4097th descriptor in one pass fails closed atomically");
    }
    {
        Scenario scenario;
        scenario.write_ignored_fds(descriptors_per_pass / 2);
        scenario.add_member(second_pid, 998);
        scenario.write_ignored_fds(descriptors_per_pass / 2 + 1,
                                   second_pid);
        scenario.reset_snapshots(
            scenario.identity, {birth(), birth(second_pid, 998)});
        auto binding = scenario.binding();
        binding.max_read_duration = 30s;
        Scenario::ReadOptions options;
        options.source_binding = std::move(binding);
        require_unavailable(
            state, scenario.read(std::move(options)), scenario.tree.root(),
            "the per-pass descriptor cap is aggregate across members");
    }
}

void test_between_pass_churn_fails_closed(TestState &state) {
    const auto target_contents =
        fdinfo("drm-resident-gtt:\t4 KiB\n");
    const auto fd_path = [](Scenario &scenario, int fd) {
        return scenario.proc_root / std::to_string(kPid) / "fdinfo" /
               std::to_string(fd);
    };

    {
        Scenario scenario;
        require_unavailable(
            state,
            scenario.read(read_options_at_boundary(
                ReadBoundary::AfterGlobalPoint,
                [&scenario, &target_contents] {
                    scenario.write_fd(3, target_contents);
                })),
            scenario.tree.root(),
            "an empty-to-client transition cannot become known zero");
    }
    {
        Scenario scenario;
        scenario.write_fd(3, target_contents);
        require_unavailable(
            state,
            scenario.read(read_options_at_boundary(
                ReadBoundary::AfterGlobalPoint,
                [&scenario, &fd_path] {
                    std::error_code error;
                    if (!std::filesystem::remove(fd_path(scenario, 3), error) ||
                        error) {
                        setup_failure("could not remove churn fdinfo entry");
                    }
                })),
            scenario.tree.root(),
            "a client-to-empty transition fails closed");
    }
    {
        Scenario scenario;
        scenario.write_fd(3, target_contents);
        require_unavailable(
            state,
            scenario.read(read_options_at_boundary(
                ReadBoundary::AfterGlobalPoint,
                [&scenario, &fd_path] {
                    std::error_code error;
                    std::filesystem::rename(fd_path(scenario, 3),
                                            fd_path(scenario, 4), error);
                    if (error) setup_failure("could not rename churn fdinfo entry");
                })),
            scenario.tree.root(),
            "an FD replacement fails even for one coherent client");
    }
    {
        Scenario scenario;
        scenario.write_fd(3, target_contents);
        require_unavailable(
            state,
            scenario.read(read_options_at_boundary(
                ReadBoundary::AfterGlobalPoint,
                [&scenario, &target_contents] {
                    scenario.write_fd(4, target_contents);
                })),
            scenario.tree.root(),
            "an added duplicate FD fails between complete passes");
    }
    {
        Scenario scenario;
        scenario.write_fd(3, target_contents);
        require_unavailable(
            state,
            scenario.read(read_options_at_boundary(
                ReadBoundary::AfterGlobalPoint, [&scenario] {
                    scenario.write_fd(
                        3, fdinfo("drm-resident-gtt:\t4 KiB\n",
                                  "drm-shared-gtt:\t0 KiB\n", "8"));
                })),
            scenario.tree.root(),
            "a client-id transition fails closed");
    }
    {
        Scenario scenario;
        scenario.write_fd(3, target_contents);
        require_unavailable(
            state,
            scenario.read(read_options_at_boundary(
                ReadBoundary::AfterGlobalPoint, [&scenario] {
                    scenario.write_fd(
                        3, fdinfo("drm-resident-gtt:\t8 KiB\n"));
                })),
            scenario.tree.root(),
            "a normalized owner-value transition fails closed");
    }
    {
        Scenario scenario;
        scenario.write_fd(3, target_contents);
        require_unavailable(
            state,
            scenario.read(read_options_at_boundary(
                ReadBoundary::AfterGlobalPoint, [&scenario] {
                    scenario.write_fd(
                        3, fdinfo("drm-resident-gtt:\t4 KiB\n",
                                  "drm-shared-gtt:\t1 KiB\n"));
                })),
            scenario.tree.root(),
            "pass 2 revalidates zero shared-GTT evidence");
    }
}

void test_point_read_phase_order_and_single_global(TestState &state) {
    const std::vector<ReadBoundary> expected_order{
        ReadBoundary::AfterOpeningSnapshot,
        ReadBoundary::AfterFirstFdinfoPass,
        ReadBoundary::AfterGlobalPoint,
        ReadBoundary::AfterSecondFdinfoPass,
        ReadBoundary::AfterClosingSnapshot,
    };
    {
        Scenario scenario;
        scenario.write_fd(3, fdinfo("drm-resident-gtt:\t4 KiB\n"));
        std::vector<ReadBoundary> observed;
        Scenario::ReadOptions options;
        options.on_read_boundary = [&observed](ReadBoundary boundary) {
            observed.push_back(boundary);
        };
        const auto result = scenario.read(std::move(options));
        require_success(state, result, 4096,
                        "a point read closes one coherent five-phase sample");
        state.require(observed == expected_order,
                      "the point read uses opening, A, global, B, closing order");
    }
    {
        Scenario scenario;
        scenario.write_fd(3, fdinfo("drm-resident-gtt:\t4 KiB\n"));
        require_unavailable(
            state,
            scenario.read(read_options_at_boundary(
                ReadBoundary::AfterFirstFdinfoPass, [&scenario] {
                    scenario.write_fd(
                        3, fdinfo(
                               "drm-resident-gtt:\t8 KiB\n"));
                })),
            scenario.tree.root(),
            "target mutation immediately after pass A fails closed");
    }
    {
        Scenario scenario;
        scenario.write_fd(3, fdinfo("drm-resident-gtt:\t4 KiB\n"));
        require_unavailable(
            state,
            scenario.read(read_options_at_boundary(
                ReadBoundary::AfterGlobalPoint, [&scenario] {
                    scenario.write_fd(
                        3, fdinfo(
                               "drm-resident-gtt:\t8 KiB\n"));
                })),
            scenario.tree.root(),
            "target mutation immediately after the global point fails closed");
    }
    {
        Scenario scenario;
        scenario.write_fd(3, fdinfo("drm-resident-gtt:\t4 KiB\n"));
        const auto global_path =
            scenario.device_directory / "mem_info_gtt_used";
        write_file(global_path, "invalid-before-pass-a\n");
        FileReadCounter global_reads(scenario.device_directory,
                                     "mem_info_gtt_used");
        for (int calibration = 0; calibration < 2; ++calibration) {
            std::ifstream input(global_path, std::ios::binary);
            char byte = '\0';
            input.get(byte);
            if (!input) setup_failure("could not calibrate fixture read watch");
        }
        state.require(
            global_reads.consume() == 2,
            "the file-read observer distinguishes consecutive unread events");
        Scenario::ReadOptions options;
        options.on_read_boundary =
            [&global_path](ReadBoundary boundary) {
                if (boundary == ReadBoundary::AfterFirstFdinfoPass) {
                    write_file(global_path, "12288\n");
                } else if (boundary == ReadBoundary::AfterGlobalPoint) {
                    write_file(global_path, "invalid-after-global\n");
                }
            };
        const auto result = scenario.read(std::move(options));
        require_success(
            state, result, 4096,
            "the global value is sampled strictly between fdinfo passes",
            12288);
        state.require(
            global_reads.consume() == 1,
            "a point read opens the global sysfs counter exactly once");
    }
}

void test_global_counter_grammar(TestState &state) {
    const std::vector<std::string> failures{
        "",          "+16384\n", "-16384\n", "016384\n",
        "16384 \\n", "16384 B\n", "16384\n0\n",
        "18446744073709551616\n",
        std::string(1024 * 1024, '9'),
    };
    for (const auto &contents : failures) {
        Scenario scenario;
        write_file(scenario.device_directory / "mem_info_gtt_used",
                   contents);
        require_unavailable(
            state, scenario.read(), scenario.tree.root(),
            "the sysfs byte counter accepts only one bounded canonical value");
    }
    {
        Scenario scenario;
        scenario.write_fd(3, fdinfo("drm-resident-gtt:\t4 KiB\n"));
        write_file(scenario.device_directory / "mem_info_gtt_used", "0\n");
        require_unavailable(
            state, scenario.read(), scenario.tree.root(),
            "a target projection above the physical domain fails closed");
    }
}

void test_device_binding(TestState &state) {
    {
        Scenario scenario;
        auto binding = scenario.binding();
        binding.drm_pdev = "c6:00.0";
        Scenario::ReadOptions options;
        options.source_binding = std::move(binding);
        require_unavailable(
            state, scenario.read(std::move(options)),
            scenario.tree.root(), "a noncanonical PCI BDF fails closed");
    }
    {
        Scenario scenario;
        write_file(scenario.device_directory / "vendor", "0x10de\n");
        require_unavailable(state, scenario.read(), scenario.tree.root(),
                            "the retained device must be AMD");
    }
    {
        Scenario scenario;
        write_file(scenario.device_directory / "uevent",
                   "DRIVER=vfio-pci\nPCI_SLOT_NAME=0000:c6:00.0\n");
        require_unavailable(state, scenario.read(), scenario.tree.root(),
                            "the retained device must use amdgpu");
    }
    {
        Scenario scenario;
        write_file(scenario.device_directory / "uevent",
                   "DRIVER=amdgpu\nPCI_SLOT_NAME=0000:c7:00.0\n");
        require_unavailable(state, scenario.read(), scenario.tree.root(),
                            "PCI_SLOT_NAME must match the bound BDF");
    }
    {
        Scenario scenario;
        auto binding = scenario.binding();
        binding.sysfs_device_directory = scenario.tree.root() / "missing";
        Scenario::ReadOptions options;
        options.source_binding = std::move(binding);
        require_unavailable(
            state, scenario.read(std::move(options)),
            scenario.tree.root(),
            "the sysfs device directory participates in the binding");
    }
    {
        Scenario scenario;
        auto binding = scenario.binding();
        binding.max_read_duration = 0ms;
        Scenario::ReadOptions options;
        options.source_binding = std::move(binding);
        require_unavailable(
            state, scenario.read(std::move(options)),
            scenario.tree.root(),
            "the maximum read duration participates in the binding");
    }
    {
        Scenario scenario;
        const auto base = std::chrono::steady_clock::now();
        auto source = LinuxAmdGttPointObservationSourceTestHook::make(
            scenario.binding(), scenario.containment, scenario.proc_root,
            [base] { return base; },
            filesystem_magic(scenario.proc_root),
            filesystem_magic(scenario.device_directory));
        const auto retained = scenario.tree.root() / "retained-device";
        std::error_code error;
        std::filesystem::rename(scenario.device_directory, retained, error);
        if (error) setup_failure("could not retain fixture device directory");
        std::filesystem::create_directories(scenario.device_directory, error);
        if (error) setup_failure("could not substitute fixture device directory");
        write_file(scenario.device_directory / "vendor", "0x1002\n");
        write_file(scenario.device_directory / "uevent",
                   "DRIVER=amdgpu\nPCI_SLOT_NAME=0000:c6:00.0\n");
        write_file(scenario.device_directory / "mem_info_gtt_used",
                   "16384\n");
        require_unavailable(
            state, source.read(scenario.request, [] { return false; }),
            scenario.tree.root(),
            "a named-path substitution cannot replace the retained device");
    }
    {
        Scenario scenario;
        scenario.write_fd(3, fdinfo("drm-resident-gtt:\t4 KiB\n"));
        require_unavailable(
            state,
            scenario.read(read_options_at_boundary(
                ReadBoundary::AfterGlobalPoint, [&scenario] {
                    const auto retained =
                        scenario.tree.root() / "mid-read-retained-device";
                    std::error_code error;
                    std::filesystem::rename(scenario.device_directory,
                                            retained, error);
                    if (error) {
                        setup_failure(
                            "could not retain the mid-read device directory");
                    }
                    std::filesystem::create_directories(
                        scenario.device_directory, error);
                    if (error) {
                        setup_failure(
                            "could not substitute the mid-read device directory");
                    }
                    write_file(scenario.device_directory / "vendor",
                               "0x1002\n");
                    write_file(
                        scenario.device_directory / "uevent",
                        "DRIVER=amdgpu\nPCI_SLOT_NAME=0000:c6:00.0\n");
                    write_file(scenario.device_directory /
                                   "mem_info_gtt_used",
                               "16384\n");
                })),
            scenario.tree.root(),
            "a mid-read named-path substitution invalidates the whole read");
        state.require(
            scenario.control->snapshot_calls.load(
                std::memory_order_relaxed) == 2,
            "device identity is revalidated after the closing snapshot");
    }
    {
        Scenario scenario;
        Scenario::ReadOptions proc_options;
        proc_options.expected_proc_filesystem_magic =
            filesystem_magic(scenario.proc_root) + 1;
        proc_options.expected_sysfs_filesystem_magic =
            filesystem_magic(scenario.device_directory);
        require_unavailable(
            state,
            scenario.read(std::move(proc_options)),
            scenario.tree.root(),
            "a proc filesystem identity mismatch fails closed");
        scenario.reset_snapshots();
        Scenario::ReadOptions sysfs_options;
        sysfs_options.expected_proc_filesystem_magic =
            filesystem_magic(scenario.proc_root);
        sysfs_options.expected_sysfs_filesystem_magic =
            filesystem_magic(scenario.device_directory) + 1;
        require_unavailable(
            state,
            scenario.read(std::move(sysfs_options)),
            scenario.tree.root(),
            "a sysfs filesystem identity mismatch fails closed");
    }
}

void require_cancelled(TestState &state,
                       const ProfilingRawReadResult &result,
                       const std::filesystem::path &fixture_root,
                       const char *message) {
    state.require(
        result.error == ProfilingSourceError::Cancelled &&
            result.samples.empty() && result.owner_scope_set_sha256.empty() &&
            !result.diagnostic.empty() &&
            result.diagnostic.size() <=
                lemon::residency::max_local_overlay_diagnostic_bytes &&
            result.diagnostic.find(fixture_root.string()) ==
                std::string::npos &&
            result.diagnostic.find("/proc/") == std::string::npos &&
            result.diagnostic.find("/sys/") == std::string::npos,
        message);
}

void test_containment_closure_cancellation_and_deadline(TestState &state) {
    {
        Scenario scenario;
        scenario.write_fd(3, fdinfo("drm-resident-gtt:\t4 KiB\n"));
        Scenario::ReadOptions options;
        options.cancellation_check = [] { return true; };
        require_cancelled(
            state, scenario.read(std::move(options)),
            scenario.tree.root(),
            "cancellation before the opening snapshot stays cancellation");
        state.require(scenario.control->snapshot_calls == 0,
                      "pre-read cancellation stops before containment access");
    }
    {
        Scenario scenario;
        scenario.control->active = false;
        require_unavailable(
            state, scenario.read(), scenario.tree.root(),
            "an inactive prepared containment fails closed");
        state.require(scenario.control->snapshot_calls == 0,
                      "inactive containment fails before snapshot access");
    }
    {
        Scenario scenario;
        scenario.write_fd(3, fdinfo("drm-resident-gtt:\t4 KiB\n"));
        Scenario::ReadOptions options;
        options.cancellation_check = [&scenario] {
            return scenario.control->snapshot_calls >= 1;
        };
        require_cancelled(
            state, scenario.read(std::move(options)),
            scenario.tree.root(),
            "cancellation after the opening snapshot discards all evidence");
        state.require(scenario.control->snapshot_calls == 1,
                      "opening-boundary cancellation stops later work");
    }
    {
        Scenario scenario;
        scenario.write_fd(3, fdinfo("drm-resident-gtt:\t4 KiB\n"));
        Scenario::ReadOptions options;
        options.cancellation_check = [&scenario] {
            return scenario.control->snapshot_calls >= 2;
        };
        require_cancelled(
            state, scenario.read(std::move(options)),
            scenario.tree.root(),
            "cancellation at completion discards an otherwise closed read");
        state.require(scenario.control->snapshot_calls == 2,
                      "completion cancellation occurs after closure");
    }
    {
        Scenario scenario;
        const auto calls = std::make_shared<std::size_t>(0);
        const auto start = std::chrono::steady_clock::time_point{} + 1s;
        auto clock = [calls, start] {
            const auto call = ++*calls;
            return call == 1 ? start : start + 50ms;
        };
        Scenario::ReadOptions options;
        options.monotonic_now = std::move(clock);
        require_unavailable(
            state, scenario.read(std::move(options)),
            scenario.tree.root(),
            "expiry at the first bounded boundary produces no evidence");
        state.require(scenario.control->snapshot_calls == 0,
                      "an expired read stops before containment access");
    }
    {
        Scenario scenario;
        const auto near_max =
            std::chrono::steady_clock::time_point::max() - 49ms;
        Scenario::ReadOptions options;
        options.monotonic_now = [near_max] { return near_max; };
        require_unavailable(
            state, scenario.read(std::move(options)),
            scenario.tree.root(),
            "a valid 50ms duration that overflows its absolute deadline fails closed");
        state.require(scenario.control->snapshot_calls == 0,
                      "deadline overflow fails before containment access");
    }
    {
        Scenario scenario;
        scenario.control->snapshots.clear();
        scenario.control->snapshots.push_back(successful_snapshot(
            scenario.identity, 71, {birth()}));
        auto changed = birth();
        ++changed.start_time_ticks;
        scenario.control->snapshots.push_back(successful_snapshot(
            scenario.identity, 72, {changed}));
        require_unavailable(state, scenario.read(), scenario.tree.root(),
                            "opening and closing members must be identical");
    }
    {
        Scenario scenario;
        scenario.control->snapshots.clear();
        scenario.control->snapshots.push_back(successful_snapshot(
            scenario.identity, 71, {birth()}));
        auto changed = scenario.identity;
        ++changed.inode;
        scenario.control->snapshots.push_back(successful_snapshot(
            changed, 72, {birth()}));
        require_unavailable(state, scenario.read(), scenario.tree.root(),
                            "opening and closing lease identities must match");
    }
    {
        Scenario scenario;
        scenario.control->snapshots.clear();
        scenario.control->snapshots.push_back(successful_snapshot(
            scenario.identity, 71, {birth()}));
        scenario.control->snapshots.push_back(successful_snapshot(
            scenario.identity, 71, {birth()}));
        require_unavailable(
            state, scenario.read(), scenario.tree.root(),
            "a closing snapshot must advance the containment generation");
    }
    {
        Scenario scenario;
        scenario.control->snapshots.clear();
        scenario.control->snapshots.push_back(successful_snapshot(
            scenario.identity, 72, {birth()}));
        scenario.control->snapshots.push_back(successful_snapshot(
            scenario.identity, 71, {birth()}));
        require_unavailable(
            state, scenario.read(), scenario.tree.root(),
            "a regressed containment generation fails closed");
    }
}

void test_mid_read_controls_and_containment_statuses(TestState &state) {
    const std::vector<ReadBoundary> interruptible_boundaries{
        ReadBoundary::AfterFirstFdinfoPass,
        ReadBoundary::AfterGlobalPoint,
        ReadBoundary::AfterSecondFdinfoPass,
    };
    for (const auto boundary : interruptible_boundaries) {
        Scenario scenario;
        scenario.write_fd(3, fdinfo("drm-resident-gtt:\t4 KiB\n"));
        const auto cancelled = std::make_shared<std::atomic<bool>>(false);
        Scenario::ReadOptions options;
        options.cancellation_check = [cancelled] {
            return cancelled->load(std::memory_order_acquire);
        };
        options.on_read_boundary =
            [boundary, cancelled](ReadBoundary observed) {
                if (observed == boundary) {
                    cancelled->store(true, std::memory_order_release);
                }
            };
        const auto result = scenario.read(std::move(options));
        require_cancelled(
            state, result, scenario.tree.root(),
            "cancellation after each target/global phase discards evidence");
        state.require(
            scenario.control->snapshot_calls.load(
                std::memory_order_relaxed) == 1,
            "mid-read cancellation stops before the closing snapshot");
    }

    for (const auto boundary : interruptible_boundaries) {
        Scenario scenario;
        scenario.write_fd(3, fdinfo("drm-resident-gtt:\t4 KiB\n"));
        auto binding = scenario.binding();
        binding.max_read_duration = 5s;
        const auto expired = std::make_shared<std::atomic<bool>>(false);
        const auto base = std::chrono::steady_clock::now();
        Scenario::ReadOptions options;
        options.source_binding = std::move(binding);
        options.monotonic_now = [base, expired] {
            return expired->load(std::memory_order_acquire) ? base + 5s
                                                            : base;
        };
        options.on_read_boundary =
            [boundary, expired](ReadBoundary observed) {
                if (observed == boundary) {
                    expired->store(true, std::memory_order_release);
                }
            };
        const auto result = scenario.read(std::move(options));
        require_unavailable(
            state, result, scenario.tree.root(),
            "deadline expiry after each target/global phase discards evidence");
        state.require(
            scenario.control->snapshot_calls.load(
                std::memory_order_relaxed) == 1,
            "mid-read deadline expiry stops before the closing snapshot");
    }

    {
        Scenario scenario;
        scenario.write_fd(3, fdinfo("drm-resident-gtt:\t4 KiB\n"));
        const auto base = std::chrono::steady_clock::now() + 1s;
        Scenario::ReadOptions options;
        options.monotonic_now = [base] { return base; };
        require_success(state, scenario.read(std::move(options)), 4096,
                        "a successful read closes both containment snapshots");
        const auto &controls = scenario.control->snapshot_controls;
        state.require(
            controls.size() == 2 &&
                controls[0].deadline == base + 50ms &&
                controls[1].deadline == base + 50ms &&
                controls[0].cancellation != nullptr &&
                controls[0].cancellation == controls[1].cancellation,
            "both snapshots carry the exact deadline and one non-null cancellation");
    }

    {
        Scenario scenario;
        scenario.control->snapshots.clear();
        scenario.control->snapshots.push_back(failed_snapshot(
            ProcessContainmentStatus::Cancelled,
            "/sensitive/containment/opening"));
        const auto result = scenario.read();
        require_cancelled(state, result, scenario.tree.root(),
                          "opening containment cancellation stays cancellation");
        state.require(result.diagnostic.find("/sensitive/") ==
                          std::string::npos,
                      "opening cancellation never forwards a path diagnostic");
    }
    {
        Scenario scenario;
        scenario.control->snapshots.clear();
        scenario.control->snapshots.push_back(successful_snapshot(
            scenario.identity, 71, {birth()}));
        scenario.control->snapshots.push_back(failed_snapshot(
            ProcessContainmentStatus::Cancelled,
            "/sensitive/containment/closing"));
        const auto result = scenario.read();
        require_cancelled(state, result, scenario.tree.root(),
                          "closing containment cancellation stays cancellation");
        state.require(result.diagnostic.find("/sensitive/") ==
                          std::string::npos,
                      "closing cancellation never forwards a path diagnostic");
    }

    const std::vector<ProcessContainmentStatus> unavailable_statuses{
        ProcessContainmentStatus::TimedOut,
        ProcessContainmentStatus::Closed,
    };
    for (const auto status : unavailable_statuses) {
        {
            Scenario scenario;
            scenario.control->snapshots.clear();
            scenario.control->snapshots.push_back(failed_snapshot(
                status, "/sensitive/containment/opening"));
            const auto result = scenario.read();
            require_unavailable(
                state, result, scenario.tree.root(),
                "opening timeout or closure is evidence unavailability");
            state.require(result.diagnostic.find("/sensitive/") ==
                              std::string::npos,
                          "opening failure never forwards a path diagnostic");
        }
        {
            Scenario scenario;
            scenario.control->snapshots.clear();
            scenario.control->snapshots.push_back(successful_snapshot(
                scenario.identity, 71, {birth()}));
            scenario.control->snapshots.push_back(failed_snapshot(
                status, "/sensitive/containment/closing"));
            const auto result = scenario.read();
            require_unavailable(
                state, result, scenario.tree.root(),
                "closing timeout or closure is evidence unavailability");
            state.require(result.diagnostic.find("/sensitive/") ==
                              std::string::npos,
                          "closing failure never forwards a path diagnostic");
        }
    }
}

void test_overlapping_read_fails_without_waiting(TestState &state) {
    Scenario scenario;
    scenario.write_fd(3, fdinfo("drm-resident-gtt:\t4 KiB\n"));
    auto binding = scenario.binding();
    binding.max_read_duration = 5s;

    std::mutex gate_mutex;
    std::condition_variable gate_changed;
    bool first_reached_gate = false;
    bool release_first = false;
    const auto base = std::chrono::steady_clock::now();
    auto source = LinuxAmdGttPointObservationSourceTestHook::make(
        std::move(binding), scenario.containment, scenario.proc_root,
        [base] { return base; }, filesystem_magic(scenario.proc_root),
        filesystem_magic(scenario.device_directory),
        at_boundary(ReadBoundary::AfterGlobalPoint, [&] {
            std::unique_lock<std::mutex> lock(gate_mutex);
            first_reached_gate = true;
            gate_changed.notify_all();
            gate_changed.wait(lock, [&] { return release_first; });
        }));

    std::optional<ProfilingRawReadResult> first_result;
    std::thread first([&] {
        first_result = source.read(scenario.request, [] { return false; });
    });
    bool reached_gate = false;
    {
        std::unique_lock<std::mutex> lock(gate_mutex);
        reached_gate = gate_changed.wait_for(
            lock, 2s, [&] { return first_reached_gate; });
        if (!reached_gate) release_first = true;
    }
    if (!reached_gate) {
        gate_changed.notify_all();
        first.join();
        state.require(false,
                      "the active read reaches the between-pass test gate");
        return;
    }
    state.require(
        scenario.control->snapshot_calls.load(std::memory_order_relaxed) == 1,
        "the held read reaches the between-pass gate after one snapshot");

    std::promise<void> second_started;
    auto started = second_started.get_future();
    auto second = std::async(std::launch::async, [&] {
        second_started.set_value();
        return source.read(scenario.request, [] { return false; });
    });
    started.wait();
    const bool returned_without_waiting =
        second.wait_for(250ms) == std::future_status::ready;
    const auto snapshots_before_release =
        scenario.control->snapshot_calls.load(std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(gate_mutex);
        release_first = true;
    }
    gate_changed.notify_all();
    first.join();
    const auto second_result = second.get();

    state.require(returned_without_waiting,
                  "an overlapping read never waits behind the active read");
    state.require(snapshots_before_release == 1,
                  "an overlapping read does not enter containment");
    require_unavailable(state, second_result, scenario.tree.root(),
                        "an overlapping read fails closed and path-free");
    state.require(first_result.has_value(),
                  "the held read returns after the test gate opens");
    if (first_result) {
        require_success(state, *first_result, 4096,
                        "the active read remains coherent after overlap refusal");
    }
    state.require(
        scenario.control->snapshot_calls.load(std::memory_order_relaxed) == 2,
        "only the active read performs opening and closing snapshots");
}

const ClaimFamilyClosure *capacity_family(
    const std::vector<ClaimFamilyClosure> &claims) {
    for (const auto &family : claims) {
        if (family.family == ClaimFamily::ConsumableCapacity) return &family;
    }
    return nullptr;
}

bool has_capacity(const std::vector<ClaimFamilyClosure> &claims,
                  std::uint64_t amount) {
    const auto *family = capacity_family(claims);
    return family != nullptr &&
           family->completeness == ClaimCompleteness::Bounded &&
           family->entries.size() == 1 &&
           family->entries[0].constraint_id == "gpu/gtt" &&
           family->entries[0].unit == ClaimUnit::Bytes &&
           family->entries[0].amount == amount;
}

void test_collector_keeps_physical_and_owner_views_distinct(TestState &state) {
    Scenario scenario;
    scenario.write_fd(3, fdinfo("drm-resident-gtt:\t4 KiB\n"));
    const auto owner = profiling_owner_scope_binding(scenario.identity);
    if (!owner) setup_failure("collector owner binding is invalid");

    ProfilingDerivationContract contract;
    contract.provider_id = "provider/linux-amdgpu-gtt-point";
    contract.provider_revision_sha256 = std::string(64, 'e');
    contract.sensors = {ProfilingSensorContract{
        std::string(linux_amd_gtt_point_sensor_id), "gpu/gtt",
        ClaimFamily::ConsumableCapacity, 512, 32768}};
    contract.owner_scopes = {*owner};
    contract.freshness_window = 60s;
    contract.max_source_skew = 20ms;
    contract.interval.event_semantics_revision_sha256 = std::string(64, '9');
    contract.interval.max_observation_gap = 50ms;
    contract.interval.baseline_stability_window = 100ms;
    contract.interval.release_stability_window = 200ms;
    contract.interval.max_interval_frames = 128;
    const auto contract_digest =
        profiling_derivation_contract_sha256(contract);
    if (!contract_digest) setup_failure("collector contract is invalid");

    ProfilingTransactionContext context;
    context.deployment_id = std::string(64, 'a');
    context.sequence = 1;
    context.profiling_transaction_id = "profile/linux-amdgpu-gtt-point";
    context.selector_sha256 = std::string(64, 'b');
    context.generations = {1, 2, 3, 4, 5, 6, 7};
    context.observation_contract_sha256 = *contract_digest;
    context.predictor_contract_sha256 = std::string(64, 'd');

    auto source = LinuxAmdGttPointObservationSourceTestHook::make(
        scenario.binding(), scenario.containment, scenario.proc_root,
        [base = std::chrono::steady_clock::now()] { return base; },
        filesystem_magic(scenario.proc_root),
        filesystem_magic(scenario.device_directory));
    auto monotonic =
        std::make_shared<std::deque<std::chrono::steady_clock::time_point>>();
    monotonic->push_back(std::chrono::steady_clock::time_point{});
    monotonic->push_back(std::chrono::steady_clock::time_point{} + 1ms);
    ProfilingCollectionClock clock;
    clock.monotonic_now = [monotonic] {
        if (monotonic->empty()) {
            setup_failure("collector clock exhausted");
        }
        const auto value = monotonic->front();
        monotonic->pop_front();
        return value;
    };
    clock.utc_now = [] {
        return std::chrono::system_clock::time_point{} + 1000s;
    };
    ProfilingObservationCollector collector(contract, source, std::move(clock));

    const auto result = collector.collect(context, [] { return false; });
    state.require(
        result.status == ProfilingCollectionStatus::Accepted &&
            result.observation.has_value() &&
            has_capacity(result.observation->observed_claims, 4096) &&
            has_capacity(result.observation->attributed_claims, 4096) &&
            has_capacity(result.observation->safety_margin_claims, 15872) &&
            result.observation->source_generation == 72,
        "the collector keeps global physical use separate from owner attribution");
}

} // namespace

int main() {
    TestState state;
    try {
        test_binding_identity(state);
        test_binding_is_acquired_from_a_normalized_snapshot(state);
        test_request_and_basic_reads(state);
        test_fdinfo_units_aliases_and_cardinality(state);
        test_fdinfo_device_selection(state);
        test_fdinfo_completeness_failures(state);
        test_multiple_containment_members(state);
        test_fdinfo_descriptor_limits(state);
        test_between_pass_churn_fails_closed(state);
        test_point_read_phase_order_and_single_global(state);
        test_global_counter_grammar(state);
        test_device_binding(state);
        test_containment_closure_cancellation_and_deadline(state);
        test_mid_read_controls_and_containment_statuses(state);
        test_overlapping_read_fails_without_waiting(state);
        test_collector_keeps_physical_and_owner_views_distinct(state);
    } catch (const std::exception &error) {
        std::cerr << "FAIL: test setup failed: " << error.what() << '\n';
        return 1;
    }
    return state.failures == 0 ? 0 : 1;
}
