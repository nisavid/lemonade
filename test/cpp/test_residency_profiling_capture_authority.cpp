#include "lemon/config_file.h"
#include "lemon/residency/profiling_capture_authority.h"
#include "lemon/router.h"

#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace lemon;
using namespace lemon::residency;
using namespace std::chrono_literals;

static_assert(std::has_virtual_destructor_v<ProfilingWorkloadDriver>);
static_assert(std::is_same_v<
              decltype(std::declval<ProfilingWorkloadDriver &>().run(
                  std::declval<Router &>(),
                  std::declval<const ProfilingTransactionContext &>(),
                  std::declval<const ProfilingCancellationCheck &>())),
              ProfilingWorkloadStepResult>);
static_assert(std::is_same_v<
              decltype(std::declval<ProfilingWorkloadDriver &>().release(
                  std::declval<Router &>(),
                  std::declval<const ProfilingTransactionContext &>())),
              ProfilingWorkloadStepResult>);
static_assert(noexcept(std::declval<ProfilingWorkloadDriver &>().release(
    std::declval<Router &>(),
    std::declval<const ProfilingTransactionContext &>())));

struct TestState {
    std::atomic<bool> ok{true};

    void require(bool condition, const char *message) {
        if (condition) return;
        ok.store(false);
        std::cerr << "FAIL: " << message << '\n';
    }
};

std::string digest(char value) { return std::string(64, value); }

class SharedCaptureClock {
public:
    std::chrono::steady_clock::time_point monotonic_now() noexcept {
        return std::chrono::steady_clock::time_point{} +
               std::chrono::milliseconds(
                   next_milliseconds_.fetch_add(5, std::memory_order_relaxed));
    }

    ProfilingSourceAcquisitionWindow source_window() const noexcept {
        const auto next =
            next_milliseconds_.load(std::memory_order_relaxed);
        const auto offset = next == 0 ? 0 : next - 1;
        const auto point = std::chrono::steady_clock::time_point{} +
                           std::chrono::milliseconds(offset);
        return ProfilingSourceAcquisitionWindow{point, point};
    }

private:
    std::atomic<std::uint64_t> next_milliseconds_{0};
};

struct ThreadLifetime {
    std::uint64_t token = 0;
};

const std::shared_ptr<const ThreadLifetime> &current_thread_lifetime() {
    static std::atomic<std::uint64_t> next_token{1};
    thread_local const auto lifetime =
        std::make_shared<const ThreadLifetime>(
            ThreadLifetime{next_token.fetch_add(1)});
    return lifetime;
}

std::uint64_t current_thread_token() {
    return current_thread_lifetime()->token;
}

class ReleaseSettleProbe {
public:
    explicit ReleaseSettleProbe(
        std::atomic<bool> *cancelled = nullptr) noexcept
        : cancelled_(cancelled) {}

    void note_release_read() noexcept {
        completed_release_reads_.fetch_add(1, std::memory_order_relaxed);
        clocks_since_release_read_.store(0, std::memory_order_release);
    }

    void note_monotonic_read() noexcept {
        const auto prior = clocks_since_release_read_.load(
            std::memory_order_acquire);
        if (prior < 0) return;
        const auto current = clocks_since_release_read_.fetch_add(
                                 1, std::memory_order_acq_rel) +
                             1;
        if (current != 3 ||
            settle_checkpoint_started_.exchange(
                true, std::memory_order_acq_rel)) {
            return;
        }
        release_reads_at_settle_entry_.store(
            completed_release_reads_.load(std::memory_order_acquire),
            std::memory_order_release);
        if (cancelled_) {
            cancelled_->store(true, std::memory_order_release);
        }
    }

    bool consume_settle_checkpoint() noexcept {
        return settle_checkpoint_started_.load(std::memory_order_acquire) &&
               !settle_checkpoint_consumed_.exchange(
                   true, std::memory_order_acq_rel);
    }

    bool settle_checkpoint_started() const noexcept {
        return settle_checkpoint_started_.load(std::memory_order_acquire);
    }

    std::size_t completed_release_reads() const noexcept {
        return completed_release_reads_.load(std::memory_order_acquire);
    }

    std::size_t release_reads_at_settle_entry() const noexcept {
        return release_reads_at_settle_entry_.load(
            std::memory_order_acquire);
    }

private:
    std::atomic<int> clocks_since_release_read_{-1};
    std::atomic<bool> settle_checkpoint_started_{false};
    std::atomic<bool> settle_checkpoint_consumed_{false};
    std::atomic<std::size_t> completed_release_reads_{0};
    std::atomic<std::size_t> release_reads_at_settle_entry_{0};
    std::atomic<bool> *cancelled_ = nullptr;
};

std::vector<ClaimFamilyClosure> claims(std::uint64_t bytes = 0) {
    std::vector<ClaimAmount> capacity;
    if (bytes != 0) {
        capacity.push_back(ClaimAmount{"gpu/gtt", ClaimUnit::Bytes, bytes});
    }
    return {
        ClaimFamilyClosure{
            ClaimFamily::ConsumableCapacity,
            bytes == 0 ? ClaimCompleteness::KnownZero
                       : ClaimCompleteness::Bounded,
            std::move(capacity),
        },
        ClaimFamilyClosure{
            ClaimFamily::SafetyFloor,
            ClaimCompleteness::KnownZero,
            {},
        },
        ClaimFamilyClosure{
            ClaimFamily::CardinalityPool,
            ClaimCompleteness::KnownZero,
            {},
        },
        ClaimFamilyClosure{
            ClaimFamily::CompatibilityExclusivity,
            ClaimCompleteness::NotApplicable,
            {},
        },
    };
}

const ClaimFamilyClosure *capacity_family(
    const std::vector<ClaimFamilyClosure> &families) {
    for (const auto &family : families) {
        if (family.family == ClaimFamily::ConsumableCapacity) return &family;
    }
    return nullptr;
}

bool has_capacity(const std::vector<ClaimFamilyClosure> &families,
                  std::uint64_t expected) {
    const auto *family = capacity_family(families);
    return family != nullptr &&
           family->completeness == ClaimCompleteness::Bounded &&
           family->entries.size() == 1 &&
           family->entries.front().constraint_id == "gpu/gtt" &&
           family->entries.front().unit == ClaimUnit::Bytes &&
           family->entries.front().amount == expected;
}

bool has_zero_capacity(const std::vector<ClaimFamilyClosure> &families) {
    const auto *family = capacity_family(families);
    return family != nullptr &&
           family->completeness == ClaimCompleteness::KnownZero &&
           family->entries.empty();
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
        digest('3'), digest('4'),         digest('5'), digest('6'),
        digest('7'), digest('8'),         digest('9'), digest('a'),
    };
}

ProfilingDerivationContract contract() {
    ProfilingDerivationContract value;
    value.provider_id = "provider/dynamic-interval";
    value.provider_revision_sha256 = digest('e');
    value.sensors = {
        ProfilingSensorContract{
            "sensor/gtt-used",
            "gpu/gtt",
            ClaimFamily::ConsumableCapacity,
            10,
            1000,
        },
    };
    value.owner_scopes = {
        ProfilingOwnerScopeBinding{"owner/model-alpha", digest('f')},
    };
    value.freshness_window = 10min;
    value.max_source_skew = 20ms;
    value.interval.event_semantics_revision_sha256 = digest('9');
    value.interval.max_observation_gap = 500ms;
    value.interval.baseline_stability_window = 1ms;
    value.interval.release_stability_window = 1ms;
    value.interval.max_interval_frames = 128;
    return value;
}

