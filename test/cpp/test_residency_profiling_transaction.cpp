#include "lemon/config_file.h"
#include "lemon/residency/profiling_transaction.h"
#include "lemon/router.h"

#include <mbedtls/md.h>
#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace lemon {

struct ProfilingTransactionTestHook {
    static std::unique_lock<std::mutex> lock_load_state(Router &router) {
        return std::unique_lock<std::mutex>(router.load_mutex_);
    }

    static bool exclusive_pending(const Router &router) { return router.exclusive_pending(); }

    static std::uint64_t exclusive_pending_generation(const Router &router) {
        return router.exclusive_pending_generation_.load(std::memory_order_acquire);
    }

    static void cancel_pending(Router &router, std::uint64_t generation) {
        router.cancel_exclusive_request(generation);
    }
};

} // namespace lemon

namespace {

using namespace std::chrono_literals;
using namespace lemon;
using namespace lemon::residency;

std::string digest(char value) { return std::string(64, value); }

std::string raw_sha256(std::string_view bytes) {
    const auto *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == nullptr) throw std::runtime_error("SHA-256 is unavailable");
    mbedtls_md_context_t context;
    mbedtls_md_init(&context);
    std::array<unsigned char, 32> output{};
    const bool failed =
        mbedtls_md_setup(&context, info, 0) != 0 || mbedtls_md_starts(&context) != 0 ||
        mbedtls_md_update(&context, reinterpret_cast<const unsigned char *>(bytes.data()),
                          bytes.size()) != 0 ||
        mbedtls_md_finish(&context, output.data()) != 0;
    mbedtls_md_free(&context);
    if (failed) throw std::runtime_error("SHA-256 failed");
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const auto byte : output) {
        result.push_back(hex[(byte >> 4) & 0x0f]);
        result.push_back(hex[byte & 0x0f]);
    }
    return result;
}

static_assert(!std::is_default_constructible_v<AcceptedProfilingEvidence>);
static_assert(std::is_nothrow_move_constructible_v<ParsedProfilingInputEnvelope>);
static_assert(std::is_nothrow_move_constructible_v<ParsedProfilingPhaseAttestation>);
static_assert(std::is_nothrow_move_constructible_v<AcceptedProfilingEvidence>);
static_assert(std::is_same_v<decltype(std::declval<const AcceptedProfilingEvidence &>().input()),
                             const ParsedProfilingInputEnvelope &>);

std::vector<ClaimFamilyClosure> claims(std::uint64_t bytes = 0) {
    std::vector<ClaimAmount> capacity;
    if (bytes != 0) {
        capacity.push_back(ClaimAmount{"gpu/gtt", ClaimUnit::Bytes, bytes});
    }
    return {
        ClaimFamilyClosure{ClaimFamily::ConsumableCapacity,
                           bytes == 0 ? ClaimCompleteness::KnownZero : ClaimCompleteness::Bounded,
                           std::move(capacity)},
        ClaimFamilyClosure{ClaimFamily::SafetyFloor, ClaimCompleteness::KnownZero, {}},
        ClaimFamilyClosure{ClaimFamily::CardinalityPool, ClaimCompleteness::KnownZero, {}},
        ClaimFamilyClosure{
            ClaimFamily::CompatibilityExclusivity, ClaimCompleteness::NotApplicable, {}},
    };
}

bool claim_sets_equal(const std::vector<ClaimFamilyClosure> &left,
                      const std::vector<ClaimFamilyClosure> &right) {
    const auto checked_left = check_claim_closure(left);
    const auto checked_right = check_claim_closure(right);
    return checked_left.accepted() && checked_right.accepted() &&
           *checked_left.claims == *checked_right.claims;
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
        {"configuration_profile", "profile-free-residency-estimation-v1-text-only"},
        {"hardware_profile", "hatchery-gfx1151-shared-gtt-v1"},
        {"workload_profile", "hatchery-text-generation-campaign-v1"},
    };

    return LocalOverlaySelectorIdentity{
        digest('1'), std::move(catalog), "model/alpha", digest('2'), digest('3'), digest('4'),
        digest('5'), digest('6'),        digest('7'),   digest('8'), digest('9'), digest('a')};
}

ProfilingTransactionContext context(std::string transaction_id = "profile/1") {
    ProfilingTransactionContext result;
    result.deployment_id = digest('b');
    result.sequence = 1;
    result.profiling_transaction_id = std::move(transaction_id);
    result.selector = selector();
    result.generations = OverlaySourceGenerations{1, 2, 3, 4, 5, 6, 7};
    result.observation_contract_sha256 = digest('f');
    result.predictor_contract_sha256 = digest('0');

    ProfilingInputEnvelopeDraft value;
    value.schema = supported_local_overlay_schema;
    value.deployment_id = result.deployment_id;
    value.sequence = result.sequence;
    value.profiling_transaction_id = result.profiling_transaction_id;
    value.selector = result.selector;
    value.generations = result.generations;
    value.attributed_claims = claims();
    value.baseline_observation_sha256 = digest('c');
    value.workload_observation_sha256 = digest('d');
    value.release_observation_sha256 = digest('e');
    value.observation_contract_sha256 = result.observation_contract_sha256;
    value.predictor_contract_sha256 = result.predictor_contract_sha256;
    value.observed_at = "2026-08-23T10:00:00Z";
    value.fresh_until = "2026-08-23T10:05:00Z";
    value.max_clock_skew_milliseconds = 1000;
    value.attribution_complete = true;
    value.external_demand_absent = true;
    value.lifecycle_release_verified = true;
    auto sealed = seal_profiling_input(std::move(value));
    if (!sealed.accepted()) {
        throw std::runtime_error("profiling context selector could not seal");
    }
    result.selector_sha256 = sealed.candidate->selector_sha256();
    return result;
}

