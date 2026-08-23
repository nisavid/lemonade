#include "journal_persistence_test_support.h"

#include "durable_local_overlay_test_factory.h"
#include "platform/durable_file_adapter.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace lemon::residency::testing {

namespace {

using detail::DurableFileChannel;
using detail::DurableFileResult;
using detail::DurableFileStatus;
using detail::DurableInterruptibleCall;
using detail::DurableReadChannel;
using detail::DurableReadChunkResult;
using detail::DurableWriteResult;

constexpr std::string_view journal_name = "journal.jsonl";
constexpr std::string_view root_name = "authority-root.json";
constexpr std::string_view lock_name = "authority.lock";
constexpr std::string_view journal_stage_name = ".journal.jsonl.stage";
constexpr std::string_view root_stage_name = ".authority-root.json.stage";
constexpr std::string_view preflight_probe_name = ".durability-probe";
constexpr std::string_view preflight_stage_name = ".durability-probe.stage";

std::atomic<std::uint64_t> next_directory_identity{1};
std::atomic<std::uint64_t> next_entry_identity{1};
std::atomic<std::uint64_t> next_handle_identity{1};

DurableFileResult result(DurableFileStatus status) {
    return DurableFileResult{status, {}};
}

DurableFileResult succeeded() { return result(DurableFileStatus::Succeeded); }

bool is_succeeded(const DurableFileResult &value) {
    return value.status == DurableFileStatus::Succeeded;
}

DurableFileResult
promote_after_persistent_effect(DurableFileResult value) {
    if (is_succeeded(value) ||
        value.status == DurableFileStatus::EffectMayHaveOccurred) {
        return value;
    }
    return {DurableFileStatus::EffectMayHaveOccurred,
            std::move(value.diagnostic)};
}

HelperResultKind helper_result_kind(DurableFileStatus status) {
    switch (status) {
    case DurableFileStatus::Succeeded:
        return HelperResultKind::Succeeded;
    case DurableFileStatus::Interrupted:
        return HelperResultKind::Interrupted;
    case DurableFileStatus::EffectMayHaveOccurred:
        return HelperResultKind::EffectMayHaveOccurred;
    case DurableFileStatus::Unsupported:
    case DurableFileStatus::NotFound:
    case DurableFileStatus::AlreadyExists:
    case DurableFileStatus::FailedBeforeEffect:
        return HelperResultKind::FailedBeforeEffect;
    }
    return HelperResultKind::FailedBeforeEffect;
}

bool is_authority_operation(FaultOperation operation) {
    switch (operation) {
    case FaultOperation::AuthorityIdentityRead:
    case FaultOperation::FixedNamespaceInspect:
    case FaultOperation::RootOpen:
    case FaultOperation::RootRead:
    case FaultOperation::RootClose:
    case FaultOperation::JournalOpen:
    case FaultOperation::JournalRead:
    case FaultOperation::JournalReadClose:
    case FaultOperation::ObjectOpen:
    case FaultOperation::ObjectRead:
    case FaultOperation::ObjectReadClose:
    case FaultOperation::ObjectStageCreate:
    case FaultOperation::ObjectWrite:
    case FaultOperation::ObjectFlush:
    case FaultOperation::ObjectClose:
    case FaultOperation::ObjectPublish:
    case FaultOperation::ObjectNamespaceDurability:
    case FaultOperation::ObjectStageCleanup:
    case FaultOperation::JournalAppendOpen:
    case FaultOperation::InitialJournalCreate:
    case FaultOperation::JournalWrite:
    case FaultOperation::JournalFlush:
    case FaultOperation::JournalClose:
    case FaultOperation::RootStageCreate:
    case FaultOperation::RootStageWrite:
    case FaultOperation::RootStageFlush:
    case FaultOperation::RootStageClose:
    case FaultOperation::RootReplace:
    case FaultOperation::NamespaceDurability:
    case FaultOperation::TailRepairOpen:
    case FaultOperation::TailRepairTruncate:
    case FaultOperation::TailRepairFlush:
    case FaultOperation::TailRepairClose:
    case FaultOperation::CompactionStageCreate:
    case FaultOperation::CompactionWrite:
    case FaultOperation::CompactionFlush:
    case FaultOperation::CompactionClose:
    case FaultOperation::CompactionReplace:
    case FaultOperation::CompactionNamespaceDurability:
        return true;
    default:
        return false;
    }
}

class ScriptedProbeChannel final : public DurableFileChannel {
public:
    explicit ScriptedProbeChannel(std::vector<FaultAction> script,
                                  std::string initial = {})
        : script_(std::move(script)), bytes_(std::move(initial)) {}

    DurableWriteResult write_some(std::string_view bytes) override {
        ++attempts_;
        const auto action = next_action();
        if (!action.has_value()) {
            bytes_.append(bytes);
            return {succeeded(), bytes.size()};
        }
        switch (*action) {
        case FaultAction::ShortWrite: {
            const auto transferred =
                bytes.size() <= 1 ? bytes.size() : bytes.size() / 2;
            bytes_.append(bytes.substr(0, transferred));
            return {succeeded(), transferred};
        }
        case FaultAction::InterruptedOnce:
            return {result(DurableFileStatus::Interrupted), 0};
        case FaultAction::ZeroProgress:
            return {succeeded(), 0};
        case FaultAction::OverreportedWrite:
            return {succeeded(), bytes.size() + 1};
        case FaultAction::EffectThenError:
            bytes_.append(bytes);
            return {result(DurableFileStatus::EffectMayHaveOccurred),
                    bytes.size()};
        case FaultAction::Error:
        case FaultAction::Crash:
            return {result(DurableFileStatus::FailedBeforeEffect), 0};
        }
        return {result(DurableFileStatus::Unsupported), 0};
    }

    DurableFileResult flush() override {
        ++attempts_;
        return scripted_call([] {});
    }

    DurableFileResult truncate(std::size_t bytes) override {
        ++attempts_;
        return scripted_call(
            [&] { bytes_.resize(std::min(bytes, bytes_.size())); });
    }

    DurableFileResult close() override {
        ++attempts_;
        return scripted_call([] {});
    }

    const std::string &bytes() const noexcept { return bytes_; }
    std::size_t attempts() const noexcept { return attempts_; }

private:
    std::optional<FaultAction> next_action() {
        if (next_action_ == script_.size()) {
            return std::nullopt;
        }
        return script_[next_action_++];
    }

    template <typename Effect> DurableFileResult scripted_call(Effect effect) {
        const auto action = next_action();
        if (!action.has_value()) {
            effect();
            return succeeded();
        }
        switch (*action) {
        case FaultAction::InterruptedOnce:
            return result(DurableFileStatus::Interrupted);
        case FaultAction::Error:
        case FaultAction::Crash:
        case FaultAction::ZeroProgress:
        case FaultAction::OverreportedWrite:
            return result(DurableFileStatus::FailedBeforeEffect);
        case FaultAction::EffectThenError:
            effect();
            return result(DurableFileStatus::EffectMayHaveOccurred);
        case FaultAction::ShortWrite:
            effect();
            return succeeded();
        }
        return result(DurableFileStatus::Unsupported);
    }

    std::vector<FaultAction> script_;
    std::size_t next_action_ = 0;
    std::string bytes_;
    std::size_t attempts_ = 0;
};

class ScriptedOrchestrationChannel final : public DurableFileChannel {
public:
    ScriptedOrchestrationChannel(std::vector<FaultAction> write_script,
                                 std::vector<FaultAction> flush_script,
                                 std::vector<FaultAction> truncate_script,
                                 std::vector<FaultAction> close_script,
                                 std::string initial = {})
        : write_script_(std::move(write_script)),
          flush_script_(std::move(flush_script)),
          truncate_script_(std::move(truncate_script)),
          close_script_(std::move(close_script)), bytes_(std::move(initial)) {}

    DurableWriteResult write_some(std::string_view bytes) override {
        ++write_attempts_;
        const auto action = next_action(write_script_, next_write_action_);
        if (!action.has_value()) {
            bytes_.append(bytes);
            return {succeeded(), bytes.size()};
        }
        switch (*action) {
        case FaultAction::ShortWrite: {
            const auto transferred =
                bytes.size() <= 1 ? bytes.size() : bytes.size() / 2;
            bytes_.append(bytes.substr(0, transferred));
            return {succeeded(), transferred};
        }
        case FaultAction::InterruptedOnce:
            return {result(DurableFileStatus::Interrupted), 0};
        case FaultAction::ZeroProgress:
            return {succeeded(), 0};
        case FaultAction::OverreportedWrite:
            return {succeeded(), bytes.size() + 1};
        case FaultAction::EffectThenError:
            bytes_.append(bytes);
            return {result(DurableFileStatus::EffectMayHaveOccurred),
                    bytes.size()};
        case FaultAction::Error:
        case FaultAction::Crash:
            return {result(DurableFileStatus::FailedBeforeEffect), 0};
        }
        return {result(DurableFileStatus::Unsupported), 0};
    }

    DurableFileResult flush() override {
        ++flush_attempts_;
        return scripted_call(flush_script_, next_flush_action_, [] {});
    }

    DurableFileResult truncate(std::size_t bytes) override {
        ++truncate_attempts_;
        return scripted_call(truncate_script_, next_truncate_action_, [&] {
            bytes_.resize(std::min(bytes, bytes_.size()));
        });
    }

    DurableFileResult close() override {
        ++close_attempts_;
        return scripted_call(close_script_, next_close_action_, [] {});
    }

    const std::string &bytes() const noexcept { return bytes_; }
    std::size_t write_attempts() const noexcept { return write_attempts_; }
    std::size_t flush_attempts() const noexcept { return flush_attempts_; }
    std::size_t truncate_attempts() const noexcept {
        return truncate_attempts_;
    }
    std::size_t close_attempts() const noexcept { return close_attempts_; }

private:
    static std::optional<FaultAction>
    next_action(const std::vector<FaultAction> &script, std::size_t &next) {
        if (next == script.size()) {
            return std::nullopt;
        }
        return script[next++];
    }

    template <typename Effect>
    static DurableFileResult
    scripted_call(const std::vector<FaultAction> &script, std::size_t &next,
                  Effect effect) {
        const auto action = next_action(script, next);
        if (!action.has_value()) {
            effect();
            return succeeded();
        }
        switch (*action) {
        case FaultAction::InterruptedOnce:
            return result(DurableFileStatus::Interrupted);
        case FaultAction::Error:
        case FaultAction::Crash:
        case FaultAction::ZeroProgress:
        case FaultAction::OverreportedWrite:
            return result(DurableFileStatus::FailedBeforeEffect);
        case FaultAction::EffectThenError:
            effect();
            return result(DurableFileStatus::EffectMayHaveOccurred);
        case FaultAction::ShortWrite:
            effect();
            return succeeded();
        }
        return result(DurableFileStatus::Unsupported);
    }

    std::vector<FaultAction> write_script_;
    std::vector<FaultAction> flush_script_;
    std::vector<FaultAction> truncate_script_;
    std::vector<FaultAction> close_script_;
    std::size_t next_write_action_ = 0;
    std::size_t next_flush_action_ = 0;
    std::size_t next_truncate_action_ = 0;
    std::size_t next_close_action_ = 0;
    std::string bytes_;
    std::size_t write_attempts_ = 0;
    std::size_t flush_attempts_ = 0;
    std::size_t truncate_attempts_ = 0;
    std::size_t close_attempts_ = 0;
};

class ScriptedReadChannel final : public DurableReadChannel {
public:
    ScriptedReadChannel(std::string bytes, std::vector<FaultAction> read_script,
                        std::vector<FaultAction> close_script)
        : bytes_(std::move(bytes)), read_script_(std::move(read_script)),
          close_script_(std::move(close_script)) {}

