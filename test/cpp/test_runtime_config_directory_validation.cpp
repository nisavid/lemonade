#include <lemon/runtime_config.h>
#include <lemon/utils/path_utils.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using lemon::RuntimeConfig;
using lemon::json;
using lemon::utils::path_to_utf8;

namespace {

unsigned long current_process_id() {
#ifdef _WIN32
    return static_cast<unsigned long>(_getpid());
#else
    return static_cast<unsigned long>(getpid());
#endif
}

class TempDirectory {
public:
    TempDirectory() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() /
                ("lemonade-runtime-config-dir-validation-" +
                 std::to_string(current_process_id()) + "-" + std::to_string(stamp));
        fs::create_directories(path_);
    }

    ~TempDirectory() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    const fs::path& path() const {
        return path_;
    }

private:
    fs::path path_;
};

bool accepts_directory_value(const fs::path& path, const char* label) {
    RuntimeConfig config(json{{"extra_models_dir", ""}});
    try {
        config.set(json{{"extra_models_dir", path_to_utf8(path)}});
        std::printf("[PASS] %s\n", label);
        return true;
    } catch (const std::exception& error) {
        std::printf("[FAIL] %s: %s\n", label, error.what());
        return false;
    }
}

bool rejects_directory_value(const fs::path& path, const char* label) {
    RuntimeConfig config(json{{"extra_models_dir", ""}});
    try {
        config.set(json{{"extra_models_dir", path_to_utf8(path)}});
        std::printf("[FAIL] %s: value was unexpectedly accepted\n", label);
        return false;
    } catch (const std::invalid_argument&) {
        std::printf("[PASS] %s\n", label);
        return true;
    } catch (const std::exception& error) {
        std::printf("[FAIL] %s: unexpected exception: %s\n", label, error.what());
        return false;
    }
}

} // namespace

int main() {
    TempDirectory temp;
    int failures = 0;

    const fs::path readable_dir = temp.path() / "readable";
    fs::create_directory(readable_dir);
    if (!accepts_directory_value(readable_dir, "existing readable directory is accepted")) {
        ++failures;
    }

    const fs::path missing_dir = temp.path() / "not-created";
    if (!accepts_directory_value(missing_dir, "nonexistent directory is accepted")) {
        ++failures;
    }

    const fs::path regular_file = temp.path() / "model.gguf";
    {
        std::ofstream output(regular_file);
        output << "test";
    }
    if (!rejects_directory_value(regular_file, "existing regular file is rejected")) {
        ++failures;
    }

#ifdef _WIN32
    const fs::path reparse_dir = temp.path() / "directory-link";
    std::error_code symlink_ec;
    fs::create_directory_symlink(readable_dir, reparse_dir, symlink_ec);
    if (symlink_ec) {
        std::printf("[SKIP] directory reparse point validation: %s\n",
                    symlink_ec.message().c_str());
    } else if (!accepts_directory_value(
                   reparse_dir, "existing directory reparse point is accepted")) {
        ++failures;
    }
#endif

    return failures == 0 ? 0 : 1;
}
