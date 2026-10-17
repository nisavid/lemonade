#include <lemon/runtime_config.h>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <string>

using lemon::RuntimeConfig;
using lemon::json;

namespace {

void set_environment(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

void unset_environment(const char* name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

class ScopedEnvironment {
public:
    explicit ScopedEnvironment(const char* name) : name_(name) {
        if (const char* value = std::getenv(name)) {
            original_ = value;
        }
    }

    ~ScopedEnvironment() {
        if (original_) {
            set_environment(name_, *original_);
        } else {
            unset_environment(name_);
        }
    }

private:
    const char* name_;
    std::optional<std::string> original_;
};

}  // namespace

int main() {
    int failures = 0;

    try {
        RuntimeConfig::validate_backend_choice("llamacpp", "system");
#ifdef __linux__
        std::puts("[PASS] llamacpp system backend is accepted on Linux");
#else
        std::puts("[FAIL] llamacpp system backend was accepted on an unsupported platform");
        ++failures;
#endif
    } catch (const std::invalid_argument& error) {
#ifdef __linux__
        std::printf("[FAIL] llamacpp system backend was rejected on Linux: %s\n", error.what());
        ++failures;
#else
        std::puts("[PASS] llamacpp system backend is rejected on unsupported platforms");
#endif
    }

    constexpr const char* install_method_env = "LEMONADE_ROCM_INSTALL_METHOD";
    ScopedEnvironment restore_install_method(install_method_env);
    RuntimeConfig config(json{{"rocm_install_method", "auto"}});

    for (const std::string method : {"auto", "wheel", "tarball"}) {
        set_environment(install_method_env, method);
        if (config.rocm_install_method() == method) {
            std::printf("[PASS] ROCm install method environment accepts %s\n", method.c_str());
        } else {
            std::printf("[FAIL] ROCm install method environment rejected %s\n", method.c_str());
            ++failures;
        }
    }

    set_environment(install_method_env, "unexpected");
    try {
        (void)config.rocm_install_method();
        std::puts("[FAIL] invalid ROCm install method environment was accepted");
        ++failures;
    } catch (const std::invalid_argument&) {
        std::puts("[PASS] invalid ROCm install method environment is rejected");
    }

    unset_environment(install_method_env);
    RuntimeConfig invalid_config(json{{"rocm_install_method", "unexpected"}});
    try {
        (void)invalid_config.rocm_install_method();
        std::puts("[FAIL] invalid ROCm install method config value was accepted");
        ++failures;
    } catch (const std::invalid_argument&) {
        std::puts("[PASS] invalid ROCm install method config value is rejected");
    }

    const std::string bin_field_error = "'llamacpp.vulkan_bin' must be a string";
    for (const json& malformed :
         {json(7), json(true), json(nullptr), json::object(), json::array()}) {
        json values = json::object();
        values["llamacpp"]["vulkan_bin"] = malformed;
        RuntimeConfig bin_config(values);
        try {
            (void)bin_config.backend_string("llamacpp", "vulkan_bin");
            std::printf("[FAIL] loaded llamacpp.vulkan_bin %s was accepted\n",
                        malformed.dump().c_str());
            ++failures;
        } catch (const std::invalid_argument& error) {
            if (error.what() == bin_field_error) {
                std::printf("[PASS] loaded llamacpp.vulkan_bin %s reports its field\n",
                            malformed.dump().c_str());
            } else {
                std::printf("[FAIL] loaded llamacpp.vulkan_bin %s reported: %s\n",
                            malformed.dump().c_str(), error.what());
                ++failures;
            }
        }
    }

    json absent_bin = json::object();
    absent_bin["llamacpp"] = json::object();
    if (RuntimeConfig(absent_bin).backend_string("llamacpp", "vulkan_bin").empty()) {
        std::puts("[PASS] an absent llamacpp.vulkan_bin reads as empty");
    } else {
        std::puts("[FAIL] an absent llamacpp.vulkan_bin read as non-empty");
        ++failures;
    }

    json pinned_bin = json::object();
    pinned_bin["llamacpp"]["vulkan_bin"] = "builtin";
    RuntimeConfig set_config(pinned_bin);
    json malformed_change = json::object();
    malformed_change["llamacpp"]["vulkan_bin"] = 7;
    try {
        (void)set_config.set(malformed_change);
        std::puts("[FAIL] runtime set accepted a non-string llamacpp.vulkan_bin");
        ++failures;
    } catch (const std::invalid_argument& error) {
        if (error.what() == bin_field_error &&
            set_config.backend_string("llamacpp", "vulkan_bin") == "builtin") {
            std::puts("[PASS] runtime set rejects a non-string llamacpp.vulkan_bin unchanged");
        } else {
            std::printf("[FAIL] runtime set rejection reported %s or changed the value\n",
                        error.what());
            ++failures;
        }
    }

    return failures == 0 ? 0 : 1;
}
