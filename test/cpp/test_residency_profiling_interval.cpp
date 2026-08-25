#include "lemon/residency/profiling_provider.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace lemon::residency;
using namespace std::chrono_literals;

namespace {

template <typename T, typename = void>
struct HasPhase : std::false_type {};

template <typename T>
struct HasPhase<T, std::void_t<decltype(T::phase)>> : std::true_type {};

template <typename T, typename = void>
struct HasLifecycleState : std::false_type {};

template <typename T>
struct HasLifecycleState<T, std::void_t<decltype(T::lifecycle_state)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasAttestation : std::false_type {};

template <typename T>
struct HasAttestation<T, std::void_t<decltype(T::attestation)>>
    : std::true_type {};

static_assert(!HasPhase<ProfilingIntervalSegment>::value);
static_assert(!HasLifecycleState<ProfilingIntervalSegment>::value);
static_assert(!HasAttestation<ProfilingIntervalSegment>::value);
static_assert(noexcept(
    std::declval<ProfilingIntervalObservationSource &>().finish(
        ProfilingRawIntervalToken{}, ProfilingEventWatermark{})));

struct TestState {
    std::atomic<bool> ok{true};

    void require(bool condition, const char *message) {
        if (condition) return;
        ok.store(false);
        std::cerr << "FAIL: " << message << '\n';
    }
};

std::string digest(char value) { return std::string(64, value); }

ProfilingDerivationContract contract() {
    ProfilingDerivationContract value;
    value.provider_id = "provider/scripted-interval";
    value.provider_revision_sha256 = digest('e');
    value.sensors = {
        ProfilingSensorContract{"sensor/gtt-used", "gpu/gtt",
                                ClaimFamily::ConsumableCapacity, 10, 1000},
    };
    value.owner_scopes = {
        ProfilingOwnerScopeBinding{"owner/model-alpha", digest('f')},
    };
    value.freshness_window = 60s;
    value.max_source_skew = 20ms;
    value.interval.event_semantics_revision_sha256 = digest('9');
    value.interval.max_observation_gap = 50ms;
    value.interval.baseline_stability_window = 100ms;
    value.interval.release_stability_window = 200ms;
    value.interval.max_interval_frames = 128;
    return value;
}

ProfilingTransactionContext context(
    const ProfilingDerivationContract &derivation_contract = contract()) {
    const auto contract_sha256 =
        profiling_derivation_contract_sha256(derivation_contract);
    if (!contract_sha256) throw std::runtime_error("invalid test contract");

    ProfilingTransactionContext value;
    value.deployment_id = digest('a');
    value.sequence = 1;
    value.profiling_transaction_id = "profile/scripted-interval";
    value.selector_sha256 = digest('b');
    value.generations = OverlaySourceGenerations{1, 2, 3, 4, 5, 6, 7};
    value.observation_contract_sha256 = *contract_sha256;
    value.predictor_contract_sha256 = digest('d');
    return value;
}

ProfilingCollectionClock clock() {
    auto monotonic = std::make_shared<std::uint64_t>(0);
    auto utc = std::make_shared<std::uint64_t>(1000);
    ProfilingCollectionClock value;
    value.monotonic_now = [monotonic] {
        const auto result = std::chrono::steady_clock::time_point{} +
                            std::chrono::milliseconds(*monotonic);
        *monotonic += 5;
        return result;
    };
    value.utc_now = [utc] {
        const auto result = std::chrono::system_clock::time_point{} +
                            std::chrono::seconds(*utc);
        ++*utc;
        return result;
    };
    return value;
}

ProfilingCollectionClock clock_at(std::uint64_t utc_seconds) {
    auto value = clock();
    auto utc = std::make_shared<std::uint64_t>(utc_seconds);
    value.utc_now = [utc] {
        const auto result = std::chrono::system_clock::time_point{} +
                            std::chrono::seconds(*utc);
        ++*utc;
        return result;
    };
    return value;
}

ProfilingRawIntervalFrame frame(std::uint64_t watermark,
                                std::uint64_t total,
                                std::uint64_t owner) {
    ProfilingRawIntervalFrame value;
    value.source_epoch_sha256 = digest('1');
    const auto owner_scope_set =
        profiling_owner_scope_set_sha256(contract().owner_scopes);
    if (!owner_scope_set) throw std::runtime_error("invalid owner scope set");
    value.owner_scope_set_sha256 = *owner_scope_set;
    value.event_semantics_revision_sha256 = digest('9');
    value.event_watermark = ProfilingEventWatermark{watermark};
    value.samples = {
        ProfilingRawSample{"sensor/gtt-used", std::nullopt, total, watermark},
        ProfilingRawSample{"sensor/gtt-used", "owner/model-alpha", owner,
                           watermark},
    };
    return value;
}

ProfilingRawIntervalBatch batch(std::uint64_t after, std::uint64_t through,
                                std::vector<ProfilingRawIntervalFrame> events,
                                ProfilingRawIntervalFrame checkpoint) {
    return ProfilingRawIntervalBatch{
        ProfilingIntervalSourceError::None,
        ProfilingEventWatermark{after},
        ProfilingEventWatermark{through},
        std::move(events),
        std::move(checkpoint),
        {},
    };
}

class ScriptedIntervalSource final : public ProfilingIntervalObservationSource {
public:
    ProfilingRawIntervalBeginResult begin_result;
    std::deque<ProfilingRawIntervalBatch> reads;
    ProfilingRawIntervalBatch finish_result;
    bool throw_on_read = false;
    std::function<void()> on_begin;
    std::size_t begin_calls = 0;
    std::size_t read_calls = 0;
    std::size_t finish_calls = 0;

