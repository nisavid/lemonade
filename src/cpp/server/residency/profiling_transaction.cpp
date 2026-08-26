#include "lemon/residency/profiling_transaction.h"

#include "lemon/router.h"
#include "profiling_common.h"

#include <array>
#include <chrono>
#include <ctime>
#include <exception>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <string_view>
#include <thread>
#include <utility>

namespace lemon::residency {
namespace {

using Clock = std::chrono::steady_clock;
using profiling_internal::bounded_diagnostic;
using profiling_internal::sha256_hex;

std::string system_utc_now() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

std::int64_t days_from_civil(int year, unsigned month, unsigned day) noexcept {
    year -= month <= 2;
    const auto era = (year >= 0 ? year : year - 399) / 400;
    const auto year_of_era = static_cast<unsigned>(year - era * 400);
    const auto adjusted_month = month > 2 ? month - 3 : month + 9;
    const auto day_of_year = (153U * adjusted_month + 2U) / 5U + day - 1U;
    const auto day_of_era =
        year_of_era * 365U + year_of_era / 4U - year_of_era / 100U + day_of_year;
    return static_cast<std::int64_t>(era) * 146097 + static_cast<std::int64_t>(day_of_era) - 719468;
}

std::optional<std::int64_t> utc_seconds(std::string_view value) noexcept {
    if (value.size() != 20 || value[4] != '-' || value[7] != '-' || value[10] != 'T' ||
        value[13] != ':' || value[16] != ':' || value[19] != 'Z') {
        return std::nullopt;
    }
    auto number = [&](std::size_t offset, std::size_t count) {
        int result = 0;
        for (std::size_t index = 0; index < count; ++index) {
            const auto character = value[offset + index];
            if (character < '0' || character > '9') {
                return -1;
            }
            result = result * 10 + character - '0';
        }
        return result;
    };
    const auto year = number(0, 4);
    const auto month = number(5, 2);
    const auto day = number(8, 2);
    const auto hour = number(11, 2);
    const auto minute = number(14, 2);
    const auto second = number(17, 2);
    if (year <= 0 || month <= 0 || month > 12 || day <= 0 || hour < 0 || hour > 23 || minute < 0 ||
        minute > 59 || second < 0 || second > 59) {
        return std::nullopt;
    }
    static constexpr std::array<unsigned, 12> days_per_month{
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    auto maximum_day = days_per_month[static_cast<std::size_t>(month - 1)];
    if (month == 2 && (year % 400 == 0 || (year % 4 == 0 && year % 100 != 0))) {
        maximum_day = 29;
    }
    if (static_cast<unsigned>(day) > maximum_day) {
        return std::nullopt;
    }
    return days_from_civil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 +
           hour * 3600 + minute * 60 + second;
}

ProfilingTransactionResult result_for(ProfilingTransactionStatus status,
                                      std::string diagnostic = {}) {
    return ProfilingTransactionResult{status, bounded_diagnostic(std::move(diagnostic)),
                                      std::nullopt};
}

class RouterExclusiveLease {
public:
    explicit RouterExclusiveLease(Router &router) : router_(&router) {}

    RouterExclusiveLease(const RouterExclusiveLease &) = delete;
    RouterExclusiveLease &operator=(const RouterExclusiveLease &) = delete;

    ~RouterExclusiveLease() {
        if (router_ != nullptr) router_->end_exclusive();
    }

private:
    Router *router_;
};

bool generations_equal(const OverlaySourceGenerations &left,
                       const OverlaySourceGenerations &right) noexcept {
    return left.model == right.model && left.backend == right.backend &&
           left.device == right.device && left.topology == right.topology &&
           left.driver == right.driver && left.configuration == right.configuration &&
           left.workload == right.workload;
}

bool claim_sets_equal(const std::vector<ClaimFamilyClosure> &left,
                      const std::vector<ClaimFamilyClosure> &right) {
    auto checked_left = check_claim_closure(left);
    auto checked_right = check_claim_closure(right);
    return checked_left.accepted() && checked_right.accepted() &&
           *checked_left.claims == *checked_right.claims;
}

bool claim_set_covers_ordinary_constraints(
    const std::vector<ClaimFamilyClosure> &claims,
    const std::vector<ConstraintKind> &constraints) {
    const auto checked = check_claim_closure(claims);
    return checked.accepted() &&
           claim_families_cover_ordinary_constraints(*checked.claims,
                                                      constraints);
}

bool phase_covers_required_claim_families(
    const ParsedProfilingPhaseAttestation &phase,
    const std::vector<ConstraintKind> &constraints) {
    return claim_set_covers_ordinary_constraints(phase.observed_claims(), constraints) &&
           claim_set_covers_ordinary_constraints(phase.attributed_claims(), constraints) &&
           claim_set_covers_ordinary_constraints(phase.external_change_claims(), constraints) &&
           claim_set_covers_ordinary_constraints(phase.unattributed_claims(), constraints) &&
           claim_set_covers_ordinary_constraints(phase.uncertainty_claims(), constraints) &&
           claim_set_covers_ordinary_constraints(phase.safety_margin_claims(), constraints);
}

bool release_is_bounded(const ParsedProfilingPhaseAttestation &baseline,
                        const ParsedProfilingPhaseAttestation &release) {
    auto baseline_claims = check_claim_closure(baseline.observed_claims());
    auto uncertainty_claims = check_claim_closure(release.uncertainty_claims());
    auto release_claims = check_claim_closure(release.observed_claims());
    if (!baseline_claims.accepted() || !uncertainty_claims.accepted() ||
        !release_claims.accepted()) {
        return false;
    }
    auto upper_bound = checked_add(*baseline_claims.claims, *uncertainty_claims.claims);
    if (!upper_bound.accepted()) {
        return false;
    }
    return checked_subtract(*upper_bound.claims, *release_claims.claims).accepted();
}

std::string earliest_fresh_until(const ParsedProfilingPhaseAttestation &baseline,
                                 const ParsedProfilingPhaseAttestation &workload,
                                 const ParsedProfilingPhaseAttestation &release) {
    auto earliest = std::string(baseline.fresh_until());
    if (workload.fresh_until() < earliest) {
        earliest = workload.fresh_until();
    }
    if (release.fresh_until() < earliest) {
        earliest = release.fresh_until();
    }
    return earliest;
}

} // namespace

AcceptedProfilingEvidence::AcceptedProfilingEvidence(ParsedProfilingInputEnvelope input,
                                                     ParsedProfilingPhaseAttestation baseline,
                                                     ParsedProfilingPhaseAttestation workload,
                                                     ParsedProfilingPhaseAttestation release)
    : input_(std::move(input)), baseline_(std::move(baseline)), workload_(std::move(workload)),
      release_(std::move(release)) {}

const ParsedProfilingInputEnvelope &AcceptedProfilingEvidence::input() const noexcept {
    return input_;
}

const ParsedProfilingPhaseAttestation &AcceptedProfilingEvidence::baseline() const noexcept {
    return baseline_;
}

const ParsedProfilingPhaseAttestation &AcceptedProfilingEvidence::workload() const noexcept {
    return workload_;
}

const ParsedProfilingPhaseAttestation &AcceptedProfilingEvidence::release() const noexcept {
    return release_;
}

struct ProfilingTransaction::RunState {
    // Zero means no abort. A single atomic keeps restart precedence ordered
    // with caller cancellation when lifecycle teardown races a capture poll.
    std::atomic<int> abort_reason{0};

