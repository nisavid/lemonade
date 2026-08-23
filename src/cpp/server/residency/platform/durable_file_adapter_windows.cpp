#include "platform/durable_file_adapter.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace lemon::residency::detail {

namespace {

constexpr std::wstring_view journal_name = L"journal.jsonl";
constexpr std::wstring_view root_name = L"authority-root.json";
constexpr std::wstring_view lock_name = L"authority.lock";
constexpr std::wstring_view journal_stage_name = L".journal.jsonl.stage";
constexpr std::wstring_view root_stage_name = L".authority-root.json.stage";
constexpr std::size_t preflight_collision_attempts = 8;
std::atomic<std::uint64_t> preflight_sequence{0};

struct PreflightNames {
    std::wstring probe;
    std::wstring stage;
};

PreflightNames next_preflight_names() {
    const auto sequence =
        preflight_sequence.fetch_add(1, std::memory_order_relaxed);
    auto probe = L".durability-probe." +
                 std::to_wstring(
                     static_cast<std::uint64_t>(::GetCurrentProcessId())) +
                 L"." + std::to_wstring(sequence);
    return {probe, probe + L".stage"};
}

DurableFileResult make_result(DurableFileStatus status) {
    return {status, {}};
}

DurableFileResult make_windows_result(DurableFileStatus status, DWORD error) {
    return {status, std::to_string(error)};
}

DurableFileResult open_error_result(DWORD error) {
    if (error == ERROR_OPERATION_ABORTED) {
        return make_windows_result(DurableFileStatus::Interrupted, error);
    }
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
        return make_windows_result(DurableFileStatus::NotFound, error);
    }
    if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) {
        return make_windows_result(DurableFileStatus::AlreadyExists, error);
    }
    if (error == ERROR_CANT_ACCESS_FILE || error == ERROR_DIRECTORY ||
        error == ERROR_INVALID_NAME || error == ERROR_REPARSE_TAG_INVALID) {
        return make_windows_result(DurableFileStatus::Unsupported, error);
    }
    return make_windows_result(DurableFileStatus::FailedBeforeEffect, error);
}

void close_best_effort(HANDLE handle) {
    if (handle != INVALID_HANDLE_VALUE) {
        static_cast<void>(::CloseHandle(handle));
    }
}

DurableFileResult close_open_handle(HANDLE handle,
                                    DurableFileResult operation_result) {
    if (!::CloseHandle(handle) && operation_result.succeeded()) {
        return make_windows_result(
            DurableFileStatus::EffectMayHaveOccurred, ::GetLastError());
    }
    return operation_result;
}

std::string file_id_string(const FILE_ID_INFO &information) {
    constexpr char digits[] = "0123456789abcdef";
    std::string encoded = std::to_string(information.VolumeSerialNumber);
    encoded.push_back(':');
    for (const auto byte : information.FileId.Identifier) {
        encoded.push_back(digits[(byte >> 4) & 0x0f]);
        encoded.push_back(digits[byte & 0x0f]);
    }
    return encoded;
}

class WindowsDurableFileChannel final : public DurableFileChannel {
public:
    explicit WindowsDurableFileChannel(HANDLE handle) : handle_(handle) {}

    ~WindowsDurableFileChannel() override { close_best_effort(handle_); }

    DurableWriteResult write_some(std::string_view bytes) override {
        const auto request = static_cast<DWORD>(
            std::min(bytes.size(), std::size_t(MAXDWORD)));
        DWORD written = 0;
        if (!::WriteFile(handle_, bytes.data(), request, &written, nullptr)) {
            const auto error = ::GetLastError();
            return {::lemon::residency::detail::DurableFileResult{
                        ::lemon::residency::detail::DurableFileStatus::
                            EffectMayHaveOccurred,
                        ::std::to_string(error)},
                    0};
        }
        return {::lemon::residency::detail::DurableFileResult{
                    ::lemon::residency::detail::DurableFileStatus::Succeeded,
                    {}},
                written};
    }

    DurableFileResult flush() override {
        if (!::FlushFileBuffers(handle_)) {
            const auto error = ::GetLastError();
            if (error == ERROR_OPERATION_ABORTED) {
                return make_windows_result(DurableFileStatus::Interrupted,
                                           error);
            }
            return make_windows_result(
                DurableFileStatus::EffectMayHaveOccurred, error);
        }
        return make_result(DurableFileStatus::Succeeded);
    }

    DurableFileResult truncate(std::size_t bytes) override {
        if (bytes > static_cast<std::size_t>(
                        std::numeric_limits<LONGLONG>::max())) {
            return make_result(DurableFileStatus::FailedBeforeEffect);
        }
        LARGE_INTEGER position {};
        position.QuadPart = static_cast<LONGLONG>(bytes);
        if (!::SetFilePointerEx(handle_, position, nullptr, FILE_BEGIN)) {
            const auto error = ::GetLastError();
            if (error == ERROR_OPERATION_ABORTED) {
                return make_windows_result(DurableFileStatus::Interrupted,
                                           error);
            }
            return make_windows_result(
                DurableFileStatus::EffectMayHaveOccurred, error);
        }
        if (!::SetEndOfFile(handle_)) {
            const auto error = ::GetLastError();
            if (error == ERROR_OPERATION_ABORTED) {
                return make_windows_result(DurableFileStatus::Interrupted,
                                           error);
            }
            return make_windows_result(
                DurableFileStatus::EffectMayHaveOccurred, error);
        }
        return make_result(DurableFileStatus::Succeeded);
    }

    DurableFileResult close() override {
        const auto handle = std::exchange(handle_, INVALID_HANDLE_VALUE);
        if (!::CloseHandle(handle)) {
            return make_windows_result(
                DurableFileStatus::EffectMayHaveOccurred, ::GetLastError());
        }
        return make_result(DurableFileStatus::Succeeded);
    }

private:
    HANDLE handle_;
};

class WindowsDurableReadChannel final : public DurableReadChannel {
public:
    explicit WindowsDurableReadChannel(HANDLE handle) : handle_(handle) {}

    ~WindowsDurableReadChannel() override { close_best_effort(handle_); }

    DurableReadChunkResult read_some(std::size_t max_bytes) override {
        const DWORD request = static_cast<DWORD>(
            std::min(max_bytes, std::size_t(MAXDWORD)));
        std::string buffer(request, '\0');
        DWORD transferred = 0;
        if (!::ReadFile(handle_, buffer.data(), request, &transferred,
                        nullptr)) {
            const auto error = ::GetLastError();
            if (error == ERROR_OPERATION_ABORTED) {
                return {make_windows_result(DurableFileStatus::Interrupted,
                                            error),
                        {}, false};
            }
            return {make_windows_result(
                        DurableFileStatus::FailedBeforeEffect, error),
                    {}, false};
        }
        buffer.resize(transferred);
        return {make_result(DurableFileStatus::Succeeded), std::move(buffer),
                transferred == 0};
    }

    DurableFileResult close() override {
        const auto handle = std::exchange(handle_, INVALID_HANDLE_VALUE);
        if (!::CloseHandle(handle)) {
            return make_windows_result(
                DurableFileStatus::EffectMayHaveOccurred, ::GetLastError());
        }
        return make_result(DurableFileStatus::Succeeded);
    }

private:
    HANDLE handle_;
};

std::filesystem::path child_path(const std::filesystem::path &directory,
                                 std::wstring_view name) {
    return directory / std::filesystem::path(name);
}