ProfilingTransactionContext context(
    const ProfilingDerivationContract &derivation_contract = contract(),
    std::vector<ConstraintKind> constraints = {
        ConstraintKind::Ownership,
        ConstraintKind::GpuSharedResidency,
    }) {
    const auto contract_sha256 =
        profiling_derivation_contract_sha256(derivation_contract);
    if (!contract_sha256) throw std::runtime_error("invalid test contract");

    ProfilingTransactionContext result;
    result.deployment_id = digest('b');
    result.sequence = 1;
    result.profiling_transaction_id = "profile/capture-authority";
    result.selector = selector();
    result.selector.catalog_selector.constraints = std::move(constraints);
    result.generations = OverlaySourceGenerations{1, 2, 3, 4, 5, 6, 7};
    result.observation_contract_sha256 = *contract_sha256;
    result.predictor_contract_sha256 = digest('0');
    result.ownership_recovery_evidence_sha256 = digest('7');
    result.action_lease_closure_sha256 = digest('8');

    ProfilingInputEnvelopeDraft input;
    input.schema = supported_profiling_input_schema;
    input.deployment_id = result.deployment_id;
    input.sequence = result.sequence;
    input.profiling_transaction_id = result.profiling_transaction_id;
    input.selector = result.selector;
    input.generations = result.generations;
    MutationCompleteIntervalEvidenceDraft method_evidence;
    method_evidence.baseline_observation_sha256 = digest('c');
    method_evidence.workload_observation_sha256 = digest('d');
    method_evidence.release_observation_sha256 = digest('e');
    input.method_evidence = std::move(method_evidence);
    input.completion.manifest_claims = claims();
    input.completion.ownership_recovery_evidence_sha256 =
        result.ownership_recovery_evidence_sha256;
    input.completion.action_lease_closure_sha256 =
        result.action_lease_closure_sha256;
    input.observation_contract_sha256 = result.observation_contract_sha256;
    input.predictor_contract_sha256 = result.predictor_contract_sha256;
    input.observed_at = "2026-08-23T10:00:00Z";
    input.fresh_until = "2026-08-23T10:10:00Z";
    input.max_clock_skew_milliseconds = 1000;
    const auto sealed = seal_profiling_input(std::move(input));
    if (!sealed.accepted()) {
        throw std::runtime_error("profiling context selector could not seal");
    }
    result.selector_sha256 = sealed.candidate->selector_sha256();
    return result;
}

ProfilingCollectionClock clock(
    std::shared_ptr<SharedCaptureClock> shared_clock,
    std::shared_ptr<ReleaseSettleProbe> release_settle_probe = {}) {
    ProfilingCollectionClock value;
    value.monotonic_now = [shared_clock = std::move(shared_clock),
                           release_settle_probe] {
        if (release_settle_probe) {
            release_settle_probe->note_monotonic_read();
        }
        return shared_clock->monotonic_now();
    };
    value.utc_now = [] {
        return std::chrono::system_clock::time_point{} +
               std::chrono::seconds(1787479200);
    };
    return value;
}

ProfilingCaptureSchedule schedule(
    std::shared_ptr<SharedCaptureClock> shared_clock,
    std::shared_ptr<ReleaseSettleProbe> release_settle_probe = {}) {
    ProfilingCaptureSchedule value;
    value.observation_poll_interval = 1ms;
    value.poll_cycle_overhead_allowance = 10ms;
    value.clock = clock(std::move(shared_clock),
                        std::move(release_settle_probe));
    return value;
}

ProfilingTransactionOptions transaction_options() {
    ProfilingTransactionOptions value;
    value.gate_timeout = 500ms;
    value.retry_interval = 1ms;
    value.utc_now = [] { return "2026-08-23T10:05:00Z"; };
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

ProfilingRawIntervalFrame frame(
    const ProfilingDerivationContract &derivation_contract,
    std::uint64_t watermark,
    std::uint64_t total,
    std::uint64_t owner,
    ProfilingSourceAcquisitionWindow acquisition_window) {
    const auto owner_scope_set =
        profiling_owner_scope_set_sha256(derivation_contract.owner_scopes);
    if (!owner_scope_set) throw std::runtime_error("invalid owner scope set");

    ProfilingRawIntervalFrame value;
    value.source_epoch_sha256 = digest('1');
    value.owner_scope_set_sha256 = *owner_scope_set;
    value.event_semantics_revision_sha256 =
        derivation_contract.interval.event_semantics_revision_sha256;
    value.event_watermark = ProfilingEventWatermark{watermark};
    value.acquisition_window = acquisition_window;
    value.samples = {
        ProfilingRawSample{
            "sensor/gtt-used",
            std::nullopt,
            total,
            watermark,
        },
        ProfilingRawSample{
            "sensor/gtt-used",
            "owner/model-alpha",
            owner,
            watermark,
        },
    };
    return value;
}

enum class SourceStage {
    Baseline,
    Workload,
    Release,
};

struct DynamicFrameReading {
    std::uint64_t total = 0;
    std::uint64_t owner = 0;
};

struct DynamicIntervalScenario {
    DynamicFrameReading initial_reading{100, 100};
    std::vector<DynamicFrameReading> baseline_stability_events;
    std::function<void()> after_first_baseline_read;
    std::vector<DynamicFrameReading> workload_events = {
        {300, 300},
        {500, 500},
        {250, 250},
    };
    std::vector<DynamicFrameReading> release_events = {{100, 100}};
    std::shared_ptr<ReleaseSettleProbe> release_settle_probe;
    std::optional<DynamicFrameReading> release_stability_event;
    std::optional<DynamicFrameReading> final_drain_event;
    bool throw_on_finish = false;
};

class DynamicIntervalSource final : public ProfilingIntervalObservationSource {
public:
    explicit DynamicIntervalSource(
        ProfilingDerivationContract contract_value,
        std::shared_ptr<SharedCaptureClock> shared_clock,
        DynamicIntervalScenario scenario = {})
        : contract_(std::move(contract_value)),
          shared_clock_(std::move(shared_clock)),
          scenario_(std::move(scenario)) {
        frames_.push_back(frame(
            contract_, 10, scenario_.initial_reading.total,
            scenario_.initial_reading.owner, shared_clock_->source_window()));
    }

    ProfilingRawIntervalBeginResult begin(
        const ProfilingRawIntervalReadRequest &request,
        const ProfilingCancellationCheck &should_abort) override {
        if (should_abort && should_abort()) {
            return {
                ProfilingIntervalSourceError::Cancelled,
                {},
                {},
                "dynamic begin cancelled",
            };
        }
        std::lock_guard<std::mutex> lock(mutex_);
        ++begin_calls_;
        begin_thread_ = std::this_thread::get_id();
        begin_thread_token_ = current_thread_token();
        if (active_) {
            return {
                ProfilingIntervalSourceError::Failed,
                {},
                {},
                "dynamic token already active",
            };
        }
        const auto owner_scope_set =
            profiling_owner_scope_set_sha256(contract_.owner_scopes);
        if (!owner_scope_set ||
            request.read.owner_scope_set_sha256 != *owner_scope_set ||
            request.event_semantics_revision_sha256 !=
                contract_.interval.event_semantics_revision_sha256) {
            return {
                ProfilingIntervalSourceError::Failed,
                {},
                {},
                "dynamic request identity mismatch",
            };
        }
        active_ = true;
        auto checkpoint = frames_.back();
        checkpoint.acquisition_window = shared_clock_->source_window();
        return {
            ProfilingIntervalSourceError::None,
            token_,
            std::move(checkpoint),
            {},
        };
    }

    ProfilingRawIntervalBatch read_since(
        ProfilingRawIntervalToken token,
        ProfilingEventWatermark after_event_watermark,
        const ProfilingCancellationCheck &should_abort) override {
        if (should_abort && should_abort()) {
            return failure_batch(
                ProfilingIntervalSourceError::Cancelled,
                after_event_watermark,
                "dynamic read cancelled");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        ++read_calls_;
        if (!active_ || token.opaque_id != token_.opaque_id) {
            return failure_batch(
                ProfilingIntervalSourceError::Failed,
                after_event_watermark,
                "dynamic token is inactive");
        }

        auto &stage_read_calls = read_calls_by_stage_[stage_index(stage_)];
        ++stage_read_calls;
        if (stage_ == SourceStage::Baseline && stage_read_calls == 1 &&
            !scenario_.baseline_stability_events.empty()) {
            for (const auto &reading :
                 scenario_.baseline_stability_events) {
                append_frame(reading);
            }
            baseline_stability_event_observed_ = true;
        } else if (stage_ == SourceStage::Release &&
                   scenario_.release_settle_probe &&
                   scenario_.release_settle_probe
                       ->consume_settle_checkpoint() &&
                   scenario_.release_stability_event) {
            append_frame(*scenario_.release_stability_event);
            release_stability_event_observed_ = true;
            scenario_.release_stability_event.reset();
        }
        if (stage_ == SourceStage::Baseline && stage_read_calls == 1 &&
            scenario_.after_first_baseline_read) {
            scenario_.after_first_baseline_read();
        }
        const auto current_watermark = frames_.back().event_watermark.value;
        if (after_event_watermark.value > current_watermark) {
            return failure_batch(
                ProfilingIntervalSourceError::HistoryLost,
                after_event_watermark,
                "dynamic cursor is ahead of retained history");
        }

        std::vector<ProfilingRawIntervalFrame> events;
        for (const auto &candidate : frames_) {
            if (candidate.event_watermark.value >
                after_event_watermark.value) {
                events.push_back(candidate);
            }
        }

        if (stage_ == SourceStage::Workload && workload_published_ &&
            current_watermark >= workload_final_watermark_) {
            workload_poll_thread_ = std::this_thread::get_id();
            workload_poll_thread_token_ = current_thread_token();
            workload_observer_lifetime_ = current_thread_lifetime();
        } else if (stage_ == SourceStage::Release && release_published_ &&
                   current_watermark >= release_watermark_) {
            release_poll_thread_ = std::this_thread::get_id();
            release_poll_thread_token_ = current_thread_token();
        }
        if (stage_ == SourceStage::Release &&
            scenario_.release_settle_probe) {
            scenario_.release_settle_probe->note_release_read();
        }

        auto checkpoint = frames_.back();
        checkpoint.acquisition_window = shared_clock_->source_window();
        return {
            ProfilingIntervalSourceError::None,
            after_event_watermark,
            frames_.back().event_watermark,
            std::move(events),
            std::move(checkpoint),
            {},
        };
    }

    ProfilingRawIntervalBatch finish(
        ProfilingRawIntervalToken token,
        ProfilingEventWatermark after_event_watermark) noexcept override {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            ++finish_calls_;
            finish_thread_ = std::this_thread::get_id();
            if (!active_ || token.opaque_id != token_.opaque_id) {
                return failure_batch(
                    ProfilingIntervalSourceError::Failed,
                    after_event_watermark,
                    "dynamic finish token is inactive");
            }
            active_ = false;
            finish_thread_token_ = current_thread_token();
            finish_observer_lifetime_ = current_thread_lifetime();
            if (scenario_.throw_on_finish) {
                throw std::runtime_error("dynamic finish failure");
            }

            if (scenario_.final_drain_event) {
                append_frame(*scenario_.final_drain_event);
                final_drain_event_observed_ = true;
            }

            std::vector<ProfilingRawIntervalFrame> events;
            for (const auto &candidate : frames_) {
                if (candidate.event_watermark.value >
                    after_event_watermark.value) {
                    events.push_back(candidate);
                }
            }
            auto checkpoint = frames_.back();
            checkpoint.acquisition_window = shared_clock_->source_window();
            return {
                ProfilingIntervalSourceError::None,
                after_event_watermark,
                frames_.back().event_watermark,
                std::move(events),
                std::move(checkpoint),
                {},
            };
        } catch (...) {
            return {
                ProfilingIntervalSourceError::Failed,
                after_event_watermark,
                after_event_watermark,
                {},
                {},
                {},
            };
        }
    }

    void publish_workload() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stage_ = SourceStage::Workload;
            workload_published_ = true;
            for (const auto &reading : scenario_.workload_events) {
                append_frame(reading);
            }
            workload_final_watermark_ = frames_.back().event_watermark.value;
        }
    }

    void publish_release() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stage_ = SourceStage::Release;
            release_published_ = true;
            for (const auto &reading : scenario_.release_events) {
                append_frame(reading);
            }
            release_watermark_ = frames_.back().event_watermark.value;
        }
    }

    std::size_t begin_calls() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return begin_calls_;
    }

    std::size_t finish_calls() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return finish_calls_;
    }

    bool active() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_;
    }

    std::thread::id begin_thread() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return begin_thread_;
    }

    std::thread::id workload_poll_thread() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return workload_poll_thread_;
    }

    std::thread::id release_poll_thread() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return release_poll_thread_;
    }

    std::thread::id finish_thread() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return finish_thread_;
    }

    std::uint64_t begin_thread_token() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return begin_thread_token_;
    }

    std::uint64_t workload_poll_thread_token() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return workload_poll_thread_token_;
    }

    std::uint64_t release_poll_thread_token() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return release_poll_thread_token_;
    }

    std::uint64_t finish_thread_token() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return finish_thread_token_;
    }

    bool observer_thread_exited() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return workload_observer_lifetime_.expired() &&
               finish_observer_lifetime_.expired();
    }

    std::size_t stage_read_calls(SourceStage stage) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return read_calls_by_stage_[stage_index(stage)];
    }

    bool baseline_stability_event_observed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return baseline_stability_event_observed_;
    }

    bool release_stability_event_observed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return release_stability_event_observed_;
    }

    bool final_drain_event_observed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return final_drain_event_observed_;
    }

