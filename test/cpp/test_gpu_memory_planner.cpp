#include "lemon/backends/sdcpp/sdcpp.h"
#include "lemon/backends/whispercpp/whispercpp.h"
#include "lemon/gpu_memory_planner.h"

#include <cstdio>
#include <string>
#include <vector>

using lemon::GpuMemoryAdmissionInputs;
using lemon::GpuMemoryResident;
using lemon::gpu_memory_capacity_from_pools_gb;
using lemon::plan_gpu_memory_admission;
using lemon::uses_gpu_memory_capacity;

static int failures = 0;

static void expect_true(const char* name, bool ok) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

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
    if (!ok) ++failures;
}

static void expect_capacity(const char* name, double actual, double expected) {
    bool ok = actual == expected;
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) {
        std::printf("  got: %.3f GB\n want: %.3f GB\n", actual, expected);
    }
    if (!ok) ++failures;
}

int main() {
    {
        const auto cpu_device =
            lemon::backends::whispercpp::device_for_backend("");
        const auto rocm_device =
            lemon::backends::whispercpp::device_for_backend("rocm");
        expect_true("default Whisper CPU load does not consume GPU capacity",
                    !uses_gpu_memory_capacity(cpu_device));
        expect_true("ROCm Whisper load consumes GPU capacity",
                    uses_gpu_memory_capacity(rocm_device));

        expect_true(
            "unselected SD backend has a CPU fallback",
            !uses_gpu_memory_capacity(
                lemon::backends::sdcpp::device_for_backend("")));
        expect_true(
            "CPU SD load does not consume GPU capacity",
            !uses_gpu_memory_capacity(
                lemon::backends::sdcpp::device_for_backend("cpu")));
        expect_true(
            "ROCm SD load consumes GPU capacity",
            uses_gpu_memory_capacity(
                lemon::backends::sdcpp::device_for_backend("rocm")));
    }

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
        expect_true("candidate fits after planned evictions", plan.can_fit);
        expect_evictions("evicts largest residents first until candidate fits",
                         plan.models_to_evict,
                         {"large", "medium"});
        expect_capacity("configured capacity bounds effective capacity",
                        plan.effective_capacity_gb, 12.0);
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
        expect_true("candidate is rejected when evictions cannot make room",
                    !plan.can_fit);
        expect_evictions("does not evict when candidate cannot fit after dry run",
                         plan.models_to_evict,
                         {});
        expect_capacity("available memory bounds effective capacity",
                        plan.effective_capacity_gb, 6.0);
    }

    {
        GpuMemoryAdmissionInputs inputs;
        inputs.configured_capacity_gb = -1.0;
        inputs.total_capacity_gb = 24.0;
        inputs.free_capacity_gb = 20.0;
        inputs.lemonade_occupancy_gb = 2.0;
        inputs.candidate_occupancy_gb = 4.0;

        auto plan = plan_gpu_memory_admission(inputs);
        expect_true("auto-capacity candidate fits", plan.can_fit);
        expect_capacity("auto capacity uses available memory",
                        plan.effective_capacity_gb, 22.0);
        expect_evictions("auto capacity uses current memory available to Lemonade",
                         plan.models_to_evict,
                         {});
    }

    {
        expect_capacity("APU capacity includes GTT when dGPU GTT is disabled",
                        gpu_memory_capacity_from_pools_gb(0.5, 63.5, true, false),
                        64.0);
    }

    {
        expect_capacity("dGPU capacity ignores GTT by default",
                        gpu_memory_capacity_from_pools_gb(16.0, 64.0, false, false),
                        16.0);
        expect_capacity("dGPU capacity includes GTT when enabled",
                        gpu_memory_capacity_from_pools_gb(16.0, 64.0, false, true),
                        80.0);
        expect_capacity("dGPU capacity is unavailable when VRAM is unavailable and GTT is disabled",
                        gpu_memory_capacity_from_pools_gb(0.0, 20.0, false, false),
                        0.0);
    }

    if (failures == 0) {
        std::printf("\nAll GPU memory planner cases passed\n");
    } else {
        std::printf("\n%d GPU memory planner case(s) FAILED\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
