#define RESIDENCY_EXPLANATIONS_SEAM_NO_MAIN
#include "../residency/contract/explanations_public_seam.cpp"
#undef RESIDENCY_EXPLANATIONS_SEAM_NO_MAIN

namespace {

void test_operation_identity_stability() {
    const auto empty = OperationExplanationStoreSnapshot::empty();
    auto initial = resource_draft("stable-operation", 0);
    initial.plan_id = std::nullopt;
    const auto first = empty.with_revision(initial, at(10));
    require(first.status == ExplanationUpdateStatus::Accepted,
            "initial operation identity was rejected");

    auto bound = resource_draft("stable-operation", 1);
    bound.plan_id = std::string("stable-plan");
    const auto second = first.snapshot.with_revision(bound, at(20));
    require(second.status == ExplanationUpdateStatus::Accepted,
            "nullable plan identity could not bind once");

    auto missing_plan = resource_draft("stable-operation", 2);
    missing_plan.plan_id = std::nullopt;
    require_rejection(second.snapshot.with_revision(missing_plan, at(30)),
                      ExplanationUpdateStatus::Invalid,
                      "bound plan identity returned to null");

    auto changed_plan = resource_draft("stable-operation", 2);
    changed_plan.plan_id = std::string("other-plan");
    require_rejection(second.snapshot.with_revision(changed_plan, at(30)),
                      ExplanationUpdateStatus::Invalid,
                      "bound plan identity changed");

    auto changed_kind = resource_draft("stable-operation", 2);
    changed_kind.plan_id = std::string("stable-plan");
    changed_kind.family = OperationFamily::ResidentState;
    changed_kind.operation_kind = OperationKind::SavedPinMutation;
    require_rejection(second.snapshot.with_revision(changed_kind, at(30)),
                      ExplanationUpdateStatus::Invalid,
                      "operation kind and family changed across revisions");
}

void test_commit_time_and_retention_bounds() {
    const auto empty = OperationExplanationStoreSnapshot::empty();
    const auto first = empty.with_revision(
        resource_draft("monotonic-operation", 0), at(20));
    require(first.status == ExplanationUpdateStatus::Accepted,
            "initial monotonic operation was rejected");
    require_rejection(first.snapshot.with_revision(
                          resource_draft("monotonic-operation", 1), at(19)),
                      ExplanationUpdateStatus::Invalid,
                      "operation commit time moved backward");

    const auto overflow = empty.with_revision(
        resource_draft("overflow-operation", 0, OperationPhase::Terminal,
                       TerminalOutcome::Succeeded, {success_reason()}),
        ExplanationTimePoint::max());
    require_rejection(overflow, ExplanationUpdateStatus::Invalid,
                      "terminal retention deadline overflowed");
}

void test_reason_code_boundaries() {
    const auto empty = OperationExplanationStoreSnapshot::empty();
    const std::vector<std::string> unsafe_codes{
        "residency_cancelled ", "residency_cancelled\n",
        u8"residency_cancelled-é"};
    for (std::size_t index = 0; index < unsafe_codes.size(); ++index) {
        auto draft = resource_draft(
            "unsafe-reason-" + std::to_string(index), 0,
            OperationPhase::Evaluating, std::nullopt, {cancelled_reason()});
        draft.reasons.front().code = unsafe_codes[index];
        require_rejection(empty.with_revision(draft, at(10)),
                          ExplanationUpdateStatus::Invalid,
                          "unsafe reason code gained storage authority");

        const auto rendered = render_reason_projection(
            {1, 0}, {1, 1},
            reason(unsafe_codes[index], "capacity", "p_capacity", "critical",
                   "Injected title", "Injected message"));
        require(rendered.status == ReasonRenderStatus::UnknownFallback,
                "unsafe reason code selected a category fallback");
        require(rendered.reason.has_value(),
                "unsafe reason code omitted its safe fallback");
        require(rendered.reason->code == "unknown_reason" &&
                    rendered.reason->category_id.empty() &&
                    rendered.reason->presentation_id.empty() &&
                    rendered.reason->severity == "warning" &&
                    rendered.reason->title == "Residency condition" &&
                    rendered.reason->default_message ==
                        "A residency condition is not recognized by this "
                        "version.",
                "unsafe reason code did not use the fixed local fallback");
    }
}

}

int main() {
    const auto public_seam_result = run_residency_explanations_public_seam();
    if (public_seam_result != 0) {
        return public_seam_result;
    }
    test_operation_identity_stability();
    test_commit_time_and_retention_bounds();
    test_reason_code_boundaries();
    return 0;
}