ProfilingPhaseAttestationDraft phase_draft(const ProfilingTransactionContext &context,
                                           ProfilingPhase phase) {
    ProfilingPhaseAttestationDraft draft;
    draft.schema = supported_local_overlay_schema;
    draft.phase = phase;
    draft.deployment_id = context.deployment_id;
    draft.profiling_transaction_id = context.profiling_transaction_id;
    draft.selector_sha256 = context.selector_sha256;
    draft.provider_id = "provider/system-residency";
    draft.provider_revision_sha256 = digest('1');
    draft.provenance_sha256 = phase == ProfilingPhase::Baseline   ? digest('2')
                              : phase == ProfilingPhase::Workload ? digest('3')
                                                                  : digest('4');
    draft.observation_contract_sha256 = context.observation_contract_sha256;
    draft.predictor_contract_sha256 = context.predictor_contract_sha256;
    draft.generations = context.generations;
    draft.observation_generation = phase == ProfilingPhase::Baseline   ? 1
                                   : phase == ProfilingPhase::Workload ? 2
                                                                       : 3;
    draft.observed_at = phase == ProfilingPhase::Baseline   ? "2026-08-23T10:00:00Z"
                        : phase == ProfilingPhase::Workload ? "2026-08-23T10:01:00Z"
                                                            : "2026-08-23T10:02:00Z";
    draft.fresh_until = "2026-08-23T10:05:00Z";
    draft.source_skew_milliseconds = 10;
    draft.max_source_skew_milliseconds = 1000;
    draft.health = ProfilingObservationHealth::Valid;
    draft.owner_coverage = ProfilingOwnerCoverage::Complete;
    draft.observed_claims = claims(4096);
    draft.attributed_claims = claims(4096);
    draft.external_change_claims = claims();
    draft.unattributed_claims = claims();
    draft.uncertainty_claims = claims(512);
    draft.safety_margin_claims = claims(256);
    draft.lifecycle_state =
        phase == ProfilingPhase::Baseline   ? ProfilingLifecycleState::BaselineQuiescent
        : phase == ProfilingPhase::Workload ? ProfilingLifecycleState::WorkloadComplete
                                            : ProfilingLifecycleState::ReleaseVerified;
    return draft;
}

std::string phase_bytes(ProfilingPhaseAttestationDraft draft) {
    auto sealed = seal_profiling_phase_attestation(std::move(draft));
    if (!sealed.accepted()) {
        throw std::runtime_error("profiling phase fixture could not seal");
    }
    return std::string(sealed.candidate->canonical_bytes());
}

std::string phase_bytes(const ProfilingTransactionContext &context, ProfilingPhase phase) {
    return phase_bytes(phase_draft(context, phase));
}

ProfilingTransactionCapture capture(const ProfilingTransactionContext &context) {
    ProfilingTransactionCapture result;
    result.baseline_attestation = phase_bytes(context, ProfilingPhase::Baseline);
    result.workload_attestation = phase_bytes(context, ProfilingPhase::Workload);
    result.release_attestation = phase_bytes(context, ProfilingPhase::Release);
    return result;
}