private:
    static constexpr std::size_t stage_index(SourceStage stage) noexcept {
        return static_cast<std::size_t>(stage);
    }

    void append_frame(const DynamicFrameReading &reading) {
        const auto watermark = frames_.back().event_watermark.value + 1;
        frames_.push_back(frame(contract_, watermark, reading.total,
                                reading.owner, shared_clock_->source_window()));
    }

    ProfilingRawIntervalBatch failure_batch(
        ProfilingIntervalSourceError error,
        ProfilingEventWatermark after_event_watermark,
        std::string diagnostic) const {
        return {
            error,
            after_event_watermark,
            after_event_watermark,
            {},
            {},
            std::move(diagnostic),
        };
    }

    ProfilingDerivationContract contract_;
    std::shared_ptr<SharedCaptureClock> shared_clock_;
    DynamicIntervalScenario scenario_;
    ProfilingRawIntervalToken token_{71};
    mutable std::mutex mutex_;
    std::vector<ProfilingRawIntervalFrame> frames_;
    SourceStage stage_ = SourceStage::Baseline;
    bool active_ = false;
    bool workload_published_ = false;
    bool release_published_ = false;
    bool baseline_stability_event_observed_ = false;
    bool release_stability_event_observed_ = false;
    bool final_drain_event_observed_ = false;
    std::uint64_t workload_final_watermark_ = 0;
    std::uint64_t release_watermark_ = 0;
    std::size_t begin_calls_ = 0;
    std::size_t read_calls_ = 0;
    std::size_t finish_calls_ = 0;
    std::array<std::size_t, 3> read_calls_by_stage_{};
    std::thread::id begin_thread_;
    std::thread::id workload_poll_thread_;
    std::thread::id release_poll_thread_;
    std::thread::id finish_thread_;
    std::uint64_t begin_thread_token_ = 0;
    std::uint64_t workload_poll_thread_token_ = 0;
    std::uint64_t release_poll_thread_token_ = 0;
    std::uint64_t finish_thread_token_ = 0;
    std::weak_ptr<const ThreadLifetime> workload_observer_lifetime_;
    std::weak_ptr<const ThreadLifetime> finish_observer_lifetime_;
};

class CountingDriver final : public ProfilingWorkloadDriver {
public:
    ProfilingWorkloadStepResult run(
        Router &,
        const ProfilingTransactionContext &,
        const ProfilingCancellationCheck &) override {
        ++run_calls;
        return ProfilingWorkloadStepResult::success();
    }

    ProfilingWorkloadStepResult release(
        Router &,
        const ProfilingTransactionContext &) noexcept override {
        ++release_calls;
        return ProfilingWorkloadStepResult::success();
    }

    std::size_t run_calls = 0;
    std::size_t release_calls = 0;
};

class PredispatchAbort {
public:
    void arm_after_baseline_read() noexcept {
        state_.store(State::PermitRecorderCompletion,
                     std::memory_order_release);
    }

    bool should_abort() noexcept {
        auto expected = State::PermitRecorderCompletion;
        if (state_.compare_exchange_strong(
                expected, State::AbortOnNextCheck,
                std::memory_order_acq_rel)) {
            return false;
        }
        if (expected == State::AbortOnNextCheck) {
            state_.store(State::Aborted, std::memory_order_release);
            return true;
        }
        return expected == State::Aborted;
    }

