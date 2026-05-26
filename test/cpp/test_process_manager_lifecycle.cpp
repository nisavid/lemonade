// Standalone test for Lemonade backend process lifecycle behavior.
// Compile with:
//   g++ -std=c++17 -pthread -I src/cpp/include \
//       test/cpp/test_process_manager_lifecycle.cpp \
//       src/cpp/server/utils/process_manager.cpp \
//       -o /tmp/test_process_manager_lifecycle

#include "lemon/utils/process_manager.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using lemon::utils::ProcessHandle;
using lemon::utils::ProcessManager;

namespace {

struct Command {
    std::string executable;
    std::vector<std::string> args;
};

Command long_running_command() {
#ifdef _WIN32
    return {"cmd.exe", {"/C", "timeout /T 5 /NOBREAK > NUL"}};
#else
    return {"sleep", {"5"}};
#endif
}

Command successful_exit_command() {
#ifdef _WIN32
    return {"cmd.exe", {"/C", "exit 0"}};
#else
    return {"true", {}};
#endif
}

} // namespace

int main() {
    int failures = 0;
    auto expect_true = [&failures](bool condition, const char* name) {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
        if (!condition) ++failures;
    };

    ProcessHandle worker_child{nullptr, 0};
    std::thread launcher([&worker_child] {
        const Command command = long_running_command();
        worker_child = ProcessManager::start_process(
            command.executable, command.args, "", false);
    });
    launcher.join();

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    expect_true(
        ProcessManager::is_running(worker_child),
        "process launched from a temporary worker thread stays alive after the thread exits");

    ProcessManager::stop_process(worker_child);

    const Command command = successful_exit_command();
    ProcessHandle short_lived = ProcessManager::start_process(
        command.executable, command.args, "", false);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    expect_true(
        !ProcessManager::is_running(short_lived),
        "process liveness check detects an exited child");

    const auto cleanup_start = std::chrono::steady_clock::now();
    ProcessManager::stop_process(short_lived);
    const auto cleanup_duration = std::chrono::steady_clock::now() - cleanup_start;
    expect_true(
        cleanup_duration < std::chrono::seconds(1),
        "cleanup returns promptly after liveness reaps an exited child");

    return failures == 0 ? 0 : 1;
}
