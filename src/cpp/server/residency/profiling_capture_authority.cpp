#include "lemon/residency/profiling_capture_authority.h"

#include "lemon/residency/claims.h"
#include "profiling_common.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace lemon::residency {

ProfilingWorkloadStepResult::ProfilingWorkloadStepResult(
    ProfilingWorkloadStepStatus status,
    std::string_view diagnostic) noexcept
    : status_(status) {
    auto boundary = diagnostic.size();
    if (boundary > diagnostic_.size()) {
        boundary = diagnostic_.size();
        while (boundary > 0 &&
               (static_cast<unsigned char>(diagnostic[boundary]) & 0xc0u) ==
                   0x80u) {
            --boundary;
        }
    }
    for (std::size_t index = 0; index < boundary; ++index) {
        diagnostic_[index] = diagnostic[index];
    }
    diagnostic_size_ = boundary;
}

ProfilingWorkloadStepResult ProfilingWorkloadStepResult::success() noexcept {
    return ProfilingWorkloadStepResult{
        ProfilingWorkloadStepStatus::Succeeded, {}};
}

ProfilingWorkloadStepResult ProfilingWorkloadStepResult::cancelled(
    std::string_view diagnostic) noexcept {
    return ProfilingWorkloadStepResult{
        ProfilingWorkloadStepStatus::Cancelled, diagnostic};
}

ProfilingWorkloadStepResult ProfilingWorkloadStepResult::failed(
    std::string_view diagnostic) noexcept {
    return ProfilingWorkloadStepResult{
        ProfilingWorkloadStepStatus::Failed, diagnostic};
}

ProfilingWorkloadStepResult ProfilingWorkloadStepResult::ambiguous(
    std::string_view diagnostic) noexcept {
    return ProfilingWorkloadStepResult{
        ProfilingWorkloadStepStatus::Ambiguous, diagnostic};
}

ProfilingWorkloadStepStatus
ProfilingWorkloadStepResult::status() const noexcept {
    return status_;
}

std::string_view
ProfilingWorkloadStepResult::diagnostic() const noexcept {
    return {diagnostic_.data(), diagnostic_size_};
}

bool ProfilingWorkloadStepResult::succeeded() const noexcept {
    return status_ == ProfilingWorkloadStepStatus::Succeeded;
}

