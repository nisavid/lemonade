#pragma once

#include <lemon/utils/process_manager.h>

#include "process_containment_linux_fake_ops.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

#include <stdlib.h>

namespace process_containment_test {

struct TestState {
    int failures = 0;

    void require(bool condition, const char *message) {
        if (condition) {
            return;
        }
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
};

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        std::error_code error;
        const auto temporary_root =
            std::filesystem::temp_directory_path(error);
        if (error) {
            return;
        }
        std::string pattern =
            (temporary_root / "lemonade-containment-XXXXXX").string();
        if (char *created = ::mkdtemp(pattern.data())) {
            path_ = created;
        }
    }

    ~TemporaryDirectory() {
        if (path_.empty()) {
            return;
        }
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    bool valid() const noexcept { return !path_.empty(); }

    const std::filesystem::path &path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

inline lemon::utils::ProcessContainmentOperationControl operation_control() {
    return {std::chrono::steady_clock::now() + std::chrono::seconds(5), {}};
}

inline std::string nonce(char value) { return std::string(64U, value); }

inline bool begin_scenario(TestState &state,
                           const TemporaryDirectory &root) {
    state.require(root.valid(),
                  "scenario temporary root creation succeeds");
    if (!root.valid()) {
        return false;
    }
    const bool reset =
        lemon::utils::internal::testing::reset_process_containment_linux_fake();
    state.require(reset,
                  "scenario reset refuses to race retained child or reaper state");
    return reset;
}

} // namespace process_containment_test
