#pragma once

#include "lemon/residency/profiling_provider.h"

#include <chrono>
#include <string>

namespace lemon {

class Router;

namespace residency {

struct ProfilingCaptureSchedule {
    std::chrono::milliseconds observation_poll_interval{0};
    // Reserves scheduler wakeup and recorder overhead in each start-to-start gap.
    std::chrono::milliseconds poll_cycle_overhead_allowance{0};
    ProfilingCollectionClock clock;
};

enum class ProfilingWorkloadStepStatus {
    Succeeded,
    Cancelled,
    Failed,
    Ambiguous,
};

class ProfilingWorkloadStepResult {
public:
    ProfilingWorkloadStepResult(const ProfilingWorkloadStepResult &) = default;
    ProfilingWorkloadStepResult(ProfilingWorkloadStepResult &&) noexcept = default;
    ProfilingWorkloadStepResult &
    operator=(const ProfilingWorkloadStepResult &) = default;
    ProfilingWorkloadStepResult &
    operator=(ProfilingWorkloadStepResult &&) noexcept = default;

    static ProfilingWorkloadStepResult success() noexcept;
    static ProfilingWorkloadStepResult cancelled(std::string diagnostic);
    static ProfilingWorkloadStepResult failed(std::string diagnostic);
    static ProfilingWorkloadStepResult ambiguous(std::string diagnostic);

    ProfilingWorkloadStepStatus status() const noexcept;
    const std::string &diagnostic() const noexcept;
    bool succeeded() const noexcept;

private:
    ProfilingWorkloadStepResult(ProfilingWorkloadStepStatus status,
                                std::string diagnostic) noexcept;

    ProfilingWorkloadStepStatus status_ =
        ProfilingWorkloadStepStatus::Ambiguous;
    std::string diagnostic_;
};

class ProfilingWorkloadDriver {
public:
    virtual ~ProfilingWorkloadDriver() = default;

    // Both calls are synchronous, run on the Router lease owner, retain no
    // references, and leave no unjoined work. Release is self-contained, does
    // not wait for observation progress, and safely completes a partial run.
    virtual ProfilingWorkloadStepResult run(
        Router &router,
        const ProfilingTransactionContext &context,
        const ProfilingCancellationCheck &should_abort) = 0;
    virtual ProfilingWorkloadStepResult release(
        Router &router,
        const ProfilingTransactionContext &context) noexcept = 0;
};

class ProfilingCaptureAuthority {
public:
    ProfilingCaptureAuthority(
        ProfilingDerivationContract contract,
        ProfilingCaptureSchedule schedule);
    // The injected source must outlive this authority.
    ProfilingCaptureAuthority(
        ProfilingDerivationContract contract,
        ProfilingIntervalObservationSource &source,
        ProfilingCaptureSchedule schedule);

    ProfilingCaptureAuthority(const ProfilingCaptureAuthority &) = delete;
    ProfilingCaptureAuthority &
    operator=(const ProfilingCaptureAuthority &) = delete;
    ProfilingCaptureAuthority(ProfilingCaptureAuthority &&) = delete;
    ProfilingCaptureAuthority &
    operator=(ProfilingCaptureAuthority &&) = delete;

    ProfilingTransactionCapture capture(
        Router &router,
        const ProfilingTransactionContext &context,
        ProfilingWorkloadDriver &workload,
        const ProfilingCancellationCheck &should_abort);

private:
    ProfilingDerivationContract contract_;
    UnavailableProfilingIntervalObservationSource unavailable_source_;
    ProfilingIntervalObservationSource *source_ = nullptr;
    ProfilingCaptureSchedule schedule_;
};

} // namespace residency
} // namespace lemon