    ProfilingRawIntervalBeginResult
    begin(const ProfilingRawIntervalReadRequest &request,
          const ProfilingCancellationCheck &should_abort) override {
        ++begin_calls;
        last_request = request;
        if (should_abort && should_abort()) {
            return ProfilingRawIntervalBeginResult{
                ProfilingIntervalSourceError::Cancelled,
                {},
                {},
                "scripted begin cancelled",
            };
        }
        if (on_begin) on_begin();
        return begin_result;
    }

    ProfilingRawIntervalBatch
    read_since(ProfilingRawIntervalToken token,
               ProfilingEventWatermark after_event_watermark,
               const ProfilingCancellationCheck &should_abort) override {
        ++read_calls;
        last_token = token;
        last_after = after_event_watermark;
        if (throw_on_read) throw std::runtime_error("scripted read failure");
        if (should_abort && should_abort()) {
            return ProfilingRawIntervalBatch{
                ProfilingIntervalSourceError::Cancelled,
                after_event_watermark,
                after_event_watermark,
                {},
                {},
                "scripted read cancelled",
            };
        }
        if (reads.empty()) {
            return ProfilingRawIntervalBatch{
                ProfilingIntervalSourceError::Unavailable,
                after_event_watermark,
                after_event_watermark,
                {},
                {},
                "script exhausted",
            };
        }
        auto result = std::move(reads.front());
        reads.pop_front();
        return result;
    }

    ProfilingRawIntervalBatch
    finish(ProfilingRawIntervalToken token,
           ProfilingEventWatermark after_event_watermark) noexcept override {
        ++finish_calls;
        last_token = token;
        last_after = after_event_watermark;
        return finish_result;
    }

