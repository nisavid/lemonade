// Standalone test for WrappedServer backend liveness checks.
// Compile with:
//   g++ -std=c++17 -pthread -ffunction-sections -fdata-sections \
//       -I src/cpp/include -I build/_deps/httplib-src \
//       test/cpp/test_wrapped_server_lifecycle.cpp \
//       src/cpp/server/wrapped_server.cpp \
//       src/cpp/server/utils/process_manager.cpp \
//       -Wl,--gc-sections \
//       -o /tmp/test_wrapped_server_lifecycle

#include "lemon/wrapped_server.h"
#include "lemon/streaming_proxy.h"

#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>

using lemon::ModelInfo;
using lemon::RecipeOptions;
using lemon::WrappedServer;
using lemon::json;
using lemon::utils::ProcessHandle;
using lemon::utils::ProcessManager;

namespace lemon {
namespace utils {

std::atomic<long> HttpClient::default_timeout_seconds_{300};

bool HttpClient::is_reachable(const std::string&, int) {
    return false;
}

} // namespace utils

void StreamingProxy::forward_sse_stream(
    const std::string&,
    const std::string&,
    httplib::DataSink&,
    std::function<void(const TelemetryData&)>,
    long) {
    throw std::logic_error("not used by this test");
}

void StreamingProxy::forward_byte_stream(
    const std::string&,
    const std::string&,
    httplib::DataSink&,
    long) {
    throw std::logic_error("not used by this test");
}

} // namespace lemon

class TestWrappedServer : public WrappedServer {
public:
    TestWrappedServer() : WrappedServer("test-backend", "info") {}

    void set_process_handle(ProcessHandle handle) {
        process_handle_ = handle;
    }

    bool process_running() const {
        return is_process_running();
    }

    void load(const std::string&, const ModelInfo&, const RecipeOptions&, bool) override {}
    void unload() override {
        ProcessManager::stop_process(process_handle_);
    }
    json chat_completion(const json&) override {
        return json::object();
    }
    json completion(const json&) override {
        return json::object();
    }
    json responses(const json&) override {
        return json::object();
    }
};

int main() {
    int failures = 0;
    auto expect_true = [&failures](bool condition, const char* name) {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
        if (!condition) ++failures;
    };

    TestWrappedServer server;
    ProcessHandle child = ProcessManager::start_process("true", {}, "", false);
    server.set_process_handle(child);

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    expect_true(
        !server.process_running(),
        "wrapped server liveness check detects exited backend process");

    server.unload();

    return failures == 0 ? 0 : 1;
}