    bool aborted() const noexcept {
        return state_.load(std::memory_order_acquire) == State::Aborted;
    }

private:
    enum class State {
        Disarmed,
        PermitRecorderCompletion,
        AbortOnNextCheck,
        Aborted,
    };

    std::atomic<State> state_{State::Disarmed};
};

class CoordinatedDriver final : public ProfilingWorkloadDriver {
public:
    explicit CoordinatedDriver(DynamicIntervalSource &source)
        : source_(source) {}

    ProfilingWorkloadStepResult run(
        Router &router,
        const ProfilingTransactionContext &context,
        const ProfilingCancellationCheck &should_abort) override {
        ++run_calls;
        run_thread = std::this_thread::get_id();
        run_thread_token = current_thread_token();
        run_router = &router;
        transaction_id = context.profiling_transaction_id;
        source_.publish_workload();
        aborted_during_run = should_abort && should_abort();
        if (aborted_during_run) {
            return ProfilingWorkloadStepResult::cancelled(
                "profiling workload was cancelled");
        }
        return ProfilingWorkloadStepResult::success();
    }

    ProfilingWorkloadStepResult release(
        Router &router,
        const ProfilingTransactionContext &context) noexcept override {
        ++release_calls;
        release_thread = std::this_thread::get_id();
        release_thread_token = current_thread_token();
        release_router = &router;
        release_transaction_id = context.profiling_transaction_id;
        try {
            source_.publish_release();
        } catch (...) {
            release_failed = true;
            return ProfilingWorkloadStepResult::failed(
                "profiling workload release failed");
        }
        return ProfilingWorkloadStepResult::success();
    }

    DynamicIntervalSource &source_;
    std::size_t run_calls = 0;
    std::size_t release_calls = 0;
    std::thread::id run_thread;
    std::thread::id release_thread;
    std::uint64_t run_thread_token = 0;
    std::uint64_t release_thread_token = 0;
    Router *run_router = nullptr;
    Router *release_router = nullptr;
    std::string transaction_id;
    std::string release_transaction_id;
    bool aborted_during_run = false;
    bool release_failed = false;
};

enum class DriverRunOutcome {
    Complete,
    Throw,
    Cancel,
};

class AdversarialDriver final : public ProfilingWorkloadDriver {
public:
    AdversarialDriver(DynamicIntervalSource &source,
                      DriverRunOutcome outcome,
                      bool publish_release,
                      std::atomic<bool> *caller_cancelled = nullptr)
        : source_(source), outcome_(outcome),
          publish_release_(publish_release),
          caller_cancelled_(caller_cancelled) {}

    ProfilingWorkloadStepResult run(
        Router &,
        const ProfilingTransactionContext &,
        const ProfilingCancellationCheck &) override {
        ++run_calls;
        source_.publish_workload();
        if (outcome_ == DriverRunOutcome::Cancel && caller_cancelled_) {
            caller_cancelled_->store(true, std::memory_order_release);
        }
        if (outcome_ == DriverRunOutcome::Throw) {
            throw std::runtime_error("adversarial workload failure");
        }
        if (outcome_ == DriverRunOutcome::Cancel) {
            return ProfilingWorkloadStepResult::cancelled(
                "adversarial workload cancelled");
        }
        return ProfilingWorkloadStepResult::success();
    }

    ProfilingWorkloadStepResult release(
        Router &,
        const ProfilingTransactionContext &) noexcept override {
        ++release_calls;
        if (!publish_release_) {
            return ProfilingWorkloadStepResult::ambiguous(
                "workload release was not verified");
        }
        try {
            source_.publish_release();
        } catch (...) {
            release_failed = true;
            return ProfilingWorkloadStepResult::failed(
                "workload release failed");
        }
        return ProfilingWorkloadStepResult::success();
    }

    DynamicIntervalSource &source_;
    DriverRunOutcome outcome_ = DriverRunOutcome::Complete;
    bool publish_release_ = false;
    std::atomic<bool> *caller_cancelled_ = nullptr;
    std::size_t run_calls = 0;
    std::size_t release_calls = 0;
    bool release_failed = false;
};

ProfilingWorkloadStepResult reported_step_result(
    ProfilingWorkloadStepStatus status,
    std::string diagnostic) {
    switch (status) {
    case ProfilingWorkloadStepStatus::Succeeded:
        return ProfilingWorkloadStepResult::success();
    case ProfilingWorkloadStepStatus::Cancelled:
        return ProfilingWorkloadStepResult::cancelled(
            std::move(diagnostic));
    case ProfilingWorkloadStepStatus::Failed:
        return ProfilingWorkloadStepResult::failed(std::move(diagnostic));
    case ProfilingWorkloadStepStatus::Ambiguous:
        return ProfilingWorkloadStepResult::ambiguous(
            std::move(diagnostic));
    default:
        return ProfilingWorkloadStepResult::ambiguous(
            "workload step returned an unknown result");
    }
}

class ReportingDriver final : public ProfilingWorkloadDriver {
public:
    ReportingDriver(DynamicIntervalSource &source,
                    ProfilingWorkloadStepStatus run_status,
                    ProfilingWorkloadStepStatus release_status)
        : source_(source), run_status_(run_status),
          release_status_(release_status) {}

    ProfilingWorkloadStepResult run(
        Router &,
        const ProfilingTransactionContext &,
        const ProfilingCancellationCheck &) override {
        ++run_calls;
        source_.publish_workload();
        return reported_step_result(run_status_, "model load failed");
    }

    ProfilingWorkloadStepResult release(
        Router &,
        const ProfilingTransactionContext &) noexcept override {
        ++release_calls;
        try {
            source_.publish_release();
        } catch (...) {
            return ProfilingWorkloadStepResult::failed(
                "workload release transition failed");
        }
        return reported_step_result(
            release_status_,
            release_status_ == ProfilingWorkloadStepStatus::Ambiguous
                ? "workload release outcome is ambiguous"
                : "workload release failed");
    }

    DynamicIntervalSource &source_;
    ProfilingWorkloadStepStatus run_status_;
    ProfilingWorkloadStepStatus release_status_;
    std::size_t run_calls = 0;
    std::size_t release_calls = 0;
};

void require_empty_capture(TestState &state,
                           const ProfilingTransactionCapture &capture,
                           const char *message) {
    state.require(capture.baseline_attestation.empty() &&
                      capture.workload_attestation.empty() &&
                      capture.release_attestation.empty() &&
                      !capture.diagnostic.empty(),
                  message);
}

void test_invalid_and_default_source_never_invoke_driver(
    TestState &state,
    Router &router) {
    const auto derivation_contract = contract();
    const auto transaction_context = context(derivation_contract);

    auto invalid_clock = std::make_shared<SharedCaptureClock>();
    DynamicIntervalSource source(derivation_contract, invalid_clock);
    auto invalid_schedule = schedule(invalid_clock);
    invalid_schedule.observation_poll_interval = 471ms;
    ProfilingCaptureAuthority invalid(
        derivation_contract, source, std::move(invalid_schedule));
    CountingDriver invalid_driver;
    const auto invalid_capture = invalid.capture(
        router, transaction_context, invalid_driver, [] { return false; });

    require_empty_capture(
        state,
        invalid_capture,
        "a polling schedule without its reserved cycle-overhead budget emits no phase attestations");
    state.require(source.begin_calls() == 0 && invalid_driver.run_calls == 0 &&
                      invalid_driver.release_calls == 0,
                  "an invalid schedule is rejected before source or driver access");

    auto no_allowance_clock = std::make_shared<SharedCaptureClock>();
    DynamicIntervalSource no_allowance_source(derivation_contract,
                                              no_allowance_clock);
    auto no_allowance_schedule = schedule(no_allowance_clock);
    no_allowance_schedule.poll_cycle_overhead_allowance = 0ms;
    ProfilingCaptureAuthority no_allowance(
        derivation_contract, no_allowance_source,
        std::move(no_allowance_schedule));
    CountingDriver no_allowance_driver;
    const auto no_allowance_capture = no_allowance.capture(
        router, transaction_context, no_allowance_driver,
        [] { return false; });

    require_empty_capture(
        state,
        no_allowance_capture,
        "a polling schedule without a defined cycle-overhead allowance emits no phase attestations");
    state.require(no_allowance_source.begin_calls() == 0 &&
                      no_allowance_driver.run_calls == 0 &&
                      no_allowance_driver.release_calls == 0,
                  "a missing cycle-overhead allowance is rejected before source or driver access");

    auto incomplete_clock = std::make_shared<SharedCaptureClock>();
    DynamicIntervalSource incomplete_source(derivation_contract,
                                            incomplete_clock);
    ProfilingCaptureAuthority incomplete(
        derivation_contract, incomplete_source, schedule(incomplete_clock));
    CountingDriver incomplete_driver;
    const auto incomplete_context = context(
        derivation_contract,
        {
            ConstraintKind::Ownership,
            ConstraintKind::GpuSharedResidency,
            ConstraintKind::ModelTypePool,
            ConstraintKind::HostMemAvailableFloor,
        });
    const auto incomplete_capture = incomplete.capture(
        router, incomplete_context, incomplete_driver,
        [] { return false; });

    require_empty_capture(
        state,
        incomplete_capture,
        "a derivation contract that omits required selector families emits no phase attestations");
    state.require(incomplete_source.begin_calls() == 0 &&
                      incomplete_driver.run_calls == 0 &&
                      incomplete_driver.release_calls == 0,
                  "incomplete selector coverage is rejected before source or driver access");

    auto unavailable_clock = std::make_shared<SharedCaptureClock>();
    ProfilingCaptureAuthority unavailable(
        derivation_contract, schedule(unavailable_clock));
    CountingDriver unavailable_driver;
    const auto unavailable_capture = unavailable.capture(
        router, transaction_context, unavailable_driver,
        [] { return false; });

    require_empty_capture(
        state,
        unavailable_capture,
        "the default unavailable source emits no phase attestations");
    state.require(unavailable_driver.run_calls == 0 &&
                      unavailable_driver.release_calls == 0,
                  "the unavailable default source never invokes the driver");
}