std::optional<std::filesystem::path>
final_directory_path(HANDLE directory_handle) {
    constexpr DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
    auto capacity = ::GetFinalPathNameByHandleW(directory_handle, nullptr, 0,
                                                flags);
    if (capacity == 0) {
        return std::nullopt;
    }
    std::wstring buffer(static_cast<std::size_t>(capacity), L'\0');
    while (true) {
        const auto length = ::GetFinalPathNameByHandleW(
            directory_handle, buffer.data(), capacity, flags);
        if (length == 0) {
            return std::nullopt;
        }
        if (length < capacity) {
            buffer.resize(static_cast<std::size_t>(length));
            return std::filesystem::path(std::move(buffer));
        }
        if (length == MAXDWORD) {
            return std::nullopt;
        }
        capacity = length + 1;
        buffer.assign(static_cast<std::size_t>(capacity), L'\0');
    }
}

DurableFileResult validate_open_regular_single_link(HANDLE handle) {
    FILE_ATTRIBUTE_TAG_INFO attributes {};
    if (!::GetFileInformationByHandleEx(
            handle, FileAttributeTagInfo, &attributes,
            sizeof(attributes))) {
        const auto error = ::GetLastError();
        return make_windows_result(DurableFileStatus::Unsupported, error);
    }
    if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return make_result(DurableFileStatus::Unsupported);
    }
    FILE_STANDARD_INFO standard {};
    if (!::GetFileInformationByHandleEx(handle, FileStandardInfo, &standard,
                                        sizeof(standard))) {
        const auto error = ::GetLastError();
        return make_windows_result(DurableFileStatus::Unsupported, error);
    }
    if (standard.NumberOfLinks != 1) {
        return make_result(DurableFileStatus::Unsupported);
    }
    return make_result(DurableFileStatus::Succeeded);
}

bool same_file_identity(const FILE_ID_INFO &left,
                        const FILE_ID_INFO &right) {
    return left.VolumeSerialNumber == right.VolumeSerialNumber &&
           std::memcmp(left.FileId.Identifier, right.FileId.Identifier,
                       sizeof(left.FileId.Identifier)) == 0;
}

DurableFileResult capture_regular_identity(HANDLE handle,
                                           FILE_ID_INFO &identity) {
    const auto safe = validate_open_regular_single_link(handle);
    if (!safe.succeeded()) {
        return safe;
    }
    if (!::GetFileInformationByHandleEx(handle, FileIdInfo, &identity,
                                        sizeof(identity))) {
        return make_windows_result(DurableFileStatus::Unsupported,
                                   ::GetLastError());
    }
    return make_result(DurableFileStatus::Succeeded);
}

DurableFileResult post_create_failure(DurableFileResult failure) {
    failure.status = DurableFileStatus::EffectMayHaveOccurred;
    return failure;
}

DurableFileResult open_verified_owned_probe(
    const std::filesystem::path &path, const FILE_ID_INFO &expected_identity,
    DWORD access, HANDLE &handle) {
    handle = ::CreateFileW(
        path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return make_windows_result(DurableFileStatus::EffectMayHaveOccurred,
                                   ::GetLastError());
    }
    FILE_ID_INFO observed {};
    const auto safe = capture_regular_identity(handle, observed);
    if (!safe.succeeded() ||
        !same_file_identity(observed, expected_identity)) {
        const auto result =
            safe.succeeded()
                ? make_result(DurableFileStatus::EffectMayHaveOccurred)
                : DurableFileResult{DurableFileStatus::EffectMayHaveOccurred,
                                    safe.diagnostic};
        const auto opened = handle;
        handle = INVALID_HANDLE_VALUE;
        return close_open_handle(opened, result);
    }
    return make_result(DurableFileStatus::Succeeded);
}

DurableFileResult verify_owned_probe(
    const std::filesystem::path &path,
    const FILE_ID_INFO &expected_identity) {
    HANDLE handle = INVALID_HANDLE_VALUE;
    const auto verified = open_verified_owned_probe(
        path, expected_identity, FILE_READ_ATTRIBUTES, handle);
    if (!verified.succeeded()) {
        return verified;
    }
    return close_open_handle(handle, verified);
}

DurableFileResult remove_owned_probe(
    const std::filesystem::path &path,
    const FILE_ID_INFO &expected_identity) {
    HANDLE handle = INVALID_HANDLE_VALUE;
    const auto verified = open_verified_owned_probe(
        path, expected_identity, DELETE | FILE_READ_ATTRIBUTES, handle);
    if (!verified.succeeded()) {
        return verified;
    }
    FILE_DISPOSITION_INFO disposition {TRUE};
    if (!::SetFileInformationByHandle(handle, FileDispositionInfo,
                                      &disposition,
                                      sizeof(disposition))) {
        const auto error = ::GetLastError();
        return close_open_handle(
            handle,
            make_windows_result(DurableFileStatus::EffectMayHaveOccurred,
                                error));
    }
    return close_open_handle(handle,
                             make_result(DurableFileStatus::Succeeded));
}

DurableFileResult verify_replaced_target(
    const std::filesystem::path &target_path,
    const FILE_ID_INFO &expected_identity, std::string_view expected_bytes) {
    const auto handle = ::CreateFileW(
        target_path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto error = ::GetLastError();
        return make_windows_result(DurableFileStatus::EffectMayHaveOccurred,
                                   error);
    }
    FILE_ATTRIBUTE_TAG_INFO attributes {};
    if (!::GetFileInformationByHandleEx(
            handle, FileAttributeTagInfo, &attributes,
            sizeof(attributes))) {
        const auto error = ::GetLastError();
        return close_open_handle(
            handle,
            make_windows_result(DurableFileStatus::EffectMayHaveOccurred,
                                error));
    }
    if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return close_open_handle(
            handle,
            make_result(DurableFileStatus::EffectMayHaveOccurred));
    }
    FILE_STANDARD_INFO standard {};
    if (!::GetFileInformationByHandleEx(handle, FileStandardInfo, &standard,
                                        sizeof(standard))) {
        const auto error = ::GetLastError();
        return close_open_handle(
            handle,
            make_windows_result(DurableFileStatus::EffectMayHaveOccurred,
                                error));
    }
    if (standard.NumberOfLinks != 1) {
        return close_open_handle(
            handle,
            make_result(DurableFileStatus::EffectMayHaveOccurred));
    }
    FILE_ID_INFO identity {};
    if (!::GetFileInformationByHandleEx(handle, FileIdInfo, &identity,
                                        sizeof(identity))) {
        const auto error = ::GetLastError();
        return close_open_handle(
            handle,
            make_windows_result(DurableFileStatus::EffectMayHaveOccurred,
                                error));
    }
    if (!same_file_identity(identity, expected_identity)) {
        return close_open_handle(
            handle,
            make_result(DurableFileStatus::EffectMayHaveOccurred));
    }
    WindowsDurableReadChannel channel(handle);
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

DurableFileResult clear_verified_stage(HANDLE handle) {
    LARGE_INTEGER beginning {};
    while (!::SetFilePointerEx(handle, beginning, nullptr, FILE_BEGIN)) {
        const auto error = ::GetLastError();
        if (error == ERROR_OPERATION_ABORTED) {
            continue;
        }
        return make_windows_result(
            DurableFileStatus::EffectMayHaveOccurred, error);
    }
    while (!::SetEndOfFile(handle)) {
        const auto error = ::GetLastError();
        if (error == ERROR_OPERATION_ABORTED) {
            continue;
        }
        return make_windows_result(
            DurableFileStatus::EffectMayHaveOccurred, error);
    }
    return make_result(DurableFileStatus::Succeeded);
}

