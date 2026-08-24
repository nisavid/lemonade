#pragma once

#include "lemon/residency/local_overlay.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace lemon {

class Router;

namespace residency {

enum class ProfilingTransactionStatus {
    Accepted,
    AlreadyRunning,
    Cancelled,
    Restarted,
    GateTimeout,
    EvidenceUnavailable,
    InvalidEvidence,
    Failed,
};

enum class ProfilingAbortReason {
    Cancelled,
    Restarted,
    Failed,
};

using ProfilingCancellationCheck = std::function<bool()>;

struct ProfilingTransactionCapture {
    std::optional<ProfilingInputEnvelopeDraft> draft;
    // These attestations come from the server-owned observation provider. The
    // transaction never infers identity, freshness, health, uncertainty, or
    // safety from aggregate benchmark output.
    bool baseline_captured = false;
    bool workload_captured = false;
    bool release_captured = false;
    bool identity_complete = false;
    bool observations_fresh = false;
    bool observations_healthy = false;
    bool uncertainty_bound_verified = false;
    bool safety_margin_verified = false;
    std::string diagnostic;
};

struct ProfilingTransactionResult {
    ProfilingTransactionStatus status = ProfilingTransactionStatus::Failed;
    std::string diagnostic;
    std::optional<ParsedProfilingInputEnvelope> candidate;

    bool accepted() const noexcept {
        return status == ProfilingTransactionStatus::Accepted &&
               candidate.has_value();
    }
};

struct ProfilingTransactionOptions {
    std::chrono::milliseconds gate_timeout{std::chrono::seconds(30)};
    std::chrono::milliseconds retry_interval{25};
};

class ProfilingTransaction {
public:
    // Server owns one instance per Router. That instance is the admission point
    // for profiling; callers must not create parallel coordinators. Capture is
    // synchronous: it must not retain Router, the cancellation check, or the
    // cancel pointer, and it must join any work it starts before returning.
    // Capture code must poll the cancellation check at bounded points, must
    // not call Router::end_exclusive(), and must not invoke Server lifecycle
    // or configuration mutations (directly or from spawned work) while the
    // exclusive lease is held. It must also avoid nested HTTP/jobs calls and
    // Router work from non-owner threads, which would queue behind this lease.
    using Capture = std::function<ProfilingTransactionCapture(
        Router &, const ProfilingCancellationCheck &)>;

    explicit ProfilingTransaction(Router &router,
                                  ProfilingTransactionOptions options = {});
    // Destruction must occur outside the synchronous capture callback. An
    // owner-thread destruction cannot safely wait for the active Router lease.
    ~ProfilingTransaction();

    ProfilingTransaction(const ProfilingTransaction &) = delete;
    ProfilingTransaction &operator=(const ProfilingTransaction &) = delete;

    // restart_detected is a cheap, non-blocking lifecycle predicate owned by
    // the Server. Capture providers must not use it as a substitute for their
    // own bounded cancellation checks.
    ProfilingTransactionResult run(std::string transaction_id, Capture capture,
                                   std::atomic<bool> *cancel = nullptr,
                                   std::function<bool()> restart_detected = {});

    // Latch a lifecycle abort for the active run. The latch is scoped to that
    // run and remains set even when an external flag is later cleared.
    void request_abort(
        ProfilingAbortReason reason = ProfilingAbortReason::Restarted) noexcept;

    bool running() const noexcept;
    bool running_on_current_thread() const noexcept;
    // Returns false when called by the active capture thread; waiting there
    // would deadlock and the caller must defer Router teardown.
    bool wait_for_idle();

private:
    struct RunState;
    class RunningGuard;

    Router &router_;
    ProfilingTransactionOptions options_;
    mutable std::mutex mutex_;
    std::condition_variable idle_cv_;
    bool running_ = false;
    std::shared_ptr<RunState> active_run_;
    std::thread::id owner_thread_;
};

} // namespace residency
} // namespace lemon