    void latch(ProfilingAbortReason reason) noexcept {
        const auto status =
            reason == ProfilingAbortReason::Restarted   ? ProfilingTransactionStatus::Restarted
            : reason == ProfilingAbortReason::Cancelled ? ProfilingTransactionStatus::Cancelled
                                                        : ProfilingTransactionStatus::Failed;
        // A lifecycle restart always wins over an ordinary caller cancel.
        if (status == ProfilingTransactionStatus::Restarted) {
            abort_reason.store(static_cast<int>(status), std::memory_order_seq_cst);
        } else {
            int expected = 0;
            abort_reason.compare_exchange_strong(expected, static_cast<int>(status),
                                                 std::memory_order_seq_cst);
        }
    }

    std::optional<ProfilingTransactionStatus> reason() const noexcept {
        const int raw = abort_reason.load(std::memory_order_seq_cst);
        if (raw == 0) return std::nullopt;
        const auto value = static_cast<ProfilingTransactionStatus>(raw);
        if (value == ProfilingTransactionStatus::Restarted ||
            value == ProfilingTransactionStatus::Cancelled ||
            value == ProfilingTransactionStatus::Failed) {
            return value;
        }
        return ProfilingTransactionStatus::Failed;
    }
};

class ProfilingTransaction::RunningGuard {
public:
    RunningGuard(ProfilingTransaction &transaction, std::shared_ptr<RunState> state)
        : transaction_(transaction), state_(std::move(state)) {}

