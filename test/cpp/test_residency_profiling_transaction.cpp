#include "lemon/config_file.h"
#include "lemon/residency/profiling_transaction.h"
#include "lemon/router.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using namespace lemon;
using namespace lemon::residency;

std::string digest(char value) { return std::string(64, value); }

std::vector<ClaimFamilyClosure> claims() {
    return {
        ClaimFamilyClosure{
            ClaimFamily::ConsumableCapacity, ClaimCompleteness::KnownZero, {}},
        ClaimFamilyClosure{
            ClaimFamily::SafetyFloor, ClaimCompleteness::KnownZero, {}},
        ClaimFamilyClosure{
            ClaimFamily::CardinalityPool, ClaimCompleteness::KnownZero, {}},
        ClaimFamilyClosure{ClaimFamily::CompatibilityExclusivity,
                           ClaimCompleteness::NotApplicable,
                           {}},
    };
}

LocalOverlaySelectorIdentity selector() {
    RuntimeCatalogSelector catalog;
    catalog.source_support_baseline = std::string(40, 'a');
    catalog.base_variant = "llamacpp-rocm";
    catalog.platform = "linux-amd-rocm-llamacpp";
    catalog.backend_channel = "stable";
    catalog.model_type = "llm";
    catalog.operation_template = OperationTemplate::Adm;
    catalog.operation_kind = OperationKind::Admission;
    catalog.constraints = {
        ConstraintKind::Ownership,
        ConstraintKind::GpuSharedResidency,
        ConstraintKind::ModelTypePool,
        ConstraintKind::HostMemAvailableFloor,
    };
    catalog.recovery = "native_subprocess_tree";
    catalog.material_profiles = {
        {"configuration_profile",
         "profile-free-residency-estimation-v1-text-only"},
        {"hardware_profile", "hatchery-gfx1151-shared-gtt-v1"},
        {"workload_profile", "hatchery-text-generation-campaign-v1"},
    };

    return LocalOverlaySelectorIdentity{
        digest('1'), std::move(catalog), "model/alpha", digest('2'),
        digest('3'), digest('4'),        digest('5'),   digest('6'),
        digest('7'), digest('8'),        digest('9'),   digest('a')};
}

ProfilingInputEnvelopeDraft draft(std::string transaction_id = "profile/1") {
    ProfilingInputEnvelopeDraft value;
    value.schema = supported_local_overlay_schema;
    value.deployment_id = digest('b');
    value.sequence = 1;
    value.profiling_transaction_id = std::move(transaction_id);
    value.selector = selector();
    value.generations = OverlaySourceGenerations{1, 2, 3, 4, 5, 6, 7};
    value.attributed_claims = claims();
    value.baseline_observation_sha256 = digest('c');
    value.workload_observation_sha256 = digest('d');
    value.release_observation_sha256 = digest('e');
    value.observation_contract_sha256 = digest('f');
    value.predictor_contract_sha256 = digest('0');
    value.observed_at = "2026-08-23T10:00:00Z";
    value.fresh_until = "2026-08-23T10:05:00Z";
    value.max_clock_skew_milliseconds = 1000;
    value.attribution_complete = true;
    value.external_demand_absent = true;
    value.lifecycle_release_verified = true;
    return value;
}

ProfilingTransactionCapture capture(ProfilingInputEnvelopeDraft value) {
    ProfilingTransactionCapture result;
    result.draft = std::move(value);
    result.baseline_captured = true;
    result.workload_captured = true;
    result.release_captured = true;
    result.identity_complete = true;
    result.observations_fresh = true;
    result.observations_healthy = true;
    result.uncertainty_bound_verified = true;
    result.safety_margin_verified = true;
    return result;
}

ProfilingTransactionOptions options() {
    ProfilingTransactionOptions value;
    value.gate_timeout = 500ms;
    value.retry_interval = 1ms;
    return value;
}