ProfilingTransactionOptions options() {
    ProfilingTransactionOptions value;
    value.gate_timeout = 500ms;
    value.retry_interval = 1ms;
    value.utc_now = [] { return "2026-08-23T10:03:00Z"; };
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
            if (result == Router::ExclusiveAcquireResult::Acquired) return true;
            if (result != Router::ExclusiveAcquireResult::Retry) return false;
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
    const auto transaction_context = context();
    auto captured = capture(transaction_context);
    auto workload_fixture = phase_draft(transaction_context, ProfilingPhase::Workload);
    workload_fixture.fresh_until = "2026-08-23T10:04:00Z";
    captured.workload_attestation = phase_bytes(std::move(workload_fixture));
    auto release_fixture = phase_draft(transaction_context, ProfilingPhase::Release);
    release_fixture.fresh_until = "2026-08-23T10:06:00Z";
    captured.release_attestation = phase_bytes(std::move(release_fixture));

    std::promise<void> capture_entered;
    auto capture_entered_signal = capture_entered.get_future();
    std::promise<void> release_capture;
    auto release_capture_signal = release_capture.get_future().share();
    std::promise<void> queued_started;
    std::future<Router::PreparedModelLoad> queued;

    auto run = std::async(std::launch::async, [&, transaction_context, captured] {
        return transaction.run(transaction_context, [&,
                                                     captured](Router &gated_router,
                                                               const ProfilingTransactionContext &,
                                                               const ProfilingCancellationCheck &) {
            capture_entered.set_value();
            auto nested_request = gated_router.request_exclusive();
            state.require(nested_request.result() == Router::ExclusiveAcquireResult::InvalidOrder,
                          "profiling capture cannot nest a Router exclusive gate");
            release_capture_signal.wait();
            return captured;
        });
    });

    state.require(capture_entered_signal.wait_for(1s) == std::future_status::ready,
                  "profiling capture acquires the gate before queued work");
    queued = std::async(std::launch::async, [&] {
        queued_started.set_value();
        return router.prepare_model_load("queued.model", LoadPurpose::UserInference);
    });
    state.require(queued_started.get_future().wait_for(1s) == std::future_status::ready,
                  "ordinary model work starts while profiling owns the gate");
    state.require(queued.wait_for(50ms) == std::future_status::timeout,
                  "ordinary model work queues behind profiling gate");
    release_capture.set_value();
    auto result = run.get();

    state.require(result.accepted(), "valid profiling capture is accepted");
    state.require(result.evidence.has_value(), "accepted capture returns evidence");
    if (result.evidence.has_value()) {
        state.require(result.evidence->baseline().phase() == ProfilingPhase::Baseline,
                      "accepted evidence retains baseline attestation");
        state.require(result.evidence->workload().phase() == ProfilingPhase::Workload,
                      "accepted evidence retains workload attestation");
        state.require(result.evidence->release().phase() == ProfilingPhase::Release,
                      "accepted evidence retains release attestation");
        state.require(result.evidence->input().observed_at() ==
                          result.evidence->release().observed_at(),
                      "profiling input derives its observation time");
        state.require(result.evidence->input().fresh_until() == "2026-08-23T10:04:00Z",
                      "profiling input uses the shortest phase freshness");
        state.require(result.evidence->input().max_clock_skew_milliseconds() == 1000,
                      "profiling input retains the common skew contract");
        state.require(claim_sets_equal(
                          result.evidence->input().attributed_claims(),
                          result.evidence->workload().attributed_claims()),
                      "profiling input derives attributed workload claims");
        state.require(
            result.evidence->baseline().canonical_bytes() == captured.baseline_attestation &&
                result.evidence->workload().canonical_bytes() == captured.workload_attestation &&
                result.evidence->release().canonical_bytes() == captured.release_attestation,
            "accepted evidence retains exact canonical phase bytes");
        state.require(result.evidence->input().baseline_observation_sha256() ==
                              raw_sha256(captured.baseline_attestation) &&
                          result.evidence->input().workload_observation_sha256() ==
                              raw_sha256(captured.workload_attestation) &&
                          result.evidence->input().release_observation_sha256() ==
                              raw_sha256(captured.release_attestation),
                      "profiling input binds raw phase-attestation digests");
    }
    try {
        auto preparation = queued.get();
        router.abandon_prepared_model_load(std::move(preparation));
    } catch (const std::exception &error) {
        state.require(false, error.what());
    }
    const bool reacquired = acquire_gate(router);
    state.require(reacquired, "profiling transaction releases Router gate");
    if (reacquired) router.end_exclusive();
}

void test_rejects_incomplete_capture(TestState &state, Router &router) {
    ProfilingTransaction transaction(router, options());
    auto result = transaction.run(context(), [&](Router &, const ProfilingTransactionContext &ctx,
                                                 const ProfilingCancellationCheck &) {
        auto value = capture(ctx);
        value.release_attestation.clear();
        return value;
    });
    state.require(result.status == ProfilingTransactionStatus::EvidenceUnavailable,
                  "incomplete lifecycle evidence returns inactive result");
    state.require(!result.evidence.has_value(), "incomplete capture has no evidence");
    const bool reacquired = acquire_gate(router);
    state.require(reacquired, "failed capture releases Router gate");
    if (reacquired) router.end_exclusive();
}