void test_captures_canonical_phases_across_a_complete_interval(
    TestState &state,
    Router &router) {
    const auto derivation_contract = contract();
    const auto transaction_context = context(derivation_contract);
    auto shared_clock = std::make_shared<SharedCaptureClock>();
    DynamicIntervalSource source(derivation_contract, shared_clock);
    CoordinatedDriver driver(source);
    ProfilingCaptureAuthority authority(
        derivation_contract, source, schedule(shared_clock));
    ProfilingTransaction transaction(router, transaction_options());
    ProfilingTransactionCapture captured;
    const auto owner_thread = std::this_thread::get_id();
    const auto owner_thread_token = current_thread_token();

    const auto result = transaction.run(
        transaction_context,
        [&](Router &gated_router,
            const ProfilingTransactionContext &gated_context,
            const ProfilingCancellationCheck &should_abort) {
            captured = authority.capture(
                gated_router, gated_context, driver, should_abort);
            return captured;
        });

    state.require(result.accepted(),
                  "one complete interval is accepted through ProfilingTransaction");
    state.require(captured.diagnostic.empty() &&
                      !captured.baseline_attestation.empty() &&
                      !captured.workload_attestation.empty() &&
                      !captured.release_attestation.empty(),
                  "the authority publishes all three phase bytes atomically");

    const auto baseline =
        parse_profiling_phase_attestation(captured.baseline_attestation);
    const auto workload =
        parse_profiling_phase_attestation(captured.workload_attestation);
    const auto release =
        parse_profiling_phase_attestation(captured.release_attestation);
    state.require(baseline.accepted() && workload.accepted() &&
                      release.accepted(),
                  "every emitted phase attestation is canonical and parseable");
    if (baseline.candidate && workload.candidate && release.candidate) {
        state.require(
            baseline.candidate->canonical_bytes() ==
                    captured.baseline_attestation &&
                workload.candidate->canonical_bytes() ==
                    captured.workload_attestation &&
                release.candidate->canonical_bytes() ==
                    captured.release_attestation,
            "phase bytes round-trip through the canonical public parser");
        state.require(
            baseline.candidate->phase() == ProfilingPhase::Baseline &&
                workload.candidate->phase() == ProfilingPhase::Workload &&
                release.candidate->phase() == ProfilingPhase::Release &&
                baseline.candidate->lifecycle_state() ==
                    ProfilingLifecycleState::BaselineQuiescent &&
                workload.candidate->lifecycle_state() ==
                    ProfilingLifecycleState::WorkloadComplete &&
                release.candidate->lifecycle_state() ==
                    ProfilingLifecycleState::ReleaseVerified,
            "the Server-owned authority assigns the three lifecycle phases");
        state.require(
            has_capacity(baseline.candidate->observed_claims(), 100) &&
                has_capacity(workload.candidate->observed_claims(), 500) &&
                has_capacity(release.candidate->observed_claims(), 100),
            "the workload phase retains a hidden interval peak between stable endpoints");
        state.require(
            has_capacity(baseline.candidate->uncertainty_claims(), 10) &&
                has_capacity(workload.candidate->uncertainty_claims(), 10) &&
                has_capacity(release.candidate->uncertainty_claims(), 10) &&
                has_capacity(baseline.candidate->safety_margin_claims(), 890) &&
                has_capacity(workload.candidate->safety_margin_claims(), 490) &&
                has_capacity(release.candidate->safety_margin_claims(), 890),
            "phase attestations retain interval uncertainty and minimum safety slack");
        state.require(
            baseline.candidate->observation_generation() <
                    workload.candidate->observation_generation() &&
                workload.candidate->observation_generation() <
                    release.candidate->observation_generation(),
            "selected phase checkpoints remain strictly capture ordered");
        state.require(
            baseline.candidate->provenance_sha256().size() == 64 &&
                workload.candidate->provenance_sha256().size() == 64 &&
                release.candidate->provenance_sha256().size() == 64 &&
                baseline.candidate->provenance_sha256() !=
                    workload.candidate->provenance_sha256() &&
                workload.candidate->provenance_sha256() !=
                    release.candidate->provenance_sha256(),
            "each phase binds its ordered interval transcript independently");
    }

    state.require(result.evidence.has_value() &&
                      result.evidence->baseline().canonical_bytes() ==
                          captured.baseline_attestation &&
                      result.evidence->workload().canonical_bytes() ==
                          captured.workload_attestation &&
                      result.evidence->release().canonical_bytes() ==
                          captured.release_attestation,
                  "ProfilingTransaction retains the authority's canonical bytes");

    state.require(driver.run_calls == 1 && driver.release_calls == 1 &&
                      !driver.aborted_during_run && !driver.release_failed,
                  "the owner drives one nonblocking workload and terminal release");
    state.require(driver.run_router == &router &&
                      driver.release_router == &router &&
                      driver.transaction_id ==
                          transaction_context.profiling_transaction_id &&
                      driver.release_transaction_id ==
                          transaction_context.profiling_transaction_id,
                  "both driver calls receive the gated Router and transaction identity");

    const auto observer_thread = source.workload_poll_thread();
    const auto observer_thread_token = source.workload_poll_thread_token();
    state.require(source.begin_thread() == owner_thread &&
                      driver.run_thread == owner_thread &&
                      driver.release_thread == owner_thread &&
                      source.begin_thread_token() == owner_thread_token &&
                      driver.run_thread_token == owner_thread_token &&
                      driver.release_thread_token == owner_thread_token,
                  "source registration and all Router work stay on the owner thread");
    state.require(observer_thread != std::thread::id{} &&
                      observer_thread != owner_thread &&
                      source.release_poll_thread() == observer_thread &&
                      source.finish_thread() == observer_thread &&
                      observer_thread_token != 0 &&
                      observer_thread_token != owner_thread_token &&
                      source.release_poll_thread_token() ==
                          observer_thread_token &&
                      source.finish_thread_token() == observer_thread_token,
                  "one distinct observer polls workload and release and performs the final drain");
    state.require(source.begin_calls() == 1 && source.finish_calls() == 1 &&
                      !source.active() && source.observer_thread_exited(),
                  "capture returns only after the observer drains, unregisters, and exits");
}