RuntimeConfig make_config() {
    nlohmann::json values = ConfigFile::get_defaults();
    values["max_loaded_models"] = 2;
    values["log_level"] = "error";
    values["offline"] = true;
    values["no_fetch_executables"] = true;
    return RuntimeConfig(values);
}

bool acquire_gate(Router &router) {
    for (int attempt = 0; attempt < 500; ++attempt) {
        auto request = router.request_exclusive();
        if (request.pending()) {
            const auto result = router.try_begin_exclusive(request);
            if (result == Router::ExclusiveAcquireResult::Acquired)
                return true;
            if (result != Router::ExclusiveAcquireResult::Retry)
                return false;
        } else if (request.result() != Router::ExclusiveAcquireResult::Retry) {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
    return false;
}

struct TestState {
    std::atomic<bool> ok{true};

    void require(bool condition, const char *message) {
        if (!condition) {
            std::cerr << "[FAIL] " << message << '\n';
            ok.store(false);
        }
    }

    void require(bool condition, const std::string &message) {
        require(condition, message.c_str());
    }
};

void test_accepts_and_releases_gate(TestState &state, Router &router) {
    ProfilingTransaction transaction(router, options());

    std::promise<void> capture_entered;
    auto capture_entered_signal = capture_entered.get_future();
    std::promise<void> release_capture;
    auto release_capture_signal = release_capture.get_future().share();
    std::promise<void> queued_started;
    std::future<Router::PreparedModelLoad> queued;

    auto run = std::async(std::launch::async, [&] {
        return transaction.run(
            "profile/1",
            [&](Router &gated_router, const ProfilingCancellationCheck &) {
                capture_entered.set_value();
                auto nested_request = gated_router.request_exclusive();
                state.require(
                    nested_request.result() ==
                        Router::ExclusiveAcquireResult::InvalidOrder,
                    "profiling capture cannot nest a Router exclusive gate");
                release_capture_signal.wait();
                return capture(draft());
            });
    });

    state.require(capture_entered_signal.wait_for(1s) ==
                      std::future_status::ready,
                  "profiling capture acquires the gate before queued work");
    queued = std::async(std::launch::async, [&] {
        queued_started.set_value();
        return router.prepare_model_load("queued.model",
                                         LoadPurpose::UserInference);
    });
    state.require(queued_started.get_future().wait_for(1s) ==
                      std::future_status::ready,
                  "ordinary model work starts while profiling owns the gate");
    state.require(queued.wait_for(50ms) == std::future_status::timeout,
                  "ordinary model work queues behind profiling gate");
    release_capture.set_value();
    auto result = run.get();

    state.require(result.accepted(), "valid profiling capture is accepted");
    state.require(result.candidate.has_value(),
                  "accepted capture returns candidate");
    try {
        auto preparation = queued.get();
        router.abandon_prepared_model_load(std::move(preparation));
    } catch (const std::exception &error) {
        state.require(false, error.what());
    }
    const bool reacquired = acquire_gate(router);
    state.require(reacquired, "profiling transaction releases Router gate");
    if (reacquired)
        router.end_exclusive();
}

void test_rejects_incomplete_capture(TestState &state, Router &router) {
    ProfilingTransaction transaction(router, options());
    auto result = transaction.run(
        "profile/1", [&](Router &, const ProfilingCancellationCheck &) {
            auto value = capture(draft());
            value.release_captured = false;
            return value;
        });
    state.require(result.status ==
                      ProfilingTransactionStatus::EvidenceUnavailable,
                  "incomplete lifecycle evidence returns inactive result");
    state.require(!result.candidate.has_value(),
                  "incomplete evidence has no candidate");
    const bool reacquired = acquire_gate(router);
    state.require(reacquired, "failed capture releases Router gate");
    if (reacquired)
        router.end_exclusive();
}

void test_rejects_unsafe_evidence(TestState &state, Router &router) {
    const auto expect_rejected =
        [&](const char *label,
            const std::function<void(ProfilingTransactionCapture &)> &mutate,
            ProfilingTransactionStatus status) {
            ProfilingTransaction transaction(router, options());
            auto result = transaction.run(
                "profile/1", [&](Router &, const ProfilingCancellationCheck &) {
                    auto value = capture(draft());
                    mutate(value);
                    return value;
                });
            state.require(result.status == status, label);
            state.require(!result.candidate.has_value(),
                          "unsafe profiling evidence has no candidate");
            state.require(
                result.diagnostic.size() <= max_local_overlay_diagnostic_bytes,
                "profiling diagnostics stay within the local bound");
            const bool reacquired = acquire_gate(router);
            state.require(reacquired, "unsafe evidence releases Router gate");
            if (reacquired)
                router.end_exclusive();
        };

    expect_rejected(
        "incomplete attribution returns inactive result",
        [](auto &value) { value.draft->attribution_complete = false; },
        ProfilingTransactionStatus::EvidenceUnavailable);
    expect_rejected(
        "external demand returns inactive result",
        [](auto &value) { value.draft->external_demand_absent = false; },
        ProfilingTransactionStatus::EvidenceUnavailable);
    expect_rejected(
        "unverified release returns inactive result",
        [](auto &value) { value.draft->lifecycle_release_verified = false; },
        ProfilingTransactionStatus::EvidenceUnavailable);
    expect_rejected(
        "stale observations return inactive result",
        [](auto &value) { value.observations_fresh = false; },
        ProfilingTransactionStatus::EvidenceUnavailable);
    expect_rejected(
        "incomplete identity returns inactive result",
        [](auto &value) { value.identity_complete = false; },
        ProfilingTransactionStatus::EvidenceUnavailable);
    expect_rejected(
        "unhealthy observations return inactive result",
        [](auto &value) { value.observations_healthy = false; },
        ProfilingTransactionStatus::EvidenceUnavailable);
    expect_rejected(
        "unbounded uncertainty returns inactive result",
        [](auto &value) { value.uncertainty_bound_verified = false; },
        ProfilingTransactionStatus::EvidenceUnavailable);
    expect_rejected(
        "unverified safety margin returns inactive result",
        [](auto &value) { value.safety_margin_verified = false; },
        ProfilingTransactionStatus::EvidenceUnavailable);
    expect_rejected(
        "unknown claim family returns invalid evidence",
        [](auto &value) {
            value.draft->attributed_claims.front().family =
                static_cast<ClaimFamily>(99);
        },
        ProfilingTransactionStatus::InvalidEvidence);
    expect_rejected(
        "capture transaction ID must match request",
        [](auto &value) {
            value.draft->profiling_transaction_id = "profile/other";
        },
        ProfilingTransactionStatus::InvalidEvidence);
}

void test_rejects_concurrent_run(TestState &state, Router &router) {
    ProfilingTransaction transaction(router, options());
    std::promise<void> entered;
    std::promise<void> release;
    auto release_signal = release.get_future().share();
    auto first = std::async(std::launch::async, [&] {
        return transaction.run(
            "profile/1", [&](Router &, const ProfilingCancellationCheck &) {
                state.require(
                    transaction.running(),
                    "transaction marks itself running during capture");
                state.require(!transaction.wait_for_idle(),
                              "owner thread does not wait on itself");
                entered.set_value();
                release_signal.wait();
                return capture(draft());
            });
    });
    auto entered_signal = entered.get_future();
    state.require(entered_signal.wait_for(1s) == std::future_status::ready,
                  "first profiling transaction enters capture");
    auto second = transaction.run(
        "profile/2", [&](Router &, const ProfilingCancellationCheck &) {
            return capture(draft("profile/2"));
        });
    state.require(
        second.status == ProfilingTransactionStatus::AlreadyRunning,
        "a second profiling transaction is rejected deterministically");
    release.set_value();
    state.require(first.get().accepted(),
                  "the first profiling transaction completes");
    state.require(!transaction.running(), "transaction clears running state");
}

void test_latched_abort_pulses(TestState &state, Router &router) {
    ProfilingTransaction transaction(router, options());

    {
        std::promise<void> entered;
        std::promise<void> observed;
        std::promise<void> release;
        auto release_signal = release.get_future().share();
        std::atomic<bool> cancel{false};
        std::atomic<bool> stop_capture{false};
        auto entered_signal = entered.get_future();
        auto observed_signal = observed.get_future();
        auto run = std::async(std::launch::async, [&] {
            return transaction.run(
                "profile/1",
                [&](Router &, const ProfilingCancellationCheck &should_abort) {
                    entered.set_value();
                    while (!should_abort() && !stop_capture.load())
                        std::this_thread::sleep_for(1ms);
                    observed.set_value();
                    release_signal.wait();
                    return capture(draft());
                },
                &cancel);
        });
        const bool entered_ready =
            entered_signal.wait_for(1s) == std::future_status::ready;
        state.require(entered_ready, "cancel pulse capture starts");
        cancel.store(true);
        const bool observed_ready =
            observed_signal.wait_for(1s) == std::future_status::ready;
        state.require(observed_ready, "capture observes cancellation pulse");
        if (!observed_ready)
            stop_capture.store(true);
        if (observed_ready)
            cancel.store(false);
        release.set_value();
        auto result = run.get();
        state.require(result.status == ProfilingTransactionStatus::Cancelled,
                      "latched cancellation survives source reset");
        state.require(!result.candidate.has_value(),
                      "latched cancellation has no candidate");
    }

    {
        std::promise<void> entered;
        std::promise<void> observed;
        std::promise<void> release;
        auto release_signal = release.get_future().share();
        std::atomic<bool> restart{false};
        std::atomic<bool> stop_capture{false};
        auto entered_signal = entered.get_future();
        auto observed_signal = observed.get_future();
        auto run = std::async(std::launch::async, [&] {
            return transaction.run(
                "profile/1",
                [&](Router &, const ProfilingCancellationCheck &should_abort) {
                    entered.set_value();
                    while (!should_abort() && !stop_capture.load())
                        std::this_thread::sleep_for(1ms);
                    observed.set_value();
                    release_signal.wait();
                    return capture(draft());
                },
                nullptr, [&] { return restart.load(); });
        });
        const bool entered_ready =
            entered_signal.wait_for(1s) == std::future_status::ready;
        state.require(entered_ready, "restart pulse capture starts");
        restart.store(true);
        const bool observed_ready =
            observed_signal.wait_for(1s) == std::future_status::ready;
        state.require(observed_ready, "capture observes restart pulse");
        if (!observed_ready)
            stop_capture.store(true);
        if (observed_ready)
            restart.store(false);
        release.set_value();
        auto result = run.get();
        state.require(result.status == ProfilingTransactionStatus::Restarted,
                      "latched restart survives source reset");
        state.require(!result.candidate.has_value(),
                      "latched restart has no candidate");
    }

    std::promise<void> entered;
    std::promise<void> release;
    auto release_signal = release.get_future().share();
    auto entered_signal = entered.get_future();
    auto run = std::async(std::launch::async, [&] {
        return transaction.run(
            "profile/1", [&](Router &, const ProfilingCancellationCheck &) {
                entered.set_value();
                release_signal.wait();
                return capture(draft());
            });
    });
    const bool entered_ready =
        entered_signal.wait_for(1s) == std::future_status::ready;
    state.require(entered_ready, "direct lifecycle abort capture starts");
    transaction.request_abort(ProfilingAbortReason::Restarted);
    release.set_value();
    auto result = run.get();
    state.require(result.status == ProfilingTransactionStatus::Restarted,
                  "server lifecycle abort is sticky");
    state.require(!result.candidate.has_value(),
                  "server lifecycle abort has no candidate");

    std::promise<void> precedence_entered;
    std::promise<void> precedence_poll;
    std::promise<void> precedence_observed;
    std::promise<void> precedence_release;
    auto precedence_poll_signal = precedence_poll.get_future().share();
    auto precedence_observed_signal = precedence_observed.get_future();
    auto precedence_signal = precedence_release.get_future().share();
    std::atomic<bool> precedence_cancel{false};
    std::atomic<bool> precedence_cancel_observed{false};
    auto precedence_run = std::async(std::launch::async, [&] {
        return transaction.run(
            "profile/1",
            [&](Router &, const ProfilingCancellationCheck &should_abort) {
                precedence_entered.set_value();
                precedence_poll_signal.wait();
                if (should_abort())
                    precedence_cancel_observed.store(true);
                precedence_observed.set_value();
                precedence_signal.wait();
                return capture(draft());
            },
            &precedence_cancel);
    });
    auto precedence_entered_signal = precedence_entered.get_future();
    state.require(precedence_entered_signal.wait_for(1s) ==
                      std::future_status::ready,
                  "abort precedence capture starts");
    precedence_cancel.store(true);
    precedence_poll.set_value();
    const bool cancellation_observed =
        precedence_observed_signal.wait_for(1s) == std::future_status::ready;
    state.require(cancellation_observed,
                  "precedence capture observes cancellation before restart");
    state.require(precedence_cancel_observed.load(),
                  "precedence capture latches cancellation before restart");
    transaction.request_abort(ProfilingAbortReason::Restarted);
    precedence_release.set_value();
    auto precedence_result = precedence_run.get();
    state.require(precedence_result.status ==
                      ProfilingTransactionStatus::Restarted,
                  "restart supersedes a latched cancellation");
    state.require(!precedence_result.candidate.has_value(),
                  "restart precedence has no candidate");

    const bool reacquired = acquire_gate(router);
    state.require(reacquired, "latched abort releases Router gate");
    if (reacquired)
        router.end_exclusive();
}

void test_cancellation_and_restart(TestState &state, Router &router) {
    ProfilingTransactionOptions timeout_options = options();
    timeout_options.gate_timeout = 100ms;
    ProfilingTransaction timeout_transaction(router, timeout_options);
    std::promise<void> timeout_gate_entered;
    std::promise<void> timeout_gate_release;
    auto timeout_release = timeout_gate_release.get_future().share();
    auto timeout_holder = std::async(std::launch::async, [&] {
        const bool acquired = acquire_gate(router);
        state.require(acquired, "test owns Router gate for timeout");
        if (acquired) {
            timeout_gate_entered.set_value();
            timeout_release.wait();
            router.end_exclusive();
        } else {
            timeout_gate_entered.set_value();
        }
    });
    auto timeout_entered_signal = timeout_gate_entered.get_future();
    state.require(timeout_entered_signal.wait_for(1s) ==
                      std::future_status::ready,
                  "timeout test owns Router gate");
    auto timeout_result = timeout_transaction.run(
        "profile/1", [&](Router &, const ProfilingCancellationCheck &) {
            state.require(false, "timed-out transaction must not capture");
            return capture(draft());
        });
    state.require(timeout_result.status ==
                      ProfilingTransactionStatus::GateTimeout,
                  "gate acquisition is bounded");
    state.require(!timeout_result.candidate.has_value(),
                  "gate timeout has no candidate");
    timeout_gate_release.set_value();
    timeout_holder.get();

    ProfilingTransaction transaction(router, options());
    const bool gate_acquired = acquire_gate(router);
    state.require(gate_acquired, "test owns Router gate");
    if (!gate_acquired)
        return;

    std::atomic<bool> cancelled{false};
    auto waiting = std::async(std::launch::async, [&] {
        return transaction.run(
            "profile/1",
            [&](Router &, const ProfilingCancellationCheck &) {
                state.require(false, "cancelled transaction must not capture");
                return capture(draft());
            },
            &cancelled);
    });
    std::this_thread::sleep_for(50ms);
    cancelled.store(true);
    auto cancelled_result = waiting.get();
    state.require(cancelled_result.status ==
                      ProfilingTransactionStatus::Cancelled,
                  "cancellation aborts a transaction waiting for the gate");
    state.require(!cancelled_result.candidate.has_value(),
                  "gate cancellation has no candidate");
    router.end_exclusive();

    std::promise<void> entered;
    std::promise<void> release;
    auto release_signal = release.get_future().share();
    std::atomic<bool> capture_cancelled{false};
    auto entered_signal = entered.get_future();
    auto active_cancel = std::async(std::launch::async, [&] {
        return transaction.run(
            "profile/1",
            [&](Router &, const ProfilingCancellationCheck &should_abort) {
                entered.set_value();
                while (!should_abort() && !capture_cancelled.load())
                    std::this_thread::sleep_for(1ms);
                release_signal.wait();
                return capture(draft());
            },
            &capture_cancelled);
    });
    const bool entered_ready =
        entered_signal.wait_for(1s) == std::future_status::ready;
    state.require(entered_ready, "active cancellation capture starts");
    capture_cancelled.store(true);
    release.set_value();
    auto active_cancel_result = active_cancel.get();
    state.require(active_cancel_result.status ==
                      ProfilingTransactionStatus::Cancelled,
                  "cancellation during capture releases Router gate");
    state.require(!active_cancel_result.candidate.has_value(),
                  "capture cancellation has no candidate");

    std::promise<void> restart_entered;
    std::promise<void> restart_release;
    auto restart_signal = restart_release.get_future().share();
    std::atomic<bool> restarting{false};
    std::atomic<bool> stop_restart_capture{false};
    auto restart_entered_signal = restart_entered.get_future();
    auto active_restart = std::async(std::launch::async, [&] {
        return transaction.run(
            "profile/1",
            [&](Router &, const ProfilingCancellationCheck &should_abort) {
                restart_entered.set_value();
                while (!should_abort() && !stop_restart_capture.load())
                    std::this_thread::sleep_for(1ms);
                restart_signal.wait();
                return capture(draft());
            },
            nullptr, [&] { return restarting.load(); });
    });
    const bool restart_entered_ready =
        restart_entered_signal.wait_for(1s) == std::future_status::ready;
    state.require(restart_entered_ready, "active restart capture starts");
    restarting.store(true);
    if (!restart_entered_ready)
        stop_restart_capture.store(true);
    restart_release.set_value();
    auto active_restart_result = active_restart.get();
    state.require(active_restart_result.status ==
                      ProfilingTransactionStatus::Restarted,
                  "restart during capture releases Router gate");
    state.require(!active_restart_result.candidate.has_value(),
                  "capture restart has no candidate");

    auto restarted = transaction.run(
        "profile/1",
        [&](Router &, const ProfilingCancellationCheck &) {
            state.require(false, "restart abort must not capture");
            return capture(draft());
        },
        nullptr, [] { return true; });
    state.require(restarted.status == ProfilingTransactionStatus::Restarted,
                  "restart aborts a transaction before gate acquisition");
}

void test_exception_releases_gate(TestState &state, Router &router) {
    ProfilingTransaction transaction(router, options());
    auto result = transaction.run(
        "profile/1",
        [&](Router &,
            const ProfilingCancellationCheck &) -> ProfilingTransactionCapture {
            throw std::runtime_error("capture failed");
        });
    state.require(result.status == ProfilingTransactionStatus::Failed,
                  "capture exception returns failed result");
    state.require(!result.candidate.has_value(),
                  "capture exception has no candidate");
    const bool reacquired = acquire_gate(router);
    state.require(reacquired, "capture exception releases Router gate");
    if (reacquired)
        router.end_exclusive();
}

} // namespace

int main() {
    TestState state;
    RuntimeConfig config = make_config();
    RuntimeConfig::set_global(&config);
    {
        Router router(&config, nullptr, nullptr);
        test_accepts_and_releases_gate(state, router);
        test_rejects_incomplete_capture(state, router);
        test_rejects_unsafe_evidence(state, router);
        test_rejects_concurrent_run(state, router);
        test_latched_abort_pulses(state, router);
        test_cancellation_and_restart(state, router);
        test_exception_releases_gate(state, router);
    }
    RuntimeConfig::set_global(nullptr);
    return state.ok.load() ? 0 : 1;
}
