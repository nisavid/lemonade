#include "lemon/gpu_memory_planner.h"

#include <algorithm>
#include <sstream>
#include <utility>

namespace lemon {

GpuMemoryAdmissionPlan plan_gpu_memory_admission(const GpuMemoryAdmissionInputs& inputs) {
    GpuMemoryAdmissionPlan plan;

    const double configured_capacity =
        inputs.configured_capacity_gb < 0.0 ? inputs.total_capacity_gb : inputs.configured_capacity_gb;
    const double memory_available_to_lemonade =
        std::max(0.0, inputs.free_capacity_gb) + std::max(0.0, inputs.lemonade_occupancy_gb);

    plan.effective_capacity_gb = std::min(configured_capacity, memory_available_to_lemonade);
    plan.projected_occupancy_gb =
        std::max(0.0, inputs.lemonade_occupancy_gb) + std::max(0.0, inputs.candidate_occupancy_gb);

    if (plan.projected_occupancy_gb <= plan.effective_capacity_gb) {
        return plan;
    }

    auto candidates = inputs.residents;
    std::sort(candidates.begin(), candidates.end(),
              [](const GpuMemoryResident& a, const GpuMemoryResident& b) {
                  if (a.occupancy_gb == b.occupancy_gb) return a.model_name < b.model_name;
                  return a.occupancy_gb > b.occupancy_gb;
              });

    double projected_after_eviction = plan.projected_occupancy_gb;
    std::vector<std::string> evictions;
    for (const auto& resident : candidates) {
        projected_after_eviction -= std::max(0.0, resident.occupancy_gb);
        evictions.push_back(resident.model_name);
        if (projected_after_eviction <= plan.effective_capacity_gb) {
            plan.projected_occupancy_gb = projected_after_eviction;
            plan.models_to_evict = std::move(evictions);
            return plan;
        }
    }

    plan.can_fit = false;
    std::ostringstream message;
    message << "Predicted GPU memory occupancy "
            << inputs.candidate_occupancy_gb << " GB for requested model cannot fit within effective capacity "
            << plan.effective_capacity_gb << " GB";
    plan.rejection_reason = message.str();
    return plan;
}

} // namespace lemon
