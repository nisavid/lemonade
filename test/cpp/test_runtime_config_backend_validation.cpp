#include <lemon/runtime_config.h>

#include <cstdlib>
#include <cstdio>
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

    return failures == 0 ? 0 : 1;
}
