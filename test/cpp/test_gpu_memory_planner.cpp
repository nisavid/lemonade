#include "lemon/gpu_memory_planner.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using lemon::GpuMemoryAdmissionInputs;
using lemon::GpuMemoryResident;
using lemon::plan_gpu_memory_admission;

static void expect_evictions(const char* name,
                             const std::vector<std::string>& actual,
                             const std::vector<std::string>& expected) {
    bool ok = actual == expected;
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) {
        std::printf("  got:");
        for (const auto& value : actual) std::printf(" %s", value.c_str());
        std::printf("\n want:");
        for (const auto& value : expected) std::printf(" %s", value.c_str());
        std::printf("\n");
    }
    assert(ok);
}

int main() {
    {
        GpuMemoryAdmissionInputs inputs;
        inputs.configured_capacity_gb = 12.0;
        inputs.total_capacity_gb = 16.0;
        inputs.free_capacity_gb = 4.0;
        inputs.lemonade_occupancy_gb = 8.0;
        inputs.candidate_occupancy_gb = 11.0;
        inputs.residents = {
            {"small", 1.0},
            {"large", 5.0},
            {"medium", 3.0},
        };

        auto plan = plan_gpu_memory_admission(inputs);
        assert(plan.can_fit);
        expect_evictions("evicts largest residents first until candidate fits",
                         plan.models_to_evict,
                         {"large", "medium"});
        assert(plan.effective_capacity_gb == 12.0);
    }

    {
        GpuMemoryAdmissionInputs inputs;
        inputs.configured_capacity_gb = 16.0;
        inputs.total_capacity_gb = 16.0;
        inputs.free_capacity_gb = 2.0;
        inputs.lemonade_occupancy_gb = 4.0;
        inputs.candidate_occupancy_gb = 10.0;
        inputs.residents = {
            {"resident", 4.0},
        };

        auto plan = plan_gpu_memory_admission(inputs);
        assert(!plan.can_fit);
        expect_evictions("does not evict when candidate cannot fit after dry run",
                         plan.models_to_evict,
                         {});
        assert(plan.effective_capacity_gb == 6.0);
    }

    {
        GpuMemoryAdmissionInputs inputs;
        inputs.configured_capacity_gb = -1.0;
        inputs.total_capacity_gb = 24.0;
        inputs.free_capacity_gb = 20.0;
        inputs.lemonade_occupancy_gb = 2.0;
        inputs.candidate_occupancy_gb = 4.0;

        auto plan = plan_gpu_memory_admission(inputs);
        assert(plan.can_fit);
        assert(plan.effective_capacity_gb == 22.0);
        expect_evictions("auto capacity uses current memory available to Lemonade",
                         plan.models_to_evict,
                         {});
    }

    std::printf("\nAll GPU memory planner cases passed\n");
    return 0;
}