void test_accepts_stable_nonzero_domain_background(
    TestState &state,
    Router &router) {
    const auto derivation_contract = contract();
    const auto transaction_context = context(derivation_contract);
    DynamicIntervalScenario scenario;
    scenario.initial_reading = {500, 100};
    scenario.workload_events = {
        {700, 300},
        {900, 500},
        {500, 100},
    };
    scenario.release_events = {{500, 100}};
    auto shared_clock = std::make_shared<SharedCaptureClock>();
    DynamicIntervalSource source(derivation_contract, shared_clock,
                                 std::move(scenario));
    CoordinatedDriver driver(source);
    ProfilingCaptureAuthority authority(
        derivation_contract, source, schedule(shared_clock));

    const auto captured = authority.capture(
        router, transaction_context, driver, [] { return false; });
    const auto baseline =
        parse_profiling_phase_attestation(captured.baseline_attestation);
    const auto workload =
        parse_profiling_phase_attestation(captured.workload_attestation);
    const auto release =
        parse_profiling_phase_attestation(captured.release_attestation);

    state.require(captured.diagnostic.empty() && baseline.accepted() &&
                      workload.accepted() && release.accepted(),
                  "stable nonzero domain background yields all three canonical phases");
    if (baseline.candidate && workload.candidate && release.candidate) {
        state.require(
            has_capacity(baseline.candidate->observed_claims(), 100) &&
                has_capacity(workload.candidate->observed_claims(), 500) &&
                has_capacity(release.candidate->observed_claims(), 100) &&
                has_capacity(baseline.candidate->attributed_claims(), 100) &&
                has_capacity(workload.candidate->attributed_claims(), 500) &&
                has_capacity(release.candidate->attributed_claims(), 100),
            "phase claims expose only the target-owned effect");
        state.require(
            has_zero_capacity(baseline.candidate->unattributed_claims()) &&
                has_zero_capacity(workload.candidate->unattributed_claims()) &&
                has_zero_capacity(release.candidate->unattributed_claims()),
            "a constant domain-wide residual is not unattributed movement");
        state.require(
            has_capacity(baseline.candidate->safety_margin_claims(), 490) &&
                has_capacity(workload.candidate->safety_margin_claims(), 90) &&
                has_capacity(release.candidate->safety_margin_claims(), 490),
            "phase safety slack retains peak domain-wide use");
    }
    state.require(driver.run_calls == 1 && driver.release_calls == 1 &&
                      source.finish_calls() == 1 && !source.active() &&
                      source.observer_thread_exited(),
                  "stable background capture releases and drains exactly once");
}

void test_stability_windows_restart_after_retained_events(
    TestState &state,
    Router &router) {
    const auto derivation_contract = contract();
    const auto transaction_context = context(derivation_contract);
    auto release_settle_probe = std::make_shared<ReleaseSettleProbe>();
    DynamicIntervalScenario scenario;
    scenario.baseline_stability_events = {{120, 120}};
    scenario.release_settle_probe = release_settle_probe;
    scenario.release_stability_event = DynamicFrameReading{105, 105};
    auto shared_clock = std::make_shared<SharedCaptureClock>();
    DynamicIntervalSource source(derivation_contract, shared_clock,
                                 std::move(scenario));
    CoordinatedDriver driver(source);
    ProfilingCaptureAuthority authority(
        derivation_contract, source,
        schedule(shared_clock, release_settle_probe));

    const auto captured = authority.capture(
        router, transaction_context, driver, [] { return false; });
    state.require(captured.diagnostic.empty(),
                  "retained events restart both stability windows without rejecting a complete capture");
    const auto baseline =
        parse_profiling_phase_attestation(captured.baseline_attestation);
    const auto release =
        parse_profiling_phase_attestation(captured.release_attestation);
    state.require(baseline.accepted() && release.accepted(),
                  "stability-reset phase attestations remain canonical");
    if (baseline.candidate && release.candidate) {
        state.require(
            has_capacity(baseline.candidate->observed_claims(), 120) &&
                has_capacity(release.candidate->observed_claims(), 105),
            "baseline and release seal only the stable checkpoints after retained movement");
    }
    state.require(
        source.baseline_stability_event_observed() &&
            source.release_stability_event_observed() &&
            release_settle_probe->settle_checkpoint_started() &&
            source.stage_read_calls(SourceStage::Baseline) >= 2 &&
            release_settle_probe->completed_release_reads() >=
                release_settle_probe->release_reads_at_settle_entry() + 2,
        "each moved stability boundary requires a later unchanged checkpoint");
}

void test_domain_residual_excursions_discard_every_phase(
    TestState &state,
    Router &router) {
    const auto derivation_contract = contract();
    const auto transaction_context = context(derivation_contract);

    DynamicIntervalScenario baseline_scenario;
    baseline_scenario.baseline_stability_events = {
        {300, 200},
        {100, 100},
    };
    auto baseline_clock = std::make_shared<SharedCaptureClock>();
    DynamicIntervalSource baseline_source(
        derivation_contract, baseline_clock, std::move(baseline_scenario));
    CountingDriver baseline_driver;
    ProfilingCaptureAuthority baseline_authority(
        derivation_contract, baseline_source, schedule(baseline_clock));
    const auto baseline_capture = baseline_authority.capture(
        router, transaction_context, baseline_driver,
        [] { return false; });
    require_empty_capture(
        state,
        baseline_capture,
        "a baseline domain-residual excursion discards all phases even when the following frame returns");
    state.require(baseline_driver.run_calls == 1 &&
                      baseline_driver.release_calls == 1 &&
                      baseline_source.finish_calls() == 1 &&
                      !baseline_source.active(),
                  "baseline residual movement remains structural until the authority rejects atomic publication");

    DynamicIntervalScenario workload_scenario;
    workload_scenario.workload_events = {
        {300, 200},
        {100, 100},
    };
    auto workload_clock = std::make_shared<SharedCaptureClock>();
    DynamicIntervalSource workload_source(
        derivation_contract, workload_clock, std::move(workload_scenario));
    AdversarialDriver workload_driver(
        workload_source, DriverRunOutcome::Complete, false);
    ProfilingCaptureAuthority workload_authority(
        derivation_contract, workload_source, schedule(workload_clock));
    const auto workload_capture = workload_authority.capture(
        router, transaction_context, workload_driver,
        [] { return false; });
    require_empty_capture(
        state,
        workload_capture,
        "a workload domain-residual excursion discards every phase byte");
    state.require(workload_driver.run_calls == 1 &&
                      workload_driver.release_calls == 1 &&
                      workload_source.finish_calls() == 1 &&
                      !workload_source.active() &&
                      workload_source.observer_thread_exited(),
                  "workload residual movement releases once and drains before returning");

    DynamicIntervalScenario release_scenario;
    release_scenario.release_events = {
        {300, 200},
        {100, 100},
    };
    auto release_clock = std::make_shared<SharedCaptureClock>();
    DynamicIntervalSource release_source(
        derivation_contract, release_clock, std::move(release_scenario));
    AdversarialDriver release_driver(
        release_source, DriverRunOutcome::Complete, true);
    ProfilingCaptureAuthority release_authority(
        derivation_contract, release_source, schedule(release_clock));
    const auto release_capture = release_authority.capture(
        router, transaction_context, release_driver,
        [] { return false; });
    require_empty_capture(
        state,
        release_capture,
        "a release domain-residual excursion discards every phase byte");
    state.require(release_driver.run_calls == 1 &&
                      release_driver.release_calls == 1 &&
                      release_source.finish_calls() == 1 &&
                      !release_source.active() &&
                      release_source.observer_thread_exited(),
                  "release residual movement drains and joins the observer before returning");

    DynamicIntervalScenario final_drain_scenario;
    final_drain_scenario.initial_reading = {500, 100};
    final_drain_scenario.workload_events = {
        {700, 300},
        {900, 500},
        {500, 100},
    };
    final_drain_scenario.release_events = {{500, 100}};
    final_drain_scenario.final_drain_event =
        DynamicFrameReading{501, 100};
    auto final_drain_clock = std::make_shared<SharedCaptureClock>();
    DynamicIntervalSource final_drain_source(
        derivation_contract, final_drain_clock,
        std::move(final_drain_scenario));
    CoordinatedDriver final_drain_driver(final_drain_source);
    ProfilingCaptureAuthority final_drain_authority(
        derivation_contract, final_drain_source,
        schedule(final_drain_clock));
    const auto final_drain_capture = final_drain_authority.capture(
        router, transaction_context, final_drain_driver,
        [] { return false; });
    require_empty_capture(
        state, final_drain_capture,
        "a final-drain domain-residual excursion discards every phase byte");
    state.require(
        final_drain_source.final_drain_event_observed() &&
            final_drain_driver.run_calls == 1 &&
            final_drain_driver.release_calls == 1 &&
            final_drain_source.finish_calls() == 1 &&
            !final_drain_source.active() &&
            final_drain_source.observer_thread_exited(),
        "final-drain residual movement closes workload and source exactly once");
}