namespace {

using SteadyClock = std::chrono::steady_clock;
using SystemClock = std::chrono::system_clock;
using profiling_internal::append_string;
using profiling_internal::append_u64;
using profiling_internal::cancelled;
using profiling_internal::digest_is_valid;
using profiling_internal::elapsed_between;
using profiling_internal::sha256_hex;

constexpr std::string_view phase_provenance_domain =
    "lemonade/profiling-phase-interval/v1";

ProfilingTransactionCapture capture_failure(std::string_view diagnostic) {
    if (diagnostic.empty()) diagnostic = "profiling interval capture failed";
    return ProfilingTransactionCapture{
        {}, {}, {},
        profiling_internal::bounded_diagnostic(std::string(diagnostic))};
}

std::string_view select_capture_failure_diagnostic(
    const ProfilingWorkloadStepResult &workload_result,
    const ProfilingWorkloadStepResult *release_result,
    std::string_view fallback_diagnostic) noexcept {
    if (release_result == nullptr) {
        return "profiling workload release was not verified";
    }
    if (!release_result->succeeded()) {
        return release_result->diagnostic().empty()
                   ? std::string_view{
                         "profiling workload release was not verified"}
                   : release_result->diagnostic();
    }
    if (!workload_result.succeeded()) {
        return workload_result.diagnostic().empty()
                   ? std::string_view{
                         "profiling workload did not complete"}
                   : workload_result.diagnostic();
    }
    return fallback_diagnostic;
}

void initialize_clock(ProfilingCollectionClock &clock) {
    if (!clock.monotonic_now) {
        clock.monotonic_now = [] { return SteadyClock::now(); };
    }
    if (!clock.utc_now) clock.utc_now = [] { return SystemClock::now(); };
}

bool schedule_is_valid(const ProfilingDerivationContract &contract,
                       const ProfilingCaptureSchedule &schedule) noexcept {
    if (schedule.observation_poll_interval <=
            std::chrono::milliseconds::zero() ||
        schedule.poll_cycle_overhead_allowance <=
            std::chrono::milliseconds::zero() ||
        contract.max_source_skew <= std::chrono::milliseconds::zero() ||
        contract.interval.max_observation_gap <= contract.max_source_skew) {
        return false;
    }
    const auto cycle_budget_after_source =
        contract.interval.max_observation_gap - contract.max_source_skew;
    if (cycle_budget_after_source <=
        schedule.poll_cycle_overhead_allowance) {
        return false;
    }
    return schedule.observation_poll_interval <=
           cycle_budget_after_source -
               schedule.poll_cycle_overhead_allowance;
}

bool contract_covers_selector(
    const ProfilingDerivationContract &contract,
    const ProfilingTransactionContext &context) {
    const auto contract_sha256 =
        profiling_derivation_contract_sha256(contract);
    if (!contract_sha256 ||
        *contract_sha256 != context.observation_contract_sha256 ||
        context.selector.catalog_selector.constraints.size() != 2) {
        return false;
    }

    bool has_owner_coverage = false;
    std::size_t capacity_constraints = 0;
    for (const auto constraint :
         context.selector.catalog_selector.constraints) {
        const auto requirement = constraint_evidence_requirement(constraint);
        if (!requirement) return false;
        if (requirement->kind == ConstraintEvidenceKind::OwnerCoverage) {
            has_owner_coverage = true;
            continue;
        }
        if (requirement->family != ClaimFamily::ConsumableCapacity ||
            (constraint != ConstraintKind::GpuSharedResidency &&
             constraint != ConstraintKind::GpuProviderResolvedCapacity)) {
            return false;
        }
        ++capacity_constraints;
    }
    return has_owner_coverage && capacity_constraints == 1;
}

std::optional<bool> elapsed_at_least(
    SteadyClock::time_point started,
    SteadyClock::time_point finished,
    std::chrono::milliseconds required) noexcept {
    if (required <= std::chrono::milliseconds::zero()) return std::nullopt;
    const auto elapsed = elapsed_between(started, finished);
    if (!elapsed) return std::nullopt;
    return std::chrono::duration_cast<std::chrono::milliseconds>(*elapsed) >=
           required;
}

bool segment_moved(const ProfilingIntervalSegment &segment) noexcept {
    return segment.after_event_watermark != segment.through_event_watermark;
}

bool claim_closure_is_zero(std::vector<ClaimFamilyClosure> closure) {
    const auto checked = check_claim_closure(std::move(closure));
    if (!checked.accepted()) return false;
    for (const auto family : {ClaimFamily::ConsumableCapacity,
                              ClaimFamily::SafetyFloor,
                              ClaimFamily::CardinalityPool,
                              ClaimFamily::CompatibilityExclusivity}) {
        if (!checked.claims->entries(family).empty()) return false;
    }
    return true;
}

bool claim_closures_equal(std::vector<ClaimFamilyClosure> left,
                          std::vector<ClaimFamilyClosure> right) {
    const auto checked_left = check_claim_closure(std::move(left));
    const auto checked_right = check_claim_closure(std::move(right));
    return checked_left.accepted() && checked_right.accepted() &&
           *checked_left.claims == *checked_right.claims;
}

bool release_is_bounded(
    const std::vector<ClaimFamilyClosure> &baseline,
    const std::vector<ClaimFamilyClosure> &release,
    const std::vector<ClaimFamilyClosure> &uncertainty) {
    const auto checked_baseline = check_claim_closure(baseline);
    const auto checked_release = check_claim_closure(release);
    const auto checked_uncertainty = check_claim_closure(uncertainty);
    if (!checked_baseline.accepted() || !checked_release.accepted() ||
        !checked_uncertainty.accepted()) {
        return false;
    }
    const auto upper_bound =
        checked_add(*checked_baseline.claims, *checked_uncertainty.claims);
    return upper_bound.accepted() &&
           checked_subtract(*upper_bound.claims, *checked_release.claims)
               .accepted();
}

bool observation_is_valid(const ProfilingDerivedObservation &observation,
                          const ProfilingDerivationContract &contract,
                          const ProfilingTransactionContext &context) {
    return observation.provider_id == contract.provider_id &&
           observation.provider_revision_sha256 ==
               contract.provider_revision_sha256 &&
           digest_is_valid(observation.raw_provenance_sha256) &&
           observation.observation_contract_sha256 ==
               context.observation_contract_sha256 &&
           observation.max_source_skew_milliseconds ==
               static_cast<std::uint64_t>(contract.max_source_skew.count()) &&
           observation.source_skew_milliseconds <=
               observation.max_source_skew_milliseconds &&
           observation.health == ProfilingObservationHealth::Valid &&
           observation.owner_coverage == ProfilingOwnerCoverage::Complete &&
           check_claim_closure(observation.observed_claims).accepted() &&
           check_claim_closure(observation.attributed_claims).accepted() &&
           check_claim_closure(observation.uncertainty_claims).accepted() &&
           check_claim_closure(observation.safety_margin_claims).accepted();
}

bool segment_is_valid(const ProfilingIntervalSegment &segment,
                      const ProfilingDerivationContract &contract,
                      const ProfilingTransactionContext &context,
                      std::string_view owner_scope_set_sha256) {
    return digest_is_valid(segment.source_epoch_sha256) &&
           segment.owner_scope_set_sha256 == owner_scope_set_sha256 &&
           segment.event_semantics_revision_sha256 ==
               contract.interval.event_semantics_revision_sha256 &&
           segment.first_capture_generation != 0 &&
           segment.first_capture_generation <=
               segment.last_capture_generation &&
           segment.frame_count != 0 &&
           digest_is_valid(segment.provenance_sha256) &&
           segment.checkpoint.capture_generation ==
               segment.last_capture_generation &&
           segment.checkpoint.event_watermark ==
               segment.through_event_watermark &&
           observation_is_valid(segment.checkpoint.observation, contract,
                                context) &&
           check_claim_closure(segment.peak_observed_claims).accepted() &&
           check_claim_closure(segment.peak_attributed_claims).accepted() &&
           claim_closure_is_zero(segment.external_change_claims) &&
           claim_closure_is_zero(segment.unattributed_claims) &&
           check_claim_closure(segment.uncertainty_claims).accepted() &&
           check_claim_closure(segment.safety_margin_claims).accepted();
}

bool segment_chain_is_valid(
    const std::vector<const ProfilingIntervalSegment *> &segments,
    const ProfilingDerivationContract &contract,
    const ProfilingTransactionContext &context) {
    const auto owner_scope_set_sha256 =
        profiling_owner_scope_set_sha256(contract.owner_scopes);
    if (!owner_scope_set_sha256 || segments.empty()) return false;

    const auto &first = *segments.front();
    for (std::size_t index = 0; index < segments.size(); ++index) {
        const auto &segment = *segments[index];
        if (!segment_is_valid(segment, contract, context,
                              *owner_scope_set_sha256) ||
            segment.source_epoch_sha256 != first.source_epoch_sha256) {
            return false;
        }
        if (index != 0) {
            const auto &previous = *segments[index - 1];
            if (previous.through_event_watermark !=
                    segment.after_event_watermark ||
                previous.last_capture_generation !=
                    segment.first_capture_generation) {
                return false;
            }
        }
    }
    return true;
}

std::string_view phase_wire(ProfilingPhase phase) noexcept {
    switch (phase) {
    case ProfilingPhase::Baseline:
        return "baseline";
    case ProfilingPhase::Workload:
        return "workload";
    case ProfilingPhase::Release:
        return "release";
    default:
        return {};
    }
}

std::optional<std::string> phase_provenance_sha256(
    ProfilingPhase phase,
    const ProfilingTransactionContext &context,
    const std::vector<ProfilingIntervalSegment> &segments) {
    const auto wire = phase_wire(phase);
    if (wire.empty() || segments.empty()) return std::nullopt;

    std::string bytes;
    append_string(bytes, phase_provenance_domain);
    append_string(bytes, wire);
    append_string(bytes, context.deployment_id);
    append_u64(bytes, context.sequence);
    append_string(bytes, context.profiling_transaction_id);
    append_string(bytes, context.selector_sha256);
    append_string(bytes, context.observation_contract_sha256);
    append_string(bytes, context.predictor_contract_sha256);
    append_u64(bytes, context.generations.model);
    append_u64(bytes, context.generations.backend);
    append_u64(bytes, context.generations.device);
    append_u64(bytes, context.generations.topology);
    append_u64(bytes, context.generations.driver);
    append_u64(bytes, context.generations.configuration);
    append_u64(bytes, context.generations.workload);
    append_u64(bytes, static_cast<std::uint64_t>(segments.size()));
    for (const auto &segment : segments) {
        if (!digest_is_valid(segment.provenance_sha256)) {
            return std::nullopt;
        }
        append_string(bytes, segment.provenance_sha256);
    }
    return sha256_hex(bytes);
}

ProfilingPhaseAttestationDraft phase_draft(
    ProfilingPhase phase,
    ProfilingLifecycleState lifecycle_state,
    const ProfilingTransactionContext &context,
    const ProfilingIntervalSegment &selected,
    const std::string &provenance_sha256,
    bool use_interval_peaks) {
    const auto &observation = selected.checkpoint.observation;
    ProfilingPhaseAttestationDraft draft;
    draft.schema = supported_local_overlay_schema;
    draft.phase = phase;
    draft.deployment_id = context.deployment_id;
    draft.profiling_transaction_id = context.profiling_transaction_id;
    draft.selector_sha256 = context.selector_sha256;
    draft.provider_id = observation.provider_id;
    draft.provider_revision_sha256 = observation.provider_revision_sha256;
    draft.provenance_sha256 = provenance_sha256;
    draft.observation_contract_sha256 =
        observation.observation_contract_sha256;
    draft.predictor_contract_sha256 = context.predictor_contract_sha256;
    draft.generations = context.generations;
    draft.observation_generation = selected.checkpoint.capture_generation;
    draft.observed_at = observation.observed_at;
    draft.fresh_until = observation.fresh_until;
    draft.source_skew_milliseconds =
        observation.source_skew_milliseconds;
    draft.max_source_skew_milliseconds =
        observation.max_source_skew_milliseconds;
    draft.health = observation.health;
    draft.owner_coverage = observation.owner_coverage;
    draft.observed_claims = use_interval_peaks
                                ? selected.peak_observed_claims
                                : observation.observed_claims;
    draft.attributed_claims = use_interval_peaks
                                  ? selected.peak_attributed_claims
                                  : observation.attributed_claims;
    draft.external_change_claims = selected.external_change_claims;
    draft.unattributed_claims = selected.unattributed_claims;
    draft.uncertainty_claims = selected.uncertainty_claims;
    draft.safety_margin_claims = selected.safety_margin_claims;
    draft.lifecycle_state = lifecycle_state;
    return draft;
}

class WorkloadReleaseGuard {
public:
    WorkloadReleaseGuard(Router &router,
                         const ProfilingTransactionContext &context,
                         ProfilingWorkloadDriver &workload) noexcept
        : router_(router), context_(context), workload_(workload) {}

