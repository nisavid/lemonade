#pragma once

#include "lemon/residency/local_overlay.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
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

struct ProfilingTransactionContext {
    std::string deployment_id;
    std::uint64_t sequence = 0;
    std::string profiling_transaction_id;
    LocalOverlaySelectorIdentity selector;
    std::string selector_sha256;
    OverlaySourceGenerations generations;
    std::string observation_contract_sha256;
    std::string predictor_contract_sha256;
};

struct ProfilingTransactionCapture {
    std::string baseline_attestation;
    std::string workload_attestation;
    std::string release_attestation;
    std::string diagnostic;
};

class AcceptedProfilingEvidence {
public:
    AcceptedProfilingEvidence(const AcceptedProfilingEvidence &) = default;
    AcceptedProfilingEvidence(AcceptedProfilingEvidence &&) noexcept = default;
    AcceptedProfilingEvidence &operator=(const AcceptedProfilingEvidence &) = default;
    AcceptedProfilingEvidence &operator=(AcceptedProfilingEvidence &&) noexcept = default;

    const ParsedProfilingInputEnvelope &input() const noexcept;
    const ParsedProfilingPhaseAttestation &baseline() const noexcept;
    const ParsedProfilingPhaseAttestation &workload() const noexcept;
    const ParsedProfilingPhaseAttestation &release() const noexcept;

private:
    AcceptedProfilingEvidence(ParsedProfilingInputEnvelope input,
                              ParsedProfilingPhaseAttestation baseline,
                              ParsedProfilingPhaseAttestation workload,
                              ParsedProfilingPhaseAttestation release);

    ParsedProfilingInputEnvelope input_;
    ParsedProfilingPhaseAttestation baseline_;
    ParsedProfilingPhaseAttestation workload_;
    ParsedProfilingPhaseAttestation release_;

    friend class ProfilingTransaction;
};

struct ProfilingTransactionResult {
    ProfilingTransactionStatus status = ProfilingTransactionStatus::Failed;
    std::string diagnostic;
    std::optional<AcceptedProfilingEvidence> evidence;

    bool accepted() const noexcept {
        return status == ProfilingTransactionStatus::Accepted && evidence.has_value();
    }
};

struct ProfilingTransactionOptions {
    // Bounds pending-to-active Router admission. Capture duration remains
    // provider-controlled and ends cooperatively through should_abort.
    std::chrono::milliseconds gate_timeout{std::chrono::seconds(30)};
    std::chrono::milliseconds retry_interval{25};
    std::function<std::string()> utc_now;
};

class ProfilingTransaction {
public:
    // Server owns one instance per Router. That instance is the admission point
    // for profiling; callers must not create parallel coordinators. Capture is
    // synchronous: it must not retain Router, the transaction context, the
    // cancellation check, or the cancel pointer, and it must join any work it
    // starts before returning.
    // Capture code must poll the cancellation check at bounded points, must
    // not call Router::end_exclusive(), and must not invoke Server lifecycle
    // or configuration mutations (directly or from spawned work) while the
    // exclusive lease is held. It must also avoid nested HTTP/jobs calls and
    // Router work from non-owner threads, which would queue behind this lease.
    using Capture = std::function<ProfilingTransactionCapture(
        Router &, const ProfilingTransactionContext &, const ProfilingCancellationCheck &)>;

    explicit ProfilingTransaction(Router &router, ProfilingTransactionOptions options = {});
    // Destruction must occur outside the synchronous capture callback. An
    // owner-thread destruction cannot safely wait for the active Router lease.
    ~ProfilingTransaction();

    ProfilingTransaction(const ProfilingTransaction &) = delete;
    ProfilingTransaction &operator=(const ProfilingTransaction &) = delete;

    // restart_detected is a cheap, non-blocking lifecycle predicate owned by
    // the Server. Capture providers must not use it as a substitute for their
    // own bounded cancellation checks.
    ProfilingTransactionResult run(ProfilingTransactionContext context, Capture capture,
                                   std::atomic<bool> *cancel = nullptr,
                                   std::function<bool()> restart_detected = {});

    // Latch a lifecycle abort for the active run. The latch is scoped to that
    // run and remains set even when an external flag is later cleared.
    void request_abort(ProfilingAbortReason reason = ProfilingAbortReason::Restarted) noexcept;

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