void test_rejects_unsafe_evidence(TestState &state, Router &router) {
    const auto expect_rejected =
        [&](const char *label,
            const std::function<void(ProfilingTransactionContext &, ProfilingTransactionCapture &)>
                &mutate,
            ProfilingTransactionStatus status) {
            ProfilingTransaction transaction(router, options());
            auto transaction_context = context();
            auto captured = capture(transaction_context);
            mutate(transaction_context, captured);
            auto result = transaction.run(
                std::move(transaction_context),
                [captured = std::move(captured)](Router &, const ProfilingTransactionContext &,
                                                 const ProfilingCancellationCheck &) mutable {
                    return std::move(captured);
                });
            state.require(result.status == status, label);
            state.require(!result.evidence.has_value(), "unsafe profiling result has no evidence");
            state.require(result.diagnostic.size() <= max_local_overlay_diagnostic_bytes,
                          "profiling diagnostics stay within the local bound");
            const bool reacquired = acquire_gate(router);
            state.require(reacquired, "unsafe evidence releases Router gate");
            if (reacquired) router.end_exclusive();
        };

    expect_rejected(
        "noncanonical workload evidence is rejected",
        [](auto &, auto &value) { value.workload_attestation.push_back(' '); },
        ProfilingTransactionStatus::InvalidEvidence);
    expect_rejected(
        "phase slots cannot be interchanged",
        [](auto &, auto &value) { value.workload_attestation = value.baseline_attestation; },
        ProfilingTransactionStatus::InvalidEvidence);
    expect_rejected(
        "selector attestation must match context",
        [](auto &ctx, auto &) { ctx.selector_sha256 = digest('9'); },
        ProfilingTransactionStatus::InvalidEvidence);
    expect_rejected(
        "selector identity must match its attested digest",
        [](auto &ctx, auto &) { ctx.selector.canonical_model_id = "model/other"; },
        ProfilingTransactionStatus::InvalidEvidence);
    expect_rejected(
        "deployment identity must remain stable",
        [](auto &ctx, auto &value) {
            auto phase = phase_draft(ctx, ProfilingPhase::Workload);
            phase.deployment_id = digest('c');
            value.workload_attestation = phase_bytes(std::move(phase));
        },
        ProfilingTransactionStatus::InvalidEvidence);
    expect_rejected(
        "source generations must remain stable",
        [](auto &ctx, auto &value) {
            auto phase = phase_draft(ctx, ProfilingPhase::Workload);
            ++phase.generations.model;
            value.workload_attestation = phase_bytes(std::move(phase));
        },
        ProfilingTransactionStatus::InvalidEvidence);
    expect_rejected(
        "observation contract must remain stable",
        [](auto &ctx, auto &value) {
            auto phase = phase_draft(ctx, ProfilingPhase::Workload);
            phase.observation_contract_sha256 = digest('a');
            value.workload_attestation = phase_bytes(std::move(phase));
        },
        ProfilingTransactionStatus::InvalidEvidence);
    expect_rejected(
        "predictor contract must remain stable",
        [](auto &ctx, auto &value) {
            auto phase = phase_draft(ctx, ProfilingPhase::Workload);
            phase.predictor_contract_sha256 = digest('a');
            value.workload_attestation = phase_bytes(std::move(phase));
        },
        ProfilingTransactionStatus::InvalidEvidence);
    expect_rejected(
        "provider identity must remain stable",
        [](auto &ctx, auto &value) {
            auto phase = phase_draft(ctx, ProfilingPhase::Workload);
            phase.provider_id = "provider/other";
            value.workload_attestation = phase_bytes(std::move(phase));
        },
        ProfilingTransactionStatus::InvalidEvidence);
    expect_rejected(
        "provider revision must remain stable",
        [](auto &ctx, auto &value) {
            auto phase = phase_draft(ctx, ProfilingPhase::Workload);
            phase.provider_revision_sha256 = digest('8');
            value.workload_attestation = phase_bytes(std::move(phase));
        },
        ProfilingTransactionStatus::InvalidEvidence);
    expect_rejected(
        "observation generations must advance",
        [](auto &ctx, auto &value) {
            auto phase = phase_draft(ctx, ProfilingPhase::Release);
            phase.observation_generation = 2;
            value.release_attestation = phase_bytes(std::move(phase));
        },
        ProfilingTransactionStatus::InvalidEvidence);
    expect_rejected(
        "observation times must advance",
        [](auto &ctx, auto &value) {
            auto phase = phase_draft(ctx, ProfilingPhase::Release);
            phase.observed_at = "2026-08-23T10:01:00Z";
            value.release_attestation = phase_bytes(std::move(phase));
        },
        ProfilingTransactionStatus::InvalidEvidence);
    expect_rejected(
        "baseline attribution must cover observed claims",
        [](auto &ctx, auto &value) {
            auto phase = phase_draft(ctx, ProfilingPhase::Baseline);
            phase.attributed_claims = claims();
            value.baseline_attestation = phase_bytes(std::move(phase));
        },
        ProfilingTransactionStatus::InvalidEvidence);
    expect_rejected(
        "release attribution must cover observed claims",
        [](auto &ctx, auto &value) {
            auto phase = phase_draft(ctx, ProfilingPhase::Release);
            phase.attributed_claims = claims();
            value.release_attestation = phase_bytes(std::move(phase));
        },
        ProfilingTransactionStatus::InvalidEvidence);
    expect_rejected(
        "release claims must return within uncertainty",
        [](auto &ctx, auto &value) {
            auto phase = phase_draft(ctx, ProfilingPhase::Release);
            phase.observed_claims = claims(8192);
            phase.attributed_claims = claims(8192);
            value.release_attestation = phase_bytes(std::move(phase));
        },
        ProfilingTransactionStatus::InvalidEvidence);
    expect_rejected(
        "source skew contracts must agree",
        [](auto &ctx, auto &value) {
            auto phase = phase_draft(ctx, ProfilingPhase::Release);
            phase.max_source_skew_milliseconds = 2000;
            value.release_attestation = phase_bytes(std::move(phase));
        },
        ProfilingTransactionStatus::InvalidEvidence);
    expect_rejected(
        "phase transaction ID must match context",
        [](auto &ctx, auto &value) {
            auto phase = phase_draft(ctx, ProfilingPhase::Workload);
            phase.profiling_transaction_id = "profile/other";
            value.workload_attestation = phase_bytes(std::move(phase));
        },
        ProfilingTransactionStatus::InvalidEvidence);

    auto contaminated_phase = phase_draft(context(), ProfilingPhase::Workload);
    contaminated_phase.external_change_claims = claims(4096);
    auto contaminated =
        seal_profiling_phase_attestation(std::move(contaminated_phase));
    state.require(contaminated.status ==
                      OverlayContractStatus::InvalidClaimClosure &&
                      !contaminated.candidate.has_value(),
                  "phase attestation accepted external-demand contamination");

    auto stale_options = options();
    stale_options.utc_now = [] { return "2026-08-23T10:06:00Z"; };
    ProfilingTransaction stale_transaction(router, stale_options);
    auto stale_context = context();
    auto stale = stale_transaction.run(
        stale_context, [](Router &, const ProfilingTransactionContext &ctx,
                          const ProfilingCancellationCheck &) { return capture(ctx); });
    state.require(stale.status == ProfilingTransactionStatus::EvidenceUnavailable,
                  "expired observations return inactive evidence");
    state.require(!stale.evidence.has_value(), "expired observations return no evidence");

    auto future_options = options();
    future_options.utc_now = [] { return "2026-08-23T10:00:00Z"; };
    ProfilingTransaction future_transaction(router, future_options);
    auto future_context = context();
    auto future = future_transaction.run(
        future_context, [](Router &, const ProfilingTransactionContext &ctx,
                           const ProfilingCancellationCheck &) { return capture(ctx); });
    state.require(future.status == ProfilingTransactionStatus::EvidenceUnavailable,
                  "future-dated observations return inactive evidence");
    state.require(!future.evidence.has_value(), "future-dated observations return no evidence");

    auto invalid_clock_options = options();
    invalid_clock_options.utc_now = [] { return "2026-02-31T10:03:00Z"; };
    ProfilingTransaction invalid_clock_transaction(router, invalid_clock_options);
    auto invalid_clock_context = context();
    auto invalid_clock = invalid_clock_transaction.run(
        invalid_clock_context, [](Router &, const ProfilingTransactionContext &ctx,
                                  const ProfilingCancellationCheck &) { return capture(ctx); });
    state.require(invalid_clock.status == ProfilingTransactionStatus::Failed,
                  "invalid acceptance clock fails closed");
    state.require(!invalid_clock.evidence.has_value(),
                  "invalid acceptance clock returns no evidence");

    auto throwing_clock_options = options();
    throwing_clock_options.utc_now = []() -> std::string {
        throw std::runtime_error("clock failed");
    };
    ProfilingTransaction throwing_clock_transaction(router, throwing_clock_options);
    auto throwing_clock_context = context();
    auto throwing_clock = throwing_clock_transaction.run(
        throwing_clock_context, [](Router &, const ProfilingTransactionContext &ctx,
                                   const ProfilingCancellationCheck &) { return capture(ctx); });
    state.require(throwing_clock.status == ProfilingTransactionStatus::Failed,
                  "throwing acceptance clock fails closed");
    state.require(!throwing_clock.evidence.has_value(),
                  "throwing acceptance clock returns no evidence");
}

