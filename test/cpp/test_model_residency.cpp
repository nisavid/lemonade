// Standalone contract test for model residency roles and slot-pool limits.
// Compile manually with:
//   g++ -std=c++17 -I src/cpp/include test/cpp/test_model_residency.cpp -o test_model_residency

#include "lemon/backends/llamacpp/llamacpp.h"
#include "lemon/backends/whispercpp/whispercpp.h"
#include "lemon/model_residency.h"

#include <cstdio>
#include <vector>

using lemon::LoadPurpose;
using lemon::ModelType;
using lemon::AdmissionEvictionCandidate;
using lemon::ResidencyClass;
using lemon::is_automatic_eviction_candidate;
using lemon::load_purpose_for_residency_class;
using lemon::plan_admission_evictions;
using lemon::residency_class_for_load_purpose;
using lemon::residency_class_to_string;
using lemon::residency_limit;
using lemon::residency_pool_to_string;
using lemon::same_residency_pool;
using lemon::should_reject_residency_displacement;
using lemon::backends::whispercpp::device_for_backend;
using lemon::backends::whispercpp::slot_policy_for_backend;

static int failures = 0;

static void check(const char* name, bool ok) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

int main() {
    check("user inference maps to standard",
          residency_class_for_load_purpose(LoadPurpose::UserInference) ==
              ResidencyClass::Standard);
    check("routing dependency maps to routing helper",
          residency_class_for_load_purpose(LoadPurpose::RoutingDependency) ==
              ResidencyClass::RoutingHelper);
    check("routing helper round-trips to routing dependency",
          load_purpose_for_residency_class(ResidencyClass::RoutingHelper) ==
              LoadPurpose::RoutingDependency);

    check("standard pool honors configured limit",
          residency_limit(ResidencyClass::Standard, 3) == 3);
    check("standard pool preserves unlimited",
          residency_limit(ResidencyClass::Standard, -1) == -1);
    check("each distinct helper model has one capacity identity",
          residency_limit(ResidencyClass::RoutingHelper, -1) == 1 &&
              residency_limit(ResidencyClass::RoutingHelper, 8) == 1);

    check("standard same type and different models share capacity",
          same_residency_pool(ModelType::LLM, ResidencyClass::Standard, "model-a",
                              ModelType::LLM, ResidencyClass::Standard, "model-b"));
    check("same helper model shares its helper capacity identity",
          same_residency_pool(ModelType::LLM, ResidencyClass::RoutingHelper, "helper-a",
                              ModelType::LLM, ResidencyClass::RoutingHelper, "helper-a"));
    check("distinct same-type helper models do not compete",
          !same_residency_pool(ModelType::LLM, ResidencyClass::RoutingHelper, "helper-a",
                               ModelType::LLM, ResidencyClass::RoutingHelper, "helper-b"));
    check("different residency classes never share capacity",
          !same_residency_pool(ModelType::LLM, ResidencyClass::Standard, "model-a",
                               ModelType::LLM, ResidencyClass::RoutingHelper, "model-a"));
    check("different standard model types do not share capacity",
          !same_residency_pool(ModelType::LLM, ResidencyClass::Standard, "model-a",
                               ModelType::EMBEDDING, ResidencyClass::Standard, "model-b"));

    check("routing helper cannot displace standard residency",
          should_reject_residency_displacement(
              ResidencyClass::RoutingHelper,
              ResidencyClass::Standard));
    check("routing helper cannot displace another routing helper",
          should_reject_residency_displacement(
              ResidencyClass::RoutingHelper,
              ResidencyClass::RoutingHelper));
    check("standard request may displace routing helper",
          !should_reject_residency_displacement(
              ResidencyClass::Standard,
              ResidencyClass::RoutingHelper));
    check("standard request may replace standard residency",
          !should_reject_residency_displacement(
              ResidencyClass::Standard,
              ResidencyClass::Standard));

    check("whispercpp npu variant resolves exclusive NPU admission",
          device_for_backend("npu") == lemon::DEVICE_NPU &&
              slot_policy_for_backend("npu") == lemon::SlotPolicy::ExclusiveNpu);
    check("whispercpp cpu variant keeps standard CPU admission",
          device_for_backend("cpu") == lemon::DEVICE_CPU &&
              slot_policy_for_backend("cpu") == lemon::SlotPolicy::Standard);
    check("llamacpp cpu and system variants resolve CPU admission",
          lemon::backends::llamacpp::device_for_backend("cpu") == lemon::DEVICE_CPU &&
              lemon::backends::llamacpp::device_for_backend("system") == lemon::DEVICE_CPU);
    check("llamacpp accelerator variants resolve GPU admission",
          lemon::backends::llamacpp::device_for_backend("vulkan") == lemon::DEVICE_GPU &&
              lemon::backends::llamacpp::device_for_backend("rocm-stable") == lemon::DEVICE_GPU);

    const auto protected_plan = plan_admission_evictions({
        {"npu-victim", false, false},
        {"count-victim", true, false},
    });
    check("complete admission rejects before returning any eviction",
          !protected_plan.can_admit &&
              protected_plan.models_to_evict.empty() &&
              protected_plan.blocked_model == "count-victim");
    check("automatic eviction requires an idle unpinned resident",
          is_automatic_eviction_candidate(false, false) &&
              !is_automatic_eviction_candidate(true, false) &&
              !is_automatic_eviction_candidate(false, true));
    const auto busy_plan = plan_admission_evictions({
        {"idle-npu-victim", false, false},
        {"busy-gpu-victim", false, true},
    });
    check("in-use victim rejects the complete admission without eviction",
          !busy_plan.can_admit &&
              busy_plan.models_to_evict.empty() &&
              busy_plan.blocked_model == "busy-gpu-victim");

    check("health class string is stable",
          residency_class_to_string(ResidencyClass::RoutingHelper) ==
              "routing_helper");
    check("pool string contains role and type",
          residency_pool_to_string(ModelType::LLM,
                                   ResidencyClass::RoutingHelper) ==
              "routing_helper/llm");

    if (failures == 0) {
        std::printf("\nAll model_residency tests passed\n");
        return 0;
    }
    std::printf("\n%d model_residency test(s) FAILED\n", failures);
    return 1;
}