    RunningGuard(const RunningGuard &) = delete;
    RunningGuard &operator=(const RunningGuard &) = delete;

    ~RunningGuard() {
        std::lock_guard<std::mutex> lock(transaction_.mutex_);
        if (transaction_.active_run_ == state_) transaction_.active_run_.reset();
        transaction_.owner_thread_ = std::thread::id{};
        transaction_.running_ = false;
        transaction_.idle_cv_.notify_all();
    }

private:
    ProfilingTransaction &transaction_;
    std::shared_ptr<RunState> state_;
};

ProfilingTransaction::ProfilingTransaction(Router &router, ProfilingTransactionOptions options)
    : router_(router), options_(std::move(options)) {
    if (options_.gate_timeout <= std::chrono::milliseconds::zero())
        options_.gate_timeout = std::chrono::milliseconds(1);
    if (options_.retry_interval <= std::chrono::milliseconds::zero())
        options_.retry_interval = std::chrono::milliseconds(1);
    if (!options_.utc_now) options_.utc_now = system_utc_now;
}

ProfilingTransaction::~ProfilingTransaction() {
    if (running_on_current_thread()) {
        // Destroying the coordinator from its capture stack would invalidate
        // the Router lease and the RunningGuard before run() can unwind.
        std::terminate();
    }
    request_abort(ProfilingAbortReason::Restarted);
    wait_for_idle();
}

bool ProfilingTransaction::running() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

bool ProfilingTransaction::running_on_current_thread() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_ && owner_thread_ == std::this_thread::get_id();
}

void ProfilingTransaction::request_abort(ProfilingAbortReason reason) noexcept {
    std::shared_ptr<RunState> state;
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        state = active_run_;
    } catch (...) {
        return;
    }
    if (state) state->latch(reason);
    idle_cv_.notify_all();
}

bool ProfilingTransaction::wait_for_idle() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!running_) return true;
    if (owner_thread_ == std::this_thread::get_id()) return false;
    idle_cv_.wait(lock, [this] { return !running_; });
    return true;
}