    ProfilingRawIntervalReadRequest last_request;
    ProfilingRawIntervalToken last_token;
    ProfilingEventWatermark last_after;
};

ScriptedIntervalSource source() {
    ScriptedIntervalSource value;
    value.begin_result = ProfilingRawIntervalBeginResult{
        ProfilingIntervalSourceError::None,
        ProfilingRawIntervalToken{7},
        frame(10, 100, 100),
        {},
    };
    value.reads.push_back(batch(
        10, 12, {frame(11, 200, 200), frame(12, 300, 300)},
        frame(12, 300, 300)));
    value.finish_result =
        batch(12, 13, {frame(13, 100, 100)}, frame(13, 100, 100));
    return value;
}

const ClaimFamilyClosure *family(const std::vector<ClaimFamilyClosure> &claims,
                                 ClaimFamily wanted) {
    for (const auto &candidate : claims) {
        if (candidate.family == wanted) return &candidate;
    }
    return nullptr;
}

void require_capacity(TestState &state,
                      const std::vector<ClaimFamilyClosure> &claims,
                      std::uint64_t expected, const char *message) {
    const auto *capacity = family(claims, ClaimFamily::ConsumableCapacity);
    const bool matches = capacity != nullptr &&
                         capacity->completeness == ClaimCompleteness::Bounded &&
                         capacity->entries.size() == 1 &&
                         capacity->entries.front().constraint_id == "gpu/gtt" &&
                         capacity->entries.front().unit == ClaimUnit::Bytes &&
                         capacity->entries.front().amount == expected;
    state.require(matches, message);
}

void require_capacity_zero(TestState &state,
                           const std::vector<ClaimFamilyClosure> &claims,
                           const char *message) {
    const auto *capacity = family(claims, ClaimFamily::ConsumableCapacity);
    state.require(capacity != nullptr &&
                      capacity->completeness == ClaimCompleteness::KnownZero &&
                      capacity->entries.empty(),
                  message);
}

void test_records_complete_interval_segments(TestState &state) {
    auto scripted = source();
    ProfilingIntervalRecorder recorder(contract(), scripted, clock());

    const auto started = recorder.begin(context(), [] { return false; });
    state.require(started.ok() && !started.segment.has_value() &&
                      recorder.active(),
                  "a valid atomic checkpoint opens the interval");
    const auto owner_scope_set =
        profiling_owner_scope_set_sha256(contract().owner_scopes);
    state.require(owner_scope_set.has_value() &&
                      scripted.last_request.read.owner_scope_set_sha256 ==
                          *owner_scope_set &&
                      scripted.last_request.event_semantics_revision_sha256 ==
                          digest('9'),
                  "the recorder binds containment and event semantics");

    const auto checkpoint = recorder.checkpoint([] { return false; });
    state.require(checkpoint.ok() && checkpoint.segment.has_value(),
                  "a complete retained batch closes one segment");
    if (checkpoint.segment) {
        const auto &segment = *checkpoint.segment;
        state.require(segment.after_event_watermark.value == 10 &&
                          segment.through_event_watermark.value == 12 &&
                          segment.first_capture_generation == 1 &&
                          segment.last_capture_generation == 4 &&
                          segment.frame_count == 4,
                      "the segment keeps separate event and capture order");
        state.require(segment.checkpoint.capture_generation == 4 &&
                          segment.checkpoint.event_watermark.value == 12 &&
                          segment.checkpoint.observation.source_generation ==
                              12,
                      "the checkpoint binds both ordering domains");
        require_capacity(state, segment.peak_observed_claims, 300,
                         "the segment derives the hidden workload peak");
        require_capacity(state, segment.peak_attributed_claims, 300,
                         "the segment peak remains owner attributed");
        require_capacity_zero(state, segment.external_change_claims,
                              "complete event history proves no external change");
        require_capacity_zero(state, segment.unattributed_claims,
                              "every interval frame proves current owner closure");
        require_capacity(state, segment.uncertainty_claims, 10,
                         "the segment retains maximum uncertainty");
        require_capacity(state, segment.safety_margin_claims, 690,
                         "the segment retains minimum safety slack");
        state.require(segment.provenance_sha256.size() == 64,
                      "the segment binds its ordered transcript");
    }

    const auto finished = recorder.finish();
    state.require(finished.ok() && finished.segment.has_value() &&
                      !recorder.active() && scripted.finish_calls == 1,
                  "finish drains and unregisters the interval once");
    if (finished.segment) {
        state.require(finished.segment->after_event_watermark.value == 12 &&
                          finished.segment->through_event_watermark.value ==
                              13 &&
                          finished.segment->checkpoint.capture_generation == 6,
                      "the final segment begins at the prior atomic boundary");
    }
}

void test_zero_event_watermark_is_not_a_capture_generation(TestState &state) {
    auto scripted = source();
    scripted.begin_result.checkpoint = frame(0, 100, 100);
    scripted.finish_result = batch(0, 0, {}, frame(0, 100, 100));
    ProfilingIntervalRecorder recorder(contract(), scripted, clock());

    const auto started = recorder.begin(context(), [] { return false; });
    const auto finished = recorder.finish();

    state.require(started.ok() && finished.ok() && finished.segment.has_value(),
                  "a source-native zero watermark remains a valid cursor");
    if (finished.segment) {
        state.require(finished.segment->checkpoint.event_watermark.value == 0 &&
                          finished.segment->checkpoint.capture_generation == 2,
                      "Server capture order stays independent from watermark zero");
    }
}

void test_poll_retains_history_for_the_next_checkpoint(TestState &state) {
    auto scripted = source();
    scripted.reads.push_back(batch(
        12, 13, {frame(13, 400, 400)}, frame(13, 400, 400)));
    scripted.finish_result =
        batch(13, 14, {frame(14, 100, 100)}, frame(14, 100, 100));
    ProfilingIntervalRecorder recorder(contract(), scripted, clock());

    const auto started = recorder.begin(context(), [] { return false; });
    const auto polled = recorder.poll([] { return false; });
    const auto checkpointed = recorder.checkpoint([] { return false; });

    state.require(started.ok() && polled.ok() &&
                      !polled.segment.has_value() && recorder.active() &&
                      checkpointed.segment.has_value() &&
                      scripted.last_after.value == 12,
                  "poll advances the cursor without closing the interval");
    if (checkpointed.segment) {
        const auto &segment = *checkpointed.segment;
        state.require(segment.after_event_watermark.value == 10 &&
                          segment.through_event_watermark.value == 13 &&
                          segment.first_capture_generation == 1 &&
                          segment.last_capture_generation == 6 &&
                          segment.frame_count == 6 &&
                          segment.provenance_sha256.size() == 64,
                      "the next checkpoint retains every polled frame");
        require_capacity(state, segment.peak_observed_claims, 400,
                         "the checkpoint retains the polled peak");
    }

    const auto finished = recorder.finish();
    state.require(finished.ok() && !recorder.active() &&
                      scripted.finish_calls == 1 &&
                      scripted.last_token.opaque_id == 7 &&
                      scripted.last_after.value == 13,
                  "finish drains the owned token at the final cursor");

    auto alternate_source = source();
    alternate_source.reads.front().event_frames.front() =
        frame(11, 250, 250);
    alternate_source.reads.push_back(batch(
        12, 13, {frame(13, 400, 400)}, frame(13, 400, 400)));
    alternate_source.finish_result =
        batch(13, 14, {frame(14, 100, 100)}, frame(14, 100, 100));
    ProfilingIntervalRecorder alternate(contract(), alternate_source, clock());
    const auto alternate_started =
        alternate.begin(context(), [] { return false; });
    const auto alternate_polled = alternate.poll([] { return false; });
    const auto alternate_checkpointed =
        alternate.checkpoint([] { return false; });

    state.require(alternate_started.ok() && alternate_polled.ok() &&
                      alternate_checkpointed.segment.has_value() &&
                      checkpointed.segment.has_value() &&
                      alternate_checkpointed.segment->frame_count ==
                          checkpointed.segment->frame_count &&
                      alternate_checkpointed.segment->through_event_watermark ==
                          checkpointed.segment->through_event_watermark &&
                      alternate_checkpointed.segment->provenance_sha256 !=
                          checkpointed.segment->provenance_sha256,
                  "polled frames change provenance even below the same peak");
    if (alternate_checkpointed.segment) {
        require_capacity(state,
                         alternate_checkpointed.segment->peak_observed_claims,
                         400, "the alternate transcript has the same peak");
        require_capacity(
            state, alternate_checkpointed.segment->uncertainty_claims, 10,
            "the alternate transcript has the same uncertainty");
        require_capacity(
            state, alternate_checkpointed.segment->safety_margin_claims, 590,
            "the alternate transcript has the same safety margin");
    }
    const auto alternate_finished = alternate.finish();
    state.require(alternate_finished.ok() &&
                      alternate_source.finish_calls == 1,
                  "the alternate transcript also drains exactly once");
}

void test_transient_external_demand_fails_closed(TestState &state) {
    auto scripted = source();
    scripted.reads.clear();
    scripted.reads.push_back(batch(
        10, 12, {frame(11, 200, 100), frame(12, 100, 100)},
        frame(12, 100, 100)));
    ProfilingIntervalRecorder recorder(contract(), scripted, clock());

    const auto started = recorder.begin(context(), [] { return false; });
    const auto result = recorder.checkpoint([] { return false; });

    state.require(started.ok() &&
                      result.status ==
                          ProfilingIntervalRecorderStatus::InvalidObservation &&
                      !result.segment.has_value(),
                  "transient external demand invalidates the whole interval");
    state.require(!recorder.active() && scripted.finish_calls == 1,
                  "invalid history is drained before returning failure");
}

void test_rejects_incomplete_or_rebound_history(TestState &state) {
    struct Mutation {
        const char *message;
        std::function<void(ProfilingRawIntervalBatch &)> apply;
    };
    const std::vector<Mutation> mutations{
        {"a missing retained event invalidates the interval",
         [](auto &value) { value.event_frames.erase(value.event_frames.begin()); }},
        {"an out-of-order event invalidates the interval",
         [](auto &value) {
             value.event_frames.front().event_watermark =
                 ProfilingEventWatermark{12};
         }},
        {"a source epoch change invalidates the interval",
         [](auto &value) {
             value.event_frames.front().source_epoch_sha256 = digest('2');
         }},
        {"an owner-containment change invalidates the interval",
         [](auto &value) {
             value.event_frames.front().owner_scope_set_sha256 = digest('2');
         }},
        {"an event-semantics change invalidates the interval",
         [](auto &value) {
             value.event_frames.front().event_semantics_revision_sha256 =
                 digest('2');
         }},
        {"a sample-generation mismatch invalidates the interval",
         [](auto &value) {
             value.event_frames.front().samples.front().source_generation = 99;
         }},
        {"a mismatched atomic checkpoint invalidates the interval",
         [](auto &value) {
             value.checkpoint.event_watermark = ProfilingEventWatermark{11};
         }},
        {"a stale requested cursor invalidates the interval",
         [](auto &value) {
             value.after_event_watermark = ProfilingEventWatermark{9};
         }},
        {"a changed no-event checkpoint cannot reuse its watermark",
         [](auto &value) {
             value.through_event_watermark = value.after_event_watermark;
             value.event_frames.clear();
             value.checkpoint = frame(10, 300, 300);
         }},
        {"a changed checkpoint cannot reuse the final event watermark",
         [](auto &value) { value.checkpoint = frame(12, 250, 250); }},
    };

    for (const auto &mutation : mutations) {
        auto scripted = source();
        mutation.apply(scripted.reads.front());
        ProfilingIntervalRecorder recorder(contract(), scripted, clock());
        const auto started = recorder.begin(context(), [] { return false; });
        const auto result = recorder.checkpoint([] { return false; });
        state.require(started.ok() &&
                          result.status == ProfilingIntervalRecorderStatus::
                                               InvalidObservation &&
                          !result.segment.has_value(),
                      mutation.message);
        state.require(scripted.finish_calls == 1 && !recorder.active(),
                      "invalid history is always drained");
    }
}

void test_segment_provenance_binds_checkpoint_evidence(TestState &state) {
    auto first_source = source();
    auto second_source = source();
    ProfilingIntervalRecorder first(contract(), first_source, clock_at(1000));
    ProfilingIntervalRecorder second(contract(), second_source, clock_at(2000));

    const auto first_started = first.begin(context(), [] { return false; });
    const auto second_started = second.begin(context(), [] { return false; });
    const auto first_segment = first.checkpoint([] { return false; });
    const auto second_segment = second.checkpoint([] { return false; });

    state.require(first_started.ok() && second_started.ok() &&
                      first_segment.segment.has_value() &&
                      second_segment.segment.has_value() &&
                      first_segment.segment->checkpoint.observation.observed_at !=
                          second_segment.segment->checkpoint.observation.observed_at &&
                      first_segment.segment->provenance_sha256 !=
                          second_segment.segment->provenance_sha256,
                  "segment provenance binds checkpoint timing evidence");
}

void test_enforces_cadence_and_frame_bound(TestState &state) {
    {
        auto monotonic = std::make_shared<
            std::deque<std::chrono::steady_clock::time_point>>();
        monotonic->push_back(std::chrono::steady_clock::time_point{});
        monotonic->push_back(std::chrono::steady_clock::time_point{} + 5ms);
        monotonic->push_back(std::chrono::steady_clock::time_point{} + 100ms);
        ProfilingCollectionClock delayed_clock;
        delayed_clock.monotonic_now = [monotonic] {
            if (monotonic->empty()) {
                throw std::runtime_error("scripted clock exhausted");
            }
            const auto result = monotonic->front();
            monotonic->pop_front();
            return result;
        };
        delayed_clock.utc_now = [] {
            return std::chrono::system_clock::time_point{} + 1000s;
        };
        auto scripted = source();
        ProfilingIntervalRecorder recorder(contract(), scripted,
                                            std::move(delayed_clock));
        const auto started = recorder.begin(context(), [] { return false; });
        const auto result = recorder.checkpoint([] { return false; });
        state.require(started.ok() &&
                          result.status == ProfilingIntervalRecorderStatus::
                                               InvalidObservation &&
                          scripted.read_calls == 0 &&
                          scripted.finish_calls == 1,
                      "an observation cadence gap fails before another read");
    }

    {
        auto bounded_contract = contract();
        bounded_contract.interval.max_interval_frames = 3;
        auto scripted = source();
        ProfilingIntervalRecorder recorder(bounded_contract, scripted, clock());
        const auto started =
            recorder.begin(context(bounded_contract), [] { return false; });
        const auto result = recorder.checkpoint([] { return false; });
        state.require(started.ok() &&
                          result.status == ProfilingIntervalRecorderStatus::
                                               InvalidObservation &&
                          scripted.finish_calls == 1,
                      "the whole interval cannot exceed its retained-frame bound");
    }
}

void test_source_failures_discard_interval(TestState &state) {
    {
        auto scripted = source();
        scripted.reads.front().error =
            ProfilingIntervalSourceError::HistoryLost;
        scripted.reads.front().diagnostic = "scripted history overflow";
        ProfilingIntervalRecorder recorder(contract(), scripted, clock());
        const auto started = recorder.begin(context(), [] { return false; });
        const auto result = recorder.checkpoint([] { return false; });
        state.require(started.ok() &&
                          result.status == ProfilingIntervalRecorderStatus::
                                               EvidenceUnavailable &&
                          !result.segment.has_value() &&
                          scripted.finish_calls == 1,
                      "reported history loss discards the whole interval");
    }

    {
        auto scripted = source();
        scripted.finish_result.error =
            ProfilingIntervalSourceError::HistoryLost;
        scripted.finish_result.diagnostic = "scripted final history loss";
        ProfilingIntervalRecorder recorder(contract(), scripted, clock());
        const auto started = recorder.begin(context(), [] { return false; });
        const auto result = recorder.finish();
        state.require(started.ok() &&
                          result.status == ProfilingIntervalRecorderStatus::
                                               EvidenceUnavailable &&
                          !result.segment.has_value() && !recorder.active() &&
                          scripted.finish_calls == 1,
                      "a failed final drain cannot yield a segment");
    }

    {
        auto monotonic = std::make_shared<
            std::deque<std::chrono::steady_clock::time_point>>();
        monotonic->push_back(std::chrono::steady_clock::time_point{});
        monotonic->push_back(std::chrono::steady_clock::time_point{} + 5ms);
        monotonic->push_back(std::chrono::steady_clock::time_point{} + 10ms);
        ProfilingCollectionClock failing_clock;
        failing_clock.monotonic_now = [monotonic] {
            if (monotonic->empty()) {
                throw std::runtime_error("scripted clock exhausted");
            }
            const auto result = monotonic->front();
            monotonic->pop_front();
            return result;
        };
        failing_clock.utc_now = [] {
            return std::chrono::system_clock::time_point{} + 1000s;
        };
        auto scripted = source();
        ProfilingIntervalRecorder recorder(contract(), scripted,
                                            std::move(failing_clock));
        const auto started = recorder.begin(context(), [] { return false; });
        const auto result = recorder.finish();
        state.require(started.ok() &&
                          result.status == ProfilingIntervalRecorderStatus::
                                               InvalidObservation &&
                          !recorder.active() && scripted.finish_calls == 1,
                      "a post-drain clock failure does not finish twice");
    }
}

void test_failures_and_destruction_release_source(TestState &state) {
    {
        auto monotonic = std::make_shared<
            std::deque<std::chrono::steady_clock::time_point>>();
        monotonic->push_back(std::chrono::steady_clock::time_point{});
        ProfilingCollectionClock failing_clock;
        failing_clock.monotonic_now = [monotonic] {
            if (monotonic->empty()) {
                throw std::runtime_error("scripted post-begin clock failure");
            }
            const auto result = monotonic->front();
            monotonic->pop_front();
            return result;
        };
        failing_clock.utc_now = [] {
            return std::chrono::system_clock::time_point{} + 1000s;
        };
        auto scripted = source();
        ProfilingIntervalRecorder recorder(contract(), scripted,
                                            std::move(failing_clock));
        const auto result = recorder.begin(context(), [] { return false; });
        state.require(result.status ==
                              ProfilingIntervalRecorderStatus::InvalidObservation &&
                          scripted.finish_calls == 1 && !recorder.active(),
                      "a post-begin clock failure drains the registered interval");
    }

    {
        std::atomic<bool> abort{false};
        auto scripted = source();
        scripted.on_begin = [&abort] { abort.store(true); };
        ProfilingIntervalRecorder recorder(contract(), scripted, clock());
        const auto result = recorder.begin(
            context(), [&abort] { return abort.load(); });
        state.require(result.status ==
                              ProfilingIntervalRecorderStatus::Cancelled &&
                          scripted.finish_calls == 1 && !recorder.active(),
                      "post-registration cancellation drains the interval");
    }

    {
        auto scripted = source();
        scripted.throw_on_read = true;
        ProfilingIntervalRecorder recorder(contract(), scripted, clock());
        const auto started = recorder.begin(context(), [] { return false; });
        const auto result = recorder.poll([] { return false; });
        state.require(started.ok() &&
                          result.status == ProfilingIntervalRecorderStatus::
                                               EvidenceUnavailable &&
                          scripted.finish_calls == 1 && !recorder.active(),
                      "source exceptions trigger uncancellable cleanup");
    }

    auto scripted = source();
    {
        ProfilingIntervalRecorder recorder(contract(), scripted, clock());
        const auto started = recorder.begin(context(), [] { return false; });
        state.require(started.ok() && recorder.active(),
                      "the cleanup fixture owns an active interval");
    }
    state.require(scripted.finish_calls == 1,
                  "recorder destruction drains an abandoned interval");
}

void test_default_recorder_is_unavailable(TestState &state) {
    ProfilingIntervalRecorder recorder(contract(), clock());
    const auto result = recorder.begin(context(), [] { return false; });
    state.require(result.status ==
                          ProfilingIntervalRecorderStatus::EvidenceUnavailable &&
                      !result.segment.has_value() && !recorder.active(),
                  "the production recorder defaults to unavailable");
}

} // namespace

int main() {
    TestState state;
    test_default_recorder_is_unavailable(state);
    test_records_complete_interval_segments(state);
    test_zero_event_watermark_is_not_a_capture_generation(state);
    test_poll_retains_history_for_the_next_checkpoint(state);
    test_transient_external_demand_fails_closed(state);
    test_rejects_incomplete_or_rebound_history(state);
    test_segment_provenance_binds_checkpoint_evidence(state);
    test_enforces_cadence_and_frame_bound(state);
    test_source_failures_discard_interval(state);
    test_failures_and_destruction_release_source(state);
    return state.ok.load() ? 0 : 1;
}