void test_workload_failure_and_cancellation_close_the_interval(
    TestState &state,
    Router &router) {
    const auto derivation_contract = contract();
    const auto transaction_context = context(derivation_contract);

    auto throwing_clock = std::make_shared<SharedCaptureClock>();
    DynamicIntervalSource throwing_source(derivation_contract,
                                          throwing_clock);
    AdversarialDriver throwing_driver(
        throwing_source, DriverRunOutcome::Throw, true);
    ProfilingCaptureAuthority throwing_authority(
        derivation_contract, throwing_source, schedule(throwing_clock));
    const auto throwing_capture = throwing_authority.capture(
        router, transaction_context, throwing_driver,
        [] { return false; });
    require_empty_capture(
        state,
        throwing_capture,
        "a workload exception publishes no lifecycle attestations");
    state.require(
        throwing_driver.run_calls == 1 &&
            throwing_driver.release_calls == 1 &&
            !throwing_driver.release_failed &&
            throwing_source.finish_calls() == 1 &&
            !throwing_source.active() &&
            throwing_source.observer_thread_exited(),
        "a workload exception releases once, finishes once, joins the observer, and returns no live token");

    std::atomic<bool> caller_cancelled{false};
    auto cancelled_clock = std::make_shared<SharedCaptureClock>();
    DynamicIntervalSource cancelled_source(derivation_contract,
                                           cancelled_clock);
    AdversarialDriver cancelled_driver(
        cancelled_source, DriverRunOutcome::Cancel, false,
        &caller_cancelled);
    ProfilingCaptureAuthority cancelled_authority(
        derivation_contract, cancelled_source, schedule(cancelled_clock));
    const auto cancelled_capture = cancelled_authority.capture(
        router, transaction_context, cancelled_driver, [&] {
            return caller_cancelled.load(std::memory_order_acquire);
        });
    require_empty_capture(
        state,
        cancelled_capture,
        "caller cancellation publishes no lifecycle attestations");
    state.require(
        caller_cancelled.load(std::memory_order_acquire) &&
            cancelled_driver.run_calls == 1 &&
            cancelled_driver.release_calls == 1 &&
            !cancelled_driver.release_failed &&
            cancelled_source.finish_calls() == 1 &&
            !cancelled_source.active() &&
            cancelled_source.observer_thread_exited(),
        "caller cancellation releases once, finishes once, joins the observer, and returns no live token");
}

void test_reported_workload_results_gate_phase_evidence(
    TestState &state,
    Router &router) {
    const auto derivation_contract = contract();
    const auto transaction_context = context(derivation_contract);
    const auto bounded_failure = ProfilingWorkloadStepResult::failed(
        std::string(max_local_overlay_diagnostic_bytes + 64, 'x'));
    state.require(
        bounded_failure.status() == ProfilingWorkloadStepStatus::Failed &&
            !bounded_failure.succeeded() &&
            bounded_failure.diagnostic().size() <=
                max_local_overlay_diagnostic_bytes,
        "workload step results retain an explicit state and bounded diagnostic");

    auto load_clock = std::make_shared<SharedCaptureClock>();
    DynamicIntervalSource load_source(derivation_contract, load_clock);
    ReportingDriver load_driver(
        load_source, ProfilingWorkloadStepStatus::Failed,
        ProfilingWorkloadStepStatus::Succeeded);
    ProfilingCaptureAuthority load_authority(
        derivation_contract, load_source, schedule(load_clock));
    const auto load_capture = load_authority.capture(
        router, transaction_context, load_driver, [] { return false; });
    require_empty_capture(
        state, load_capture,
        "a reported load failure publishes no phase-evidence candidate");
    state.require(
        load_driver.run_calls == 1 && load_driver.release_calls == 1 &&
            load_source.finish_calls() == 1 && !load_source.active() &&
            load_source.observer_thread_exited(),
        "a reported load failure still releases, drains, and joins exactly once");

    auto ambiguous_clock = std::make_shared<SharedCaptureClock>();
    DynamicIntervalSource ambiguous_source(
        derivation_contract, ambiguous_clock);
    ReportingDriver ambiguous_driver(
        ambiguous_source, ProfilingWorkloadStepStatus::Succeeded,
        ProfilingWorkloadStepStatus::Ambiguous);
    ProfilingCaptureAuthority ambiguous_authority(
        derivation_contract, ambiguous_source, schedule(ambiguous_clock));
    const auto ambiguous_capture = ambiguous_authority.capture(
        router, transaction_context, ambiguous_driver,
        [] { return false; });
    require_empty_capture(
        state, ambiguous_capture,
        "an ambiguous release publishes no phase-evidence candidate");
    state.require(
        ambiguous_driver.run_calls == 1 &&
            ambiguous_driver.release_calls == 1 &&
            ambiguous_source.finish_calls() == 1 &&
            !ambiguous_source.active() &&
            ambiguous_source.observer_thread_exited(),
        "an ambiguous release still drains and joins exactly once");

    auto failed_clock = std::make_shared<SharedCaptureClock>();
    DynamicIntervalSource failed_source(derivation_contract, failed_clock);
    ReportingDriver failed_driver(
        failed_source, ProfilingWorkloadStepStatus::Succeeded,
        ProfilingWorkloadStepStatus::Failed);
    ProfilingCaptureAuthority failed_authority(
        derivation_contract, failed_source, schedule(failed_clock));
    const auto failed_capture = failed_authority.capture(
        router, transaction_context, failed_driver, [] { return false; });
    require_empty_capture(
        state, failed_capture,
        "a failed release publishes no phase-evidence candidate");
    state.require(
        failed_driver.run_calls == 1 && failed_driver.release_calls == 1 &&
            failed_source.finish_calls() == 1 && !failed_source.active() &&
            failed_source.observer_thread_exited(),
        "a failed release still drains and joins exactly once");
}

void test_predispatch_abort_never_enters_workload_ownership(
    TestState &state,
    Router &router) {
    const auto derivation_contract = contract();
    const auto transaction_context = context(derivation_contract);
    PredispatchAbort abort;
    DynamicIntervalScenario scenario;
    scenario.after_first_baseline_read = [&abort] {
        abort.arm_after_baseline_read();
    };
    auto shared_clock = std::make_shared<SharedCaptureClock>();
    DynamicIntervalSource source(derivation_contract, shared_clock,
                                 std::move(scenario));
    CountingDriver driver;
    ProfilingCaptureAuthority authority(
        derivation_contract, source, schedule(shared_clock));
    const auto owner_thread = std::this_thread::get_id();
    const auto owner_thread_token = current_thread_token();

    const auto captured = authority.capture(
        router, transaction_context, driver,
        [&abort] { return abort.should_abort(); });

    require_empty_capture(
        state,
        captured,
        "a cancellation observed at the post-readiness dispatch boundary emits no phase bytes");
    state.require(abort.aborted() && driver.run_calls == 0 &&
                      driver.release_calls == 0,
                  "pre-dispatch cancellation never enters workload or release ownership");
    state.require(
        source.begin_calls() == 1 && source.finish_calls() == 1 &&
            !source.active() && source.finish_thread() != std::thread::id{} &&
            source.finish_thread() != owner_thread &&
            source.finish_thread_token() != 0 &&
            source.finish_thread_token() != owner_thread_token &&
            source.observer_thread_exited(),
        "pre-dispatch cancellation joins the ready observer after one source finish");
}

