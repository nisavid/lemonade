#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace lemon {

struct LinuxGpuMemoryUsageSample {
    bool is_discrete_gpu = false;
    std::optional<uint64_t> vram_used_bytes;
    std::optional<uint64_t> gtt_used_bytes;
};

double aggregate_linux_gpu_memory_usage_gb(
    const std::vector<LinuxGpuMemoryUsageSample>& samples,
    bool enable_discrete_gpu_gtt);

// Platform-specific system metrics collection
class SystemMetricsPlatform {
public:
    virtual ~SystemMetricsPlatform() = default;

    // Platform name for identification
    virtual const char* get_platform_name() const = 0;

    // CPU usage percentage (0-100), -1 if not available
    // Requires mutex for delta tracking
    virtual double get_cpu_usage(std::mutex& cpu_stats_mutex,
                                 uint64_t& last_total,
                                 uint64_t& last_total_idle) = 0;

    // Memory usage in GB, 0 if not available
    virtual double get_memory_usage_gb() = 0;

    // GPU usage percentage (0-100), -1 if not available or unsupported
    virtual double get_gpu_usage() = 0;

    // VRAM usage in GB, -1 if not available or unsupported
    virtual double get_vram_usage_gb() = 0;

    virtual double get_vram_usage_gb(bool enable_discrete_gpu_gtt) {
        (void)enable_discrete_gpu_gtt;
        return get_vram_usage_gb();
    }

    // NPU utilization percentage (0-100), -1 if not available or unsupported
    virtual double get_npu_utilization() = 0;
};

// Factory function to create platform-specific implementation
std::unique_ptr<SystemMetricsPlatform> create_metrics_platform();

} // namespace lemon