    ~WorkloadReleaseGuard() { release(); }

    void enter() noexcept { entered_ = true; }

    const ProfilingWorkloadStepResult *release() noexcept {
        if (!entered_) return nullptr;
        if (released_) return result_ ? &*result_ : nullptr;
        released_ = true;
        result_.emplace(workload_.release(router_, context_));
        return &*result_;
    }

private:
    Router &router_;
    const ProfilingTransactionContext &context_;
    ProfilingWorkloadDriver &workload_;
    std::optional<ProfilingWorkloadStepResult> result_;
    bool entered_ = false;
    bool released_ = false;
};

class CaptureOperation {
public:
    CaptureOperation(const ProfilingDerivationContract &contract,
                     ProfilingIntervalObservationSource &source,
                     const ProfilingCaptureSchedule &schedule,
                     Router &router,
                     const ProfilingTransactionContext &context,
                     ProfilingWorkloadDriver &workload,
                     const ProfilingCancellationCheck &should_abort)
        : contract_(contract), schedule_(schedule), router_(router),
          context_(context), workload_(workload),
          should_abort_(should_abort),
          recorder_(contract, source, schedule.clock) {}

    ~CaptureOperation() {
        if (!observer_.joinable()) return;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_requested_ = true;
        }
        condition_.notify_all();
        observer_.join();
    }