enum class WindowsDirectoryBindStatus { Bound, NotFound, Unsafe };

struct WindowsDirectoryBinding {
    WindowsDirectoryBindStatus status = WindowsDirectoryBindStatus::Unsafe;
    std::filesystem::path path;
    HANDLE handle = INVALID_HANDLE_VALUE;
    FILE_ID_INFO identity {};
};

WindowsDirectoryBinding
bind_windows_directory(const std::filesystem::path &directory) {
    const auto handle = ::CreateFileW(
        directory.c_str(), FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto error = ::GetLastError();
        return {error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
                    ? WindowsDirectoryBindStatus::NotFound
                    : WindowsDirectoryBindStatus::Unsafe,
                {}, INVALID_HANDLE_VALUE, {}};
    }
    FILE_ATTRIBUTE_TAG_INFO attributes {};
    FILE_ID_INFO identity {};
    if (!::GetFileInformationByHandleEx(
            handle, FileAttributeTagInfo, &attributes,
            sizeof(attributes)) ||
        (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        !::GetFileInformationByHandleEx(handle, FileIdInfo, &identity,
                                        sizeof(identity))) {
        close_best_effort(handle);
        return {};
    }
    auto bound = final_directory_path(handle);
    if (!bound.has_value()) {
        close_best_effort(handle);
        return {};
    }
    return {WindowsDirectoryBindStatus::Bound, std::move(*bound), handle,
            identity};
}

std::optional<bool>
windows_directory_is_empty(const std::filesystem::path &directory) {
    WIN32_FIND_DATAW entry {};
    const auto pattern = directory / L"*";
    const auto search = ::FindFirstFileW(pattern.c_str(), &entry);
    if (search == INVALID_HANDLE_VALUE) {
        const auto error = ::GetLastError();
        if (error == ERROR_FILE_NOT_FOUND) {
            return true;
        }
        return std::nullopt;
    }
    bool empty = true;
    do {
        const std::wstring_view name(entry.cFileName);
        if (name != L"." && name != L"..") {
            empty = false;
            break;
        }
    } while (::FindNextFileW(search, &entry));
    const auto search_error = ::GetLastError();
    if (!::FindClose(search) ||
        (empty && search_error != ERROR_NO_MORE_FILES)) {
        return std::nullopt;
    }
    return empty;
}

bool close_windows_directory(WindowsDirectoryBinding &binding) {
    const auto handle = std::exchange(binding.handle, INVALID_HANDLE_VALUE);
    return handle == INVALID_HANDLE_VALUE || ::CloseHandle(handle);
}

class WindowsDurableFileAdapter final : public DurableFileAdapter {
public:
    WindowsDurableFileAdapter(std::filesystem::path bound_directory,
                              HANDLE directory_handle)
        : directory_(std::move(bound_directory)),
          directory_handle_(directory_handle) {}

#ifdef LEMONADE_RESIDENCY_DURABLE_TESTING
    WindowsDurableFileAdapter(std::filesystem::path bound_directory,
                              HANDLE directory_handle,
                              DurablePreflightTestFault preflight_test_fault)
        : directory_(std::move(bound_directory)),
          directory_handle_(directory_handle),
          preflight_test_fault_(preflight_test_fault) {}
#endif

    ~WindowsDurableFileAdapter() override {
        close_best_effort(lock_handle_);
        close_best_effort(directory_handle_);
    }

    DurableFileResult lock_authority() override {
        if (!bound() || lock_handle_ != INVALID_HANDLE_VALUE) {
            return make_result(DurableFileStatus::Unsupported);
        }
        const auto lock_path = child_path(directory_, lock_name);
        const auto opened = ::CreateFileW(
            lock_path.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (opened == INVALID_HANDLE_VALUE) {
            return open_error_result(::GetLastError());
        }
        FILE_ATTRIBUTE_TAG_INFO opened_attributes {};
        if (!::GetFileInformationByHandleEx(
                opened, FileAttributeTagInfo, &opened_attributes,
                sizeof(opened_attributes))) {
            return close_open_handle(
                opened, make_windows_result(DurableFileStatus::Unsupported,
                                            ::GetLastError()));
        }
        if ((opened_attributes.FileAttributes &
             FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            (opened_attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) !=
                0) {
            return close_open_handle(
                opened, make_result(DurableFileStatus::Unsupported));
        }
        FILE_STANDARD_INFO opened_standard {};
        if (!::GetFileInformationByHandleEx(opened, FileStandardInfo,
                                            &opened_standard,
                                            sizeof(opened_standard))) {
            return close_open_handle(
                opened, make_windows_result(DurableFileStatus::Unsupported,
                                            ::GetLastError()));
        }
        if (opened_standard.NumberOfLinks == 0) {
            return close_open_handle(
                opened, make_result(DurableFileStatus::Unsupported));
        }

        OVERLAPPED lock_overlapped {};
        if (!::LockFileEx(opened, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD,
                          MAXDWORD, &lock_overlapped)) {
            const auto error = ::GetLastError();
            close_best_effort(opened);
            if (error == ERROR_OPERATION_ABORTED) {
                return make_windows_result(DurableFileStatus::Interrupted,
                                           error);
            }
            return make_windows_result(DurableFileStatus::FailedBeforeEffect,
                                       error);
        }

        FILE_ID_INFO opened_id {};
        if (!::GetFileInformationByHandleEx(opened, FileIdInfo, &opened_id,
                                            sizeof(opened_id))) {
            const auto error = ::GetLastError();
            close_best_effort(opened);
            return make_windows_result(DurableFileStatus::Unsupported,
                                       error);
        }
        const auto current = ::CreateFileW(
            lock_path.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (current == INVALID_HANDLE_VALUE) {
            const auto error = ::GetLastError();
            close_best_effort(opened);
            return make_windows_result(DurableFileStatus::Unsupported,
                                       error);
        }
        FILE_ATTRIBUTE_TAG_INFO current_attributes {};
        if (!::GetFileInformationByHandleEx(
                current, FileAttributeTagInfo, &current_attributes,
                sizeof(current_attributes))) {
            const auto error = ::GetLastError();
            close_best_effort(current);
            close_best_effort(opened);
            return make_windows_result(DurableFileStatus::Unsupported,
                                       error);
        }
        if ((current_attributes.FileAttributes &
             FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            (current_attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) !=
                0) {
            close_best_effort(current);
            close_best_effort(opened);
            return make_result(DurableFileStatus::Unsupported);
        }
        FILE_STANDARD_INFO current_standard {};
        if (!::GetFileInformationByHandleEx(current, FileStandardInfo,
                                            &current_standard,
                                            sizeof(current_standard))) {
            const auto error = ::GetLastError();
            close_best_effort(current);
            close_best_effort(opened);
            return make_windows_result(DurableFileStatus::Unsupported,
                                       error);
        }
        if (current_standard.NumberOfLinks == 0) {
            close_best_effort(current);
            close_best_effort(opened);
            return make_result(DurableFileStatus::Unsupported);
        }
        FILE_ID_INFO current_id {};
        if (!::GetFileInformationByHandleEx(current, FileIdInfo, &current_id,
                                            sizeof(current_id))) {
            const auto error = ::GetLastError();
            close_best_effort(current);
            close_best_effort(opened);
            return make_windows_result(DurableFileStatus::Unsupported,
                                       error);
        }
        if (opened_id.VolumeSerialNumber !=
                current_id.VolumeSerialNumber ||
            std::memcmp(opened_id.FileId.Identifier,
                        current_id.FileId.Identifier,
                        sizeof(opened_id.FileId.Identifier)) != 0) {
            close_best_effort(current);
            close_best_effort(opened);
            return make_result(DurableFileStatus::Unsupported);
        }
        if (!::CloseHandle(current)) {
            const auto error = ::GetLastError();
            close_best_effort(opened);
            return make_windows_result(DurableFileStatus::Unsupported,
                                       error);
        }
        lock_handle_ = opened;
        return make_result(DurableFileStatus::Succeeded);
    }

    DurableIdentityResult authority_identity() override {
        if (directory_handle_ == INVALID_HANDLE_VALUE ||
            lock_handle_ == INVALID_HANDLE_VALUE) {
            return {make_result(DurableFileStatus::Unsupported), {}};
        }
        FILE_ID_INFO directory_id {};
        if (!::GetFileInformationByHandleEx(directory_handle_, FileIdInfo,
                                            &directory_id,
                                            sizeof(directory_id))) {
            return {make_windows_result(DurableFileStatus::Unsupported,
                                        ::GetLastError()),
                    {}};
        }
        FILE_ID_INFO lock_id {};
        if (!::GetFileInformationByHandleEx(lock_handle_, FileIdInfo, &lock_id,
                                            sizeof(lock_id))) {
            return {make_windows_result(DurableFileStatus::Unsupported,
                                        ::GetLastError()),
                    {}};
        }
        return {make_result(DurableFileStatus::Succeeded),
                "directory:" + file_id_string(directory_id) +
                    "/lock:" + file_id_string(lock_id)};
    }

    DurableFileResult preflight_capabilities() override {
        if (!bound() || lock_handle_ == INVALID_HANDLE_VALUE) {
            return make_result(DurableFileStatus::Unsupported);
        }
        for (const auto name : {journal_name, root_name, lock_name,
                                journal_stage_name, root_stage_name}) {
            const auto path = child_path(directory_, name);
            const auto fixed_handle = ::CreateFileW(
                path.c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (fixed_handle == INVALID_HANDLE_VALUE) {
                const auto error = ::GetLastError();
                if (error == ERROR_FILE_NOT_FOUND ||
                    error == ERROR_PATH_NOT_FOUND) {
                    continue;
                }
                return make_windows_result(DurableFileStatus::Unsupported,
                                           error);
            }
            FILE_ATTRIBUTE_TAG_INFO attributes {};
            if (!::GetFileInformationByHandleEx(
                    fixed_handle, FileAttributeTagInfo, &attributes,
                    sizeof(attributes))) {
                return close_open_handle(
                    fixed_handle,
                    make_windows_result(DurableFileStatus::Unsupported,
                                        ::GetLastError()));
            }
            if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) !=
                    0 ||
                (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                return close_open_handle(
                    fixed_handle,
                    make_result(DurableFileStatus::Unsupported));
            }
            FILE_STANDARD_INFO standard {};
            if (!::GetFileInformationByHandleEx(
                    fixed_handle, FileStandardInfo, &standard,
                    sizeof(standard))) {
                return close_open_handle(
                    fixed_handle,
                    make_windows_result(DurableFileStatus::Unsupported,
                                        ::GetLastError()));
            }
            if ((name != lock_name && standard.NumberOfLinks != 1) ||
                (name == lock_name && standard.NumberOfLinks == 0)) {
                return close_open_handle(
                    fixed_handle,
                    make_result(DurableFileStatus::Unsupported));
            }
            const auto closed = ::CloseHandle(fixed_handle);
            if (!closed) {
                return make_windows_result(DurableFileStatus::Unsupported,
                                           ::GetLastError());
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
            const auto probe_path = child_path(directory_, names.probe);
            const auto stage_path = child_path(directory_, names.stage);
            FILE_ID_INFO probe_identity {};
            FILE_ID_INFO stage_identity {};
            bool probe_owned = false;
            bool stage_owned = false;
            const auto cleanup_owned = [&]() {
                auto result = make_result(DurableFileStatus::Succeeded);
                if (stage_owned) {
                    const auto removed =
                        remove_owned_probe(stage_path, stage_identity);
                    if (removed.succeeded()) {
                        stage_owned = false;
                    } else {
                        result = removed;
                    }
                }
                if (probe_owned) {
                    const auto removed =
                        remove_owned_probe(probe_path, probe_identity);
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

            const auto probe_handle = ::CreateFileW(
                probe_path.c_str(), GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                nullptr);
            if (probe_handle == INVALID_HANDLE_VALUE) {
                const auto error = ::GetLastError();
                if (error == ERROR_FILE_EXISTS ||
                    error == ERROR_ALREADY_EXISTS) {
                    continue;
                }
                return terminal_failure(open_error_result(error));
            }
            scratch_create_succeeded = true;
            auto probe_safe =
                capture_regular_identity(probe_handle, probe_identity);
#ifdef LEMONADE_RESIDENCY_DURABLE_TESTING
            probe_safe = apply_created_identity_test_fault(
                DurablePreflightTestFault::ProbeIdentityCaptureFailureOnce,
                std::move(probe_safe));
#endif
            if (!probe_safe.succeeded()) {
                return terminal_failure(close_open_handle(
                    probe_handle, post_create_failure(probe_safe)));
            }
            probe_owned = true;
            WindowsDurableFileChannel probe_channel(probe_handle);
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

            const auto truncate_handle = ::CreateFileW(
                probe_path.c_str(), GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                nullptr);
            if (truncate_handle == INVALID_HANDLE_VALUE) {
                return fail_after_cleanup(
                    open_error_result(::GetLastError()));
            }
            FILE_ID_INFO truncate_identity {};
            const auto truncate_safe = capture_regular_identity(
                truncate_handle, truncate_identity);
            if (!truncate_safe.succeeded() ||
                !same_file_identity(truncate_identity, probe_identity)) {
                const auto result = close_open_handle(
                    truncate_handle,
                    truncate_safe.succeeded()
                        ? make_result(DurableFileStatus::EffectMayHaveOccurred)
                        : post_create_failure(truncate_safe));
                return fail_after_cleanup(result);
            }
            WindowsDurableFileChannel truncate_channel(truncate_handle);
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
            const auto collision = seed_stage_collision_for_test(stage_path);
            if (!collision.succeeded()) {
                return fail_after_cleanup(collision);
            }
#endif
            const auto stage_handle = ::CreateFileW(
                stage_path.c_str(), GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT,
                nullptr);
            if (stage_handle == INVALID_HANDLE_VALUE) {
                const auto error = ::GetLastError();
                const auto cleanup = cleanup_owned();
                if (!cleanup.succeeded()) {
                    return terminal_failure(cleanup);
                }
                if (error == ERROR_FILE_EXISTS ||
                    error == ERROR_ALREADY_EXISTS) {
                    continue;
                }
                return terminal_failure(open_error_result(error));
            }
            scratch_create_succeeded = true;
            auto stage_safe =
                capture_regular_identity(stage_handle, stage_identity);
#ifdef LEMONADE_RESIDENCY_DURABLE_TESTING
            stage_safe = apply_created_identity_test_fault(
                DurablePreflightTestFault::StageIdentityCaptureFailureOnce,
                std::move(stage_safe));
#endif
            if (!stage_safe.succeeded()) {
                return fail_after_cleanup(
                    close_open_handle(stage_handle,
                                      post_create_failure(stage_safe)));
            }
            stage_owned = true;
            WindowsDurableFileChannel stage_channel(stage_handle);
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
            const auto stage_current =
                verify_owned_probe(stage_path, stage_identity);
            const auto probe_current =
                verify_owned_probe(probe_path, probe_identity);
            if (!stage_current.succeeded() || !probe_current.succeeded()) {
                return fail_after_cleanup(
                    !stage_current.succeeded() ? stage_current
                                               : probe_current);
            }
            if (!::MoveFileExW(stage_path.c_str(), probe_path.c_str(),
                               MOVEFILE_REPLACE_EXISTING |
                                   MOVEFILE_WRITE_THROUGH)) {
                return fail_after_cleanup(make_windows_result(
                    DurableFileStatus::EffectMayHaveOccurred,
                    ::GetLastError()));
            }
            probe_identity = stage_identity;
            probe_owned = true;
            stage_owned = false;
            const auto cleanup = cleanup_owned();
            if (!cleanup.succeeded()) {
                return terminal_failure(cleanup);
            }
            return make_result(DurableFileStatus::Succeeded);
        }
        return terminal_failure(make_windows_result(
            DurableFileStatus::AlreadyExists, ERROR_FILE_EXISTS));
    }

    DurableFixedNamespaceResult inspect_fixed_namespace() override {
        if (!bound()) {
            return {make_result(DurableFileStatus::Unsupported), false, false,
                    false, false, false};
        }
        DurableFixedNamespaceResult inspected{
            make_result(DurableFileStatus::Succeeded), false, false, false,
            false, false};
        const std::array<std::pair<std::wstring_view, bool *>, 5> entries{
            std::pair{journal_name, &inspected.journal_present},
            std::pair{root_name, &inspected.root_present},
            std::pair{journal_stage_name, &inspected.journal_stage_present},
            std::pair{root_stage_name, &inspected.root_stage_present},
            std::pair{lock_name, &inspected.lock_present},
        };
        for (const auto &[name, present] : entries) {
            const auto path = child_path(directory_, name);
            const auto handle = ::CreateFileW(
                path.c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (handle == INVALID_HANDLE_VALUE) {
                const auto error = ::GetLastError();
                if (error == ERROR_FILE_NOT_FOUND ||
                    error == ERROR_PATH_NOT_FOUND) {
                    continue;
                }
                inspected.result = make_windows_result(
                    DurableFileStatus::Unsupported, error);
                return inspected;
            }
            FILE_ATTRIBUTE_TAG_INFO attributes {};
            if (!::GetFileInformationByHandleEx(
                    handle, FileAttributeTagInfo, &attributes,
                    sizeof(attributes))) {
                inspected.result = close_open_handle(
                    handle,
                    make_windows_result(DurableFileStatus::Unsupported,
                                        ::GetLastError()));
                return inspected;
            }
            if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) !=
                    0 ||
                (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                inspected.result = close_open_handle(
                    handle, make_result(DurableFileStatus::Unsupported));
                return inspected;
            }
            FILE_STANDARD_INFO standard {};
            if (!::GetFileInformationByHandleEx(
                    handle, FileStandardInfo, &standard,
                    sizeof(standard))) {
                inspected.result = close_open_handle(
                    handle,
                    make_windows_result(DurableFileStatus::Unsupported,
                                        ::GetLastError()));
                return inspected;
            }
            if ((name != lock_name && standard.NumberOfLinks != 1) ||
                (name == lock_name && standard.NumberOfLinks == 0)) {
                inspected.result = close_open_handle(
                    handle, make_result(DurableFileStatus::Unsupported));
                return inspected;
            }
            if (!::CloseHandle(handle)) {
                inspected.result = make_windows_result(
                    DurableFileStatus::Unsupported, ::GetLastError());
                return inspected;
            }
            *present = true;
        }
        return inspected;
    }

    DurableReadResult read_root(std::size_t max_bytes) override {
        if (!bound()) {
            return {make_result(DurableFileStatus::Unsupported), {}, false};
        }
        const auto path = child_path(directory_, root_name);
        const auto handle = ::CreateFileW(
            path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            return {open_error_result(::GetLastError()), {}, false};
        }
        FILE_ATTRIBUTE_TAG_INFO attributes {};
        if (!::GetFileInformationByHandleEx(
                handle, FileAttributeTagInfo, &attributes,
                sizeof(attributes))) {
            return {close_open_handle(
                        handle,
                        make_windows_result(DurableFileStatus::Unsupported,
                                            ::GetLastError())),
                    {}, false};
        }
        if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            return {close_open_handle(
                        handle, make_result(DurableFileStatus::Unsupported)),
                    {}, false};
        }
        FILE_STANDARD_INFO standard {};
        if (!::GetFileInformationByHandleEx(handle, FileStandardInfo,
                                            &standard, sizeof(standard))) {
            return {close_open_handle(
                        handle,
                        make_windows_result(DurableFileStatus::Unsupported,
                                            ::GetLastError())),
                    {}, false};
        }
        if (standard.NumberOfLinks != 1) {
            return {close_open_handle(
                        handle, make_result(DurableFileStatus::Unsupported)),
                    {}, false};
        }
        WindowsDurableReadChannel channel(handle);
        return read_bounded_close(channel, max_bytes);
    }

    DurableReadResult read_journal(std::size_t max_bytes) override {
        if (!bound()) {
            return {make_result(DurableFileStatus::Unsupported), {}, false};
        }
        const auto path = child_path(directory_, journal_name);
        const auto handle = ::CreateFileW(
            path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            return {open_error_result(::GetLastError()), {}, false};
        }
        FILE_ATTRIBUTE_TAG_INFO attributes {};
        if (!::GetFileInformationByHandleEx(
                handle, FileAttributeTagInfo, &attributes,
                sizeof(attributes))) {
            return {close_open_handle(
                        handle,
                        make_windows_result(DurableFileStatus::Unsupported,
                                            ::GetLastError())),
                    {}, false};
        }
        if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            return {close_open_handle(
                        handle, make_result(DurableFileStatus::Unsupported)),
                    {}, false};
        }
        FILE_STANDARD_INFO standard {};
        if (!::GetFileInformationByHandleEx(handle, FileStandardInfo,
                                            &standard, sizeof(standard))) {
            return {close_open_handle(
                        handle,
                        make_windows_result(DurableFileStatus::Unsupported,
                                            ::GetLastError())),
                    {}, false};
        }
        if (standard.NumberOfLinks != 1) {
            return {close_open_handle(
                        handle, make_result(DurableFileStatus::Unsupported)),
                    {}, false};
        }
        WindowsDurableReadChannel channel(handle);
        return read_bounded_close(channel, max_bytes);
    }

    DurableReadResult
    read_immutable_object(std::string_view sha256,
                          std::size_t max_bytes) override {
        if (!bound()) {
            return {make_result(DurableFileStatus::Unsupported), {}, false};
        }
        const auto object_name = durable_immutable_object_filename(sha256);
        if (!object_name.has_value()) {
            return {make_result(DurableFileStatus::Unsupported), {}, false};
        }
        const std::wstring wide_name(object_name->begin(),
                                     object_name->end());
        const auto path = child_path(directory_, wide_name);
        const auto handle = ::CreateFileW(
            path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            return {open_error_result(::GetLastError()), {}, false};
        }
        const auto safe = validate_open_regular_single_link(handle);
        if (!safe.succeeded()) {
            return {close_open_handle(handle, safe), {}, false};
        }
        WindowsDurableReadChannel channel(handle);
        return read_bounded_close(channel, max_bytes);
    }

    DurableFileResult
    create_immutable_object(std::string_view sha256,
                            std::string_view bytes) override {
        if (!bound()) {
            return make_result(DurableFileStatus::Unsupported);
        }
        const auto object_name = durable_immutable_object_filename(sha256);
        const auto stage_name =
            durable_immutable_object_stage_filename(sha256);
        if (!object_name.has_value() || !stage_name.has_value()) {
            return make_result(DurableFileStatus::Unsupported);
        }
        const std::wstring wide_object_name(object_name->begin(),
                                            object_name->end());
        const std::wstring wide_stage_name(stage_name->begin(),
                                           stage_name->end());
        const auto target_path = child_path(directory_, wide_object_name);
        const auto stage_path = child_path(directory_, wide_stage_name);

        const auto recovered_stage_handle = ::CreateFileW(
            stage_path.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (recovered_stage_handle != INVALID_HANDLE_VALUE) {
            FILE_ID_INFO recovered_stage_identity {};
            const auto safe = capture_regular_identity(
                recovered_stage_handle, recovered_stage_identity);
            const auto closed = close_open_handle(recovered_stage_handle,
                                                  safe);
            if (!closed.succeeded()) {
                return closed;
            }
            const auto removed = remove_owned_probe(
                stage_path, recovered_stage_identity);
            if (!removed.succeeded()) {
                return removed;
            }
        } else {
            const auto error = ::GetLastError();
            if (error != ERROR_FILE_NOT_FOUND &&
                error != ERROR_PATH_NOT_FOUND) {
                return open_error_result(error);
            }
        }

        const auto existing_attributes = ::GetFileAttributesW(
            target_path.c_str());
        if (existing_attributes != INVALID_FILE_ATTRIBUTES) {
            return make_result(DurableFileStatus::AlreadyExists);
        }
        const auto target_error = ::GetLastError();
        if (target_error != ERROR_FILE_NOT_FOUND &&
            target_error != ERROR_PATH_NOT_FOUND) {
            return open_error_result(target_error);
        }

        const auto handle = ::CreateFileW(
            stage_path.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            const auto error = ::GetLastError();
            if (error == ERROR_FILE_EXISTS ||
                error == ERROR_ALREADY_EXISTS) {
                return make_windows_result(DurableFileStatus::Unsupported,
                                           error);
            }
            return open_error_result(error);
        }
        FILE_ID_INFO object_identity {};
        const auto safe = capture_regular_identity(handle, object_identity);
        if (!safe.succeeded()) {
            return close_open_handle(handle, post_create_failure(safe));
        }
        WindowsDurableFileChannel channel(handle);
        const auto persisted = write_flush_close(channel, bytes);
        if (!persisted.succeeded()) {
            if (persisted.effect_may_have_occurred()) {
                return persisted;
            }
            return {DurableFileStatus::EffectMayHaveOccurred,
                    persisted.diagnostic};
        }
        const auto stage_current = verify_owned_probe(
            stage_path, object_identity);
        if (!stage_current.succeeded()) {
            return stage_current;
        }
        if (!::MoveFileExW(stage_path.c_str(), target_path.c_str(),
                           MOVEFILE_WRITE_THROUGH)) {
            const auto error = ::GetLastError();
            const auto removed = remove_owned_probe(stage_path,
                                                    object_identity);
            if (!removed.succeeded()) {
                return removed;
            }
            if (error == ERROR_FILE_EXISTS ||
                error == ERROR_ALREADY_EXISTS) {
                return make_windows_result(DurableFileStatus::AlreadyExists,
                                           error);
            }
            return make_windows_result(
                DurableFileStatus::EffectMayHaveOccurred, error);
        }
        return verify_replaced_target(target_path, object_identity, bytes);
    }

    DurableFileResult create_journal(std::string_view bytes) override {
        if (!bound()) {
            return make_result(DurableFileStatus::Unsupported);
        }
        const auto path = child_path(directory_, journal_name);
        const auto handle = ::CreateFileW(
            path.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            return open_error_result(::GetLastError());
        }
        FILE_ATTRIBUTE_TAG_INFO attributes {};
        if (!::GetFileInformationByHandleEx(
                handle, FileAttributeTagInfo, &attributes,
                sizeof(attributes))) {
            return close_open_handle(
                handle, make_windows_result(
                            DurableFileStatus::EffectMayHaveOccurred,
                            ::GetLastError()));
        }
        if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            return close_open_handle(
                handle,
                make_result(DurableFileStatus::EffectMayHaveOccurred));
        }
        FILE_STANDARD_INFO standard {};
        if (!::GetFileInformationByHandleEx(handle, FileStandardInfo,
                                            &standard, sizeof(standard))) {
            return close_open_handle(
                handle, make_windows_result(
                            DurableFileStatus::EffectMayHaveOccurred,
                            ::GetLastError()));
        }
        if (standard.NumberOfLinks != 1) {
            return close_open_handle(
                handle,
                make_result(DurableFileStatus::EffectMayHaveOccurred));
        }
        WindowsDurableFileChannel channel(handle);
        const auto persisted = write_flush_close(channel, bytes);
        if (!persisted.succeeded() &&
            !persisted.effect_may_have_occurred()) {
            return {DurableFileStatus::EffectMayHaveOccurred,
                    persisted.diagnostic};
        }
        return persisted;
    }

    DurableFileResult append_journal(std::string_view bytes) override {
        if (!bound()) {
            return make_result(DurableFileStatus::Unsupported);
        }
        const auto path = child_path(directory_, journal_name);
        const auto handle = ::CreateFileW(
            path.c_str(), FILE_APPEND_DATA | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            return open_error_result(::GetLastError());
        }
        FILE_ATTRIBUTE_TAG_INFO attributes {};
        if (!::GetFileInformationByHandleEx(
                handle, FileAttributeTagInfo, &attributes,
                sizeof(attributes))) {
            return close_open_handle(
                handle,
                make_windows_result(DurableFileStatus::FailedBeforeEffect,
                                    ::GetLastError()));
        }
        if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            return close_open_handle(
                handle, make_result(DurableFileStatus::Unsupported));
        }
        FILE_STANDARD_INFO standard {};
        if (!::GetFileInformationByHandleEx(handle, FileStandardInfo,
                                            &standard, sizeof(standard))) {
            return close_open_handle(
                handle,
                make_windows_result(DurableFileStatus::FailedBeforeEffect,
                                    ::GetLastError()));
        }
        if (standard.NumberOfLinks != 1) {
            return close_open_handle(
                handle, make_result(DurableFileStatus::Unsupported));
        }
        WindowsDurableFileChannel channel(handle);
        return write_flush_close(channel, bytes);
    }

    DurableFileResult truncate_journal(std::size_t bytes) override {
        if (!bound()) {
            return make_result(DurableFileStatus::Unsupported);
        }
        const auto path = child_path(directory_, journal_name);
        const auto handle = ::CreateFileW(
            path.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            return open_error_result(::GetLastError());
        }
        FILE_ATTRIBUTE_TAG_INFO attributes {};
        if (!::GetFileInformationByHandleEx(
                handle, FileAttributeTagInfo, &attributes,
                sizeof(attributes))) {
            return close_open_handle(
                handle,
                make_windows_result(DurableFileStatus::FailedBeforeEffect,
                                    ::GetLastError()));
        }
        if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            return close_open_handle(
                handle, make_result(DurableFileStatus::Unsupported));
        }
        FILE_STANDARD_INFO standard {};
        if (!::GetFileInformationByHandleEx(handle, FileStandardInfo,
                                            &standard, sizeof(standard))) {
            return close_open_handle(
                handle,
                make_windows_result(DurableFileStatus::FailedBeforeEffect,
                                    ::GetLastError()));
        }
        if (standard.NumberOfLinks != 1) {
            return close_open_handle(
                handle, make_result(DurableFileStatus::Unsupported));
        }
        WindowsDurableFileChannel channel(handle);
        return truncate_flush_close(channel, bytes);
    }

    DurableFileResult replace_journal(std::string_view bytes) override {
        if (!bound()) {
            return make_result(DurableFileStatus::Unsupported);
        }
        const auto stage_path = child_path(directory_, journal_stage_name);
        const auto target_path = child_path(directory_, journal_name);
        const auto handle = ::CreateFileW(
            stage_path.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            return open_error_result(::GetLastError());
        }
        FILE_ATTRIBUTE_TAG_INFO attributes {};
        if (!::GetFileInformationByHandleEx(
                handle, FileAttributeTagInfo, &attributes,
                sizeof(attributes))) {
            return close_open_handle(
                handle, make_windows_result(
                            DurableFileStatus::EffectMayHaveOccurred,
                            ::GetLastError()));
        }
        if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            return close_open_handle(
                handle,
                make_result(DurableFileStatus::EffectMayHaveOccurred));
        }
        FILE_STANDARD_INFO standard {};
        if (!::GetFileInformationByHandleEx(handle, FileStandardInfo,
                                            &standard, sizeof(standard))) {
            return close_open_handle(
                handle, make_windows_result(
                            DurableFileStatus::EffectMayHaveOccurred,
                            ::GetLastError()));
        }
        if (standard.NumberOfLinks != 1) {
            return close_open_handle(
                handle,
                make_result(DurableFileStatus::EffectMayHaveOccurred));
        }
        FILE_ID_INFO staged_identity {};
        if (!::GetFileInformationByHandleEx(
                handle, FileIdInfo, &staged_identity,
                sizeof(staged_identity))) {
            const auto error = ::GetLastError();
            return close_open_handle(
                handle, make_windows_result(
                            DurableFileStatus::EffectMayHaveOccurred, error));
        }
        const auto cleared = clear_verified_stage(handle);
        if (!cleared.succeeded()) {
            return close_open_handle(handle, cleared);
        }
        WindowsDurableFileChannel channel(handle);
        const auto staged = write_flush_close(channel, bytes);
        if (!staged.succeeded() && !staged.effect_may_have_occurred()) {
            return {DurableFileStatus::EffectMayHaveOccurred,
                    staged.diagnostic};
        }
        if (!staged.succeeded()) {
            return staged;
        }
        if (!::MoveFileExW(stage_path.c_str(), target_path.c_str(),
                           MOVEFILE_REPLACE_EXISTING |
                               MOVEFILE_WRITE_THROUGH)) {
            return make_windows_result(
                DurableFileStatus::EffectMayHaveOccurred, ::GetLastError());
        }
        return verify_replaced_target(target_path, staged_identity, bytes);
    }

    DurableFileResult replace_root(std::string_view bytes) override {
        if (!bound()) {
            return make_result(DurableFileStatus::Unsupported);
        }
        const auto stage_path = child_path(directory_, root_stage_name);
        const auto target_path = child_path(directory_, root_name);
        const auto handle = ::CreateFileW(
            stage_path.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            return open_error_result(::GetLastError());
        }
        FILE_ATTRIBUTE_TAG_INFO attributes {};
        if (!::GetFileInformationByHandleEx(
                handle, FileAttributeTagInfo, &attributes,
                sizeof(attributes))) {
            return close_open_handle(
                handle, make_windows_result(
                            DurableFileStatus::EffectMayHaveOccurred,
                            ::GetLastError()));
        }
        if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
            (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            return close_open_handle(
                handle,
                make_result(DurableFileStatus::EffectMayHaveOccurred));
        }
        FILE_STANDARD_INFO standard {};
        if (!::GetFileInformationByHandleEx(handle, FileStandardInfo,
                                            &standard, sizeof(standard))) {
            return close_open_handle(
                handle, make_windows_result(
                            DurableFileStatus::EffectMayHaveOccurred,
                            ::GetLastError()));
        }
        if (standard.NumberOfLinks != 1) {
            return close_open_handle(
                handle,
                make_result(DurableFileStatus::EffectMayHaveOccurred));
        }
        FILE_ID_INFO staged_identity {};
        if (!::GetFileInformationByHandleEx(
                handle, FileIdInfo, &staged_identity,
                sizeof(staged_identity))) {
            const auto error = ::GetLastError();
            return close_open_handle(
                handle, make_windows_result(
                            DurableFileStatus::EffectMayHaveOccurred, error));
        }
        const auto cleared = clear_verified_stage(handle);
        if (!cleared.succeeded()) {
            return close_open_handle(handle, cleared);
        }
        WindowsDurableFileChannel channel(handle);
        const auto staged = write_flush_close(channel, bytes);
        if (!staged.succeeded() && !staged.effect_may_have_occurred()) {
            return {DurableFileStatus::EffectMayHaveOccurred,
                    staged.diagnostic};
        }
        if (!staged.succeeded()) {
            return staged;
        }
        if (!::MoveFileExW(stage_path.c_str(), target_path.c_str(),
                           MOVEFILE_REPLACE_EXISTING |
                               MOVEFILE_WRITE_THROUGH)) {
            return make_windows_result(
                DurableFileStatus::EffectMayHaveOccurred, ::GetLastError());
        }
        return verify_replaced_target(target_path, staged_identity, bytes);
    }

    DurableFileResult unlock_authority() override {
        if (lock_handle_ == INVALID_HANDLE_VALUE) {
            return make_result(DurableFileStatus::Unsupported);
        }
        const auto handle = std::exchange(lock_handle_, INVALID_HANDLE_VALUE);
        OVERLAPPED unlock_overlapped {};
        const auto unlock_result = UnlockFileEx(
            handle, 0, MAXDWORD, MAXDWORD, &unlock_overlapped);
        const auto unlock_error =
            unlock_result ? ERROR_SUCCESS : ::GetLastError();
        const auto close_result = CloseHandle(handle);
        const auto close_error =
            close_result ? ERROR_SUCCESS : ::GetLastError();
        if (!unlock_result && !close_result) {
            return make_result(DurableFileStatus::EffectMayHaveOccurred);
        }
        if (!unlock_result) {
            return make_windows_result(
                DurableFileStatus::EffectMayHaveOccurred, unlock_error);
        }
        if (!close_result) {
            return make_windows_result(
                DurableFileStatus::EffectMayHaveOccurred, close_error);
        }
        return make_result(DurableFileStatus::Succeeded);
    }

private:
    bool bound() const noexcept {
        return directory_handle_ != INVALID_HANDLE_VALUE &&
               !directory_.empty();
    }

    std::filesystem::path directory_;
    HANDLE directory_handle_ = INVALID_HANDLE_VALUE;
    HANDLE lock_handle_ = INVALID_HANDLE_VALUE;
#ifdef LEMONADE_RESIDENCY_DURABLE_TESTING
    DurableFileResult seed_stage_collision_for_test(
        const std::filesystem::path &path) noexcept {
        if (preflight_test_fault_ !=
            DurablePreflightTestFault::StageCreateCollisionExhaustion) {
            return make_result(DurableFileStatus::Succeeded);
        }
        const auto handle = ::CreateFileW(
            path.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            return open_error_result(::GetLastError());
        }
        WindowsDurableFileChannel channel(handle);
        return write_flush_close(channel, "caller-owned-stage-collision");
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
    const auto directory_handle = ::CreateFileW(
        directory.c_str(), FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (directory_handle == INVALID_HANDLE_VALUE) {
        return std::make_unique<WindowsDurableFileAdapter>(
            std::filesystem::path{}, INVALID_HANDLE_VALUE);
    }
    FILE_ATTRIBUTE_TAG_INFO attributes {};
    if (!::GetFileInformationByHandleEx(
            directory_handle, FileAttributeTagInfo, &attributes,
            sizeof(attributes)) ||
        (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        close_best_effort(directory_handle);
        return std::make_unique<WindowsDurableFileAdapter>(
            std::filesystem::path{}, INVALID_HANDLE_VALUE);
    }
    auto bound_directory = final_directory_path(directory_handle);
    if (!bound_directory.has_value()) {
        close_best_effort(directory_handle);
        return std::make_unique<WindowsDurableFileAdapter>(
            std::filesystem::path{}, INVALID_HANDLE_VALUE);
    }
    return std::make_unique<WindowsDurableFileAdapter>(
        std::move(*bound_directory), directory_handle);
}

std::unique_ptr<DurableFileAdapter>
make_platform_durable_file_adapter_in_fixed_namespace(
    const std::filesystem::path &parent_directory,
    std::string_view child_namespace) {
    const auto unbound = [] {
        return std::make_unique<WindowsDurableFileAdapter>(
            std::filesystem::path{}, INVALID_HANDLE_VALUE);
    };
    if (!durable_fixed_namespace_name_is_valid(child_namespace)) {
        return unbound();
    }

    auto parent = bind_windows_directory(parent_directory);
    if (parent.status != WindowsDirectoryBindStatus::Bound) {
        return unbound();
    }
    const std::wstring child_name(child_namespace.begin(),
                                  child_namespace.end());
    const auto child = parent.path / child_name;
    const auto stage = parent.path /
                       (L"." + child_name + L".directory-stage");

    const auto cleanup_stage = [&](std::optional<FILE_ID_INFO> expected) {
        auto bound_stage = bind_windows_directory(stage);
        if (bound_stage.status == WindowsDirectoryBindStatus::NotFound) {
            return true;
        }
        if (bound_stage.status != WindowsDirectoryBindStatus::Bound ||
            (expected.has_value() &&
             !same_file_identity(bound_stage.identity, *expected))) {
            close_windows_directory(bound_stage);
            return false;
        }
        const auto empty = windows_directory_is_empty(bound_stage.path);
        const auto bound_stage_path = bound_stage.path;
        const auto stage_closed = close_windows_directory(bound_stage);
        if (!empty.has_value() || !*empty || !stage_closed) {
            return false;
        }
        return ::RemoveDirectoryW(bound_stage_path.c_str()) != 0;
    };
    const auto release_parent = [&] {
        return close_windows_directory(parent);
    };
    const auto reject = [&](WindowsDirectoryBinding *child_binding = nullptr) {
        if (child_binding != nullptr) {
            close_windows_directory(*child_binding);
        }
        release_parent();
        return unbound();
    };
    const auto publish = [&](WindowsDirectoryBinding &child_binding) {
        if (!release_parent()) {
            return reject(&child_binding);
        }
        return std::make_unique<WindowsDurableFileAdapter>(
            std::move(child_binding.path),
            std::exchange(child_binding.handle, INVALID_HANDLE_VALUE));
    };

    auto bound_child = bind_windows_directory(child);
    if (bound_child.status == WindowsDirectoryBindStatus::Bound) {
        if (!cleanup_stage(std::nullopt)) {
            return reject(&bound_child);
        }
        return publish(bound_child);
    }
    if (bound_child.status != WindowsDirectoryBindStatus::NotFound) {
        return reject();
    }

    if (!::CreateDirectoryW(stage.c_str(), nullptr)) {
        const auto error = ::GetLastError();
        if (error != ERROR_ALREADY_EXISTS && error != ERROR_FILE_EXISTS) {
            return reject();
        }
    }
    auto bound_stage = bind_windows_directory(stage);
    if (bound_stage.status != WindowsDirectoryBindStatus::Bound) {
        return reject();
    }
    const auto empty_stage = windows_directory_is_empty(bound_stage.path);
    const auto stage_identity = bound_stage.identity;
    const auto stage_closed = close_windows_directory(bound_stage);
    if (!empty_stage.has_value() || !*empty_stage || !stage_closed) {
        return reject();
    }

    const auto moved = ::MoveFileExW(stage.c_str(), child.c_str(),
                                     MOVEFILE_WRITE_THROUGH);
    auto published_child = bind_windows_directory(child);
    if (published_child.status != WindowsDirectoryBindStatus::Bound ||
        (!moved && !cleanup_stage(stage_identity)) ||
        (moved && !same_file_identity(published_child.identity,
                                      stage_identity))) {
        return reject(&published_child);
    }
    if (moved) {
        const auto empty_child =
            windows_directory_is_empty(published_child.path);
        if (!empty_child.has_value() || !*empty_child) {
            return reject(&published_child);
        }
    }
    return publish(published_child);
}

#ifdef LEMONADE_RESIDENCY_DURABLE_TESTING
std::unique_ptr<DurableFileAdapter>
make_platform_durable_file_adapter_for_test(
    const std::filesystem::path &directory, DurablePreflightTestFault fault) {
    const auto directory_handle = ::CreateFileW(
        directory.c_str(), FILE_READ_ATTRIBUTES | SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (directory_handle == INVALID_HANDLE_VALUE) {
        return std::make_unique<WindowsDurableFileAdapter>(
            std::filesystem::path{}, INVALID_HANDLE_VALUE, fault);
    }
    FILE_ATTRIBUTE_TAG_INFO attributes {};
    if (!::GetFileInformationByHandleEx(
            directory_handle, FileAttributeTagInfo, &attributes,
            sizeof(attributes)) ||
        (attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
        (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        close_best_effort(directory_handle);
        return std::make_unique<WindowsDurableFileAdapter>(
            std::filesystem::path{}, INVALID_HANDLE_VALUE, fault);
    }
    auto bound_directory = final_directory_path(directory_handle);
    if (!bound_directory.has_value()) {
        close_best_effort(directory_handle);
        return std::make_unique<WindowsDurableFileAdapter>(
            std::filesystem::path{}, INVALID_HANDLE_VALUE, fault);
    }
    return std::make_unique<WindowsDurableFileAdapter>(
        std::move(*bound_directory), directory_handle, fault);
}
#endif

} // namespace lemon::residency::detail