void test_rejects_concurrent_run(TestState &state, Router &router) {
    ProfilingTransaction transaction(router, options());
    std::promise<void> entered;
    std::promise<void> release;
    auto release_signal = release.get_future().share();
    auto first = std::async(std::launch::async, [&] {
        return transaction.run(context(), [&](Router &, const ProfilingTransactionContext &ctx,
                                              const ProfilingCancellationCheck &) {
            state.require(transaction.running(), "transaction marks itself running during capture");
            state.require(!transaction.wait_for_idle(), "owner thread does not wait on itself");
            entered.set_value();
            release_signal.wait();
            return capture(ctx);
        });
    });
    auto entered_signal = entered.get_future();
    state.require(entered_signal.wait_for(1s) == std::future_status::ready,
                  "first profiling transaction enters capture");
    auto second = transaction.run(context("profile/2"),
                                  [&](Router &, const ProfilingTransactionContext &ctx,
                                      const ProfilingCancellationCheck &) { return capture(ctx); });
    state.require(second.status == ProfilingTransactionStatus::AlreadyRunning,
                  "a second profiling transaction is rejected deterministically");
    release.set_value();
    state.require(first.get().accepted(), "the first profiling transaction completes");
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
                context(),
                [&](Router &, const ProfilingTransactionContext &ctx,
                    const ProfilingCancellationCheck &should_abort) {
                    entered.set_value();
                    while (!should_abort() && !stop_capture.load())
                        std::this_thread::sleep_for(1ms);
                    observed.set_value();
                    release_signal.wait();
                    return capture(ctx);
                },
                &cancel);
        });
        const bool entered_ready = entered_signal.wait_for(1s) == std::future_status::ready;
        state.require(entered_ready, "cancel pulse capture starts");
        cancel.store(true);
        const bool observed_ready = observed_signal.wait_for(1s) == std::future_status::ready;
        state.require(observed_ready, "capture observes cancellation pulse");
        if (!observed_ready) stop_capture.store(true);
        if (observed_ready) cancel.store(false);
        release.set_value();
        auto result = run.get();
        state.require(result.status == ProfilingTransactionStatus::Cancelled,
                      "latched cancellation survives source reset");
        state.require(!result.evidence.has_value(), "latched cancellation has no evidence");
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
                context(),
                [&](Router &, const ProfilingTransactionContext &ctx,
                    const ProfilingCancellationCheck &should_abort) {
                    entered.set_value();
                    while (!should_abort() && !stop_capture.load())
                        std::this_thread::sleep_for(1ms);
                    observed.set_value();
                    release_signal.wait();
                    return capture(ctx);
                },
                nullptr, [&] { return restart.load(); });
        });
        const bool entered_ready = entered_signal.wait_for(1s) == std::future_status::ready;
        state.require(entered_ready, "restart pulse capture starts");
        restart.store(true);
        const bool observed_ready = observed_signal.wait_for(1s) == std::future_status::ready;
        state.require(observed_ready, "capture observes restart pulse");
        if (!observed_ready) stop_capture.store(true);
        if (observed_ready) restart.store(false);
        release.set_value();
        auto result = run.get();
        state.require(result.status == ProfilingTransactionStatus::Restarted,
                      "latched restart survives source reset");
        state.require(!result.evidence.has_value(), "latched restart has no evidence");
    }

    std::promise<void> entered;
    std::promise<void> release;
    auto release_signal = release.get_future().share();
    auto entered_signal = entered.get_future();
    auto run = std::async(std::launch::async, [&] {
        return transaction.run(context(), [&](Router &, const ProfilingTransactionContext &ctx,
                                              const ProfilingCancellationCheck &) {
            entered.set_value();
            release_signal.wait();
            return capture(ctx);
        });
    });
    const bool entered_ready = entered_signal.wait_for(1s) == std::future_status::ready;
    state.require(entered_ready, "direct lifecycle abort capture starts");
    transaction.request_abort(ProfilingAbortReason::Restarted);
    release.set_value();
    auto result = run.get();
    state.require(result.status == ProfilingTransactionStatus::Restarted,
                  "server lifecycle abort is sticky");
    state.require(!result.evidence.has_value(), "server lifecycle abort has no evidence");

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
            context(),
            [&](Router &, const ProfilingTransactionContext &ctx,
                const ProfilingCancellationCheck &should_abort) {
                precedence_entered.set_value();
                precedence_poll_signal.wait();
                if (should_abort()) precedence_cancel_observed.store(true);
                precedence_observed.set_value();
                precedence_signal.wait();
                return capture(ctx);
            },
            &precedence_cancel);
    });
    auto precedence_entered_signal = precedence_entered.get_future();
    state.require(precedence_entered_signal.wait_for(1s) == std::future_status::ready,
                  "abort precedence capture starts");
    precedence_cancel.store(true);
    precedence_poll.set_value();
    const bool cancellation_observed =
        precedence_observed_signal.wait_for(1s) == std::future_status::ready;
    state.require(cancellation_observed, "precedence capture observes cancellation before restart");
    state.require(precedence_cancel_observed.load(),
                  "precedence capture latches cancellation before restart");
    transaction.request_abort(ProfilingAbortReason::Restarted);
    precedence_release.set_value();
    auto precedence_result = precedence_run.get();
    state.require(precedence_result.status == ProfilingTransactionStatus::Restarted,
                  "restart supersedes a latched cancellation");
    state.require(!precedence_result.evidence.has_value(), "restart precedence has no evidence");

    const bool reacquired = acquire_gate(router);
    state.require(reacquired, "latched abort releases Router gate");
    if (reacquired) router.end_exclusive();
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
    state.require(timeout_entered_signal.wait_for(1s) == std::future_status::ready,
                  "timeout test owns Router gate");
    auto timeout_result =
        timeout_transaction.run(context(), [&](Router &, const ProfilingTransactionContext &ctx,
                                               const ProfilingCancellationCheck &) {
            state.require(false, "timed-out transaction must not capture");
            return capture(ctx);
        });
    state.require(timeout_result.status == ProfilingTransactionStatus::GateTimeout,
                  "gate acquisition is bounded");
    state.require(!timeout_result.evidence.has_value(), "gate timeout has no evidence");
    {
        auto still_held = router.request_exclusive();
        state.require(!still_held.pending() &&
                          still_held.result() == Router::ExclusiveAcquireResult::Retry,
                      "profiling timeout does not release another owner's gate");
    }
    timeout_gate_release.set_value();
    timeout_holder.get();

    ProfilingTransaction transaction(router, options());
    const bool gate_acquired = acquire_gate(router);
    state.require(gate_acquired, "test owns Router gate");
    if (!gate_acquired) return;

    std::atomic<bool> cancelled{false};
    auto waiting = std::async(std::launch::async, [&] {
        return transaction.run(
            context(),
            [&](Router &, const ProfilingTransactionContext &ctx,
                const ProfilingCancellationCheck &) {
                state.require(false, "cancelled transaction must not capture");
                return capture(ctx);
            },
            &cancelled);
    });
    std::this_thread::sleep_for(50ms);
    cancelled.store(true);
    auto cancelled_result = waiting.get();
    state.require(cancelled_result.status == ProfilingTransactionStatus::Cancelled,
                  "cancellation aborts a transaction waiting for the gate");
    state.require(!cancelled_result.evidence.has_value(), "gate cancellation has no evidence");
    router.end_exclusive();

    std::promise<void> entered;
    std::promise<void> release;
    auto release_signal = release.get_future().share();
    std::atomic<bool> capture_cancelled{false};
    auto entered_signal = entered.get_future();
    auto active_cancel = std::async(std::launch::async, [&] {
        return transaction.run(
            context(),
            [&](Router &, const ProfilingTransactionContext &ctx,
                const ProfilingCancellationCheck &should_abort) {
                entered.set_value();
                while (!should_abort() && !capture_cancelled.load())
                    std::this_thread::sleep_for(1ms);
                release_signal.wait();
                return capture(ctx);
            },
            &capture_cancelled);
    });
    const bool entered_ready = entered_signal.wait_for(1s) == std::future_status::ready;
    state.require(entered_ready, "active cancellation capture starts");
    capture_cancelled.store(true);
    release.set_value();
    auto active_cancel_result = active_cancel.get();
    state.require(active_cancel_result.status == ProfilingTransactionStatus::Cancelled,
                  "cancellation during capture releases Router gate");
    state.require(!active_cancel_result.evidence.has_value(),
                  "capture cancellation has no evidence");

    std::promise<void> restart_entered;
    std::promise<void> restart_release;
    auto restart_signal = restart_release.get_future().share();
    std::atomic<bool> restarting{false};
    std::atomic<bool> stop_restart_capture{false};
    auto restart_entered_signal = restart_entered.get_future();
    auto active_restart = std::async(std::launch::async, [&] {
        return transaction.run(
            context(),
            [&](Router &, const ProfilingTransactionContext &ctx,
                const ProfilingCancellationCheck &should_abort) {
                restart_entered.set_value();
                while (!should_abort() && !stop_restart_capture.load())
                    std::this_thread::sleep_for(1ms);
                restart_signal.wait();
                return capture(ctx);
            },
            nullptr, [&] { return restarting.load(); });
    });
    const bool restart_entered_ready =
        restart_entered_signal.wait_for(1s) == std::future_status::ready;
    state.require(restart_entered_ready, "active restart capture starts");
    restarting.store(true);
    if (!restart_entered_ready) stop_restart_capture.store(true);
    restart_release.set_value();
    auto active_restart_result = active_restart.get();
    state.require(active_restart_result.status == ProfilingTransactionStatus::Restarted,
                  "restart during capture releases Router gate");
    state.require(!active_restart_result.evidence.has_value(), "capture restart has no evidence");

    auto restarted = transaction.run(
        context(),
        [&](Router &, const ProfilingTransactionContext &ctx, const ProfilingCancellationCheck &) {
            state.require(false, "restart abort must not capture");
            return capture(ctx);
        },
        nullptr, [] { return true; });
    state.require(restarted.status == ProfilingTransactionStatus::Restarted,
                  "restart aborts a transaction before gate acquisition");
}