    ProfilingTransactionCapture run() {
        const auto started = recorder_.begin(context_, should_abort_);
        if (!started.ok()) return recorder_failure(started);
        if (!settle_baseline()) return capture_failure(diagnostic_);

        try {
            observer_ = std::thread([this] { observe(); });
        } catch (...) {
            return capture_failure("profiling observer could not start");
        }

        if (!wait_until([](const SharedState &state) {
                return state.observer_ready || state.failed;
            })) {
            return finish_failed_capture();
        }
        if (cancelled(should_abort_)) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                diagnostic_ = "profiling interval capture cancelled";
            }
            return finish_failed_capture();
        }

        WorkloadReleaseGuard release_guard(router_, context_, workload_);
        auto workload_result = ProfilingWorkloadStepResult::ambiguous(
            "profiling workload result was not reported");
        release_guard.enter();
        try {
            workload_result = workload_.run(router_, context_, [this] {
                return observer_failed_.load(std::memory_order_acquire) ||
                       cancelled(should_abort_);
            });
        } catch (...) {
            workload_result = ProfilingWorkloadStepResult::ambiguous(
                "profiling workload threw before reporting a result");
        }
        const auto finish_after_release =
            [&](std::string_view fallback_diagnostic) {
                const auto *release_result = release_guard.release();
                return finish_failed_capture(
                    select_capture_failure_diagnostic(
                        workload_result, release_result,
                        fallback_diagnostic));
            };
        const bool capture_cancelled = cancelled(should_abort_);
        if (capture_cancelled) {
            return finish_after_release(
                "profiling interval capture cancelled");
        }
        if (!workload_result.succeeded()) {
            return finish_after_release({});
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.workload_done = true;
        }
        condition_.notify_all();
        if (!wait_until([](const SharedState &state) {
                return state.workload_boundary_ready || state.failed;
            })) {
            return finish_after_release({});
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.release_started = true;
        }
        condition_.notify_all();
        if (!wait_until([](const SharedState &state) {
                return state.release_observer_ready || state.failed;
            })) {
            return finish_after_release({});
        }

        const auto *release_result = release_guard.release();
        if (release_result == nullptr || !release_result->succeeded()) {
            return finish_failed_capture(
                select_capture_failure_diagnostic(
                    workload_result, release_result, {}));
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_.release_done = true;
        }
        condition_.notify_all();
        if (!wait_until([](const SharedState &state) {
                return state.complete || state.failed;
            })) {
            return finish_failed_capture();
        }
        join_observer();

        if (cancelled(should_abort_)) {
            return capture_failure("profiling interval capture cancelled");
        }
        return seal_capture();
    }