    DurableReadChunkResult read_some(std::size_t max_bytes) override {
        ++read_attempts_;
        requested_bytes_.push_back(max_bytes);
        const auto action = next_action(read_script_, next_read_action_);
        if (action == FaultAction::InterruptedOnce) {
            return record_chunk(
                {result(DurableFileStatus::Interrupted), {}, false});
        }
        if (action == FaultAction::ZeroProgress) {
            return record_chunk({succeeded(), {}, false});
        }
        if (action == FaultAction::OverreportedWrite) {
            return record_chunk(
                {succeeded(), std::string(max_bytes + 1, 'x'), false});
        }
        if (action == FaultAction::Error || action == FaultAction::Crash) {
            return record_chunk(
                {result(DurableFileStatus::FailedBeforeEffect), {}, false});
        }

        auto transferred = std::min(max_bytes, bytes_.size() - offset_);
        if (action == FaultAction::ShortWrite && transferred > 1) {
            transferred /= 2;
        }
        auto chunk = bytes_.substr(offset_, transferred);
        offset_ += transferred;
        const auto end_of_file = transferred == 0 && offset_ == bytes_.size();
        if (action == FaultAction::EffectThenError) {
            return record_chunk(
                {result(DurableFileStatus::EffectMayHaveOccurred),
                 std::move(chunk), end_of_file});
        }
        return record_chunk({succeeded(), std::move(chunk), end_of_file});
    }

    DurableFileResult close() override {
        ++close_attempts_;
        const auto action = next_action(close_script_, next_close_action_);
        if (!action.has_value() || action == FaultAction::ShortWrite) {
            return succeeded();
        }
        if (action == FaultAction::InterruptedOnce) {
            return result(DurableFileStatus::Interrupted);
        }
        if (action == FaultAction::EffectThenError) {
            return result(DurableFileStatus::EffectMayHaveOccurred);
        }
        return result(DurableFileStatus::FailedBeforeEffect);
    }

    const std::vector<std::size_t> &requested_bytes() const noexcept {
        return requested_bytes_;
    }
    const std::vector<std::size_t> &returned_bytes() const noexcept {
        return returned_bytes_;
    }
    std::size_t read_attempts() const noexcept { return read_attempts_; }
    std::size_t close_attempts() const noexcept { return close_attempts_; }

private:
    static std::optional<FaultAction>
    next_action(const std::vector<FaultAction> &script, std::size_t &next) {
        if (next == script.size()) {
            return std::nullopt;
        }
        return script[next++];
    }

    DurableReadChunkResult record_chunk(DurableReadChunkResult chunk) {
        returned_bytes_.push_back(chunk.bytes.size());
        return chunk;
    }

    std::string bytes_;
    std::vector<FaultAction> read_script_;
    std::vector<FaultAction> close_script_;
    std::vector<std::size_t> requested_bytes_;
    std::vector<std::size_t> returned_bytes_;
    std::size_t offset_ = 0;
    std::size_t next_read_action_ = 0;
    std::size_t next_close_action_ = 0;
    std::size_t read_attempts_ = 0;
    std::size_t close_attempts_ = 0;
};

} // namespace

struct JournalTestStorage::State {
    enum class EntryKind { Regular, Symlink, NonRegular, ReparsePoint };

    struct Entry {
        EntryKind kind = EntryKind::Regular;
        std::string live_bytes;
        std::string durable_bytes;
        bool live_exists = false;
        bool durable_exists = false;
        bool durable_content_available = false;
        std::uint64_t identity = 0;
    };

    struct FaultScript {
        FaultOperation operation;
        FaultPosition position;
        std::deque<FaultAction> actions;
    };

    explicit State(PlatformContract selected_platform, bool directory_present)
        : platform(selected_platform),
          authority_directory_exists(directory_present),
          directory_identity(next_directory_identity.fetch_add(1)),
          named_directory_identity(directory_identity) {}

    void add_durable_regular(std::string_view name, std::string bytes) {
        auto &entry = entries[std::string(name)];
        entry.kind = EntryKind::Regular;
        entry.live_bytes = bytes;
        entry.durable_bytes = std::move(bytes);
        entry.live_exists = true;
        entry.durable_exists = true;
        entry.durable_content_available = true;
        if (entry.identity == 0) {
            entry.identity = next_entry_identity.fetch_add(1);
        }
    }

    Entry *live_entry(std::string_view name) {
        const auto found = entries.find(std::string(name));
        if (found == entries.end() || !found->second.live_exists) {
            return nullptr;
        }
        return &found->second;
    }

    const Entry *live_entry(std::string_view name) const {
        const auto found = entries.find(std::string(name));
        if (found == entries.end() || !found->second.live_exists) {
            return nullptr;
        }
        return &found->second;
    }

    std::size_t live_link_count(std::uint64_t identity) const {
        return static_cast<std::size_t>(std::count_if(
            entries.begin(), entries.end(), [&](const auto &item) {
                return item.second.live_exists &&
                       item.second.identity == identity;
            }));
    }

    void observe(FaultOperation operation, bool lock_held,
                 bool mutation_attempted, std::uint64_t lock_identity,
                 std::size_t requested_bytes = 0,
                 std::size_t transferred_bytes = 0) {
        observations.push_back(OperationObservation{
            operation, lock_held, mutation_attempted, lock_identity,
            requested_bytes, transferred_bytes});
    }

    std::optional<FaultAction> take_fault(FaultOperation operation,
                                          FaultPosition position) {
        if (!fault.has_value() || fault->operation != operation ||
            fault->position != position || fault->actions.empty()) {
            return std::nullopt;
        }
        const auto action = fault->actions.front();
        fault->actions.pop_front();
        if (fault->actions.empty()) {
            fault.reset();
        }
        return action;
    }

    void restart() {
        if (simulated_crash) {
            for (auto &[name, entry] : entries) {
                static_cast<void>(name);
                if (entry.kind == EntryKind::Regular && entry.live_exists &&
                    entry.durable_content_available) {
                    entry.durable_exists = true;
                }
            }
        }
        for (auto &[name, entry] : entries) {
            static_cast<void>(name);
            entry.live_exists = entry.durable_exists;
            entry.live_bytes = entry.durable_bytes;
        }
        held_lock_identities.clear();
        blocked_lock_waiters = 0;
        fault.reset();
        pause_operation.reset();
        pause_occurrence = 0;
        paused = false;
        pause_released = false;
        open_entries.clear();
        open_lock_handles.clear();
        simulated_crash = false;
        condition.notify_all();
    }

    PlatformContract platform;
    bool authority_directory_exists;
    const std::uint64_t directory_identity;
    std::uint64_t named_directory_identity;
    std::map<std::string, Entry> entries;
    std::set<DurabilityCapability> unavailable_capabilities;
    std::optional<FaultScript> fault;
    std::vector<OperationObservation> observations;
    std::set<std::uint64_t> held_lock_identities;
    std::size_t blocked_lock_waiters = 0;
    std::optional<FaultOperation> pause_operation;
    std::size_t pause_occurrence = 0;
    bool pause_consumed = false;
    bool paused = false;
    bool pause_released = false;
    std::set<std::string> open_entries;
    std::set<std::uint64_t> open_lock_handles;
    bool simulated_crash = false;
    mutable std::mutex mutex;
    std::condition_variable condition;
};

HelperProbeResult probe_write_all(std::string bytes,
                                  std::vector<FaultAction> script) {
    ScriptedProbeChannel channel(std::move(script));
    const auto call_result = detail::write_all(channel, bytes);
    return {helper_result_kind(call_result.status), channel.bytes(),
            channel.attempts()};
}

HelperProbeResult probe_flush_retry(std::vector<FaultAction> script) {
    ScriptedProbeChannel channel(std::move(script));
    const auto call_result = detail::flush_retrying_interrupts(channel);
    return {helper_result_kind(call_result.status), channel.bytes(),
            channel.attempts()};
}

HelperProbeResult probe_truncate_retry(std::string bytes,
                                       std::size_t truncate_to,
                                       std::vector<FaultAction> script) {
    ScriptedProbeChannel channel(std::move(script), std::move(bytes));
    const auto call_result =
        detail::truncate_retrying_interrupts(channel, truncate_to);
    return {helper_result_kind(call_result.status), channel.bytes(),
            channel.attempts()};
}

HelperProbeResult probe_close_once(std::vector<FaultAction> script) {
    ScriptedProbeChannel channel(std::move(script));
    const auto call_result = detail::close_once(channel);
    return {helper_result_kind(call_result.status), channel.bytes(),
            channel.attempts()};
}

HelperOrchestrationProbeResult
probe_write_flush_close(std::string bytes,
                        std::vector<FaultAction> write_script,
                        std::vector<FaultAction> flush_script,
                        std::vector<FaultAction> close_script) {
    ScriptedOrchestrationChannel channel(std::move(write_script),
                                         std::move(flush_script), {},
                                         std::move(close_script));
    const auto call_result = detail::write_flush_close(channel, bytes);
    return {helper_result_kind(call_result.status),
            channel.bytes(),
            channel.write_attempts(),
            channel.flush_attempts(),
            channel.truncate_attempts(),
            channel.close_attempts()};
}

HelperOrchestrationProbeResult
probe_truncate_flush_close(std::string bytes, std::size_t truncate_to,
                           std::vector<FaultAction> truncate_script,
                           std::vector<FaultAction> flush_script,
                           std::vector<FaultAction> close_script) {
    ScriptedOrchestrationChannel channel(
        {}, std::move(flush_script), std::move(truncate_script),
        std::move(close_script), std::move(bytes));
    const auto call_result = detail::truncate_flush_close(channel, truncate_to);
    return {helper_result_kind(call_result.status),
            channel.bytes(),
            channel.write_attempts(),
            channel.flush_attempts(),
            channel.truncate_attempts(),
            channel.close_attempts()};
}

ReadHelperProbeResult
probe_read_bounded_close(std::string bytes, std::size_t max_bytes,
                         std::vector<FaultAction> read_script,
                         std::vector<FaultAction> close_script) {
    ScriptedReadChannel channel(std::move(bytes), std::move(read_script),
                                std::move(close_script));
    const auto call_result = detail::read_bounded_close(channel, max_bytes);
    return {helper_result_kind(call_result.result.status),
            std::move(call_result.bytes),
            call_result.truncated,
            channel.requested_bytes(),
            channel.returned_bytes(),
            channel.read_attempts(),
            channel.close_attempts()};
}

