#pragma once

#include <string>
#include <vector>

namespace lemon {

struct GpuMemoryResident {
    std::string model_name;
    double occupancy_gb = 0.0;
};

struct GpuMemoryAdmissionInputs {
    // Negative configured capacity means auto: use total_capacity_gb.
    double configured_capacity_gb = -1.0;
    double total_capacity_gb = 0.0;
    double free_capacity_gb = 0.0;
    double lemonade_occupancy_gb = 0.0;
    double candidate_occupancy_gb = 0.0;
    std::vector<GpuMemoryResident> residents;
};

struct GpuMemoryAdmissionPlan {
    bool can_fit = true;
    double effective_capacity_gb = 0.0;
    double projected_occupancy_gb = 0.0;
    std::vector<std::string> models_to_evict;
    std::string rejection_reason;
};

GpuMemoryAdmissionPlan plan_gpu_memory_admission(const GpuMemoryAdmissionInputs& inputs);

} // namespace lemon