private:
    struct SharedState {
        bool observer_ready = false;
        bool workload_done = false;
        bool workload_boundary_ready = false;
        bool release_started = false;
        bool release_observer_ready = false;
        bool release_done = false;
        bool complete = false;
        bool failed = false;
    };

    template <typename Predicate>
    bool wait_until(Predicate predicate) {
        std::unique_lock<std::mutex> lock(mutex_);
        condition_.wait(lock, [&] { return predicate(state_); });
        return !state_.failed;
    }

    std::optional<SteadyClock::time_point> monotonic_now() {
        try {
            return schedule_.clock.monotonic_now();
        } catch (...) {
            diagnostic_ = "profiling capture clock is unavailable";
            return std::nullopt;
        }
    }

    bool settle_baseline() {
        auto stable_started = monotonic_now();
        if (!stable_started) return false;
        for (;;) {
            if (cancelled(should_abort_)) {
                diagnostic_ = "profiling interval capture cancelled";
                return false;
            }
            std::this_thread::sleep_for(
                schedule_.observation_poll_interval);
            const auto boundary =
                recorder_.checkpoint(should_abort_);
            if (!boundary.ok() || !boundary.segment) {
                diagnostic_ = boundary.diagnostic.empty()
                                  ? "profiling baseline could not be proven"
                                  : boundary.diagnostic;
                return false;
            }
            baseline_segments_.push_back(*boundary.segment);
            const auto observed = monotonic_now();
            if (!observed) return false;
            if (segment_moved(*boundary.segment)) {
                stable_started = observed;
                continue;
            }
            const auto stable = elapsed_at_least(
                *stable_started, *observed,
                contract_.interval.baseline_stability_window);
            if (!stable) {
                diagnostic_ = "profiling baseline clock regressed";
                return false;
            }
            if (*stable) return true;
        }
    }

    void observe() noexcept {
        try {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                state_.observer_ready = true;
            }
            condition_.notify_all();

            if (!poll_until_workload_done()) return;
            const auto workload_boundary = recorder_.checkpoint(
                [this] { return observer_should_abort(); });
            if (!workload_boundary.ok() || !workload_boundary.segment) {
                fail_observer(workload_boundary.diagnostic.empty()
                                  ? "profiling workload boundary is incomplete"
                                  : workload_boundary.diagnostic);
                return;
            }
            workload_segments_.push_back(*workload_boundary.segment);
            {
                std::unique_lock<std::mutex> lock(mutex_);
                state_.workload_boundary_ready = true;
                condition_.notify_all();
                condition_.wait(lock, [this] {
                    return state_.release_started || stop_requested_;
                });
                if (stop_requested_) {
                    lock.unlock();
                    fail_observer("profiling observer stopped before release");
                    return;
                }
                state_.release_observer_ready = true;
            }
            condition_.notify_all();

            if (!poll_until_release_done()) return;
            const auto release_boundary = recorder_.checkpoint(
                [this] { return observer_should_abort(); });
            if (!release_boundary.ok() || !release_boundary.segment) {
                fail_observer(release_boundary.diagnostic.empty()
                                  ? "profiling release boundary is incomplete"
                                  : release_boundary.diagnostic);
                return;
            }
            if (!segment_moved(*release_boundary.segment)) {
                fail_observer(
                    "profiling release transition was not observed");
                return;
            }
            release_segments_.push_back(*release_boundary.segment);
            if (!settle_release()) return;
            if (cancelled(should_abort_)) {
                fail_observer("profiling interval capture cancelled");
                return;
            }

            const auto finished = recorder_.finish();
            if (!finished.ok() || !finished.segment ||
                segment_moved(*finished.segment)) {
                fail_observer(finished.diagnostic.empty()
                                  ? "profiling final drain did not remain stable"
                                  : finished.diagnostic);
                return;
            }
            release_segments_.push_back(*finished.segment);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                state_.complete = true;
            }
            condition_.notify_all();
        } catch (...) {
            fail_observer("profiling observer failed");
        }
    }

    bool poll_until_workload_done() {
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (state_.workload_done) return true;
                if (stop_requested_) {
                    lock.unlock();
                    fail_observer("profiling observer stopped during workload");
                    return false;
                }
                condition_.wait_for(
                    lock, schedule_.observation_poll_interval, [this] {
                        return state_.workload_done || stop_requested_;
                    });
                if (state_.workload_done) return true;
                if (stop_requested_) continue;
            }
            const auto polled = recorder_.poll(
                [this] { return observer_should_abort(); });
            if (!polled.ok()) {
                fail_observer(polled.diagnostic.empty()
                                  ? "profiling workload observation failed"
                                  : polled.diagnostic);
                return false;
            }
        }
    }

    bool poll_until_release_done() {
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (state_.release_done) return true;
                if (stop_requested_) {
                    lock.unlock();
                    fail_observer("profiling observer stopped during release");
                    return false;
                }
                condition_.wait_for(
                    lock, schedule_.observation_poll_interval, [this] {
                        return state_.release_done || stop_requested_;
                    });
                if (state_.release_done) return true;
                if (stop_requested_) continue;
            }
            const auto polled = recorder_.poll(
                [this] { return observer_should_abort(); });
            if (!polled.ok()) {
                fail_observer(polled.diagnostic.empty()
                                  ? "profiling release observation failed"
                                  : polled.diagnostic);
                return false;
            }
        }
    }

    bool settle_release() {
        auto stable_started = monotonic_now();
        if (!stable_started) {
            fail_observer(diagnostic_);
            return false;
        }
        for (;;) {
            if (cancelled(should_abort_)) {
                fail_observer("profiling interval capture cancelled");
                return false;
            }
            {
                std::unique_lock<std::mutex> lock(mutex_);
                if (stop_requested_) {
                    lock.unlock();
                    fail_observer("profiling observer stopped while releasing");
                    return false;
                }
                condition_.wait_for(
                    lock, schedule_.observation_poll_interval,
                    [this] { return stop_requested_; });
                if (stop_requested_) continue;
            }
            const auto boundary = recorder_.checkpoint(
                [this] { return observer_should_abort(); });
            if (!boundary.ok() || !boundary.segment) {
                fail_observer(boundary.diagnostic.empty()
                                  ? "profiling release could not stabilize"
                                  : boundary.diagnostic);
                return false;
            }
            release_segments_.push_back(*boundary.segment);
            const auto observed = monotonic_now();
            if (!observed) {
                fail_observer(diagnostic_);
                return false;
            }
            if (segment_moved(*boundary.segment)) {
                stable_started = observed;
                continue;
            }
            const auto stable = elapsed_at_least(
                *stable_started, *observed,
                contract_.interval.release_stability_window);
            if (!stable) {
                fail_observer("profiling release clock regressed");
                return false;
            }
            if (*stable) return true;
        }
    }

    void fail_observer(std::string diagnostic) noexcept {
        observer_failed_.store(true, std::memory_order_release);
        if (recorder_.active()) {
            try {
                recorder_.finish();
            } catch (...) {
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (diagnostic_.empty()) diagnostic_ = std::move(diagnostic);
            state_.failed = true;
        }
        condition_.notify_all();
    }

    bool observer_should_abort() noexcept {
        if (cancelled(should_abort_)) return true;
        std::lock_guard<std::mutex> lock(mutex_);
        return stop_requested_;
    }

    ProfilingTransactionCapture recorder_failure(
        const ProfilingIntervalRecorderResult &result) {
        return capture_failure(
            result.diagnostic.empty()
                ? "profiling interval recorder rejected the capture"
                : result.diagnostic);
    }

    ProfilingTransactionCapture finish_failed_capture(
        std::string_view diagnostic = {}) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_requested_ = true;
        }
        condition_.notify_all();
        join_observer();
        return capture_failure(
            diagnostic.empty() ? std::string_view{diagnostic_} : diagnostic);
    }

    void join_observer() {
        if (observer_.joinable()) observer_.join();
    }

    ProfilingTransactionCapture seal_capture() {
        std::vector<const ProfilingIntervalSegment *> chain;
        chain.reserve(baseline_segments_.size() +
                      workload_segments_.size() +
                      release_segments_.size());
        for (const auto &segment : baseline_segments_) {
            chain.push_back(&segment);
        }
        for (const auto &segment : workload_segments_) {
            chain.push_back(&segment);
        }
        for (const auto &segment : release_segments_) {
            chain.push_back(&segment);
        }
        if (baseline_segments_.empty() || workload_segments_.size() != 1 ||
            release_segments_.empty() ||
            !segment_chain_is_valid(chain, contract_, context_)) {
            return capture_failure(
                "profiling interval segments do not form one closed capture");
        }

        const auto &baseline = baseline_segments_.back();
        const auto &workload = workload_segments_.front();
        const auto &release = release_segments_.back();
        if (baseline.checkpoint.capture_generation >=
                workload.checkpoint.capture_generation ||
            workload.checkpoint.capture_generation >=
                release.checkpoint.capture_generation ||
            !claim_closures_equal(
                baseline.checkpoint.observation.observed_claims,
                baseline.checkpoint.observation.attributed_claims) ||
            !claim_closures_equal(workload.peak_observed_claims,
                                  workload.peak_attributed_claims) ||
            !claim_closures_equal(
                release.checkpoint.observation.observed_claims,
                release.checkpoint.observation.attributed_claims) ||
            !release_is_bounded(
                baseline.checkpoint.observation.observed_claims,
                release.checkpoint.observation.observed_claims,
                release.uncertainty_claims)) {
            return capture_failure(
                "profiling interval lifecycle evidence is not closed");
        }

        const auto baseline_provenance = phase_provenance_sha256(
            ProfilingPhase::Baseline, context_, baseline_segments_);
        const auto workload_provenance = phase_provenance_sha256(
            ProfilingPhase::Workload, context_, workload_segments_);
        const auto release_provenance = phase_provenance_sha256(
            ProfilingPhase::Release, context_, release_segments_);
        if (!baseline_provenance || !workload_provenance ||
            !release_provenance) {
            return capture_failure(
                "profiling phase provenance could not be sealed");
        }

        auto sealed_baseline = seal_profiling_phase_attestation(phase_draft(
            ProfilingPhase::Baseline,
            ProfilingLifecycleState::BaselineQuiescent, context_, baseline,
            *baseline_provenance, false));
        auto sealed_workload = seal_profiling_phase_attestation(phase_draft(
            ProfilingPhase::Workload,
            ProfilingLifecycleState::WorkloadComplete, context_, workload,
            *workload_provenance, true));
        auto sealed_release = seal_profiling_phase_attestation(phase_draft(
            ProfilingPhase::Release,
            ProfilingLifecycleState::ReleaseVerified, context_, release,
            *release_provenance, false));
        if (!sealed_baseline.accepted() || !sealed_workload.accepted() ||
            !sealed_release.accepted()) {
            const auto &diagnostic =
                !sealed_baseline.accepted()
                    ? sealed_baseline.diagnostic
                    : !sealed_workload.accepted()
                          ? sealed_workload.diagnostic
                          : sealed_release.diagnostic;
            return capture_failure(
                diagnostic.empty()
                    ? "profiling phase attestations could not be sealed"
                    : diagnostic);
        }

        return ProfilingTransactionCapture{
            std::string(sealed_baseline.candidate->canonical_bytes()),
            std::string(sealed_workload.candidate->canonical_bytes()),
            std::string(sealed_release.candidate->canonical_bytes()),
            {},
        };
    }

    const ProfilingDerivationContract &contract_;
    const ProfilingCaptureSchedule &schedule_;
    Router &router_;
    const ProfilingTransactionContext &context_;
    ProfilingWorkloadDriver &workload_;
    const ProfilingCancellationCheck &should_abort_;
    ProfilingIntervalRecorder recorder_;
    std::vector<ProfilingIntervalSegment> baseline_segments_;
    std::vector<ProfilingIntervalSegment> workload_segments_;
    std::vector<ProfilingIntervalSegment> release_segments_;
    std::thread observer_;
    std::mutex mutex_;
    std::condition_variable condition_;
    SharedState state_;
    std::atomic<bool> observer_failed_{false};
    bool stop_requested_ = false;
    std::string diagnostic_;
};

} // namespace

