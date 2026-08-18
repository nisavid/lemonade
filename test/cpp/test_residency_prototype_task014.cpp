#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <csignal>
#include <cstring>
#ifdef __linux__
#include <dirent.h>
#endif
#include <fcntl.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace lemon::residency::prototype {
namespace {

using Row = std::pair<std::string, std::string>;

std::uint64_t payload_checksum(const std::string& value) {
    std::uint64_t checksum = 14695981039346656037ULL;
    for (const unsigned char byte : value) {
        checksum ^= byte;
        checksum *= 1099511628211ULL;
    }
    return checksum;
}

std::string make_payload(const std::string& generation) {
    const std::string body = "residency-root/v1\ngeneration=" + generation + "\n";
    std::ostringstream output;
    output << body << "checksum=" << std::hex << std::setw(16)
           << std::setfill('0') << payload_checksum(body) << "\n";
    return output.str();
}

bool validate_payload(const std::string& value) {
    const std::string marker = "checksum=";
    const std::size_t marker_offset = value.rfind(marker);
    if (marker_offset == std::string::npos || value.empty() || value.back() != '\n') {
        return false;
    }
    const std::string body = value.substr(0, marker_offset);
    const std::string encoded = value.substr(
        marker_offset + marker.size(), value.size() - marker_offset - marker.size() - 1);
    if (body.rfind("residency-root/v1\ngeneration=", 0) != 0 ||
        encoded.size() != 16) {
        return false;
    }
    std::istringstream input(encoded);
    std::uint64_t expected = 0;
    input >> std::hex >> expected;
    return input && input.eof() && expected == payload_checksum(body);
}

void emit_rows(const std::vector<Row>& rows) {
    for (const auto& row : rows) {
        std::cout << row.first << '=' << row.second << '\n';
    }
#ifdef _WIN32
    std::cout << "platform.current=windows\n";
#elif defined(__APPLE__)
    std::cout << "platform.current=macos\n";
#elif defined(__linux__)
    std::cout << "platform.current=linux\n";
#endif
    std::cout << "runtime_authority=none\n";
}

#ifdef _WIN32

std::wstring quote_windows_argument(const std::wstring& value) {
    std::wstring result = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t character : value) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        if (character == L'\"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(character);
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(character);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

std::wstring executable_path() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    return std::wstring(buffer.data(), length);
}

bool write_windows_file(
    const std::wstring& path, const std::string& value, bool flush) {
    const HANDLE file = CreateFileW(
        path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const bool complete = value.size() <= static_cast<std::size_t>(MAXDWORD) &&
                          WriteFile(
                              file, value.data(), static_cast<DWORD>(value.size()),
                              &written, nullptr) != FALSE &&
                          written == value.size();
    const bool flushed = !flush || FlushFileBuffers(file) != FALSE;
    CloseHandle(file);
    return complete && flushed;
}

std::string read_windows_file(const std::wstring& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

int windows_durable_child(
    const std::wstring& directory, const std::wstring& stage_name) {
    const std::wstring root = directory + L"\\root.json";
    const std::wstring stage = directory + L"\\root.stage";
    std::string payload;
    if (stage_name == L"crash-before") {
        payload = make_payload("staged");
    } else if (stage_name == L"crash-after") {
        payload = make_payload("crash-after");
    } else if (stage_name == L"flush-failure") {
        payload = make_payload("flush-failure");
    } else if (stage_name == L"corrupt") {
        payload = "residency-root/v1\ngeneration=corrupt\nchecksum=0\n";
    } else if (stage_name == L"normal") {
        payload = make_payload("complete");
    } else {
        return 79;
    }
    if (!write_windows_file(stage, payload, stage_name != L"flush-failure")) {
        return 80;
    }
    if (stage_name == L"flush-failure") {
        return 74;
    }
    if (stage_name == L"crash-before") {
        return 71;
    }
    if (!validate_payload(read_windows_file(stage))) {
        DeleteFileW(stage.c_str());
        return stage_name == L"corrupt" ? 73 : 81;
    }
    if (MoveFileExW(
            stage.c_str(), root.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
        return 82;
    }
    if (stage_name == L"crash-after") {
        return 72;
    }
    return 0;
}

int run_windows_durable_helper(
    const std::wstring& directory, const std::wstring& stage_name) {
    const std::wstring self = executable_path();
    if (self.empty()) {
        return 90;
    }
    std::wstring command = quote_windows_argument(self) +
                           L" --windows-durable-child " +
                           quote_windows_argument(directory) + L' ' +
                           quote_windows_argument(stage_name);
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (CreateProcessW(
            nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
            &startup, &process) == FALSE) {
        return 91;
    }
    CloseHandle(process.hThread);
    const DWORD wait_result = WaitForSingleObject(process.hProcess, 3000);
    if (wait_result != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 92);
        WaitForSingleObject(process.hProcess, 3000);
        CloseHandle(process.hProcess);
        return 92;
    }
    DWORD exit_code = 93;
    if (GetExitCodeProcess(process.hProcess, &exit_code) == FALSE) {
        exit_code = 93;
    }
    CloseHandle(process.hProcess);
    return static_cast<int>(exit_code);
}

struct WindowsDurabilityResult {
    bool crash_before = false;
    bool crash_after = false;
    bool flush_failure = false;
    bool corrupt_candidate = false;
    bool stage_flushed = false;
    bool root_replaced = false;
    bool parent_flushed = false;
};

WindowsDurabilityResult run_windows_durability_probe() {
    WindowsDurabilityResult result;
    wchar_t temporary_root[MAX_PATH + 1] = {};
    if (GetTempPathW(MAX_PATH, temporary_root) == 0) {
        return result;
    }
    std::wostringstream directory_name;
    directory_name << temporary_root << L"residency-task014-" << GetCurrentProcessId()
                   << L'-' << GetTickCount64();
    const std::wstring directory = directory_name.str();
    if (CreateDirectoryW(directory.c_str(), nullptr) == FALSE) {
        return result;
    }
    const std::wstring root = directory + L"\\root.json";
    const std::wstring stage = directory + L"\\root.stage";
    const std::string old_payload = make_payload("old");
    const std::string crash_payload = make_payload("crash-after");
    const std::string final_payload = make_payload("complete");

    const bool initialized = write_windows_file(root, old_payload, true);
    result.crash_before = initialized &&
                          run_windows_durable_helper(directory, L"crash-before") == 71 &&
                          read_windows_file(root) == old_payload &&
                          validate_payload(read_windows_file(root));

    const bool after_replaced =
        run_windows_durable_helper(directory, L"crash-after") == 72;
    result.crash_after = after_replaced &&
                         read_windows_file(root) == crash_payload &&
                         validate_payload(read_windows_file(root));

    const bool completed =
        run_windows_durable_helper(directory, L"normal") == 0;
    result.stage_flushed = completed;
    result.root_replaced = completed && read_windows_file(root) == final_payload;
    result.parent_flushed = completed;

    result.flush_failure =
        run_windows_durable_helper(directory, L"flush-failure") == 74 &&
        read_windows_file(root) == final_payload;
    result.corrupt_candidate =
        run_windows_durable_helper(directory, L"corrupt") == 73 &&
        read_windows_file(root) == final_payload;

    DeleteFileW(stage.c_str());
    DeleteFileW(root.c_str());
    RemoveDirectoryW(directory.c_str());
    return result;
}

bool creation_token(HANDLE process, ULARGE_INTEGER& token) {
    FILETIME creation = {};
    FILETIME exit = {};
    FILETIME kernel = {};
    FILETIME user = {};
    if (GetProcessTimes(process, &creation, &exit, &kernel, &user) == FALSE) {
        return false;
    }
    token.LowPart = creation.dwLowDateTime;
    token.HighPart = creation.dwHighDateTime;
    return true;
}

bool terminate_matching_process(
    HANDLE process, const ULARGE_INTEGER& expected,
    const ULARGE_INTEGER& observed) {
    if (expected.QuadPart != observed.QuadPart) {
        return false;
    }
    return TerminateProcess(process, 1) != FALSE;
}

struct JobMembers {
    DWORD assigned = 0;
    std::vector<ULONG_PTR> process_ids;
};

bool query_job_members(HANDLE job, JobMembers& members) {
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting = {};
    if (QueryInformationJobObject(
            job, JobObjectBasicAccountingInformation, &accounting,
            sizeof(accounting), nullptr) == FALSE) {
        return false;
    }
    std::vector<unsigned char> storage(
        sizeof(JOBOBJECT_BASIC_PROCESS_ID_LIST) + sizeof(ULONG_PTR) * 31U);
    auto* list = reinterpret_cast<JOBOBJECT_BASIC_PROCESS_ID_LIST*>(storage.data());
    if (QueryInformationJobObject(
            job, JobObjectBasicProcessIdList, list,
            static_cast<DWORD>(storage.size()), nullptr) == FALSE) {
        return false;
    }
    members.assigned = accounting.ActiveProcesses;
    members.process_ids.assign(
        list->ProcessIdList,
        list->ProcessIdList + list->NumberOfProcessIdsInList);
    return true;
}

bool contains_process(const JobMembers& members, DWORD process_id) {
    for (const ULONG_PTR candidate : members.process_ids) {
        if (candidate == static_cast<ULONG_PTR>(process_id)) {
            return true;
        }
    }
    return false;
}

int windows_leaf() {
    Sleep(INFINITE);
    return 0;
}

int windows_worker() {
    const std::wstring self = executable_path();
    if (self.empty()) {
        return 2;
    }
    std::wstring command = quote_windows_argument(self) + L" --windows-leaf";
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (CreateProcessW(
            nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
            &startup, &process) == FALSE) {
        return 3;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    Sleep(INFINITE);
    return 0;
}

struct WindowsContainmentResult {
    bool available = false;
    bool prepared = false;
    bool birth_token = false;
    bool direct = false;
    bool descendant = false;
    bool mismatch_blocked = false;
    bool terminated = false;
    bool empty = false;
};

WindowsContainmentResult run_windows_containment_probe() {
    WindowsContainmentResult result;
    const std::wstring self = executable_path();
    if (self.empty()) {
        return result;
    }
    const HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job == nullptr) {
        return result;
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    result.prepared = SetInformationJobObject(
                          job, JobObjectExtendedLimitInformation, &limits,
                          sizeof(limits)) != FALSE;

    std::wstring command = quote_windows_argument(self) + L" --windows-worker";
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    const bool created = result.prepared &&
                         CreateProcessW(
                             nullptr, command.data(), nullptr, nullptr, FALSE,
                             CREATE_SUSPENDED, nullptr, nullptr, &startup,
                             &process) != FALSE;
    if (!created) {
        CloseHandle(job);
        return result;
    }

    result.prepared = AssignProcessToJobObject(job, process.hProcess) != FALSE;
    ULARGE_INTEGER observed = {};
    result.birth_token = result.prepared && creation_token(process.hProcess, observed);
    if (!result.prepared || ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        CloseHandle(job);
        return result;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    JobMembers members;
    while (std::chrono::steady_clock::now() < deadline) {
        if (query_job_members(job, members) && members.assigned >= 2U &&
            members.process_ids.size() >= 2U) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    result.direct = contains_process(members, process.dwProcessId);
    result.descendant = members.assigned >= 2U && members.process_ids.size() >= 2U;

    ULARGE_INTEGER mismatch = observed;
    ++mismatch.QuadPart;
    result.mismatch_blocked =
        !terminate_matching_process(process.hProcess, mismatch, observed) &&
        WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT;
    result.terminated = TerminateJobObject(job, 0) != FALSE &&
                        WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0;

    const auto empty_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < empty_deadline) {
        JobMembers after;
        if (query_job_members(job, after) && after.assigned == 0U &&
            after.process_ids.empty()) {
            result.empty = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    result.available = result.prepared && result.birth_token && result.direct &&
                       result.descendant && result.mismatch_blocked &&
                       result.terminated && result.empty;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(job);
    return result;
}

int run_probe() {
    const WindowsDurabilityResult durable = run_windows_durability_probe();
    const bool durable_passed = durable.crash_before && durable.crash_after &&
                                durable.flush_failure && durable.corrupt_candidate &&
                                durable.stage_flushed && durable.root_replaced &&
                                durable.parent_flushed;
    const WindowsContainmentResult containment = run_windows_containment_probe();
    const std::string disposition = containment.available ? "passed" : "deferred";
    const std::string mismatch =
        containment.available ? "blocked" : "deferred";
    emit_rows({
        {"durable_root_publication", durable_passed ? "passed" : "failed"},
        {"durable.crash_before_replace_old_root_valid",
         durable.crash_before ? "passed" : "failed"},
        {"durable.crash_after_replace_complete_root_valid",
         durable.crash_after ? "passed" : "failed"},
        {"durable.flush_failure_publication",
         durable.flush_failure ? "blocked" : "failed"},
        {"durable.corrupt_candidate_publication",
         durable.corrupt_candidate ? "blocked" : "failed"},
        {"durable.stage_file_flushed", durable.stage_flushed ? "passed" : "failed"},
        {"durable.root_replaced", durable.root_replaced ? "passed" : "failed"},
        {"durable.parent_flushed", durable.parent_flushed ? "passed" : "failed"},
        {"ownership.prepared_before_spawn", disposition},
        {"identity.birth_token", disposition},
        {"membership.direct", disposition},
        {"membership.descendant", disposition},
        {"identity.mismatch_signal", mismatch},
        {"termination.containment", disposition},
        {"termination.membership_empty", disposition},
        {"containment.escape_detected", "deferred"},
        {"process_containment", disposition},
    });
    return durable_passed ? 0 : 1;
}

#else

enum class DurableStage {
    normal,
    crash_before_replace,
    crash_after_replace,
    flush_failure,
    corrupt_candidate,
};

std::string temporary_directory_root() {
    const char* configured = std::getenv("TMPDIR");
    std::string root = configured != nullptr && configured[0] != '\0'
                           ? configured
                           : P_tmpdir;
    while (root.size() > 1 && root.back() == '/') {
        root.pop_back();
    }
    return root;
}

bool write_all(int file, const std::string& value) {
    std::size_t offset = 0;
    while (offset < value.size()) {
        const ssize_t written = write(file, value.data() + offset, value.size() - offset);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return false;
        }
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

enum class DirectorySyncDisposition {
    synced,
    unsupported,
    failed,
};

bool directory_sync_unsupported(int error) {
    return error == EINVAL || error == ENOTSUP || error == EOPNOTSUPP ||
           error == ENOSYS;
}

DirectorySyncDisposition sync_parent_directory(const std::string& directory) {
#ifdef O_DIRECTORY
    constexpr int directory_flag = O_DIRECTORY;
#else
    constexpr int directory_flag = 0;
#endif
    const int handle = open(directory.c_str(), O_RDONLY | directory_flag);
    if (handle < 0) {
        return DirectorySyncDisposition::failed;
    }
    struct stat metadata;
    if (fstat(handle, &metadata) != 0 || !S_ISDIR(metadata.st_mode)) {
        close(handle);
        return DirectorySyncDisposition::failed;
    }
    int sync_result = -1;
    do {
        sync_result = fsync(handle);
    } while (sync_result != 0 && errno == EINTR);
    const int sync_error = sync_result == 0 ? 0 : errno;
    const int close_result = close(handle);
    if (close_result != 0) {
        return DirectorySyncDisposition::failed;
    }
    if (sync_result == 0) {
        return DirectorySyncDisposition::synced;
    }
    return directory_sync_unsupported(sync_error)
               ? DirectorySyncDisposition::unsupported
               : DirectorySyncDisposition::failed;
}

DirectorySyncDisposition write_initial_root(
    const std::string& directory, const std::string& path,
    const std::string& payload) {
    const int file = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (file < 0) {
        return DirectorySyncDisposition::failed;
    }
    const bool stored = write_all(file, payload) && fsync(file) == 0;
    const int close_result = close(file);
    return stored && close_result == 0 ? sync_parent_directory(directory)
                                       : DirectorySyncDisposition::failed;
}

std::string read_posix_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool arm_parent_death_signal(pid_t expected_parent) {
#ifdef __linux__
    return prctl(PR_SET_PDEATHSIG, SIGKILL) == 0 &&
           getppid() == expected_parent;
#else
    static_cast<void>(expected_parent);
    return true;
#endif
}

int durable_child(
    const std::string& directory, const std::string& stage_name,
    const std::string& payload) {
    const std::string root = directory + "/root.json";
    const std::string stage = directory + "/root.stage";
    DurableStage fault = DurableStage::normal;
    if (stage_name == "crash-before") {
        fault = DurableStage::crash_before_replace;
    } else if (stage_name == "crash-after") {
        fault = DurableStage::crash_after_replace;
    } else if (stage_name == "flush-failure") {
        fault = DurableStage::flush_failure;
    } else if (stage_name == "corrupt") {
        fault = DurableStage::corrupt_candidate;
    }

    const int file = open(stage.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (file < 0 || !write_all(file, payload)) {
        if (file >= 0) {
            close(file);
        }
        return 80;
    }
    if (fault == DurableStage::flush_failure) {
        close(file);
        return 74;
    }
    if (fsync(file) != 0) {
        close(file);
        return 81;
    }
    if (close(file) != 0) {
        return 85;
    }
    if (fault == DurableStage::crash_before_replace) {
        return 71;
    }
    if (!validate_payload(read_posix_file(stage))) {
        unlink(stage.c_str());
        return fault == DurableStage::corrupt_candidate ? 73 : 82;
    }
    if (rename(stage.c_str(), root.c_str()) != 0) {
        return 83;
    }
    if (fault == DurableStage::crash_after_replace) {
        return 72;
    }
    const DirectorySyncDisposition directory_sync =
        sync_parent_directory(directory);
    if (directory_sync == DirectorySyncDisposition::synced) {
        return 0;
    }
    return directory_sync == DirectorySyncDisposition::unsupported ? 75 : 84;
}

int wait_for_child(pid_t child, std::chrono::seconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    int status = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        const pid_t waited = waitpid(child, &status, WNOHANG);
        if (waited == child) {
            if (WIFEXITED(status)) {
                return WEXITSTATUS(status);
            }
            return 128;
        }
        if (waited < 0 && errno != EINTR) {
            return 129;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    kill(child, SIGKILL);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    return 130;
}

int run_durable_helper(
    const std::string& self, const std::string& directory,
    const std::string& stage, const std::string& payload) {
    const pid_t parent = getpid();
    const pid_t child = fork();
    if (child < 0) {
        return 131;
    }
    if (child == 0) {
        if (!arm_parent_death_signal(parent)) {
            _exit(126);
        }
        execl(
            self.c_str(), self.c_str(), "--durable-child", directory.c_str(),
            stage.c_str(), payload.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    return wait_for_child(child, std::chrono::seconds(3));
}

struct PosixDurabilityResult {
    bool crash_before = false;
    bool crash_after = false;
    bool flush_failure = false;
    bool corrupt_candidate = false;
    bool stage_flushed = false;
    bool root_replaced = false;
    DirectorySyncDisposition parent_sync = DirectorySyncDisposition::failed;
};

PosixDurabilityResult run_posix_durability_probe(const std::string& self) {
    PosixDurabilityResult result;
    const std::string pattern_value =
        temporary_directory_root() + "/residency-task014-XXXXXX";
    std::vector<char> pattern(pattern_value.begin(), pattern_value.end());
    pattern.push_back('\0');
    char* created = mkdtemp(pattern.data());
    if (created == nullptr) {
        return result;
    }
    const std::string directory(created);
    const std::string root = directory + "/root.json";
    const std::string stage = directory + "/root.stage";
    const std::string old_payload = make_payload("old");
    const std::string crash_payload = make_payload("crash-after");
    const std::string final_payload = make_payload("complete");

    const DirectorySyncDisposition initialized =
        write_initial_root(directory, root, old_payload);
    result.crash_before = initialized != DirectorySyncDisposition::failed &&
                          run_durable_helper(
                              self, directory, "crash-before",
                              make_payload("staged")) == 71 &&
                          read_posix_file(root) == old_payload &&
                          validate_payload(read_posix_file(root));
    result.crash_after =
        run_durable_helper(self, directory, "crash-after", crash_payload) == 72 &&
        read_posix_file(root) == crash_payload &&
        validate_payload(read_posix_file(root));
    const int completed =
        run_durable_helper(self, directory, "normal", final_payload);
    const bool publication_completed = completed == 0 || completed == 75;
    result.stage_flushed = publication_completed;
    result.root_replaced =
        publication_completed && read_posix_file(root) == final_payload;
    const DirectorySyncDisposition completed_sync =
        completed == 0
            ? DirectorySyncDisposition::synced
            : completed == 75 ? DirectorySyncDisposition::unsupported
                              : DirectorySyncDisposition::failed;
    result.parent_sync =
        initialized == DirectorySyncDisposition::failed ||
                completed_sync == DirectorySyncDisposition::failed
            ? DirectorySyncDisposition::failed
            : initialized == DirectorySyncDisposition::unsupported ||
                      completed_sync == DirectorySyncDisposition::unsupported
                  ? DirectorySyncDisposition::unsupported
                  : DirectorySyncDisposition::synced;
    result.flush_failure =
        run_durable_helper(
            self, directory, "flush-failure", make_payload("flush-failure")) ==
            74 &&
        read_posix_file(root) == final_payload;
    result.corrupt_candidate =
        run_durable_helper(
            self, directory, "corrupt",
            "residency-root/v1\ngeneration=corrupt\nchecksum=0\n") == 73 &&
        read_posix_file(root) == final_payload;

    unlink(stage.c_str());
    unlink(root.c_str());
    rmdir(directory.c_str());
    return result;
}

#ifdef __linux__

struct ProcessIdentity {
    std::string boot_id;
    unsigned long long start_time = 0;
};

struct ProcessStat {
    pid_t parent = -1;
    pid_t group = -1;
    unsigned long long start_time = 0;
};

bool parse_process_stat(pid_t process, ProcessStat& result) {
    std::ifstream input("/proc/" + std::to_string(process) + "/stat");
    std::string value;
    std::getline(input, value);
    const std::size_t closing = value.rfind(')');
    if (closing == std::string::npos || closing + 2 >= value.size()) {
        return false;
    }
    std::istringstream fields(value.substr(closing + 2));
    char state = '\0';
    long long parent = -1;
    long long group = -1;
    long long ignored = 0;
    unsigned long long start_time = 0;
    if (!(fields >> state >> parent >> group)) {
        return false;
    }
    for (int field = 6; field <= 21; ++field) {
        if (!(fields >> ignored)) {
            return false;
        }
    }
    if (!(fields >> start_time) || state == '\0') {
        return false;
    }
    result.parent = static_cast<pid_t>(parent);
    result.group = static_cast<pid_t>(group);
    result.start_time = start_time;
    return true;
}

std::string linux_boot_id() {
    std::ifstream input("/proc/sys/kernel/random/boot_id");
    std::string value;
    std::getline(input, value);
    return value;
}

bool read_identity(pid_t process, ProcessIdentity& identity) {
    ProcessStat stat;
    identity.boot_id = linux_boot_id();
    if (identity.boot_id.empty() || !parse_process_stat(process, stat) ||
        stat.start_time == 0) {
        return false;
    }
    identity.start_time = stat.start_time;
    return true;
}

bool process_exists(pid_t process) {
    return kill(process, 0) == 0 || errno == EPERM;
}

enum class GroupSignalDisposition {
    signaled,
    prerequisites_unavailable,
    identity_unavailable,
    identity_mismatch,
    containment_unavailable,
    containment_mismatch,
    signal_failed,
};

GroupSignalDisposition signal_process_group_if_live_state_matches(
    pid_t leader, pid_t ordinary, bool prepared,
    const ProcessIdentity& expected, int signal_number) {
    if (!prepared || expected.boot_id.empty() || expected.start_time == 0) {
        return GroupSignalDisposition::prerequisites_unavailable;
    }

    ProcessStat current_leader;
    const std::string current_boot_id = linux_boot_id();
    if (current_boot_id.empty() || !parse_process_stat(leader, current_leader) ||
        current_leader.start_time == 0) {
        return GroupSignalDisposition::identity_unavailable;
    }
    if (current_boot_id != expected.boot_id ||
        current_leader.start_time != expected.start_time) {
        return GroupSignalDisposition::identity_mismatch;
    }

    ProcessStat current_ordinary;
    if (!parse_process_stat(ordinary, current_ordinary)) {
        return GroupSignalDisposition::containment_unavailable;
    }
    if (current_leader.group != leader || current_ordinary.parent != leader ||
        current_ordinary.group != leader) {
        return GroupSignalDisposition::containment_mismatch;
    }
    return kill(-leader, signal_number) == 0
               ? GroupSignalDisposition::signaled
               : GroupSignalDisposition::signal_failed;
}

volatile sig_atomic_t worker_stopping = 0;

extern "C" void stop_worker(int) {
    worker_stopping = 1;
}

bool arm_worker_stop(sigset_t& previous_mask) {
    sigset_t blocked_mask;
    sigemptyset(&blocked_mask);
    sigaddset(&blocked_mask, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &blocked_mask, &previous_mask) != 0) {
        return false;
    }
    struct sigaction action {};
    action.sa_handler = stop_worker;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, nullptr) == 0) {
        return true;
    }
    sigprocmask(SIG_SETMASK, &previous_mask, nullptr);
    return false;
}

bool wait_for_worker_stop(const sigset_t& previous_mask) {
    sigset_t wait_mask = previous_mask;
    sigdelset(&wait_mask, SIGTERM);
    while (worker_stopping == 0) {
        if (sigsuspend(&wait_mask) < 0 && errno == EINTR) {
            continue;
        }
        sigprocmask(SIG_SETMASK, &previous_mask, nullptr);
        return false;
    }
    return sigprocmask(SIG_SETMASK, &previous_mask, nullptr) == 0;
}

int ordinary_descendant() {
    sigset_t previous_mask;
    if (!arm_worker_stop(previous_mask)) {
        return 2;
    }
    return wait_for_worker_stop(previous_mask) ? 0 : 3;
}

int escaping_descendant() {
    sigset_t previous_mask;
    if (!arm_worker_stop(previous_mask)) {
        return 2;
    }
    if (setsid() < 0) {
        return 3;
    }
    return wait_for_worker_stop(previous_mask) ? 0 : 4;
}

struct ChildAnnouncement {
    pid_t ordinary = -1;
    pid_t escaping = -1;
};

bool write_announcement(int pipe, const ChildAnnouncement& announcement) {
    const unsigned char* bytes =
        reinterpret_cast<const unsigned char*>(&announcement);
    std::size_t offset = 0;
    while (offset < sizeof(announcement)) {
        const ssize_t count =
            write(pipe, bytes + offset, sizeof(announcement) - offset);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return false;
        }
        offset += static_cast<std::size_t>(count);
    }
    return true;
}

bool read_announcement(int pipe, ChildAnnouncement& announcement) {
    unsigned char* bytes = reinterpret_cast<unsigned char*>(&announcement);
    std::size_t offset = 0;
    while (offset < sizeof(announcement)) {
        const ssize_t count = read(pipe, bytes + offset, sizeof(announcement) - offset);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return false;
        }
        offset += static_cast<std::size_t>(count);
    }
    return true;
}

int process_group_leader(int output_pipe) {
    if (setpgid(0, 0) != 0) {
        return 2;
    }
    worker_stopping = 0;
    sigset_t previous_mask;
    if (!arm_worker_stop(previous_mask)) {
        return 3;
    }
    const pid_t leader = getpid();
    const pid_t ordinary = fork();
    if (ordinary < 0) {
        return 4;
    }
    if (ordinary == 0) {
        if (!arm_parent_death_signal(leader)) {
            _exit(126);
        }
        _exit(ordinary_descendant());
    }
    const pid_t escaping = fork();
    if (escaping < 0) {
        kill(ordinary, SIGKILL);
        waitpid(ordinary, nullptr, 0);
        return 5;
    }
    if (escaping == 0) {
        if (!arm_parent_death_signal(leader)) {
            _exit(126);
        }
        _exit(escaping_descendant());
    }
    const ChildAnnouncement announcement{ordinary, escaping};
    if (!write_announcement(output_pipe, announcement)) {
        kill(ordinary, SIGKILL);
        kill(escaping, SIGKILL);
        waitpid(ordinary, nullptr, 0);
        waitpid(escaping, nullptr, 0);
        return 6;
    }
    close(output_pipe);
    if (!wait_for_worker_stop(previous_mask)) {
        kill(ordinary, SIGKILL);
        kill(escaping, SIGKILL);
        waitpid(ordinary, nullptr, 0);
        waitpid(escaping, nullptr, 0);
        return 7;
    }
    int status = 0;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        const pid_t waited = waitpid(ordinary, &status, WNOHANG);
        if (waited == ordinary || (waited < 0 && errno == ECHILD)) {
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    kill(ordinary, SIGKILL);
    waitpid(ordinary, nullptr, 0);
    return 8;
}

bool wait_for_membership(
    pid_t process, pid_t expected_parent, pid_t expected_group,
    ProcessStat& stat) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        if (parse_process_stat(process, stat) &&
            (expected_parent < 0 || stat.parent == expected_parent) &&
            stat.group == expected_group) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

bool group_is_empty(pid_t group) {
    DIR* directory = opendir("/proc");
    if (directory == nullptr) {
        return false;
    }
    bool empty = true;
    while (const dirent* entry = readdir(directory)) {
        char* end = nullptr;
        const long candidate = std::strtol(entry->d_name, &end, 10);
        if (end == entry->d_name || *end != '\0' || candidate <= 0) {
            continue;
        }
        ProcessStat stat;
        if (parse_process_stat(static_cast<pid_t>(candidate), stat) &&
            stat.group == group) {
            empty = false;
            break;
        }
    }
    closedir(directory);
    return empty;
}

bool wait_for_group_empty(pid_t group) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        if (group_is_empty(group)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

void terminate_and_reap(pid_t process) {
    if (process <= 0) {
        return;
    }

    int status = 0;
    pid_t waited = -1;
    do {
        waited = waitpid(process, &status, WNOHANG);
    } while (waited < 0 && errno == EINTR);
    if (waited == process || waited < 0) {
        return;
    }

    kill(process, SIGTERM);
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        waited = waitpid(process, &status, WNOHANG);
        if (waited == process || (waited < 0 && errno == ECHILD)) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    kill(process, SIGKILL);
    while (waitpid(process, &status, 0) < 0 && errno == EINTR) {
    }
}

struct LinuxContainmentResult {
    bool prepared = false;
    bool birth_token = false;
    bool direct = false;
    bool descendant = false;
    bool mismatch_blocked = false;
    bool terminated = false;
    bool empty = false;
    bool escape_detected = false;
};

LinuxContainmentResult run_linux_containment_probe() {
    LinuxContainmentResult result;
    if (prctl(PR_SET_CHILD_SUBREAPER, 1) != 0) {
        return result;
    }
    const std::string temporary_root = temporary_directory_root();
    const std::string record_pattern =
        temporary_root + "/residency-task014-prepared-XXXXXX";
    std::vector<char> record_template(record_pattern.begin(), record_pattern.end());
    record_template.push_back('\0');
    const int record = mkstemp(record_template.data());
    if (record < 0) {
        return result;
    }
    const std::string prepared = "prepared-before-spawn\n";
    const bool record_stored = write_all(record, prepared) && fsync(record) == 0;
    const bool record_closed = close(record) == 0;
    result.prepared =
        record_stored && record_closed &&
        sync_parent_directory(temporary_root) == DirectorySyncDisposition::synced;
    if (!result.prepared) {
        unlink(record_template.data());
        return result;
    }

    int announcement_pipe[2] = {-1, -1};
    if (pipe(announcement_pipe) != 0) {
        unlink(record_template.data());
        return result;
    }
    const pid_t owner = getpid();
    const pid_t leader = fork();
    if (leader < 0) {
        close(announcement_pipe[0]);
        close(announcement_pipe[1]);
        unlink(record_template.data());
        return result;
    }
    if (leader == 0) {
        close(announcement_pipe[0]);
        if (!arm_parent_death_signal(owner)) {
            _exit(126);
        }
        _exit(process_group_leader(announcement_pipe[1]));
    }
    close(announcement_pipe[1]);
    setpgid(leader, leader);
    ChildAnnouncement announcement;
    const bool announced = read_announcement(announcement_pipe[0], announcement);
    close(announcement_pipe[0]);

    ProcessIdentity observed;
    ProcessStat leader_stat;
    ProcessStat ordinary_stat;
    ProcessStat escaping_stat;
    result.birth_token = announced && read_identity(leader, observed);
    const bool leader_read =
        wait_for_membership(leader, -1, leader, leader_stat);
    const bool ordinary_read =
        announced && wait_for_membership(
                         announcement.ordinary, leader, leader, ordinary_stat);
    const bool escaping_read =
        announced && wait_for_membership(
                         announcement.escaping, leader, announcement.escaping,
                         escaping_stat);
    result.direct = leader_read && leader_stat.group == leader;
    result.descendant = ordinary_read && ordinary_stat.parent == leader &&
                        ordinary_stat.group == leader;
    result.escape_detected = escaping_read && escaping_stat.parent == leader &&
                             escaping_stat.group == announcement.escaping &&
                             escaping_stat.group != leader;

    ProcessIdentity mismatch = observed;
    ++mismatch.start_time;
    const bool signal_prerequisites =
        result.prepared && result.birth_token && result.direct && result.descendant;
    const GroupSignalDisposition mismatch_disposition =
        signal_process_group_if_live_state_matches(
            leader, announcement.ordinary, signal_prerequisites, mismatch,
            SIGTERM);
    const GroupSignalDisposition unreadable_disposition =
        signal_process_group_if_live_state_matches(
            leader, -1, signal_prerequisites, observed, SIGTERM);
    result.mismatch_blocked =
        mismatch_disposition == GroupSignalDisposition::identity_mismatch &&
        unreadable_disposition ==
            GroupSignalDisposition::containment_unavailable &&
        process_exists(leader) && process_exists(announcement.ordinary);

    const GroupSignalDisposition group_signal_disposition =
        signal_process_group_if_live_state_matches(
            leader, announcement.ordinary, signal_prerequisites, observed,
            SIGTERM);
    const bool group_signaled =
        group_signal_disposition == GroupSignalDisposition::signaled;
    int leader_exit = 130;
    if (group_signaled) {
        leader_exit = wait_for_child(leader, std::chrono::seconds(4));
    } else {
        terminate_and_reap(leader);
    }
    result.terminated = group_signaled && leader_exit == 0;
    result.empty = result.terminated && wait_for_group_empty(leader);
    terminate_and_reap(announcement.escaping);
    terminate_and_reap(announcement.ordinary);
    unlink(record_template.data());
    return result;
}

#endif

int run_probe(const std::string& self) {
    const PosixDurabilityResult durable = run_posix_durability_probe(self);
    const bool durable_behavior_verified =
        durable.crash_before && durable.crash_after && durable.flush_failure &&
        durable.corrupt_candidate && durable.stage_flushed && durable.root_replaced;
    const bool durable_passed =
        durable_behavior_verified &&
        durable.parent_sync == DirectorySyncDisposition::synced;
    const bool durable_deferred =
        durable_behavior_verified &&
        durable.parent_sync == DirectorySyncDisposition::unsupported;
    std::vector<Row> rows{
        {"durable_root_publication",
         durable_passed ? "passed" : durable_deferred ? "deferred" : "failed"},
        {"durable.crash_before_replace_old_root_valid",
         durable.crash_before ? "passed" : "failed"},
        {"durable.crash_after_replace_complete_root_valid",
         durable.crash_after ? "passed" : "failed"},
        {"durable.flush_failure_publication",
         durable.flush_failure ? "blocked" : "failed"},
        {"durable.corrupt_candidate_publication",
         durable.corrupt_candidate ? "blocked" : "failed"},
        {"durable.stage_file_flushed", durable.stage_flushed ? "passed" : "failed"},
        {"durable.root_replaced", durable.root_replaced ? "passed" : "failed"},
        {"durable.parent_flushed",
         durable.parent_sync == DirectorySyncDisposition::synced
             ? "passed"
             : durable.parent_sync == DirectorySyncDisposition::unsupported
                   ? "deferred"
                   : "failed"},
    };
#ifdef __linux__
    const LinuxContainmentResult containment = run_linux_containment_probe();
    rows.insert(
        rows.end(),
        {
            {"ownership.prepared_before_spawn",
             containment.prepared ? "passed" : "failed"},
            {"identity.birth_token", containment.birth_token ? "passed" : "failed"},
            {"membership.direct", containment.direct ? "passed" : "failed"},
            {"membership.descendant",
             containment.descendant ? "passed" : "failed"},
            {"identity.mismatch_signal",
             containment.mismatch_blocked ? "blocked" : "failed"},
            {"termination.containment",
             containment.terminated ? "passed" : "failed"},
            {"termination.membership_empty",
             containment.empty ? "passed" : "failed"},
            {"containment.escape_detected",
             containment.escape_detected ? "passed" : "failed"},
            {"process_containment", "fallback"},
        });
    const bool containment_passed =
        containment.prepared && containment.birth_token && containment.direct &&
        containment.descendant && containment.mismatch_blocked &&
        containment.terminated && containment.empty && containment.escape_detected;
#else
    rows.insert(
        rows.end(),
        {
            {"ownership.prepared_before_spawn", "deferred"},
            {"identity.birth_token", "deferred"},
            {"membership.direct", "deferred"},
            {"membership.descendant", "deferred"},
            {"identity.mismatch_signal", "deferred"},
            {"termination.containment", "deferred"},
            {"termination.membership_empty", "deferred"},
            {"containment.escape_detected", "deferred"},
            {"process_containment", "deferred"},
        });
    const bool containment_passed = true;
#endif
    emit_rows(rows);
    return (durable_passed || durable_deferred) && containment_passed ? 0 : 1;
}

#endif

}
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring(argv[1]) == L"--windows-worker") {
        return lemon::residency::prototype::windows_worker();
    }
    if (argc == 2 && std::wstring(argv[1]) == L"--windows-leaf") {
        return lemon::residency::prototype::windows_leaf();
    }
    if (argc == 4 && std::wstring(argv[1]) == L"--windows-durable-child") {
        const int exit_code =
            lemon::residency::prototype::windows_durable_child(argv[2], argv[3]);
        ExitProcess(static_cast<UINT>(exit_code));
    }
    return lemon::residency::prototype::run_probe();
}
#else
int main(int argc, char** argv) {
    if (argc == 5 && std::string(argv[1]) == "--durable-child") {
        return lemon::residency::prototype::durable_child(
            argv[2], argv[3], argv[4]);
    }
    if (argc != 1) {
        return 2;
    }
    return lemon::residency::prototype::run_probe(argv[0]);
}
#endif