NativeLockPathProbeResult
probe_native_lock_path_revalidation(std::string directory) {
    const auto root = std::filesystem::path(std::move(directory));
    const auto lock_path = root / "authority.lock";
    auto first = detail::make_platform_durable_file_adapter(root);
    auto second = detail::make_platform_durable_file_adapter(root);
    const auto first_lock = first->lock_authority();
    const auto first_identity = first->authority_identity();
    if (!first_lock.succeeded() || !first_identity.result.succeeded()) {
        return {};
    }

    std::error_code error;
    const auto removed_while_held = std::filesystem::remove(lock_path, error);
    const bool replacement_prevented = !removed_while_held || error;
    bool stale_identity_rejected = false;
    if (!replacement_prevented) {
        std::ofstream(lock_path, std::ios::binary).close();
        stale_identity_rejected =
            !first->authority_identity().result.succeeded();
    }

#ifdef _WIN32
    const auto probe_handle = ::CreateFileW(
        lock_path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    OVERLAPPED overlapped{};
    const auto probe_locked =
        probe_handle != INVALID_HANDLE_VALUE &&
        ::LockFileEx(probe_handle,
                     LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0,
                     MAXDWORD, MAXDWORD, &overlapped) != FALSE;
    const auto nonblocking_probe_rejected =
        !probe_locked && ::GetLastError() == ERROR_LOCK_VIOLATION;
    if (probe_locked) {
        static_cast<void>(
            ::UnlockFileEx(probe_handle, 0, MAXDWORD, MAXDWORD, &overlapped));
    }
    if (probe_handle != INVALID_HANDLE_VALUE) {
        static_cast<void>(::CloseHandle(probe_handle));
    }
#else
    const auto probe_fd =
        ::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    errno = 0;
    const auto probe_locked =
        probe_fd >= 0 && ::flock(probe_fd, LOCK_EX | LOCK_NB) == 0;
    const auto nonblocking_probe_rejected =
        !probe_locked && (errno == EWOULDBLOCK || errno == EAGAIN);
    if (probe_locked) {
        static_cast<void>(::flock(probe_fd, LOCK_UN));
    }
    if (probe_fd >= 0) {
        static_cast<void>(::close(probe_fd));
    }
#endif
    const auto first_unlock = first->unlock_authority();
    detail::DurableFileResult contender_lock;
    detail::DurableIdentityResult contender_identity;
    detail::DurableFileResult contender_unlock;
    if (first_unlock.succeeded()) {
        contender_lock = second->lock_authority();
        if (contender_lock.succeeded()) {
            contender_identity = second->authority_identity();
            contender_unlock = second->unlock_authority();
        }
    }

    const bool contender_succeeded =
        contender_lock.succeeded() && contender_identity.result.succeeded() &&
        contender_unlock.succeeded();
    const bool contender_rebound =
        contender_succeeded &&
        contender_identity.identity != first_identity.identity;

    bool replacement_after_release_rebound = false;
    if (replacement_prevented && contender_succeeded) {
        error.clear();
        const auto removed_after_release =
            std::filesystem::remove(lock_path, error);
        if (removed_after_release && !error) {
            std::ofstream(lock_path, std::ios::binary).close();
            auto rebound = detail::make_platform_durable_file_adapter(root);
            const auto rebound_lock = rebound->lock_authority();
            const auto rebound_identity = rebound->authority_identity();
            const auto rebound_unlock = rebound->unlock_authority();
            replacement_after_release_rebound =
                rebound_lock.succeeded() &&
                rebound_identity.result.succeeded() &&
                rebound_identity.identity != first_identity.identity &&
                rebound_unlock.succeeded();
        }
    }

    return {replacement_prevented,
            stale_identity_rejected,
            nonblocking_probe_rejected,
            first_unlock.succeeded(),
            contender_succeeded,
            contender_rebound,
            replacement_after_release_rebound};
}

class JournalTestStorage::Adapter final : public detail::DurableFileAdapter {
public:
    explicit Adapter(std::shared_ptr<State> state,
                     bool fixed_namespace = false)
        : state_(std::move(state)),
          fixed_namespace_(fixed_namespace),
          bound_directory_identity_(fixed_namespace
                                        ? state_->named_directory_identity
                                        : state_->directory_identity),
          lock_handle_identity_(next_handle_identity.fetch_add(1)) {}

    ~Adapter() override {
        std::lock_guard lock(state_->mutex);
        release_owned_lock();
        if (!state_->simulated_crash) {
            close_open_lock_handle();
        }
    }

    DurableFileResult lock_authority() override {
        {
            std::unique_lock lock(state_->mutex);
            const auto opened =
                run_step(lock, FaultOperation::AuthorityLockOpen, false, [&] {
                    if (!state_->authority_directory_exists) {
                        return;
                    }
                    if (state_->live_entry(lock_name) == nullptr) {
                        state_->add_durable_regular(lock_name, {});
                    }
                    state_->open_lock_handles.insert(lock_handle_identity_);
                });
            if (!is_succeeded(opened)) {
                return close_lock_after(lock, opened);
            }
            const auto *entry = state_->live_entry(lock_name);
            if (!state_->authority_directory_exists || entry == nullptr ||
                entry->kind != State::EntryKind::Regular) {
                return close_lock_after(lock,
                                        result(DurableFileStatus::Unsupported));
            }
            opened_lock_identity_ = entry->identity;
        }

        class AcquireCall final : public DurableInterruptibleCall {
        public:
            explicit AcquireCall(Adapter &adapter) : adapter_(adapter) {}

            DurableFileResult attempt() override {
                return adapter_.acquire_opened_lock_once();
            }

        private:
            Adapter &adapter_;
        } acquire_call(*this);

        const auto acquired = detail::retry_interrupted(acquire_call);
        if (!is_succeeded(acquired)) {
            std::unique_lock lock(state_->mutex);
            return close_lock_after(lock, acquired);
        }

        std::unique_lock lock(state_->mutex);
        const auto *current = state_->live_entry(lock_name);
        if (current != nullptr && current->kind == State::EntryKind::Regular &&
            current->identity == opened_lock_identity_) {
            pause_if_requested(lock, FaultOperation::AuthorityLockAcquire);
            current = state_->live_entry(lock_name);
            if (current != nullptr &&
                current->kind == State::EntryKind::Regular &&
                current->identity == opened_lock_identity_ &&
                fixed_namespace_binding_is_current()) {
                return succeeded();
            }
        }
        const auto unlocked = run_step(lock, FaultOperation::AuthorityUnlock,
                                       false, [&] { release_owned_lock(); });
        return close_lock_after(
            lock, combine_results(result(DurableFileStatus::FailedBeforeEffect),
                                  unlocked));
    }

    detail::DurableIdentityResult authority_identity() override {
        std::unique_lock lock(state_->mutex);
        const auto checked =
            run_step(lock, FaultOperation::AuthorityIdentityRead, false, [] {});
        if (!is_succeeded(checked)) {
            return {checked, {}};
        }
        const auto *current = state_->live_entry(lock_name);
        if (!owns_lock() || current == nullptr ||
            current->kind != State::EntryKind::Regular ||
            current->identity != opened_lock_identity_) {
            return {result(DurableFileStatus::FailedBeforeEffect), {}};
        }
        return {succeeded(),
                "directory:" +
                    std::to_string(bound_directory_identity_) +
                    "/lock:" + std::to_string(opened_lock_identity_)};
    }

    DurableFileResult preflight_capabilities() override {
        const std::vector<
            std::pair<FaultOperation, std::optional<DurabilityCapability>>>
            operations{
                {FaultOperation::PreflightProbe, std::nullopt},
                {FaultOperation::PreflightFlushCreate, std::nullopt},
                {FaultOperation::PreflightFlushWrite, std::nullopt},
                {FaultOperation::PreflightFlushFileFlush,
                 DurabilityCapability::RegularFileFlush},
                {FaultOperation::PreflightFlushClose, std::nullopt},
                {FaultOperation::PreflightTruncate,
                 DurabilityCapability::TailTruncateAndFlush},
                {FaultOperation::PreflightTruncateFlush,
                 DurabilityCapability::TailTruncateAndFlush},
                {FaultOperation::PreflightTruncateClose, std::nullopt},
                {FaultOperation::PreflightReplaceStageCreate, std::nullopt},
                {FaultOperation::PreflightReplaceStageWrite, std::nullopt},
                {FaultOperation::PreflightReplaceStageFlush, std::nullopt},
                {FaultOperation::PreflightReplaceStageClose, std::nullopt},
                {FaultOperation::PreflightReplace,
                 DurabilityCapability::SameDirectoryReplace},
                {FaultOperation::PreflightNamespaceDurability,
                 DurabilityCapability::NamespaceDurability},
                {FaultOperation::PreflightCleanup, std::nullopt},
            };

        for (const auto &[operation, capability] : operations) {
            class PreflightCall final : public DurableInterruptibleCall {
            public:
                PreflightCall(Adapter &adapter, FaultOperation operation)
                    : adapter_(adapter), operation_(operation) {}

                DurableFileResult attempt() override {
                    std::unique_lock lock(adapter_.state_->mutex);
                    return adapter_.run_step(lock, operation_, false, [&] {
                        adapter_.apply_preflight_operation(operation_);
                    });
                }

            private:
                Adapter &adapter_;
                FaultOperation operation_;
            } call(*this, operation);
            const auto checked = is_preflight_close(operation)
                                     ? call.attempt()
                                     : detail::retry_interrupted(call);
            if (!is_succeeded(checked)) {
                return finish_preflight_failure(operation, checked);
            }
            if (is_preflight_close(operation)) {
                std::lock_guard lock(state_->mutex);
                close_preflight_handle(operation);
            }
            bool unavailable = false;
            bool unsafe_fixed_entry = false;
            {
                std::lock_guard lock(state_->mutex);
                unavailable =
                    capability.has_value() &&
                    state_->unavailable_capabilities.count(*capability) != 0;
                unsafe_fixed_entry =
                    operation == FaultOperation::PreflightProbe &&
                    !fixed_entries_are_safe();
            }
            if (unavailable) {
                return finish_preflight_failure(
                    operation, result(DurableFileStatus::Unsupported));
            }
            if (unsafe_fixed_entry) {
                return result(DurableFileStatus::Unsupported);
            }
        }
        return succeeded();
    }

    detail::DurableFixedNamespaceResult inspect_fixed_namespace() override {
        std::unique_lock lock(state_->mutex);
        const auto inspected =
            run_step(lock, FaultOperation::FixedNamespaceInspect, false, [] {});
        if (!is_succeeded(inspected)) {
            return {inspected, false, false, false, false, false};
        }
        if (!owns_lock() || !fixed_entries_are_safe()) {
            return {result(DurableFileStatus::Unsupported),
                    false,
                    false,
                    false,
                    false,
                    false};
        }
        return {succeeded(),
                state_->live_entry(journal_name) != nullptr,
                state_->live_entry(root_name) != nullptr,
                state_->live_entry(journal_stage_name) != nullptr,
                state_->live_entry(root_stage_name) != nullptr,
                state_->live_entry(lock_name) != nullptr};
    }

    detail::DurableReadResult read_root(std::size_t max_bytes) override {
        return read_fixed(root_name, FaultOperation::RootOpen,
                          FaultOperation::RootRead, FaultOperation::RootClose,
                          max_bytes);
    }

    detail::DurableReadResult read_journal(std::size_t max_bytes) override {
        return read_fixed(journal_name, FaultOperation::JournalOpen,
                          FaultOperation::JournalRead,
                          FaultOperation::JournalReadClose, max_bytes);
    }

    detail::DurableReadResult
    read_immutable_object(std::string_view sha256,
                          std::size_t max_bytes) override {
        const auto object_name =
            detail::durable_immutable_object_filename(sha256);
        if (!object_name.has_value()) {
            return {result(DurableFileStatus::Unsupported), {}, false};
        }
        return read_fixed(*object_name, FaultOperation::ObjectOpen,
                          FaultOperation::ObjectRead,
                          FaultOperation::ObjectReadClose, max_bytes, true);
    }

    DurableFileResult
    create_immutable_object(std::string_view sha256,
                            std::string_view bytes) override {
        const auto object_name =
            detail::durable_immutable_object_filename(sha256);
        const auto stage_name =
            detail::durable_immutable_object_stage_filename(sha256);
        if (!object_name.has_value() || !stage_name.has_value()) {
            return result(DurableFileStatus::Unsupported);
        }

        bool recovered_stage_present = false;
        bool recovered_linked_publish = false;
        {
            std::unique_lock lock(state_->mutex);
            const auto *stage = state_->live_entry(*stage_name);
            if (stage != nullptr) {
                recovered_stage_present = true;
                if (stage->kind != State::EntryKind::Regular) {
                    return result(DurableFileStatus::Unsupported);
                }
                const auto *target = state_->live_entry(*object_name);
                recovered_linked_publish =
                    target != nullptr &&
                    target->kind == State::EntryKind::Regular &&
                    target->identity == stage->identity;
                if (recovered_linked_publish &&
                    (stage->live_bytes != bytes ||
                     target->live_bytes != bytes)) {
                    return result(
                        DurableFileStatus::EffectMayHaveOccurred);
                }
            }
        }
        if (recovered_stage_present) {
            const auto cleaned = cleanup_object_stage(*stage_name);
            if (!cleaned.succeeded()) {
                return cleaned;
            }
            const auto namespace_result =
                make_object_namespace_durable();
            if (!namespace_result.succeeded()) {
                return namespace_result;
            }
        }
        if (recovered_linked_publish) {
            std::lock_guard lock(state_->mutex);
            const auto *target = state_->live_entry(*object_name);
            if (target == nullptr ||
                target->kind != State::EntryKind::Regular ||
                target->live_bytes != bytes || !target->durable_exists ||
                !target->durable_content_available ||
                target->durable_bytes != bytes) {
                return result(DurableFileStatus::EffectMayHaveOccurred);
            }
            return succeeded();
        }

        {
            std::unique_lock lock(state_->mutex);
            if (state_->live_entry(*object_name) != nullptr) {
                return result(DurableFileStatus::AlreadyExists);
            }
            const auto created = run_step(
                lock, FaultOperation::ObjectStageCreate, true, [&] {
                    auto &entry = state_->entries[*stage_name];
                    entry = State::Entry{};
                    entry.live_exists = true;
                    entry.identity = next_entry_identity.fetch_add(1);
                    state_->open_entries.insert(*stage_name);
                });
            if (!is_succeeded(created)) {
                if (state_->open_entries.count(*stage_name) == 0 ||
                    state_->simulated_crash) {
                    return created;
                }
                const auto closed =
                    run_step(lock, FaultOperation::ObjectClose, false, [&] {
                        state_->open_entries.erase(*stage_name);
                    });
                state_->open_entries.erase(*stage_name);
                return combine_results(created, closed);
            }
        }
        Channel channel(
            *this, *stage_name, false, FaultOperation::ObjectWrite,
            FaultOperation::ObjectFlush, FaultOperation::ObjectWrite,
            FaultOperation::ObjectClose);
        const auto persisted = promote_after_persistent_effect(
            detail::write_flush_close(channel, bytes));
        if (!persisted.succeeded()) {
            return persisted;
        }

        {
            std::unique_lock lock(state_->mutex);
            const auto *stage = state_->live_entry(*stage_name);
            if (stage == nullptr ||
                stage->kind != State::EntryKind::Regular ||
                state_->live_entry(*object_name) != nullptr) {
                return result(DurableFileStatus::EffectMayHaveOccurred);
            }
            const auto published = run_step(
                lock, FaultOperation::ObjectPublish, true, [&] {
                    const auto staged = state_->entries[*stage_name];
                    auto &target = state_->entries[*object_name];
                    target = State::Entry{};
                    target.live_exists = true;
                    target.live_bytes = staged.live_bytes;
                    target.durable_bytes = staged.durable_bytes;
                    target.durable_content_available =
                        staged.durable_content_available;
                    target.identity = staged.identity;
                    if (state_->platform == PlatformContract::Windows) {
                        erase_live(*stage_name);
                    }
                });
            if (!published.succeeded()) {
                return promote_after_persistent_effect(published);
            }
        }
        auto namespace_result = make_object_namespace_durable();
        if (!namespace_result.succeeded()) {
            return namespace_result;
        }
        if (state_->platform != PlatformContract::Windows) {
            const auto cleaned = cleanup_object_stage(*stage_name);
            if (!cleaned.succeeded()) {
                return cleaned;
            }
            namespace_result = make_object_namespace_durable();
            if (!namespace_result.succeeded()) {
                return namespace_result;
            }
        }
        std::lock_guard lock(state_->mutex);
        const auto *target = state_->live_entry(*object_name);
        const auto *stage = state_->live_entry(*stage_name);
        const auto durable_stage = state_->entries.find(*stage_name);
        if (target == nullptr ||
            target->kind != State::EntryKind::Regular ||
            target->live_bytes != bytes || !target->durable_exists ||
            !target->durable_content_available ||
            target->durable_bytes != bytes || stage != nullptr ||
            (durable_stage != state_->entries.end() &&
             durable_stage->second.durable_exists)) {
            return result(DurableFileStatus::EffectMayHaveOccurred);
        }
        return namespace_result;
    }

    DurableFileResult create_journal(std::string_view bytes) override {
        {
            std::unique_lock lock(state_->mutex);
            if (state_->live_entry(journal_name) != nullptr) {
                return result(DurableFileStatus::AlreadyExists);
            }
            const auto created =
                run_step(lock, FaultOperation::InitialJournalCreate, true, [&] {
                    auto &entry = state_->entries[std::string(journal_name)];
                    entry.kind = State::EntryKind::Regular;
                    entry.live_exists = true;
                    entry.live_bytes.clear();
                    entry.identity = next_entry_identity.fetch_add(1);
                    state_->open_entries.insert(std::string(journal_name));
                });
            if (!is_succeeded(created)) {
                if (state_->open_entries.count(std::string(journal_name)) ==
                        0 ||
                    state_->simulated_crash) {
                    return created;
                }
                const auto closed =
                    run_step(lock, FaultOperation::JournalClose, false, [&] {
                        state_->open_entries.erase(std::string(journal_name));
                    });
                state_->open_entries.erase(std::string(journal_name));
                return combine_results(created, closed);
            }
        }
        Channel channel(
            *this, journal_name, false, FaultOperation::JournalWrite,
            FaultOperation::JournalFlush, FaultOperation::JournalWrite,
            FaultOperation::JournalClose);
        return promote_after_persistent_effect(
            detail::write_flush_close(channel, bytes));
    }

    DurableFileResult append_journal(std::string_view bytes) override {
        {
            std::unique_lock lock(state_->mutex);
            const auto journal_exists =
                state_->live_entry(journal_name) != nullptr;
            const auto opened =
                run_step(lock, FaultOperation::JournalAppendOpen, false, [&] {
                    if (journal_exists) {
                        state_->open_entries.insert(std::string(journal_name));
                    }
                });
            if (!is_succeeded(opened)) {
                if (state_->open_entries.count(std::string(journal_name)) ==
                        0 ||
                    state_->simulated_crash) {
                    return result(DurableFileStatus::FailedBeforeEffect);
                }
                const auto closed =
                    run_step(lock, FaultOperation::JournalClose, false, [&] {
                        state_->open_entries.erase(std::string(journal_name));
                    });
                state_->open_entries.erase(std::string(journal_name));
                static_cast<void>(closed);
                return result(DurableFileStatus::FailedBeforeEffect);
            }
            if (state_->live_entry(journal_name) == nullptr) {
                return result(DurableFileStatus::NotFound);
            }
        }
        Channel channel(*this, journal_name, true, FaultOperation::JournalWrite,
                        FaultOperation::JournalFlush,
                        FaultOperation::JournalWrite,
                        FaultOperation::JournalClose);
        return detail::write_flush_close(channel, bytes);
    }

    DurableFileResult truncate_journal(std::size_t bytes) override {
        {
            std::unique_lock lock(state_->mutex);
            const auto journal_exists =
                state_->live_entry(journal_name) != nullptr;
            const auto opened =
                run_step(lock, FaultOperation::TailRepairOpen, false, [&] {
                    if (journal_exists) {
                        state_->open_entries.insert(std::string(journal_name));
                    }
                });
            if (!is_succeeded(opened)) {
                if (state_->open_entries.count(std::string(journal_name)) ==
                        0 ||
                    state_->simulated_crash) {
                    return result(DurableFileStatus::FailedBeforeEffect);
                }
                const auto closed =
                    run_step(lock, FaultOperation::TailRepairClose, false, [&] {
                        state_->open_entries.erase(std::string(journal_name));
                    });
                state_->open_entries.erase(std::string(journal_name));
                static_cast<void>(closed);
                return result(DurableFileStatus::FailedBeforeEffect);
            }
            if (state_->live_entry(journal_name) == nullptr) {
                return result(DurableFileStatus::NotFound);
            }
        }
        Channel channel(
            *this, journal_name, false, FaultOperation::JournalWrite,
            FaultOperation::TailRepairFlush, FaultOperation::TailRepairTruncate,
            FaultOperation::TailRepairClose);
        return detail::truncate_flush_close(channel, bytes);
    }

    DurableFileResult replace_journal(std::string_view bytes) override {
        return replace_fixed(
            journal_stage_name, journal_name, bytes,
            FaultOperation::CompactionStageCreate,
            FaultOperation::CompactionWrite, FaultOperation::CompactionFlush,
            FaultOperation::CompactionClose, FaultOperation::CompactionReplace,
            FaultOperation::CompactionNamespaceDurability);
    }

    DurableFileResult replace_root(std::string_view bytes) override {
        return replace_fixed(
            root_stage_name, root_name, bytes, FaultOperation::RootStageCreate,
            FaultOperation::RootStageWrite, FaultOperation::RootStageFlush,
            FaultOperation::RootStageClose, FaultOperation::RootReplace,
            FaultOperation::NamespaceDurability);
    }

    DurableFileResult unlock_authority() override {
        std::unique_lock lock(state_->mutex);
        const auto unlocked = run_step(lock, FaultOperation::AuthorityUnlock,
                                       false, [&] { release_owned_lock(); });
        return close_lock_after(lock, unlocked);
    }

private:
    DurableFileResult
    close_lock_after(std::unique_lock<std::mutex> &lock,
                     const DurableFileResult &operation_result) {
        if (state_->simulated_crash || !lock_handle_is_open()) {
            return operation_result;
        }
        const auto closed =
            run_step(lock, FaultOperation::AuthorityLockClose, false, [&] {
                release_owned_lock();
                close_open_lock_handle();
            });
        if (!state_->simulated_crash) {
            release_owned_lock();
            close_open_lock_handle();
        }
        return combine_results(operation_result, closed);
    }

    class Channel final : public DurableFileChannel {
    public:
        Channel(Adapter &adapter, std::string_view name, bool append,
                FaultOperation write_operation, FaultOperation flush_operation,
                FaultOperation truncate_operation,
                FaultOperation close_operation)
            : adapter_(adapter), name_(name), append_(append),
              write_operation_(write_operation),
              flush_operation_(flush_operation),
              truncate_operation_(truncate_operation),
              close_operation_(close_operation) {}

        DurableWriteResult write_some(std::string_view bytes) override {
            return adapter_.write_some(name_, bytes, append_ && wrote_any_,
                                       append_ && !wrote_any_, write_operation_,
                                       wrote_any_);
        }

        DurableFileResult flush() override {
            std::unique_lock lock(adapter_.state_->mutex);
            return adapter_.run_step(lock, flush_operation_, true, [&] {
                auto *entry = adapter_.state_->live_entry(name_);
                if (entry != nullptr) {
                    entry->durable_bytes = entry->live_bytes;
                    entry->durable_content_available = true;
                }
            });
        }

        DurableFileResult truncate(std::size_t bytes) override {
            std::unique_lock lock(adapter_.state_->mutex);
            return adapter_.run_step(
                lock, truncate_operation_, true,
                [&] {
                    auto *entry = adapter_.state_->live_entry(name_);
                    if (entry != nullptr) {
                        entry->live_bytes.resize(
                            std::min(bytes, entry->live_bytes.size()));
                    }
                },
                bytes, bytes);
        }

        DurableFileResult close() override {
            std::unique_lock lock(adapter_.state_->mutex);
            if (adapter_.state_->simulated_crash) {
                return result(DurableFileStatus::EffectMayHaveOccurred);
            }
            const auto closed =
                adapter_.run_step(lock, close_operation_, false, [&] {
                    adapter_.state_->open_entries.erase(name_);
                });
            if (!adapter_.state_->simulated_crash) {
                adapter_.state_->open_entries.erase(name_);
            }
            return closed;
        }

    private:
        Adapter &adapter_;
        std::string name_;
        bool append_;
        FaultOperation write_operation_;
        FaultOperation flush_operation_;
        FaultOperation truncate_operation_;
        FaultOperation close_operation_;
        bool wrote_any_ = false;
    };

    class ReadChannel final : public DurableReadChannel {
    public:
        ReadChannel(Adapter &adapter, std::string_view name,
                    FaultOperation read_operation,
                    FaultOperation close_operation)
            : adapter_(adapter), name_(name), read_operation_(read_operation),
              close_operation_(close_operation) {}

        DurableReadChunkResult read_some(std::size_t max_bytes) override {
            std::unique_lock lock(adapter_.state_->mutex);
            const auto *entry = adapter_.state_->live_entry(name_);
            if (entry == nullptr) {
                return {result(DurableFileStatus::NotFound), {}, false};
            }
            if (entry->kind != State::EntryKind::Regular) {
                return {result(DurableFileStatus::Unsupported), {}, false};
            }
            const auto available =
                offset_ < entry->live_bytes.size()
                    ? entry->live_bytes.size() - offset_
                    : 0;
            const auto transferred = std::min(max_bytes, available);
            std::string bytes;
            bool end_of_file = false;
            const auto read = adapter_.run_step(
                lock, read_operation_, false,
                [&] {
                    if (transferred != 0) {
                        bytes = entry->live_bytes.substr(offset_, transferred);
                        offset_ += transferred;
                    }
                    end_of_file = available == 0;
                },
                max_bytes, transferred);
            if (!is_succeeded(read) &&
                read.status != DurableFileStatus::EffectMayHaveOccurred) {
                return {read, {}, false};
            }
            return {read, std::move(bytes), end_of_file};
        }

        DurableFileResult close() override {
            std::unique_lock lock(adapter_.state_->mutex);
            if (adapter_.state_->simulated_crash) {
                return result(DurableFileStatus::EffectMayHaveOccurred);
            }
            const auto closed =
                adapter_.run_step(lock, close_operation_, false, [&] {
                    adapter_.state_->open_entries.erase(name_);
                });
            if (!adapter_.state_->simulated_crash) {
                adapter_.state_->open_entries.erase(name_);
            }
            return closed;
        }

    private:
        Adapter &adapter_;
        std::string name_;
        FaultOperation read_operation_;
        FaultOperation close_operation_;
        std::size_t offset_ = 0;
    };

    DurableFileResult acquire_opened_lock_once() {
        std::unique_lock lock(state_->mutex);
        if (!fixed_namespace_binding_is_current()) {
            return result(DurableFileStatus::Unsupported);
        }
        const auto before = state_->take_fault(
            FaultOperation::AuthorityLockAcquire, FaultPosition::Before);
        if (before.has_value()) {
            if (*before == FaultAction::Crash) {
                state_->simulated_crash = true;
            }
            if (*before == FaultAction::InterruptedOnce) {
                state_->observe(FaultOperation::AuthorityLockAcquire, false,
                                false, opened_lock_identity_);
                return result(DurableFileStatus::Interrupted);
            }
            if (*before != FaultAction::EffectThenError) {
                state_->observe(FaultOperation::AuthorityLockAcquire, false,
                                false, opened_lock_identity_);
                return result(DurableFileStatus::FailedBeforeEffect);
            }
        }
        const bool must_wait =
            state_->held_lock_identities.count(opened_lock_identity_) != 0;
        if (must_wait) {
            ++state_->blocked_lock_waiters;
            state_->condition.notify_all();
        }
        state_->condition.wait(lock, [&] {
            return state_->held_lock_identities.count(opened_lock_identity_) ==
                   0;
        });
        if (must_wait) {
            --state_->blocked_lock_waiters;
        }
        state_->held_lock_identities.insert(opened_lock_identity_);
        owned_lock_identity_ = opened_lock_identity_;
        state_->observe(FaultOperation::AuthorityLockAcquire, true, false,
                        opened_lock_identity_);
        if (!fixed_namespace_binding_is_current()) {
            release_owned_lock();
            return result(DurableFileStatus::Unsupported);
        }
        const auto after = state_->take_fault(
            FaultOperation::AuthorityLockAcquire, FaultPosition::After);
        if (before.has_value() || after.has_value()) {
            if (after == FaultAction::Crash) {
                state_->simulated_crash = true;
            }
            return result(DurableFileStatus::EffectMayHaveOccurred);
        }
        return succeeded();
    }

    template <typename Effect>
    DurableFileResult run_step(std::unique_lock<std::mutex> &lock,
                               FaultOperation operation, bool mutation,
                               Effect effect, std::size_t requested_bytes = 0,
                               std::size_t transferred_bytes = 0) {
        if (!fixed_namespace_binding_is_current()) {
            return result(DurableFileStatus::Unsupported);
        }
        const auto before =
            state_->take_fault(operation, FaultPosition::Before);
        if (before.has_value()) {
            if (*before == FaultAction::Crash) {
                state_->simulated_crash = true;
            }
            if (*before == FaultAction::InterruptedOnce) {
                state_->observe(operation, owns_lock(), false,
                                owned_lock_identity_, requested_bytes, 0);
                return result(DurableFileStatus::Interrupted);
            }
            if (*before != FaultAction::EffectThenError) {
                state_->observe(operation, owns_lock(), false,
                                owned_lock_identity_, requested_bytes, 0);
                return result(DurableFileStatus::FailedBeforeEffect);
            }
            effect();
            state_->observe(operation, owns_lock(), mutation,
                            owned_lock_identity_, requested_bytes,
                            mutation ? transferred_bytes : 0);
            return result(DurableFileStatus::EffectMayHaveOccurred);
        }

        effect();
        state_->observe(operation, owns_lock(), mutation, owned_lock_identity_,
                        requested_bytes, transferred_bytes);
        pause_if_requested(lock, operation);
        if (!fixed_namespace_binding_is_current()) {
            return result(mutation
                              ? DurableFileStatus::EffectMayHaveOccurred
                              : DurableFileStatus::Unsupported);
        }
        const auto after = state_->take_fault(operation, FaultPosition::After);
        if (after.has_value()) {
            if (*after == FaultAction::Crash) {
                state_->simulated_crash = true;
            }
            return result(DurableFileStatus::EffectMayHaveOccurred);
        }
        return succeeded();
    }

    void pause_if_requested(std::unique_lock<std::mutex> &lock,
                            FaultOperation operation) {
        if (state_->pause_operation != operation || state_->pause_consumed) {
            return;
        }
        if (state_->pause_occurrence > 1) {
            --state_->pause_occurrence;
            return;
        }
        state_->pause_consumed = true;
        state_->paused = true;
        state_->condition.notify_all();
        state_->condition.wait(lock, [&] { return state_->pause_released; });
        state_->paused = false;
    }

    DurableFileResult cleanup_object_stage(std::string_view stage_name) {
        class CleanupCall final : public DurableInterruptibleCall {
        public:
            CleanupCall(Adapter &adapter, std::string_view stage_name)
                : adapter_(adapter), stage_name_(stage_name) {}

            DurableFileResult attempt() override {
                std::unique_lock lock(adapter_.state_->mutex);
                return adapter_.run_step(
                    lock, FaultOperation::ObjectStageCleanup, true, [&] {
                        adapter_.state_->open_entries.erase(stage_name_);
                        adapter_.erase_live(stage_name_);
                    });
            }

        private:
            Adapter &adapter_;
            std::string stage_name_;
        } call(*this, stage_name);
        return promote_after_persistent_effect(
            detail::retry_interrupted(call));
    }

    DurableFileResult make_object_namespace_durable() {
        class NamespaceCall final : public DurableInterruptibleCall {
        public:
            explicit NamespaceCall(Adapter &adapter) : adapter_(adapter) {}

            DurableFileResult attempt() override {
                std::unique_lock lock(adapter_.state_->mutex);
                return adapter_.run_step(
                    lock, FaultOperation::ObjectNamespaceDurability, true,
                    [&] {
                        for (auto &[name, entry] :
                             adapter_.state_->entries) {
                            static_cast<void>(name);
                            entry.durable_exists = entry.live_exists;
                            if (!entry.live_exists) {
                                entry.durable_bytes.clear();
                                entry.durable_content_available = false;
                            }
                        }
                    });
            }

        private:
            Adapter &adapter_;
        } call(*this);
        return promote_after_persistent_effect(
            detail::retry_interrupted(call));
    }

    detail::DurableReadResult read_fixed(std::string_view name,
                                         FaultOperation open_operation,
                                         FaultOperation read_operation,
                                         FaultOperation close_operation,
                                         std::size_t max_bytes,
                                         bool require_single_link = false) {
        std::unique_lock lock(state_->mutex);
        const auto *entry_before_open = state_->live_entry(name);
        const auto opened = run_step(
            lock, open_operation, false,
            [&] {
                if (entry_before_open != nullptr) {
                    state_->open_entries.insert(std::string(name));
                }
            },
            max_bytes);
        if (!is_succeeded(opened)) {
            if (state_->open_entries.count(std::string(name)) == 0 ||
                state_->simulated_crash) {
                return {opened, {}, false};
            }
            lock.unlock();
            ReadChannel opened_channel(*this, name, read_operation,
                                       close_operation);
            const auto closed_after_open =
                detail::close_read_once(opened_channel);
            return {combine_results(opened, closed_after_open), {}, false};
        }
        const auto *entry = state_->live_entry(name);
        if (entry == nullptr) {
            return {result(DurableFileStatus::NotFound), {}, false};
        }
        if (entry->kind != State::EntryKind::Regular ||
            (require_single_link &&
             state_->live_link_count(entry->identity) != 1)) {
            const auto unsupported = result(DurableFileStatus::Unsupported);
            lock.unlock();
            ReadChannel unsafe_channel(*this, name, read_operation,
                                       close_operation);
            const auto closed_unsafe = detail::close_read_once(unsafe_channel);
            return {combine_results(unsupported, closed_unsafe), {}, false};
        }
        lock.unlock();
        ReadChannel channel(*this, name, read_operation, close_operation);
        return detail::read_bounded_close(channel, max_bytes);
    }

    DurableWriteResult write_some(std::string_view name, std::string_view bytes,
                                  bool append_after_partial,
                                  bool append_initial, FaultOperation operation,
                                  bool &wrote_any) {
        std::unique_lock lock(state_->mutex);
        if (!fixed_namespace_binding_is_current()) {
            return {result(DurableFileStatus::Unsupported), 0};
        }
        auto *entry = state_->live_entry(name);
        if (entry == nullptr || entry->kind != State::EntryKind::Regular) {
            return {result(DurableFileStatus::NotFound), 0};
        }
        const auto before =
            state_->take_fault(operation, FaultPosition::Before);
        if (before.has_value()) {
            if (*before == FaultAction::Crash) {
                state_->simulated_crash = true;
            }
            switch (*before) {
            case FaultAction::InterruptedOnce:
                state_->observe(operation, owns_lock(), false,
                                owned_lock_identity_, bytes.size(), 0);
                return {result(DurableFileStatus::Interrupted), 0};
            case FaultAction::ZeroProgress:
                state_->observe(operation, owns_lock(), false,
                                owned_lock_identity_, bytes.size(), 0);
                return {succeeded(), 0};
            case FaultAction::OverreportedWrite:
                state_->observe(operation, owns_lock(), false,
                                owned_lock_identity_, bytes.size(),
                                bytes.size() + 1);
                return {succeeded(), bytes.size() + 1};
            case FaultAction::ShortWrite: {
                const auto transferred =
                    bytes.size() <= 1 ? bytes.size() : bytes.size() / 2;
                apply_write(*entry, bytes.substr(0, transferred),
                            append_after_partial, append_initial, wrote_any);
                state_->observe(operation, owns_lock(), true,
                                owned_lock_identity_, bytes.size(),
                                transferred);
                return {succeeded(), transferred};
            }
            case FaultAction::EffectThenError:
                apply_write(*entry, bytes, append_after_partial, append_initial,
                            wrote_any);
                state_->observe(operation, owns_lock(), true,
                                owned_lock_identity_, bytes.size(),
                                bytes.size());
                return {result(DurableFileStatus::EffectMayHaveOccurred),
                        bytes.size()};
            case FaultAction::Error:
            case FaultAction::Crash:
                state_->observe(operation, owns_lock(), false,
                                owned_lock_identity_, bytes.size(), 0);
                return {result(DurableFileStatus::FailedBeforeEffect), 0};
            }
        }
        apply_write(*entry, bytes, append_after_partial, append_initial,
                    wrote_any);
        state_->observe(operation, owns_lock(), true, owned_lock_identity_,
                        bytes.size(), bytes.size());
        pause_if_requested(lock, operation);
        if (!fixed_namespace_binding_is_current()) {
            return {result(DurableFileStatus::EffectMayHaveOccurred),
                    bytes.size()};
        }
        const auto after = state_->take_fault(operation, FaultPosition::After);
        if (after.has_value()) {
            if (*after == FaultAction::Crash) {
                state_->simulated_crash = true;
            }
            return {result(DurableFileStatus::EffectMayHaveOccurred),
                    bytes.size()};
        }
        return {succeeded(), bytes.size()};
    }

    static void apply_write(State::Entry &entry, std::string_view bytes,
                            bool append_after_partial, bool append_initial,
                            bool &wrote_any) {
        if (append_after_partial || append_initial || wrote_any) {
            entry.live_bytes.append(bytes);
        } else {
            entry.live_bytes.assign(bytes);
        }
        wrote_any = true;
    }

    static DurableFileResult
    combine_results(const DurableFileResult &operation_result,
                    const DurableFileResult &close_result) {
        if (operation_result.status ==
                DurableFileStatus::EffectMayHaveOccurred ||
            close_result.status == DurableFileStatus::EffectMayHaveOccurred) {
            return result(DurableFileStatus::EffectMayHaveOccurred);
        }
        if (!is_succeeded(operation_result)) {
            return operation_result;
        }
        return close_result;
    }

    DurableFileResult replace_fixed(
        std::string_view stage_name, std::string_view target_name,
        std::string_view bytes, FaultOperation create_operation,
        FaultOperation write_operation, FaultOperation flush_operation,
        FaultOperation close_operation, FaultOperation replace_operation,
        FaultOperation namespace_operation) {
        std::uint64_t stage_identity = 0;
        {
            std::unique_lock lock(state_->mutex);
            const auto created = run_step(lock, create_operation, true, [&] {
                auto &entry = state_->entries[std::string(stage_name)];
                entry = State::Entry{};
                entry.live_exists = true;
                entry.identity = next_entry_identity.fetch_add(1);
                stage_identity = entry.identity;
                state_->open_entries.insert(std::string(stage_name));
            });
            if (!is_succeeded(created)) {
                if (state_->open_entries.count(std::string(stage_name)) == 0 ||
                    state_->simulated_crash) {
                    return created;
                }
                const auto closed = run_step(lock, close_operation, false, [&] {
                    state_->open_entries.erase(std::string(stage_name));
                });
                state_->open_entries.erase(std::string(stage_name));
                return combine_results(created, closed);
            }
        }
        Channel channel(*this, stage_name, false, write_operation,
                        flush_operation, write_operation, close_operation);
        auto call_result = detail::write_flush_close(channel, bytes);
        if (!is_succeeded(call_result)) {
            return promote_after_persistent_effect(std::move(call_result));
        }
        {
            std::unique_lock lock(state_->mutex);
            const auto replaced = run_step(lock, replace_operation, true, [&] {
                const auto *stage = state_->live_entry(stage_name);
                auto &target = state_->entries[std::string(target_name)];
                target.kind = State::EntryKind::Regular;
                target.live_exists = true;
                target.live_bytes =
                    stage == nullptr ? std::string{} : stage->live_bytes;
                target.identity = stage == nullptr ? 0 : stage->identity;
                erase_live(stage_name);
            });
            if (!is_succeeded(replaced)) {
                return promote_after_persistent_effect(replaced);
            }
        }
        class NamespaceCall final : public DurableInterruptibleCall {
        public:
            NamespaceCall(Adapter &adapter, FaultOperation operation,
                          std::string_view stage_name,
                          std::string_view target_name)
                : adapter_(adapter), operation_(operation),
                  stage_name_(stage_name), target_name_(target_name) {}

            DurableFileResult attempt() override {
                std::unique_lock lock(adapter_.state_->mutex);
                return adapter_.run_step(lock, operation_, true, [&] {
                    adapter_.commit_replacement(stage_name_, target_name_);
                });
            }

        private:
            Adapter &adapter_;
            FaultOperation operation_;
            std::string stage_name_;
            std::string target_name_;
        } namespace_call(*this, namespace_operation, stage_name, target_name);
        const auto namespace_result = promote_after_persistent_effect(
            detail::retry_interrupted(namespace_call));
        if (!is_succeeded(namespace_result)) {
            return namespace_result;
        }
        std::unique_lock lock(state_->mutex);
        const auto *target = state_->live_entry(target_name);
        if (target == nullptr || target->kind != State::EntryKind::Regular ||
            target->identity != stage_identity || target->live_bytes != bytes ||
            !target->durable_exists || !target->durable_content_available ||
            target->durable_bytes != bytes) {
            return result(DurableFileStatus::EffectMayHaveOccurred);
        }
        return namespace_result;
    }

    bool fixed_entries_are_safe() const {
        if (!state_->authority_directory_exists) {
            return false;
        }
        for (const auto name : {journal_name, root_name, lock_name,
                                journal_stage_name, root_stage_name}) {
            const auto *entry = state_->live_entry(name);
            if (entry != nullptr && entry->kind != State::EntryKind::Regular) {
                return false;
            }
        }
        return true;
    }

    bool fixed_namespace_binding_is_current() const {
        return !fixed_namespace_ ||
               (state_->authority_directory_exists &&
                state_->named_directory_identity ==
                    bound_directory_identity_);
    }

    static bool is_preflight_close(FaultOperation operation) {
        return operation == FaultOperation::PreflightFlushClose ||
               operation == FaultOperation::PreflightTruncateClose ||
               operation == FaultOperation::PreflightReplaceStageClose;
    }

    void close_preflight_handle(FaultOperation operation) {
        if (operation == FaultOperation::PreflightReplaceStageClose) {
            state_->open_entries.erase(std::string(preflight_stage_name));
        } else {
            state_->open_entries.erase(std::string(preflight_probe_name));
        }
    }

    DurableFileResult
    finish_preflight_failure(FaultOperation operation,
                             const DurableFileResult &operation_result) {
        std::unique_lock lock(state_->mutex);
        if (state_->simulated_crash) {
            return operation_result;
        }
        auto combined = operation_result;
        if (is_preflight_close(operation)) {
            close_preflight_handle(operation);
        } else {
            std::optional<FaultOperation> close_operation;
            const auto ordinal = static_cast<int>(operation);
            if (state_->open_entries.count(std::string(preflight_stage_name)) !=
                0) {
                close_operation = FaultOperation::PreflightReplaceStageClose;
            } else if (state_->open_entries.count(
                           std::string(preflight_probe_name)) != 0) {
                close_operation =
                    ordinal <= static_cast<int>(
                                   FaultOperation::PreflightFlushClose)
                        ? FaultOperation::PreflightFlushClose
                        : FaultOperation::PreflightTruncateClose;
            }
            if (close_operation.has_value()) {
                const auto closed =
                    run_step(lock, *close_operation, false,
                             [&] { close_preflight_handle(*close_operation); });
                close_preflight_handle(*close_operation);
                combined = combine_results(combined, closed);
            }
        }
        if (operation == FaultOperation::PreflightCleanup) {
            return combined;
        }
        const auto cleaned =
            run_step(lock, FaultOperation::PreflightCleanup, false, [&] {
                state_->open_entries.erase(std::string(preflight_probe_name));
                state_->open_entries.erase(std::string(preflight_stage_name));
                erase_live(preflight_probe_name);
                erase_durable(preflight_probe_name);
                erase_live(preflight_stage_name);
                erase_durable(preflight_stage_name);
            });
        return combine_results(combined, cleaned);
    }

    void commit_replacement(std::string_view stage_name,
                            std::string_view target_name) {
        auto &stage = state_->entries[std::string(stage_name)];
        auto &target = state_->entries[std::string(target_name)];
        if (stage.durable_content_available) {
            target.durable_bytes = stage.durable_bytes;
            target.durable_content_available = true;
        }
        for (auto &[name, entry] : state_->entries) {
            static_cast<void>(name);
            entry.durable_exists = entry.live_exists;
        }
        stage.durable_exists = false;
        stage.durable_bytes.clear();
        stage.durable_content_available = false;
    }

    void apply_preflight_operation(FaultOperation operation) {
        switch (operation) {
        case FaultOperation::PreflightProbe:
            erase_live(preflight_probe_name);
            erase_durable(preflight_probe_name);
            erase_live(preflight_stage_name);
            erase_durable(preflight_stage_name);
            break;
        case FaultOperation::PreflightFlushCreate: {
            auto &entry = state_->entries[std::string(preflight_probe_name)];
            entry = State::Entry{};
            entry.live_exists = true;
            entry.identity = next_entry_identity.fetch_add(1);
            state_->open_entries.insert(std::string(preflight_probe_name));
            break;
        }
        case FaultOperation::PreflightFlushWrite:
            state_->entries[std::string(preflight_probe_name)].live_bytes =
                "flush-probe";
            break;
        case FaultOperation::PreflightFlushFileFlush:
        case FaultOperation::PreflightTruncateFlush:
            state_->entries[std::string(preflight_probe_name)].durable_bytes =
                state_->entries[std::string(preflight_probe_name)].live_bytes;
            state_->entries[std::string(preflight_probe_name)].durable_exists =
                true;
            state_->entries[std::string(preflight_probe_name)]
                .durable_content_available = true;
            break;
        case FaultOperation::PreflightFlushClose:
            state_->open_entries.erase(std::string(preflight_probe_name));
            break;
        case FaultOperation::PreflightTruncateClose:
            state_->open_entries.erase(std::string(preflight_probe_name));
            break;
        case FaultOperation::PreflightReplaceStageClose:
            state_->open_entries.erase(std::string(preflight_stage_name));
            break;
        case FaultOperation::PreflightTruncate:
            state_->open_entries.insert(std::string(preflight_probe_name));
            state_->entries[std::string(preflight_probe_name)]
                .live_bytes.clear();
            break;
        case FaultOperation::PreflightReplaceStageCreate: {
            auto &entry = state_->entries[std::string(preflight_stage_name)];
            entry = State::Entry{};
            entry.live_exists = true;
            entry.identity = next_entry_identity.fetch_add(1);
            state_->open_entries.insert(std::string(preflight_stage_name));
            break;
        }
        case FaultOperation::PreflightReplaceStageWrite:
            state_->entries[std::string(preflight_stage_name)].live_bytes =
                "replace-probe";
            break;
        case FaultOperation::PreflightReplaceStageFlush:
            state_->entries[std::string(preflight_stage_name)].durable_bytes =
                state_->entries[std::string(preflight_stage_name)].live_bytes;
            state_->entries[std::string(preflight_stage_name)].durable_exists =
                true;
            state_->entries[std::string(preflight_stage_name)]
                .durable_content_available = true;
            break;
        case FaultOperation::PreflightReplace:
            state_->entries[std::string(preflight_probe_name)].live_bytes =
                state_->entries[std::string(preflight_stage_name)].live_bytes;
            erase_live(preflight_stage_name);
            break;
        case FaultOperation::PreflightNamespaceDurability: {
            auto &probe = state_->entries[std::string(preflight_probe_name)];
            auto &stage = state_->entries[std::string(preflight_stage_name)];
            if (stage.durable_content_available) {
                probe.durable_bytes = stage.durable_bytes;
                probe.durable_content_available = true;
            }
            probe.durable_exists = probe.live_exists;
            stage.durable_exists = false;
            stage.durable_bytes.clear();
            stage.durable_content_available = false;
        } break;
        case FaultOperation::PreflightCleanup:
            state_->open_entries.erase(std::string(preflight_probe_name));
            state_->open_entries.erase(std::string(preflight_stage_name));
            erase_live(preflight_probe_name);
            erase_durable(preflight_probe_name);
            erase_live(preflight_stage_name);
            erase_durable(preflight_stage_name);
            break;
        default:
            break;
        }
    }

    void erase_live(std::string_view name) {
        auto found = state_->entries.find(std::string(name));
        if (found != state_->entries.end()) {
            found->second.live_exists = false;
            found->second.live_bytes.clear();
        }
    }

    void erase_durable(std::string_view name) {
        auto found = state_->entries.find(std::string(name));
        if (found != state_->entries.end()) {
            found->second.durable_exists = false;
            found->second.durable_bytes.clear();
            found->second.durable_content_available = false;
        }
    }

    bool owns_lock() const {
        return owned_lock_identity_ != 0 &&
               state_->held_lock_identities.count(owned_lock_identity_) != 0;
    }

    bool lock_handle_is_open() const {
        return state_->open_lock_handles.count(lock_handle_identity_) != 0;
    }

    void close_open_lock_handle() {
        if (lock_handle_is_open()) {
            state_->open_lock_handles.erase(lock_handle_identity_);
        }
        opened_lock_identity_ = 0;
    }

    void release_owned_lock() {
        if (owned_lock_identity_ == 0) {
            return;
        }
        state_->held_lock_identities.erase(owned_lock_identity_);
        owned_lock_identity_ = 0;
        state_->condition.notify_all();
    }

    std::shared_ptr<State> state_;
    bool fixed_namespace_;
    std::uint64_t bound_directory_identity_;
    const std::uint64_t lock_handle_identity_;
    std::uint64_t opened_lock_identity_ = 0;
    std::uint64_t owned_lock_identity_ = 0;
};

JournalTestStorage::JournalTestStorage(std::shared_ptr<State> state)
    : state_(std::move(state)) {}

JournalTestStorage JournalTestStorage::fresh() {
#ifdef _WIN32
    return fresh(PlatformContract::Windows);
#elif defined(__APPLE__)
    return fresh(PlatformContract::MacOS);
#else
    return fresh(PlatformContract::Linux);
#endif
}

JournalTestStorage JournalTestStorage::fresh(PlatformContract platform) {
    return JournalTestStorage(std::make_shared<State>(platform, true));
}

JournalTestStorage
JournalTestStorage::seeded(std::string journal_bytes,
                           std::optional<std::string> authority_root_bytes) {
    auto storage = fresh();
    {
        std::lock_guard lock(storage.state_->mutex);
        storage.state_->add_durable_regular(journal_name,
                                            std::move(journal_bytes));
        storage.state_->add_durable_regular(lock_name, {});
        if (authority_root_bytes.has_value()) {
            storage.state_->add_durable_regular(
                root_name, std::move(*authority_root_bytes));
        }
    }
    return storage;
}

JournalTestStorage
JournalTestStorage::seeded(std::string journal_bytes,
                           std::string authority_root_bytes) {
    return seeded(std::move(journal_bytes),
                  std::optional<std::string>(std::move(authority_root_bytes)));
}

JournalTestStorage JournalTestStorage::seeded(std::string journal_bytes,
                                              std::string authority_root_bytes,
                                              PlatformContract platform) {
    auto storage = fresh(platform);
    {
        std::lock_guard lock(storage.state_->mutex);
        storage.state_->add_durable_regular(journal_name,
                                            std::move(journal_bytes));
        storage.state_->add_durable_regular(lock_name, {});
        storage.state_->add_durable_regular(root_name,
                                            std::move(authority_root_bytes));
    }
    return storage;
}

JournalTestStorage
JournalTestStorage::missing_authority_directory(PlatformContract platform) {
    return JournalTestStorage(std::make_shared<State>(platform, false));
}

ImmutableObjectLinkedPublishProbeResult
JournalTestStorage::probe_immutable_object_linked_publish_read(
    PlatformContract platform) {
    constexpr std::string_view bytes = R"({"overlay":"linked"})";
    const std::string digest(64, 'c');
    const auto object_name =
        detail::durable_immutable_object_filename(digest);
    const auto stage_name =
        detail::durable_immutable_object_stage_filename(digest);
    auto storage = fresh(platform);

    auto writer = std::make_unique<Adapter>(storage.state_);
    if (!writer->lock_authority().succeeded()) {
        return {HelperResultKind::FailedBeforeEffect, false, false, false,
                false};
    }
    storage.arm_fault(FaultOperation::ObjectStageCleanup,
                      FaultPosition::Before, FaultAction::Crash);
    const auto creation = writer->create_immutable_object(digest, bytes);
    writer.reset();
    storage.restart();

    bool linked_names_survived_restart = false;
    {
        std::lock_guard lock(storage.state_->mutex);
        const auto *object = storage.state_->live_entry(*object_name);
        const auto *stage = storage.state_->live_entry(*stage_name);
        linked_names_survived_restart =
            object != nullptr && stage != nullptr &&
            object->kind == State::EntryKind::Regular &&
            stage->kind == State::EntryKind::Regular &&
            object->identity == stage->identity &&
            object->live_bytes == bytes && stage->live_bytes == bytes;
    }

    auto reader = std::make_unique<Adapter>(storage.state_);
    const auto locked = reader->lock_authority();
    detail::DurableReadResult read;
    if (locked.succeeded()) {
        read = reader->read_immutable_object(digest, bytes.size());
    } else {
        read.result = locked;
    }
    const auto unlocked = reader->unlock_authority();
    reader.reset();
    const auto snapshot = storage.snapshot();
    return {helper_result_kind(creation.status),
            linked_names_survived_restart,
            read.result.succeeded(),
            read.result.status == DurableFileStatus::Unsupported,
            unlocked.succeeded() && snapshot.open_handles.empty()};
}

JournalTestStorage::JournalTestStorage(const JournalTestStorage &) = default;

JournalTestStorage &
JournalTestStorage::operator=(const JournalTestStorage &) = default;

JournalTestStorage::JournalTestStorage(JournalTestStorage &&) noexcept =
    default;

JournalTestStorage &
JournalTestStorage::operator=(JournalTestStorage &&) noexcept = default;

JournalTestStorage::~JournalTestStorage() = default;

JournalTestStorage JournalTestStorage::clone() const {
    std::lock_guard lock(state_->mutex);
    auto cloned = std::make_shared<State>(state_->platform,
                                          state_->authority_directory_exists);
    cloned->entries = state_->entries;
    for (auto &[name, entry] : cloned->entries) {
        static_cast<void>(name);
        if (entry.live_exists || entry.durable_exists) {
            entry.identity = next_entry_identity.fetch_add(1);
        }
    }
    cloned->unavailable_capabilities = state_->unavailable_capabilities;
    return JournalTestStorage(std::move(cloned));
}

DurableJournal JournalTestStorage::make_journal(JournalLimits limits) {
    return detail::make_durable_journal_for_test(
        std::make_unique<Adapter>(state_), limits);
}

LocalOverlayStore
JournalTestStorage::make_overlay_store(LocalOverlayStoreLimits limits) {
    return detail::make_local_overlay_store_for_test(
        std::make_unique<Adapter>(state_, true), limits);
}

void JournalTestStorage::rename_and_replace_authority_directory() {
    std::lock_guard lock(state_->mutex);
    state_->named_directory_identity =
        next_directory_identity.fetch_add(1);
    state_->entries.clear();
    state_->authority_directory_exists = true;
}

NamespaceSnapshot JournalTestStorage::snapshot() const {
    std::lock_guard lock(state_->mutex);
    NamespaceSnapshot snapshot;
    const auto *journal = state_->live_entry(journal_name);
    if (journal != nullptr && journal->kind == State::EntryKind::Regular) {
        snapshot.journal_bytes = journal->live_bytes;
    }
    const auto *root = state_->live_entry(root_name);
    if (root != nullptr && root->kind == State::EntryKind::Regular) {
        snapshot.authority_root_bytes = root->live_bytes;
    }
    for (const auto &[name, entry] : state_->entries) {
        if (entry.live_exists || entry.durable_exists) {
            switch (entry.kind) {
            case State::EntryKind::Regular:
                snapshot.entry_kinds.emplace(name, "regular");
                break;
            case State::EntryKind::Symlink:
                snapshot.entry_kinds.emplace(name, "symlink");
                break;
            case State::EntryKind::NonRegular:
                snapshot.entry_kinds.emplace(name, "nonregular");
                break;
            case State::EntryKind::ReparsePoint:
                snapshot.entry_kinds.emplace(name, "reparse");
                break;
            }
        }
        if (entry.live_exists) {
            snapshot.relative_paths.insert(name);
            if (entry.kind == State::EntryKind::Regular) {
                snapshot.live_file_bytes.emplace(name, entry.live_bytes);
            }
        }
        if (entry.durable_exists) {
            snapshot.durable_paths.insert(name);
            if (entry.durable_content_available) {
                snapshot.durable_content_available.insert(name);
            }
            if (entry.kind == State::EntryKind::Regular) {
                snapshot.durable_file_bytes.emplace(name, entry.durable_bytes);
            }
        }
    }
    snapshot.observations = state_->observations;
    snapshot.open_handles = state_->open_entries;
    for (const auto handle : state_->open_lock_handles) {
        snapshot.open_handles.insert("authority.lock#handle:" +
                                     std::to_string(handle));
    }
    snapshot.namespace_durable = std::all_of(
        state_->entries.begin(), state_->entries.end(), [](const auto &item) {
            return item.second.live_exists == item.second.durable_exists;
        });
    snapshot.journal_durable =
        journal == nullptr ||
        (journal->durable_exists && journal->durable_content_available &&
         journal->live_bytes == journal->durable_bytes);
    snapshot.authority_root_durable =
        root == nullptr ||
        (root->durable_exists && root->durable_content_available &&
         root->live_bytes == root->durable_bytes);
    snapshot.journal_closed = snapshot.open_handles.empty();
    snapshot.stage_files_absent =
        state_->live_entry(journal_stage_name) == nullptr &&
        state_->live_entry(root_stage_name) == nullptr;
    const auto journal_stage =
        state_->entries.find(std::string(journal_stage_name));
    const auto root_stage = state_->entries.find(std::string(root_stage_name));
    snapshot.durable_stage_files_absent =
        (journal_stage == state_->entries.end() ||
         !journal_stage->second.durable_exists) &&
        (root_stage == state_->entries.end() ||
         !root_stage->second.durable_exists);

    const auto *lock_entry = state_->live_entry(lock_name);
    snapshot.lock_file_present = lock_entry != nullptr;
    snapshot.lock_file_stable =
        lock_entry != nullptr && lock_entry->kind == State::EntryKind::Regular;

    snapshot.all_authority_io_under_lock = true;
    std::optional<std::size_t> first_root_read;
    std::optional<std::size_t> first_journal_read;
    snapshot.authority_mutation_attempted = false;
    for (std::size_t index = 0; index < snapshot.observations.size(); ++index) {
        const auto &observation = snapshot.observations[index];
        if (observation.operation == FaultOperation::RootRead &&
            !first_root_read.has_value()) {
            first_root_read = index;
        }
        if (observation.operation == FaultOperation::JournalRead &&
            !first_journal_read.has_value()) {
            first_journal_read = index;
        }
        snapshot.authority_mutation_attempted |= observation.mutation_attempted;
        if ((is_authority_operation(observation.operation) ||
             observation.mutation_attempted) &&
            !observation.lock_held) {
            snapshot.all_authority_io_under_lock = false;
        }
    }
    snapshot.root_read_before_journal_read =
        !first_journal_read.has_value() ||
        (first_root_read.has_value() && *first_root_read < *first_journal_read);
    return snapshot;
}

void JournalTestStorage::seed_untrusted_file(std::string name,
                                             std::string bytes) {
    std::lock_guard lock(state_->mutex);
    state_->add_durable_regular(name, std::move(bytes));
}

void JournalTestStorage::overwrite_immutable_object(std::string sha256,
                                                    std::string bytes) {
    const auto object_name =
        detail::durable_immutable_object_filename(sha256);
    if (!object_name.has_value()) {
        return;
    }
    std::lock_guard lock(state_->mutex);
    state_->entries[*object_name].kind = State::EntryKind::Regular;
    state_->add_durable_regular(*object_name, std::move(bytes));
}

void JournalTestStorage::remove_immutable_object(std::string sha256) {
    const auto object_name =
        detail::durable_immutable_object_filename(sha256);
    if (!object_name.has_value()) {
        return;
    }
    std::lock_guard lock(state_->mutex);
    state_->entries.erase(*object_name);
}

void JournalTestStorage::overwrite_fixed_child_bytes(FixedAuthorityChild child,
                                                     std::string bytes) {
    const std::map<FixedAuthorityChild, std::string_view> names{
        {FixedAuthorityChild::Journal, journal_name},
        {FixedAuthorityChild::Root, root_name},
        {FixedAuthorityChild::Lock, lock_name},
        {FixedAuthorityChild::JournalStage, journal_stage_name},
        {FixedAuthorityChild::RootStage, root_stage_name},
    };
    std::lock_guard lock(state_->mutex);
    state_->add_durable_regular(names.at(child), std::move(bytes));
}

void JournalTestStorage::replace_fixed_child_file(FixedAuthorityChild child,
                                                  std::string bytes) {
    const std::map<FixedAuthorityChild, std::string_view> names{
        {FixedAuthorityChild::Journal, journal_name},
        {FixedAuthorityChild::Root, root_name},
        {FixedAuthorityChild::Lock, lock_name},
        {FixedAuthorityChild::JournalStage, journal_stage_name},
        {FixedAuthorityChild::RootStage, root_stage_name},
    };
    std::lock_guard lock(state_->mutex);
    auto &entry = state_->entries[std::string(names.at(child))];
    entry = State::Entry{};
    entry.live_bytes = bytes;
    entry.durable_bytes = std::move(bytes);
    entry.live_exists = true;
    entry.durable_exists = true;
    entry.durable_content_available = true;
    entry.identity = next_entry_identity.fetch_add(1);
}

void JournalTestStorage::remove_fixed_child_file(FixedAuthorityChild child) {
    const std::map<FixedAuthorityChild, std::string_view> names{
        {FixedAuthorityChild::Journal, journal_name},
        {FixedAuthorityChild::Root, root_name},
        {FixedAuthorityChild::Lock, lock_name},
        {FixedAuthorityChild::JournalStage, journal_stage_name},
        {FixedAuthorityChild::RootStage, root_stage_name},
    };
    std::lock_guard lock(state_->mutex);
    state_->entries.erase(std::string(names.at(child)));
}

void JournalTestStorage::arm_fault(FaultOperation operation,
                                   FaultPosition position, FaultAction action) {
    arm_fault_sequence(operation, position, {action});
}

void JournalTestStorage::arm_fault_sequence(FaultOperation operation,
                                            FaultPosition position,
                                            std::vector<FaultAction> actions) {
    std::lock_guard lock(state_->mutex);
    state_->fault = State::FaultScript{
        operation, position,
        std::deque<FaultAction>(actions.begin(), actions.end())};
    state_->simulated_crash = false;
}

void JournalTestStorage::restart() {
    std::lock_guard lock(state_->mutex);
    state_->restart();
}

void JournalTestStorage::reset_observations() {
    std::lock_guard lock(state_->mutex);
    state_->observations.clear();
}

std::size_t JournalTestStorage::attempt_count(FaultOperation operation) const {
    std::lock_guard lock(state_->mutex);
    return static_cast<std::size_t>(
        std::count_if(state_->observations.begin(), state_->observations.end(),
                      [&](const OperationObservation &observation) {
                          return observation.operation == operation;
                      }));
}

void JournalTestStorage::make_capability_unavailable(
    DurabilityCapability capability) {
    std::lock_guard lock(state_->mutex);
    state_->unavailable_capabilities.insert(capability);
}

void JournalTestStorage::poison_fixed_child(FixedAuthorityChild child,
                                            UnsupportedEntryKind kind) {
    const std::map<FixedAuthorityChild, std::string_view> names{
        {FixedAuthorityChild::Journal, journal_name},
        {FixedAuthorityChild::Root, root_name},
        {FixedAuthorityChild::Lock, lock_name},
        {FixedAuthorityChild::JournalStage, journal_stage_name},
        {FixedAuthorityChild::RootStage, root_stage_name},
    };
    std::lock_guard lock(state_->mutex);
    auto &entry = state_->entries[std::string(names.at(child))];
    entry.live_exists = true;
    entry.durable_exists = true;
    entry.identity = next_entry_identity.fetch_add(1);
    switch (kind) {
    case UnsupportedEntryKind::Symlink:
        entry.kind = State::EntryKind::Symlink;
        break;
    case UnsupportedEntryKind::NonRegular:
        entry.kind = State::EntryKind::NonRegular;
        break;
    case UnsupportedEntryKind::ReparsePoint:
        entry.kind = State::EntryKind::ReparsePoint;
        break;
    }
}

void JournalTestStorage::pause_after(FaultOperation operation) {
    pause_after_nth(operation, 1);
}

void JournalTestStorage::pause_after_nth(FaultOperation operation,
                                         std::size_t occurrence) {
    std::lock_guard lock(state_->mutex);
    state_->pause_operation = operation;
    state_->pause_occurrence = occurrence;
    state_->pause_consumed = false;
    state_->pause_released = false;
    state_->paused = false;
}

bool JournalTestStorage::destroy_adapter_with_open_lock(bool simulated_crash) {
    auto adapter = std::make_unique<Adapter>(state_);
    const auto locked = adapter->lock_authority();
    {
        std::lock_guard lock(state_->mutex);
        state_->simulated_crash = simulated_crash;
    }
    return locked.succeeded();
}

bool JournalTestStorage::wait_until_paused(std::uint64_t timeout_milliseconds) {
    std::unique_lock lock(state_->mutex);
    return state_->condition.wait_for(
        lock, std::chrono::milliseconds(timeout_milliseconds),
        [&] { return state_->paused; });
}

bool JournalTestStorage::wait_until_lock_waiter(
    std::uint64_t timeout_milliseconds) const {
    std::unique_lock lock(state_->mutex);
    return state_->condition.wait_for(
        lock, std::chrono::milliseconds(timeout_milliseconds),
        [&] { return state_->blocked_lock_waiters != 0; });
}

void JournalTestStorage::release_pause() {
    std::lock_guard lock(state_->mutex);
    state_->pause_released = true;
    state_->condition.notify_all();
}

bool JournalTestStorage::try_unlink_lock_file() {
    std::lock_guard lock(state_->mutex);
    if (state_->platform == PlatformContract::Windows &&
        !state_->open_lock_handles.empty()) {
        return false;
    }
    auto *entry = state_->live_entry(lock_name);
    if (entry == nullptr) {
        return false;
    }
    entry->live_exists = false;
    entry->durable_exists = false;
    return true;
}

bool JournalTestStorage::try_recreate_lock_file() {
    std::lock_guard lock(state_->mutex);
    if (state_->platform == PlatformContract::Windows &&
        !state_->open_lock_handles.empty()) {
        return false;
    }
    if (state_->live_entry(lock_name) != nullptr) {
        return false;
    }
    state_->entries[std::string(lock_name)].identity = 0;
    state_->add_durable_regular(lock_name, {});
    return true;
}

bool JournalTestStorage::try_replace_lock_with_reparse_point() {
    std::lock_guard lock(state_->mutex);
    if (!state_->open_lock_handles.empty()) {
        return false;
    }
    auto *entry = state_->live_entry(lock_name);
    if (entry == nullptr) {
        return false;
    }
    entry->kind = State::EntryKind::ReparsePoint;
    return true;
}

void JournalTestStorage::alias_lock_identity_from(
    const JournalTestStorage &other) {
    if (state_.get() == other.state_.get()) {
        return;
    }
    std::scoped_lock lock(state_->mutex, other.state_->mutex);
    auto *target = state_->live_entry(lock_name);
    const auto *source = other.state_->live_entry(lock_name);
    if (target != nullptr && source != nullptr) {
        target->identity = source->identity;
    }
}

std::uint64_t JournalTestStorage::lock_identity() const {
    std::lock_guard lock(state_->mutex);
    const auto *entry = state_->live_entry(lock_name);
    return entry == nullptr ? 0 : entry->identity;
}

bool operator==(const NamespaceSnapshot &left, const NamespaceSnapshot &right) {
    const auto observations_equal =
        left.observations.size() == right.observations.size() &&
        std::equal(left.observations.begin(), left.observations.end(),
                   right.observations.begin(),
                   [](const OperationObservation &left_observation,
                      const OperationObservation &right_observation) {
                       return left_observation.operation ==
                                  right_observation.operation &&
                              left_observation.lock_held ==
                                  right_observation.lock_held &&
                              left_observation.mutation_attempted ==
                                  right_observation.mutation_attempted &&
                              left_observation.lock_identity ==
                                  right_observation.lock_identity &&
                              left_observation.requested_bytes ==
                                  right_observation.requested_bytes &&
                              left_observation.transferred_bytes ==
                                  right_observation.transferred_bytes;
                   });
    return left.journal_bytes == right.journal_bytes &&
           left.authority_root_bytes == right.authority_root_bytes &&
           left.relative_paths == right.relative_paths &&
           left.durable_paths == right.durable_paths &&
           left.durable_content_available == right.durable_content_available &&
           left.entry_kinds == right.entry_kinds &&
           left.live_file_bytes == right.live_file_bytes &&
           left.durable_file_bytes == right.durable_file_bytes &&
           left.open_handles == right.open_handles && observations_equal &&
           left.namespace_durable == right.namespace_durable &&
           left.journal_durable == right.journal_durable &&
           left.authority_root_durable == right.authority_root_durable &&
           left.journal_closed == right.journal_closed &&
           left.stage_files_absent == right.stage_files_absent &&
           left.durable_stage_files_absent ==
               right.durable_stage_files_absent &&
           left.all_authority_io_under_lock ==
               right.all_authority_io_under_lock &&
           left.lock_file_present == right.lock_file_present &&
           left.lock_file_stable == right.lock_file_stable &&
           left.root_read_before_journal_read ==
               right.root_read_before_journal_read &&
           left.authority_mutation_attempted ==
               right.authority_mutation_attempted;
}

bool same_persistent_state(const NamespaceSnapshot &left,
                           const NamespaceSnapshot &right) {
    return left.journal_bytes == right.journal_bytes &&
           left.authority_root_bytes == right.authority_root_bytes &&
           left.relative_paths == right.relative_paths &&
           left.durable_paths == right.durable_paths &&
           left.durable_content_available == right.durable_content_available &&
           left.entry_kinds == right.entry_kinds &&
           left.live_file_bytes == right.live_file_bytes &&
           left.durable_file_bytes == right.durable_file_bytes &&
           left.namespace_durable == right.namespace_durable &&
           left.journal_durable == right.journal_durable &&
           left.authority_root_durable == right.authority_root_durable &&
           left.stage_files_absent == right.stage_files_absent &&
           left.durable_stage_files_absent ==
               right.durable_stage_files_absent &&
           left.lock_file_present == right.lock_file_present &&
           left.lock_file_stable == right.lock_file_stable;
}

} // namespace lemon::residency::testing
