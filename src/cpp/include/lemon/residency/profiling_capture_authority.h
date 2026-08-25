#pragma once

#include "lemon/residency/profiling_provider.h"

#include <chrono>

namespace lemon {

class Router;

namespace residency {

struct ProfilingCaptureSchedule {
    std::chrono::milliseconds observation_poll_interval{0};
    ProfilingCollectionClock clock;
};

class ProfilingWorkloadDriver {
public:
    virtual ~ProfilingWorkloadDriver() = default;

    // Both calls are synchronous, run on the Router lease owner, retain no
    // references, and leave no unjoined work. Release is self-contained, does
    // not wait for observation progress, and safely completes a partial run.
    virtual void run(
        Router &router,
        const ProfilingTransactionContext &context,
        const ProfilingCancellationCheck &should_abort) = 0;
    virtual void release(
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
