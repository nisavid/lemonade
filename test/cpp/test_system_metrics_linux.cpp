#include "lemon/system_metrics_platform.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

namespace {

constexpr uint64_t GIB = 1024ULL * 1024ULL * 1024ULL;

void expect_usage(const char* name, double actual, double expected) {
    const bool ok = std::fabs(actual - expected) < 1e-12;
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) {
        std::printf("  got: %.3f GB\n want: %.3f GB\n", actual, expected);
    }
    assert(ok);
}

} // namespace

int main() {
    using lemon::LinuxGpuMemoryUsageSample;
    using lemon::aggregate_linux_gpu_memory_usage_gb;

    const std::vector<LinuxGpuMemoryUsageSample> hybrid_host = {
        {false, 1 * GIB, 3 * GIB},
        {true, 4 * GIB, 8 * GIB},
    };

    expect_usage("all visible GPUs contribute and APU GTT is included",
                 aggregate_linux_gpu_memory_usage_gb(hybrid_host, false),
                 8.0);
    expect_usage("dGPU GTT is included only when enabled",
                 aggregate_linux_gpu_memory_usage_gb(hybrid_host, true),
                 16.0);

    expect_usage("an available zero-valued counter reports zero usage",
                 aggregate_linux_gpu_memory_usage_gb(
                     {{true, uint64_t{0}, std::nullopt}}, false),
                 0.0);
    expect_usage("missing counters report unavailable usage",
                 aggregate_linux_gpu_memory_usage_gb(
                     {{false, std::nullopt, std::nullopt}}, true),
                 -1.0);

    std::printf("\nAll Linux system metrics cases passed\n");
    return 0;
}