ProfilingTransactionResult ProfilingTransaction::run(ProfilingTransactionContext context,
                                                     Capture capture, std::atomic<bool> *cancel,
                                                     std::function<bool()> restart_detected) {
    auto state = std::make_shared<RunState>();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_)
            return result_for(ProfilingTransactionStatus::AlreadyRunning,
                              "profiling transaction already running");
        running_ = true;
        active_run_ = state;
        owner_thread_ = std::this_thread::get_id();
    }
    RunningGuard running_guard(*this, state);

    if (!capture)
        return result_for(ProfilingTransactionStatus::Failed,
                          "profiling capture provider is unavailable");

    auto check_abort = [&]() -> std::optional<ProfilingTransactionResult> {
        const auto latched = state->reason();
        if (latched && *latched == ProfilingTransactionStatus::Restarted)
            return result_for(*latched, "profiling transaction interrupted by restart");
        if (latched) {
            try {
                if (restart_detected && restart_detected())
                    state->latch(ProfilingAbortReason::Restarted);
            } catch (...) {
            }
            const auto reason = state->reason().value_or(*latched);
            const char *message = reason == ProfilingTransactionStatus::Restarted
                                      ? "profiling transaction interrupted by restart"
                                  : reason == ProfilingTransactionStatus::Cancelled
                                      ? "profiling transaction cancelled"
                                      : "profiling lifecycle check failed";
            return result_for(reason, message);
        }
        try {
            if (restart_detected && restart_detected())
                state->latch(ProfilingAbortReason::Restarted);
            else if (cancel != nullptr && cancel->load())
                state->latch(ProfilingAbortReason::Cancelled);
        } catch (...) {
            state->latch(ProfilingAbortReason::Failed);
        }
        if (const auto reason = state->reason()) {
            const char *message = *reason == ProfilingTransactionStatus::Restarted
                                      ? "profiling transaction interrupted by restart"
                                  : *reason == ProfilingTransactionStatus::Cancelled
                                      ? "profiling transaction cancelled"
                                      : "profiling lifecycle check failed";
            return result_for(*reason, message);
        }
        return std::nullopt;
    };

    if (auto aborted = check_abort()) return std::move(*aborted);
    if (context.profiling_transaction_id.empty())
        return result_for(ProfilingTransactionStatus::InvalidEvidence,
                          "profiling transaction ID is missing");

    const auto deadline = Clock::now() + options_.gate_timeout;
    auto wait_for_retry = [&]() {
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (remaining <= std::chrono::milliseconds::zero()) return;
        const auto delay =
            options_.retry_interval < remaining ? options_.retry_interval : remaining;
        std::this_thread::sleep_for(delay);
    };
    auto timeout_or_abort = [&]() {
        if (auto aborted = check_abort()) return std::move(*aborted);
        return result_for(ProfilingTransactionStatus::GateTimeout,
                          "profiling Router gate timed out");
    };
    bool acquired = false;
    try {
        while (!acquired) {
            if (auto aborted = check_abort()) return std::move(*aborted);
            if (Clock::now() >= deadline) return timeout_or_abort();

            auto request = router_.request_exclusive_until(deadline, cancel);
            while (request.pending()) {
                if (auto aborted = check_abort()) return std::move(*aborted);
                const auto acquire_result = router_.try_begin_exclusive(request, cancel);
                if (acquire_result == Router::ExclusiveAcquireResult::Acquired) {
                    acquired = true;
                    break;
                }
                if (acquire_result == Router::ExclusiveAcquireResult::Cancelled) {
                    state->latch(ProfilingAbortReason::Cancelled);
                    if (auto aborted = check_abort()) return std::move(*aborted);
                    return result_for(ProfilingTransactionStatus::Cancelled,
                                      "profiling transaction cancelled");
                }
                if (acquire_result == Router::ExclusiveAcquireResult::DeadlineExceeded) {
                    return timeout_or_abort();
                }
                if (acquire_result != Router::ExclusiveAcquireResult::Retry)
                    return result_for(ProfilingTransactionStatus::Failed,
                                      "profiling Router gate acquisition failed");
                if (Clock::now() >= deadline) return timeout_or_abort();
                wait_for_retry();
            }
            if (acquired) break;

            if (request.result() == Router::ExclusiveAcquireResult::Cancelled) {
                state->latch(ProfilingAbortReason::Cancelled);
                if (auto aborted = check_abort()) return std::move(*aborted);
                return result_for(ProfilingTransactionStatus::Cancelled,
                                  "profiling transaction cancelled");
            }
            if (request.result() == Router::ExclusiveAcquireResult::DeadlineExceeded) {
                return timeout_or_abort();
            }
            if (request.result() != Router::ExclusiveAcquireResult::Retry)
                return result_for(ProfilingTransactionStatus::Failed,
                                  "profiling Router gate request failed");
            wait_for_retry();
        }
    } catch (...) {
        if (auto aborted = check_abort()) return std::move(*aborted);
        return result_for(ProfilingTransactionStatus::Failed,
                          "profiling Router gate acquisition failed");
    }

    RouterExclusiveLease lease(router_);
    if (auto aborted = check_abort()) return std::move(*aborted);

    ProfilingTransactionCapture capture_result;
    try {
        const ProfilingCancellationCheck should_abort = [&]() noexcept {
            const auto latched = state->reason();
            if (latched && *latched == ProfilingTransactionStatus::Restarted) return true;
            if (latched) {
                try {
                    if (restart_detected && restart_detected())
                        state->latch(ProfilingAbortReason::Restarted);
                } catch (...) {
                }
                return true;
            }
            try {
                if (restart_detected && restart_detected())
                    state->latch(ProfilingAbortReason::Restarted);
                else if (cancel != nullptr && cancel->load())
                    state->latch(ProfilingAbortReason::Cancelled);
            } catch (...) {
                state->latch(ProfilingAbortReason::Failed);
            }
            return state->reason().has_value();
        };
        capture_result = capture(router_, context, should_abort);
    } catch (...) {
        if (auto aborted = check_abort()) return std::move(*aborted);
        return result_for(ProfilingTransactionStatus::Failed, "profiling capture failed");
    }

    if (auto aborted = check_abort()) return std::move(*aborted);
    try {
        if (capture_result.baseline_attestation.empty() ||
            capture_result.workload_attestation.empty() ||
            capture_result.release_attestation.empty()) {
            return result_for(
                ProfilingTransactionStatus::EvidenceUnavailable,
                capture_result.diagnostic.empty() ? "profiling evidence is incomplete"
                                                  : capture_result.diagnostic);
        }
        auto baseline = parse_profiling_phase_attestation(capture_result.baseline_attestation);
        auto workload = parse_profiling_phase_attestation(capture_result.workload_attestation);
        auto release = parse_profiling_phase_attestation(capture_result.release_attestation);
        if (!baseline.accepted() || !workload.accepted() || !release.accepted()) {
            const auto &diagnostic = !baseline.accepted()   ? baseline.diagnostic
                                     : !workload.accepted() ? workload.diagnostic
                                                            : release.diagnostic;
            return result_for(ProfilingTransactionStatus::InvalidEvidence,
                              diagnostic.empty() ? "profiling phase attestation is invalid"
                                                 : diagnostic);
        }

        const auto &baseline_phase = *baseline.candidate;
        const auto &workload_phase = *workload.candidate;
        const auto &release_phase = *release.candidate;
        const auto common_reference_matches =
            baseline_phase.deployment_id() == context.deployment_id &&
            workload_phase.deployment_id() == context.deployment_id &&
            release_phase.deployment_id() == context.deployment_id &&
            baseline_phase.profiling_transaction_id() == context.profiling_transaction_id &&
            workload_phase.profiling_transaction_id() == context.profiling_transaction_id &&
            release_phase.profiling_transaction_id() == context.profiling_transaction_id &&
            baseline_phase.selector_sha256() == context.selector_sha256 &&
            workload_phase.selector_sha256() == context.selector_sha256 &&
            release_phase.selector_sha256() == context.selector_sha256 &&
            baseline_phase.provider_id() == workload_phase.provider_id() &&
            workload_phase.provider_id() == release_phase.provider_id() &&
            baseline_phase.provider_revision_sha256() ==
                workload_phase.provider_revision_sha256() &&
            workload_phase.provider_revision_sha256() == release_phase.provider_revision_sha256() &&
            baseline_phase.observation_contract_sha256() == context.observation_contract_sha256 &&
            workload_phase.observation_contract_sha256() == context.observation_contract_sha256 &&
            release_phase.observation_contract_sha256() == context.observation_contract_sha256 &&
            baseline_phase.predictor_contract_sha256() == context.predictor_contract_sha256 &&
            workload_phase.predictor_contract_sha256() == context.predictor_contract_sha256 &&
            release_phase.predictor_contract_sha256() == context.predictor_contract_sha256 &&
            generations_equal(baseline_phase.generations(), context.generations) &&
            generations_equal(workload_phase.generations(), context.generations) &&
            generations_equal(release_phase.generations(), context.generations);
        const auto phase_order_is_valid =
            baseline_phase.phase() == ProfilingPhase::Baseline &&
            workload_phase.phase() == ProfilingPhase::Workload &&
            release_phase.phase() == ProfilingPhase::Release &&
            baseline_phase.observation_generation() < workload_phase.observation_generation() &&
            workload_phase.observation_generation() < release_phase.observation_generation() &&
            baseline_phase.observed_at() <= workload_phase.observed_at() &&
            workload_phase.observed_at() <= release_phase.observed_at();
        const auto attribution_is_closed =
            claim_sets_equal(baseline_phase.observed_claims(),
                             baseline_phase.attributed_claims()) &&
            claim_sets_equal(workload_phase.observed_claims(),
                             workload_phase.attributed_claims()) &&
            claim_sets_equal(release_phase.observed_claims(), release_phase.attributed_claims()) &&
            release_is_bounded(baseline_phase, release_phase);
        const auto &required_constraints =
            context.selector.catalog_selector.constraints;
        const auto required_claims_are_closed =
            phase_covers_required_claim_families(baseline_phase, required_constraints) &&
            phase_covers_required_claim_families(workload_phase, required_constraints) &&
            phase_covers_required_claim_families(release_phase, required_constraints);
        if (!common_reference_matches || !phase_order_is_valid ||
            !attribution_is_closed || !required_claims_are_closed) {
            return result_for(ProfilingTransactionStatus::InvalidEvidence,
                              "profiling phase attestations do not form a closed transaction");
        }
        if (auto aborted = check_abort()) return std::move(*aborted);

        std::string accepted_at;
        try {
            accepted_at = options_.utc_now();
        } catch (...) {
            if (auto aborted = check_abort()) return std::move(*aborted);
            return result_for(ProfilingTransactionStatus::Failed,
                              "profiling acceptance clock failed");
        }
        const auto accepted_at_seconds = utc_seconds(accepted_at);
        const auto release_at_seconds = utc_seconds(release_phase.observed_at());
        const auto common_skew = baseline_phase.max_source_skew_milliseconds();
        if (!accepted_at_seconds.has_value() || !release_at_seconds.has_value()) {
            return result_for(ProfilingTransactionStatus::Failed,
                              "profiling acceptance clock is invalid");
        }
        const auto skew_seconds_unsigned = common_skew / 1000 + (common_skew % 1000 == 0 ? 0 : 1);
        if (skew_seconds_unsigned >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return result_for(ProfilingTransactionStatus::InvalidEvidence,
                              "profiling source skew bound is invalid");
        }
        const auto skew_seconds = static_cast<std::int64_t>(skew_seconds_unsigned);
        if (*accepted_at_seconds > std::numeric_limits<std::int64_t>::max() - skew_seconds) {
            return result_for(ProfilingTransactionStatus::InvalidEvidence,
                              "profiling source skew bound is invalid");
        }
        const auto fresh_until =
            earliest_fresh_until(baseline_phase, workload_phase, release_phase);
        const auto fresh_until_seconds = utc_seconds(fresh_until);
        if (!fresh_until_seconds.has_value()) {
            return result_for(ProfilingTransactionStatus::InvalidEvidence,
                              "profiling freshness timestamp is invalid");
        }
        if (workload_phase.max_source_skew_milliseconds() != common_skew ||
            release_phase.max_source_skew_milliseconds() != common_skew) {
            return result_for(ProfilingTransactionStatus::InvalidEvidence,
                              "profiling source skew contracts do not match");
        }
        if (*accepted_at_seconds >= *fresh_until_seconds ||
            *release_at_seconds > *accepted_at_seconds + skew_seconds) {
            return result_for(ProfilingTransactionStatus::EvidenceUnavailable,
                              "profiling evidence is stale or future-dated");
        }

        const auto baseline_sha256 = sha256_hex(capture_result.baseline_attestation);
        const auto workload_sha256 = sha256_hex(capture_result.workload_attestation);
        const auto release_sha256 = sha256_hex(capture_result.release_attestation);
        if (!baseline_sha256.has_value() || !workload_sha256.has_value() ||
            !release_sha256.has_value()) {
            return result_for(ProfilingTransactionStatus::Failed,
                              "profiling evidence digest is unavailable");
        }

        ProfilingInputEnvelopeDraft input;
        input.schema = supported_profiling_input_schema;
        input.deployment_id = std::move(context.deployment_id);
        input.sequence = context.sequence;
        input.profiling_transaction_id = std::move(context.profiling_transaction_id);
        input.selector = std::move(context.selector);
        input.generations = context.generations;
        MutationCompleteIntervalEvidenceDraft method_evidence;
        method_evidence.baseline_observation_sha256 = *baseline_sha256;
        method_evidence.workload_observation_sha256 = *workload_sha256;
        method_evidence.release_observation_sha256 = *release_sha256;
        input.method_evidence = std::move(method_evidence);
        input.completion.manifest_claims = workload_phase.attributed_claims();
        input.completion.ownership_recovery_evidence_sha256 =
            std::move(context.ownership_recovery_evidence_sha256);
        input.completion.action_lease_closure_sha256 =
            std::move(context.action_lease_closure_sha256);
        input.observation_contract_sha256 = std::move(context.observation_contract_sha256);
        input.predictor_contract_sha256 = std::move(context.predictor_contract_sha256);
        input.observed_at = release_phase.observed_at();
        input.fresh_until = std::move(fresh_until);
        input.max_clock_skew_milliseconds = common_skew;

        ParsedProfilingInputEnvelopeResult sealed;
        try {
            sealed = seal_profiling_input(std::move(input));
        } catch (...) {
            if (auto aborted = check_abort()) return std::move(*aborted);
            return result_for(ProfilingTransactionStatus::Failed,
                              "profiling evidence sealing failed");
        }
        if (!sealed.accepted())
            return result_for(ProfilingTransactionStatus::InvalidEvidence,
                              sealed.diagnostic.empty()
                                  ? "profiling evidence failed contract validation"
                                  : sealed.diagnostic);
        if (auto aborted = check_abort()) return std::move(*aborted);
        if (sealed.candidate->selector_sha256() != context.selector_sha256) {
            return result_for(ProfilingTransactionStatus::InvalidEvidence,
                              "profiling selector attestation does not match input");
        }
        if (auto aborted = check_abort()) return std::move(*aborted);

        ProfilingTransactionResult result;
        result.status = ProfilingTransactionStatus::Accepted;
        result.diagnostic = "profiling evidence accepted";
        result.evidence = AcceptedProfilingEvidence(
            std::move(*sealed.candidate), std::move(*baseline.candidate),
            std::move(*workload.candidate), std::move(*release.candidate));
        return result;
    } catch (...) {
        if (auto aborted = check_abort()) return std::move(*aborted);
        return result_for(ProfilingTransactionStatus::Failed,
                          "profiling evidence validation failed");
    }
}

} // namespace lemon::residency
