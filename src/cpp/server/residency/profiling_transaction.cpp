#include "lemon/residency/profiling_transaction.h"

#include "lemon/router.h"

#include <chrono>
#include <exception>
#include <memory>
#include <thread>
#include <utility>

namespace lemon::residency {
namespace {

using Clock = std::chrono::steady_clock;

std::string bounded_diagnostic(std::string value) {
    if (value.size() > max_local_overlay_diagnostic_bytes) {
        std::size_t boundary = max_local_overlay_diagnostic_bytes;
        while (boundary > 0 &&
               (static_cast<unsigned char>(value[boundary]) & 0xc0u) == 0x80u)
            --boundary;
        value.resize(boundary);
    }
    return value;
}

ProfilingTransactionResult result_for(ProfilingTransactionStatus status,
                                      std::string diagnostic = {}) {
    return ProfilingTransactionResult{
        status, bounded_diagnostic(std::move(diagnostic)), std::nullopt};
}

class RouterExclusiveLease {
public:
    explicit RouterExclusiveLease(Router &router) : router_(&router) {}

    RouterExclusiveLease(const RouterExclusiveLease &) = delete;
    RouterExclusiveLease &operator=(const RouterExclusiveLease &) = delete;

    ~RouterExclusiveLease() {
        if (router_ != nullptr)
            router_->end_exclusive();
    }

private:
    Router *router_;
};

} // namespace

struct ProfilingTransaction::RunState {
    // Zero means no abort. A single atomic keeps restart precedence ordered
    // with caller cancellation when lifecycle teardown races a capture poll.
    std::atomic<int> abort_reason{0};

    void latch(ProfilingAbortReason reason) noexcept {
        const auto status = reason == ProfilingAbortReason::Restarted
                                ? ProfilingTransactionStatus::Restarted
                            : reason == ProfilingAbortReason::Cancelled
                                ? ProfilingTransactionStatus::Cancelled
                                : ProfilingTransactionStatus::Failed;
        // A lifecycle restart always wins over an ordinary caller cancel.
        if (status == ProfilingTransactionStatus::Restarted) {
            abort_reason.store(static_cast<int>(status),
                               std::memory_order_seq_cst);
        } else {
            int expected = 0;
            abort_reason.compare_exchange_strong(
                expected, static_cast<int>(status), std::memory_order_seq_cst);
        }
    }

    std::optional<ProfilingTransactionStatus> reason() const noexcept {
        const int raw = abort_reason.load(std::memory_order_seq_cst);
        if (raw == 0)
            return std::nullopt;
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
    RunningGuard(ProfilingTransaction &transaction,
                 std::shared_ptr<RunState> state)
        : transaction_(transaction), state_(std::move(state)) {}

    RunningGuard(const RunningGuard &) = delete;
    RunningGuard &operator=(const RunningGuard &) = delete;

    ~RunningGuard() {
        std::lock_guard<std::mutex> lock(transaction_.mutex_);
        if (transaction_.active_run_ == state_)
            transaction_.active_run_.reset();
        transaction_.owner_thread_ = std::thread::id{};
        transaction_.running_ = false;
        transaction_.idle_cv_.notify_all();
    }

private:
    ProfilingTransaction &transaction_;
    std::shared_ptr<RunState> state_;
};

ProfilingTransaction::ProfilingTransaction(Router &router,
                                           ProfilingTransactionOptions options)
    : router_(router), options_(std::move(options)) {
    if (options_.gate_timeout <= std::chrono::milliseconds::zero())
        options_.gate_timeout = std::chrono::milliseconds(1);
    if (options_.retry_interval <= std::chrono::milliseconds::zero())
        options_.retry_interval = std::chrono::milliseconds(1);
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
    if (state)
        state->latch(reason);
    idle_cv_.notify_all();
}

bool ProfilingTransaction::wait_for_idle() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!running_)
        return true;
    if (owner_thread_ == std::this_thread::get_id())
        return false;
    idle_cv_.wait(lock, [this] { return !running_; });
    return true;
}