void test_gate_timeout_bounds_router_lock(TestState &state, Router &router) {
    auto timeout_options = options();
    timeout_options.gate_timeout = 100ms;
    timeout_options.retry_interval = 1ms;
    ProfilingTransaction transaction(router, timeout_options);
    auto lock = lemon::ProfilingTransactionTestHook::lock_load_state(router);
    std::promise<void> started;
    auto started_signal = started.get_future();
    auto run = std::async(std::launch::async, [&] {
        started.set_value();
        return transaction.run(context("profile/lock-timeout"),
                               [](Router &, const ProfilingTransactionContext &ctx,
                                  const ProfilingCancellationCheck &) { return capture(ctx); });
    });
    state.require(started_signal.wait_for(1s) == std::future_status::ready,
                  "Router lock timeout worker did not start");
    const bool timed_out_while_locked = run.wait_for(1s) == std::future_status::ready;
    state.require(timed_out_while_locked, "Router lock contention exceeded the gate deadline");
    if (timed_out_while_locked) {
        lock.unlock();
        const auto result = run.get();
        state.require(result.status == ProfilingTransactionStatus::GateTimeout,
                      "Router lock contention did not return GateTimeout");
        state.require(!result.evidence.has_value(), "Router lock timeout returned evidence");
    } else {
        transaction.request_abort(ProfilingAbortReason::Cancelled);
        lock.unlock();
        const auto result = run.get();
        (void)result;
    }
}

