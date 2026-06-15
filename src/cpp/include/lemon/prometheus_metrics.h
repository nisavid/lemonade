#pragma once

#include <string>

namespace lemon {

class Router;

struct SystemMetrics {
    double cpu_percent = -1.0;
    double gpu_percent = -1.0;
    double vram_gb = -1.0;
    double npu_percent = -1.0;
};

std::string build_prometheus_metrics(Router& router, const SystemMetrics& system_metrics);

} // namespace lemon