ProfilingCaptureAuthority::ProfilingCaptureAuthority(
    ProfilingDerivationContract contract,
    ProfilingCaptureSchedule schedule)
    : contract_(std::move(contract)), source_(&unavailable_source_),
      schedule_(std::move(schedule)) {
    initialize_clock(schedule_.clock);
}

ProfilingCaptureAuthority::ProfilingCaptureAuthority(
    ProfilingDerivationContract contract,
    ProfilingIntervalObservationSource &source,
    ProfilingCaptureSchedule schedule)
    : contract_(std::move(contract)), source_(&source),
      schedule_(std::move(schedule)) {
    initialize_clock(schedule_.clock);
}

ProfilingTransactionCapture ProfilingCaptureAuthority::capture(
    Router &router,
    const ProfilingTransactionContext &context,
    ProfilingWorkloadDriver &workload,
    const ProfilingCancellationCheck &should_abort) {
    try {
        if (!schedule_is_valid(contract_, schedule_)) {
            return capture_failure("profiling capture schedule is invalid");
        }
        if (!contract_covers_selector(contract_, context)) {
            return capture_failure(
                "profiling derivation contract does not cover selector");
        }
        CaptureOperation operation(contract_, *source_, schedule_, router,
                                   context, workload, should_abort);
        return operation.run();
    } catch (...) {
        return capture_failure("profiling capture authority failed");
    }
}

} // namespace lemon::residency