void test_gate_timeout_clears_pending_intent(TestState &state, Router &router) {
    auto existing_preparation =
        router.prepare_model_load("profile.deadline-existing", LoadPurpose::UserInference);

    auto timeout_options = options();
    timeout_options.gate_timeout = 2s;
    timeout_options.retry_interval = 1ms;
    ProfilingTransaction transaction(router, timeout_options);
    std::promise<void> started;
    auto started_signal = started.get_future();
    auto run = std::async(std::launch::async, [&] {
        started.set_value();
        return transaction.run(context("profile/pending-timeout"),
                               [](Router &, const ProfilingTransactionContext &ctx,
                                  const ProfilingCancellationCheck &) { return capture(ctx); });
    });
    state.require(started_signal.wait_for(1s) == std::future_status::ready,
                  "pending timeout worker did not start");

    const auto pending_deadline = std::chrono::steady_clock::now() + 1s;
    while (!lemon::ProfilingTransactionTestHook::exclusive_pending(router) &&
           std::chrono::steady_clock::now() < pending_deadline) {
        std::this_thread::sleep_for(1ms);
    }
    const bool pending = lemon::ProfilingTransactionTestHook::exclusive_pending(router);
    state.require(pending, "profiling request did not publish writer intent");
    if (!pending) {
        transaction.request_abort(ProfilingAbortReason::Cancelled);
        const auto result = run.get();
        (void)result;
        router.abandon_prepared_model_load(std::move(existing_preparation));
        return;
    }

    auto lock = lemon::ProfilingTransactionTestHook::lock_load_state(router);
    const bool pending_while_locked =
        lemon::ProfilingTransactionTestHook::exclusive_pending(router);
    state.require(pending_while_locked, "profiling writer intent cleared before lock-contention "
                                        "proof");
    if (!pending_while_locked) {
        transaction.request_abort(ProfilingAbortReason::Cancelled);
        lock.unlock();
        const auto result = run.get();
        (void)result;
        router.abandon_prepared_model_load(std::move(existing_preparation));
        return;
    }
    state.require(run.wait_for(0ms) == std::future_status::timeout,
                  "profiling transaction finished before its deadline");

    std::promise<void> queued_started;
    auto queued_started_signal = queued_started.get_future();
    auto queued_preparation = std::async(std::launch::async, [&] {
        queued_started.set_value();
        return router.prepare_model_load("profile.deadline-queued", LoadPurpose::UserInference);
    });
    state.require(queued_started_signal.wait_for(1s) == std::future_status::ready,
                  "queued model preparation did not start");
    const bool timed_out_while_locked = run.wait_for(3s) == std::future_status::ready;
    state.require(timed_out_while_locked,
                  "pending profiling timeout waited for Router lock cleanup");
    state.require(!lemon::ProfilingTransactionTestHook::exclusive_pending(router),
                  "timed-out profiling request retained writer intent");
    if (timed_out_while_locked) {
        lock.unlock();
        const auto result = run.get();
        state.require(result.status == ProfilingTransactionStatus::GateTimeout,
                      "pending writer intent did not return GateTimeout");
        state.require(!result.evidence.has_value(), "pending writer timeout returned evidence");
    } else {
        transaction.request_abort(ProfilingAbortReason::Cancelled);
        lock.unlock();
        const auto result = run.get();
        (void)result;
    }

    state.require(queued_preparation.wait_for(1s) == std::future_status::ready,
                  "timed-out writer intent stranded queued model work");
    if (queued_preparation.wait_for(0ms) == std::future_status::ready) {
        auto queued = queued_preparation.get();
        router.abandon_prepared_model_load(std::move(queued));
    }
    router.abandon_prepared_model_load(std::move(existing_preparation));
}