ProfilingTransactionResult
ProfilingTransaction::run(std::string transaction_id, Capture capture,
                          std::atomic<bool> *cancel,
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
            return result_for(*latched,
                              "profiling transaction interrupted by restart");
        if (latched) {
            try {
                if (restart_detected && restart_detected())
                    state->latch(ProfilingAbortReason::Restarted);
            } catch (...) {
            }
            const auto reason = state->reason().value_or(*latched);
            const char *message =
                reason == ProfilingTransactionStatus::Restarted
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
            const char *message =
                *reason == ProfilingTransactionStatus::Restarted
                    ? "profiling transaction interrupted by restart"
                : *reason == ProfilingTransactionStatus::Cancelled
                    ? "profiling transaction cancelled"
                    : "profiling lifecycle check failed";
            return result_for(*reason, message);
        }
        return std::nullopt;
    };

    if (auto aborted = check_abort())
        return std::move(*aborted);
    if (transaction_id.empty())
        return result_for(ProfilingTransactionStatus::InvalidEvidence,
                          "profiling transaction ID is missing");

    const auto deadline = Clock::now() + options_.gate_timeout;
    auto wait_for_retry = [&]() {
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline -
                                                                  Clock::now());
        if (remaining <= std::chrono::milliseconds::zero())
            return;
        const auto delay = options_.retry_interval < remaining
                               ? options_.retry_interval
                               : remaining;
        std::this_thread::sleep_for(delay);
    };
    auto timeout_or_abort = [&]() {
        if (auto aborted = check_abort())
            return std::move(*aborted);
        return result_for(ProfilingTransactionStatus::GateTimeout,
                          "profiling Router gate timed out");
    };
    bool acquired = false;
    try {
        while (!acquired) {
            if (auto aborted = check_abort())
                return std::move(*aborted);
            if (Clock::now() >= deadline)
                return timeout_or_abort();

            auto request = router_.request_exclusive(cancel);
            while (request.pending()) {
                if (auto aborted = check_abort())
                    return std::move(*aborted);
                const auto acquire_result =
                    router_.try_begin_exclusive(request, cancel);
                if (acquire_result ==
                    Router::ExclusiveAcquireResult::Acquired) {
                    acquired = true;
                    break;
                }
                if (acquire_result ==
                    Router::ExclusiveAcquireResult::Cancelled) {
                    state->latch(ProfilingAbortReason::Cancelled);
                    if (auto aborted = check_abort())
                        return std::move(*aborted);
                    return result_for(ProfilingTransactionStatus::Cancelled,
                                      "profiling transaction cancelled");
                }
                if (acquire_result != Router::ExclusiveAcquireResult::Retry)
                    return result_for(
                        ProfilingTransactionStatus::Failed,
                        "profiling Router gate acquisition failed");
                if (Clock::now() >= deadline)
                    return timeout_or_abort();
                wait_for_retry();
            }
            if (acquired)
                break;

            if (request.result() == Router::ExclusiveAcquireResult::Cancelled) {
                state->latch(ProfilingAbortReason::Cancelled);
                if (auto aborted = check_abort())
                    return std::move(*aborted);
                return result_for(ProfilingTransactionStatus::Cancelled,
                                  "profiling transaction cancelled");
            }
            if (request.result() != Router::ExclusiveAcquireResult::Retry)
                return result_for(ProfilingTransactionStatus::Failed,
                                  "profiling Router gate request failed");
            wait_for_retry();
        }
    } catch (...) {
        if (auto aborted = check_abort())
            return std::move(*aborted);
        return result_for(ProfilingTransactionStatus::Failed,
                          "profiling Router gate acquisition failed");
    }

    RouterExclusiveLease lease(router_);
    if (auto aborted = check_abort())
        return std::move(*aborted);

    ProfilingTransactionCapture capture_result;
    try {
        const ProfilingCancellationCheck should_abort = [&]() noexcept {
            const auto latched = state->reason();
            if (latched && *latched == ProfilingTransactionStatus::Restarted)
                return true;
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
        capture_result = capture(router_, should_abort);
    } catch (...) {
        if (auto aborted = check_abort())
            return std::move(*aborted);
        return result_for(ProfilingTransactionStatus::Failed,
                          "profiling capture failed");
    }

    if (auto aborted = check_abort())
        return std::move(*aborted);
    if (!capture_result.draft.has_value() ||
        !capture_result.baseline_captured ||
        !capture_result.workload_captured || !capture_result.release_captured ||
        !capture_result.identity_complete ||
        !capture_result.observations_fresh ||
        !capture_result.observations_healthy ||
        !capture_result.uncertainty_bound_verified ||
        !capture_result.safety_margin_verified) {
        return result_for(ProfilingTransactionStatus::EvidenceUnavailable,
                          capture_result.diagnostic.empty()
                              ? "profiling evidence is incomplete"
                              : capture_result.diagnostic);
    }
    if (!capture_result.draft->attribution_complete ||
        !capture_result.draft->external_demand_absent ||
        !capture_result.draft->lifecycle_release_verified)
        return result_for(ProfilingTransactionStatus::EvidenceUnavailable,
                          "profiling evidence is not safe to publish");
    if (capture_result.draft->profiling_transaction_id != transaction_id)
        return result_for(ProfilingTransactionStatus::InvalidEvidence,
                          "profiling transaction ID does not match capture");

    ParsedProfilingInputEnvelopeResult sealed;
    try {
        sealed = seal_profiling_input(std::move(*capture_result.draft));
    } catch (...) {
        if (auto aborted = check_abort())
            return std::move(*aborted);
        return result_for(ProfilingTransactionStatus::Failed,
                          "profiling evidence sealing failed");
    }
    if (!sealed.accepted())
        return result_for(ProfilingTransactionStatus::InvalidEvidence,
                          sealed.diagnostic.empty()
                              ? "profiling evidence failed contract validation"
                              : sealed.diagnostic);
    if (auto aborted = check_abort())
        return std::move(*aborted);

    ProfilingTransactionResult result;
    result.status = ProfilingTransactionStatus::Accepted;
    result.diagnostic = "profiling evidence accepted";
    result.candidate = std::move(sealed.candidate);
    return result;
}

} // namespace lemon::residency