void test_release_settling_abort_closes_owned_workload(
    TestState &state,
    Router &router) {
    const auto derivation_contract = contract();
    const auto transaction_context = context(derivation_contract);
    std::atomic<bool> caller_cancelled{false};
    auto release_settle_probe =
        std::make_shared<ReleaseSettleProbe>(&caller_cancelled);
    DynamicIntervalScenario scenario;
    scenario.release_settle_probe = release_settle_probe;
    auto shared_clock = std::make_shared<SharedCaptureClock>();
    DynamicIntervalSource source(derivation_contract, shared_clock,
                                 std::move(scenario));
    CoordinatedDriver driver(source);
    ProfilingCaptureAuthority authority(
        derivation_contract, source,
        schedule(shared_clock, release_settle_probe));
    const auto owner_thread = std::this_thread::get_id();
    const auto owner_thread_token = current_thread_token();

    const auto captured = authority.capture(
        router, transaction_context, driver, [&caller_cancelled] {
            return caller_cancelled.load(std::memory_order_acquire);
        });

    require_empty_capture(
        state,
        captured,
        "cancellation after the release boundary emits no phase bytes while release is settling");
    state.require(caller_cancelled.load(std::memory_order_acquire) &&
                      driver.run_calls == 1 && driver.release_calls == 1 &&
                      !driver.release_failed &&
                      release_settle_probe->settle_checkpoint_started() &&
                      release_settle_probe->completed_release_reads() ==
                          release_settle_probe->release_reads_at_settle_entry(),
                  "release-settling cancellation occurs after one completed workload and release transition");
    state.require(
        source.begin_calls() == 1 && source.finish_calls() == 1 &&
            !source.active() && source.finish_thread() != std::thread::id{} &&
            source.finish_thread() != owner_thread &&
            source.finish_thread_token() != 0 &&
            source.finish_thread_token() != owner_thread_token &&
            source.observer_thread_exited(),
        "release-settling cancellation finishes once and joins the observer before returning");
}

void test_unverified_noop_release_discards_every_phase(
    TestState &state,
    Router &router) {
    const auto derivation_contract = contract();
    const auto transaction_context = context(derivation_contract);
    DynamicIntervalScenario scenario;
    scenario.workload_events = {
        {500, 500},
        {100, 100},
    };
    scenario.release_events.clear();
    auto shared_clock = std::make_shared<SharedCaptureClock>();
    DynamicIntervalSource source(derivation_contract, shared_clock,
                                 std::move(scenario));
    AdversarialDriver driver(
        source, DriverRunOutcome::Complete, false);
    ProfilingCaptureAuthority authority(
        derivation_contract, source, schedule(shared_clock));

    const auto captured = authority.capture(
        router, transaction_context, driver, [] { return false; });
    require_empty_capture(
        state,
        captured,
        "a no-op release without retained release evidence publishes no phase bytes");
    state.require(driver.run_calls == 1 && driver.release_calls == 1 &&
                      source.finish_calls() == 1 && !source.active() &&
                      source.observer_thread_exited(),
                  "an unverified no-op release still closes the driver, observer, and source exactly once");
}

void test_final_drain_failures_discard_every_phase(
    TestState &state,
    Router &router) {
    const auto derivation_contract = contract();
    const auto transaction_context = context(derivation_contract);
    DynamicIntervalScenario scenario;
    scenario.final_drain_event = DynamicFrameReading{101, 101};
    auto shared_clock = std::make_shared<SharedCaptureClock>();
    DynamicIntervalSource source(derivation_contract, shared_clock,
                                 std::move(scenario));
    CoordinatedDriver driver(source);
    ProfilingCaptureAuthority authority(
        derivation_contract, source, schedule(shared_clock));

    const auto captured = authority.capture(
        router, transaction_context, driver, [] { return false; });
    require_empty_capture(
        state,
        captured,
        "watermark movement in the atomic final drain invalidates all phase bytes");
    state.require(source.final_drain_event_observed() &&
                      driver.release_calls == 1 &&
                      source.finish_calls() == 1 && !source.active() &&
                      source.observer_thread_exited(),
                  "a moved final drain unregisters once and joins its observer before failing closed");

    DynamicIntervalScenario throwing_scenario;
    throwing_scenario.throw_on_finish = true;
    auto throwing_clock = std::make_shared<SharedCaptureClock>();
    DynamicIntervalSource throwing_source(
        derivation_contract, throwing_clock, std::move(throwing_scenario));
    CoordinatedDriver throwing_driver(throwing_source);
    ProfilingCaptureAuthority throwing_authority(
        derivation_contract, throwing_source, schedule(throwing_clock));

    const auto throwing_capture = throwing_authority.capture(
        router, transaction_context, throwing_driver,
        [] { return false; });
    require_empty_capture(
        state,
        throwing_capture,
        "a final-drain adapter exception is contained and publishes no phase bytes");
    state.require(throwing_source.finish_calls() == 1 &&
                      !throwing_source.active() &&
                      throwing_source.observer_thread_exited(),
                  "a throwing final drain unregisters once without terminating the test process");
}

void test_ordered_below_peak_history_changes_workload_provenance(
    TestState &state,
    Router &router) {
    const auto derivation_contract = contract();
    const auto transaction_context = context(derivation_contract);
    DynamicIntervalScenario first_scenario;
    first_scenario.workload_events = {
        {200, 200},
        {500, 500},
        {300, 300},
        {100, 100},
    };
    DynamicIntervalScenario second_scenario;
    second_scenario.workload_events = {
        {300, 300},
        {500, 500},
        {200, 200},
        {100, 100},
    };

    auto first_clock = std::make_shared<SharedCaptureClock>();
    DynamicIntervalSource first_source(
        derivation_contract, first_clock, std::move(first_scenario));
    CoordinatedDriver first_driver(first_source);
    ProfilingCaptureAuthority first_authority(
        derivation_contract, first_source, schedule(first_clock));
    const auto first = first_authority.capture(
        router, transaction_context, first_driver, [] { return false; });

    auto second_clock = std::make_shared<SharedCaptureClock>();
    DynamicIntervalSource second_source(
        derivation_contract, second_clock, std::move(second_scenario));
    CoordinatedDriver second_driver(second_source);
    ProfilingCaptureAuthority second_authority(
        derivation_contract, second_source, schedule(second_clock));
    const auto second = second_authority.capture(
        router, transaction_context, second_driver, [] { return false; });

    const auto first_workload =
        parse_profiling_phase_attestation(first.workload_attestation);
    const auto second_workload =
        parse_profiling_phase_attestation(second.workload_attestation);
    state.require(first.diagnostic.empty() && second.diagnostic.empty() &&
                      first_workload.accepted() &&
                      second_workload.accepted(),
                  "both ordered-history captures seal canonical phase attestations");
    if (first_workload.candidate && second_workload.candidate) {
        state.require(
            has_capacity(first_workload.candidate->observed_claims(), 500) &&
                has_capacity(second_workload.candidate->observed_claims(),
                             500) &&
                first_workload.candidate->observation_generation() ==
                    second_workload.candidate->observation_generation() &&
                first_workload.candidate->observed_at() ==
                    second_workload.candidate->observed_at(),
            "reordered histories retain the same workload peak, endpoint generation, and observation time");
        state.require(
            first_workload.candidate->provenance_sha256() !=
                second_workload.candidate->provenance_sha256(),
            "workload provenance binds ordered below-peak history rather than only peaks and endpoints");
    }
    state.require(first.baseline_attestation == second.baseline_attestation &&
                      first.release_attestation == second.release_attestation,
                  "reordering only workload history leaves equal baseline and release phase bytes");
}

} // namespace

int main() {
    TestState state;
    RuntimeConfig config = make_config();
    RuntimeConfig::set_global(&config);
    {
        Router router(&config, nullptr, nullptr);
        test_invalid_and_default_source_never_invoke_driver(state, router);
        test_captures_canonical_phases_across_a_complete_interval(
            state, router);
        test_accepts_stable_nonzero_domain_background(state, router);
        test_stability_windows_restart_after_retained_events(state, router);
        test_domain_residual_excursions_discard_every_phase(
            state, router);
        test_workload_failure_and_cancellation_close_the_interval(
            state, router);
        test_reported_workload_results_gate_phase_evidence(state, router);
        test_predispatch_abort_never_enters_workload_ownership(
            state, router);
        test_release_settling_abort_closes_owned_workload(state, router);
        test_unverified_noop_release_discards_every_phase(state, router);
        test_final_drain_failures_discard_every_phase(state, router);
        test_ordered_below_peak_history_changes_workload_provenance(
            state, router);
    }
    RuntimeConfig::set_global(nullptr);
    return state.ok.load() ? 0 : 1;
}