void test_cross_router_request_ownership(TestState &state, Router &router, RuntimeConfig &config) {
    Router other_router(&config, nullptr, nullptr);
    const auto deadline = std::chrono::steady_clock::now() + 250ms;
    auto first_request = router.request_exclusive_until(deadline);
    auto second_request = other_router.request_exclusive();
    const auto first_generation =
        lemon::ProfilingTransactionTestHook::exclusive_pending_generation(router);
    const auto second_generation =
        lemon::ProfilingTransactionTestHook::exclusive_pending_generation(other_router);
    state.require(first_request.pending() && second_request.pending(),
                  "both Routers publish independent writer generations");
    state.require(first_generation == second_generation,
                  "both Routers reproduce the same pending generation");
    std::this_thread::sleep_until(deadline + 20ms);

    const auto wrong_router_result = other_router.try_begin_exclusive(first_request);
    state.require(wrong_router_result == Router::ExclusiveAcquireResult::Retry,
                  "a foreign Router request is rejected without mutation");
    state.require(first_request.pending(), "foreign Router did not consume the original request");

    const auto second_result = other_router.try_begin_exclusive(second_request);
    state.require(second_result == Router::ExclusiveAcquireResult::Acquired,
                  "foreign expiry did not clear the other Router generation");
    if (second_result == Router::ExclusiveAcquireResult::Acquired) other_router.end_exclusive();

    const auto first_result = router.try_begin_exclusive(first_request);
    state.require(first_result == Router::ExclusiveAcquireResult::DeadlineExceeded,
                  "the original Router expires its own request");
    lemon::ProfilingTransactionTestHook::cancel_pending(router, first_generation);
    lemon::ProfilingTransactionTestHook::cancel_pending(other_router, second_generation);
}

void test_exception_releases_gate(TestState &state, Router &router) {
    ProfilingTransaction transaction(router, options());
    auto result =
        transaction.run(context(),
                        [&](Router &, const ProfilingTransactionContext &,
                            const ProfilingCancellationCheck &) -> ProfilingTransactionCapture {
                            throw std::runtime_error("capture failed");
                        });
    state.require(result.status == ProfilingTransactionStatus::Failed,
                  "capture exception returns failed result");
    state.require(!result.evidence.has_value(), "capture exception has no evidence");
    const bool reacquired = acquire_gate(router);
    state.require(reacquired, "capture exception releases Router gate");
    if (reacquired) router.end_exclusive();
}

} // namespace

int main() {
    TestState state;
    RuntimeConfig config = make_config();
    RuntimeConfig::set_global(&config);
    {
        Router router(&config, nullptr, nullptr);
        test_cross_router_request_ownership(state, router, config);
        test_accepts_and_releases_gate(state, router);
        test_rejects_incomplete_capture(state, router);
        test_rejects_unsafe_evidence(state, router);
        test_rejects_concurrent_run(state, router);
        test_latched_abort_pulses(state, router);
        test_cancellation_and_restart(state, router);
        test_gate_timeout_bounds_router_lock(state, router);
        test_gate_timeout_clears_pending_intent(state, router);
        test_exception_releases_gate(state, router);
    }
    RuntimeConfig::set_global(nullptr);
    return state.ok.load() ? 0 : 1;
}
