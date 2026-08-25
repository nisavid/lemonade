#include "lemon/residency/profiling_provider.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono_literals;
using namespace lemon::residency;

template <typename T, typename = void>
struct HasClaimFamily : std::false_type {};
template <typename T>
struct HasClaimFamily<T, std::void_t<decltype(std::declval<T>().claim_family)>>
    : std::true_type {};

template <typename T, typename = void> struct HasUnit : std::false_type {};
template <typename T>
struct HasUnit<T, std::void_t<decltype(std::declval<T>().unit)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasObservedAt : std::false_type {};
template <typename T>
struct HasObservedAt<T, std::void_t<decltype(std::declval<T>().observed_at)>>
    : std::true_type {};

template <typename T, typename = void> struct HasHealth : std::false_type {};
template <typename T>
struct HasHealth<T, std::void_t<decltype(std::declval<T>().health)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasCompleteness : std::false_type {};
template <typename T>
struct HasCompleteness<T, std::void_t<decltype(std::declval<T>().completeness)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasSafetyMargin : std::false_type {};
template <typename T>
struct HasSafetyMargin<T,
                       std::void_t<decltype(std::declval<T>().safety_margin)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasFreshUntil : std::false_type {};
template <typename T>
struct HasFreshUntil<T, std::void_t<decltype(std::declval<T>().fresh_until)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasOwnerCoverage : std::false_type {};
template <typename T>
struct HasOwnerCoverage<T,
                        std::void_t<decltype(std::declval<T>().owner_coverage)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasLifecycleState : std::false_type {};
template <typename T>
struct HasLifecycleState<
    T, std::void_t<decltype(std::declval<T>().lifecycle_state)>>
    : std::true_type {};

template <typename T, typename = void> struct HasPhase : std::false_type {};
template <typename T>
struct HasPhase<T, std::void_t<decltype(std::declval<T>().phase)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasUncertaintyClaims : std::false_type {};
template <typename T>
struct HasUncertaintyClaims<
    T, std::void_t<decltype(std::declval<T>().uncertainty_claims)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasSafetyMarginClaims : std::false_type {};
template <typename T>
struct HasSafetyMarginClaims<
    T, std::void_t<decltype(std::declval<T>().safety_margin_claims)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasExternalChangeClaims : std::false_type {};
template <typename T>
struct HasExternalChangeClaims<
    T, std::void_t<decltype(std::declval<T>().external_change_claims)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasUnattributedClaims : std::false_type {};
template <typename T>
struct HasUnattributedClaims<
    T, std::void_t<decltype(std::declval<T>().unattributed_claims)>>
    : std::true_type {};

template <typename T, typename = void>
struct HasAttestation : std::false_type {};
template <typename T>
struct HasAttestation<T, std::void_t<decltype(std::declval<T>().attestation)>>
    : std::true_type {};

static_assert(
    std::is_same_v<decltype(ProfilingRawSample::sensor_id), std::string>);
static_assert(std::is_same_v<decltype(ProfilingRawSample::owner_scope_id),
                             std::optional<std::string>>);
static_assert(
    std::is_same_v<decltype(ProfilingRawSample::value), std::uint64_t>);
static_assert(std::is_same_v<decltype(ProfilingRawSample::source_generation),
                             std::uint64_t>);
static_assert(!HasClaimFamily<ProfilingRawSample>::value);
static_assert(!HasUnit<ProfilingRawSample>::value);
static_assert(!HasObservedAt<ProfilingRawSample>::value);
static_assert(!HasHealth<ProfilingRawSample>::value);
static_assert(!HasCompleteness<ProfilingRawSample>::value);
static_assert(!HasSafetyMargin<ProfilingRawSample>::value);
static_assert(!HasFreshUntil<ProfilingRawSample>::value);
static_assert(!HasOwnerCoverage<ProfilingRawSample>::value);
static_assert(!HasLifecycleState<ProfilingRawSample>::value);
static_assert(!HasUncertaintyClaims<ProfilingRawSample>::value);
static_assert(!HasAttestation<ProfilingRawSample>::value);
static_assert(!HasHealth<ProfilingRawReadResult>::value);
static_assert(!HasFreshUntil<ProfilingRawReadResult>::value);
static_assert(!HasOwnerCoverage<ProfilingRawReadResult>::value);
static_assert(!HasLifecycleState<ProfilingRawReadResult>::value);
static_assert(!HasUncertaintyClaims<ProfilingRawReadResult>::value);
static_assert(!HasAttestation<ProfilingRawReadResult>::value);
static_assert(!HasUncertaintyClaims<ProfilingDerivationContract>::value);
static_assert(!HasSafetyMarginClaims<ProfilingDerivationContract>::value);
static_assert(!HasPhase<ProfilingDerivedObservation>::value);
static_assert(!HasLifecycleState<ProfilingDerivedObservation>::value);
static_assert(!HasAttestation<ProfilingDerivedObservation>::value);
static_assert(!HasExternalChangeClaims<ProfilingDerivedObservation>::value);
static_assert(!HasUnattributedClaims<ProfilingDerivedObservation>::value);
static_assert(!HasPhase<ProfilingObservationCollectionResult>::value);
static_assert(!HasLifecycleState<ProfilingObservationCollectionResult>::value);
static_assert(!HasAttestation<ProfilingObservationCollectionResult>::value);
static_assert(!HasHealth<ProfilingRawIntervalFrame>::value);
static_assert(!HasOwnerCoverage<ProfilingRawIntervalFrame>::value);
static_assert(!HasLifecycleState<ProfilingRawIntervalFrame>::value);
static_assert(!HasAttestation<ProfilingRawIntervalFrame>::value);
static_assert(
    std::is_same_v<decltype(ProfilingSensorContract::uncertainty_bound),
                   std::uint64_t>);
static_assert(std::is_same_v<decltype(ProfilingSensorContract::safety_ceiling),
                             std::uint64_t>);
static_assert(!noexcept(std::declval<ProfilingObservationCollector &>().collect(
    std::declval<const ProfilingTransactionContext &>(),
    std::declval<const ProfilingCancellationCheck &>())));

struct TestState {
    std::atomic<bool> ok{true};

    void require(bool condition, const char *message) {
        if (condition) return;
        ok.store(false);
        std::cerr << "FAIL: " << message << '\n';
    }
};

void test_default_source_is_unavailable(TestState &state) {
    UnavailableProfilingObservationSource source;
    ProfilingRawReadRequest request{{"sensor/gtt-used"},
                                    {"owner/model-alpha"},
                                    std::string(64, 'c')};

    const auto result = source.read(request, [] { return false; });

    state.require(result.error == ProfilingSourceError::Unavailable,
                  "the production source defaults to unavailable");
    state.require(result.samples.empty(),
                  "the unavailable source cannot return partial samples");
    state.require(result.diagnostic ==
                      "profiling observation source is unavailable",
                  "the unavailable source reports a stable diagnostic");
}

void test_default_interval_source_is_unavailable(TestState &state) {
    UnavailableProfilingIntervalObservationSource source;
    ProfilingRawIntervalReadRequest request;
    request.read = ProfilingRawReadRequest{{"sensor/gtt-used"},
                                          {"owner/model-alpha"},
                                          std::string(64, 'c')};
    request.event_semantics_revision_sha256 = std::string(64, 'd');

    const auto result = source.begin(request, [] { return false; });

    state.require(result.error == ProfilingIntervalSourceError::Unavailable,
                  "the production interval source defaults to unavailable");
    state.require(result.token.opaque_id == 0,
                  "the unavailable interval source cannot return a token");
    state.require(result.checkpoint.samples.empty(),
                  "the unavailable interval source cannot return a checkpoint");
    state.require(result.diagnostic ==
                      "profiling interval observation source is unavailable",
                  "the unavailable interval source reports a stable diagnostic");

    const auto batch = source.read_since(
        {}, ProfilingEventWatermark{41}, [] { return false; });
    state.require(batch.error == ProfilingIntervalSourceError::Unavailable &&
                      batch.after_event_watermark.value == 41 &&
                      batch.through_event_watermark.value == 41 &&
                      batch.event_frames.empty() &&
                      batch.checkpoint.samples.empty(),
                  "the unavailable interval source cannot return history");

    const auto finished = source.finish({}, ProfilingEventWatermark{42});
    state.require(finished.error ==
                          ProfilingIntervalSourceError::Unavailable &&
                      finished.after_event_watermark.value == 42 &&
                      finished.through_event_watermark.value == 42 &&
                      finished.event_frames.empty() &&
                      finished.checkpoint.samples.empty(),
                  "the unavailable interval source cannot claim a final drain");
}

std::string digest(char value) { return std::string(64, value); }

constexpr char known_contract_sha256[] =
    "a62d1c4f2bd4345138b4cffd52e29da00bd1d06788831c4cb7fbe5dded344870";

ProfilingDerivationContract contract() {
    ProfilingDerivationContract value;
    value.provider_id = "provider/scripted-raw";
    value.provider_revision_sha256 = digest('e');
    value.sensors = {
        ProfilingSensorContract{"sensor/gtt-used", "gpu/gtt",
                                ClaimFamily::ConsumableCapacity, 512, 4864},
    };
    value.owner_scopes = {
        ProfilingOwnerScopeBinding{"owner/model-alpha", digest('f')},
    };
    value.freshness_window = std::chrono::seconds(60);
    value.max_source_skew = std::chrono::milliseconds(20);
    value.interval.event_semantics_revision_sha256 = digest('9');
    value.interval.max_observation_gap = std::chrono::milliseconds(50);
    value.interval.baseline_stability_window = std::chrono::milliseconds(100);
    value.interval.release_stability_window = std::chrono::milliseconds(200);
    value.interval.max_interval_frames = 128;
    return value;
}

ProfilingTransactionContext
context(const ProfilingDerivationContract &derivation_contract = contract()) {
    const auto contract_sha256 =
        profiling_derivation_contract_sha256(derivation_contract);
    if (!contract_sha256) {
        throw std::runtime_error("test derivation contract is invalid");
    }

    ProfilingTransactionContext value;
    value.deployment_id = digest('a');
    value.sequence = 1;
    value.profiling_transaction_id = "profile/raw-provider";
    value.selector_sha256 = digest('b');
    value.generations = OverlaySourceGenerations{1, 2, 3, 4, 5, 6, 7};
    value.observation_contract_sha256 = *contract_sha256;
    value.predictor_contract_sha256 = digest('d');
    return value;
}

ProfilingCollectionClock clock(
    std::chrono::milliseconds elapsed = std::chrono::milliseconds(10),
    std::chrono::system_clock::time_point utc =
        std::chrono::system_clock::time_point{} + std::chrono::seconds(1000)) {
    auto times =
        std::make_shared<std::deque<std::chrono::steady_clock::time_point>>();
    times->push_back(std::chrono::steady_clock::time_point{});
    times->push_back(std::chrono::steady_clock::time_point{} + elapsed);
    ProfilingCollectionClock value;
    value.monotonic_now = [times] {
        if (times->empty()) {
            throw std::runtime_error("scripted monotonic clock exhausted");
        }
        const auto result = times->front();
        times->pop_front();
        return result;
    };
    value.utc_now = [utc] { return utc; };
    return value;
}

ProfilingRawReadResult successful_read(std::uint64_t total = 4096,
                                       std::uint64_t owner = 4096,
                                       std::uint64_t generation = 7) {
    return ProfilingRawReadResult{
        ProfilingSourceError::None,
        {
            ProfilingRawSample{"sensor/gtt-used", std::nullopt, total,
                               generation},
            ProfilingRawSample{"sensor/gtt-used", "owner/model-alpha", owner,
                               generation},
        },
        {},
        {},
    };
}

class ScriptedProfilingObservationSource final
    : public ProfilingObservationSource {
public:
    explicit ScriptedProfilingObservationSource(
        std::deque<ProfilingRawReadResult> script,
        std::function<void()> on_read = {})
        : script_(std::move(script)), on_read_(std::move(on_read)) {}

    ProfilingRawReadResult
    read(const ProfilingRawReadRequest &request,
         const ProfilingCancellationCheck &should_abort) override {
        requests.push_back(request);
        if (on_read_) on_read_();
        if (should_abort && should_abort()) {
            return ProfilingRawReadResult{
                ProfilingSourceError::Cancelled, {},
                request.owner_scope_set_sha256, "scripted read cancelled"};
        }
        if (script_.empty()) {
            return ProfilingRawReadResult{
                ProfilingSourceError::Unavailable, {},
                request.owner_scope_set_sha256, "script exhausted"};
        }
        auto result = std::move(script_.front());
        script_.pop_front();
        if (result.owner_scope_set_sha256.empty()) {
            result.owner_scope_set_sha256 = request.owner_scope_set_sha256;
        }
        return result;
    }

    std::vector<ProfilingRawReadRequest> requests;

private:
    std::deque<ProfilingRawReadResult> script_;
    std::function<void()> on_read_;
};

class ThrowingProfilingObservationSource final
    : public ProfilingObservationSource {
public:
    ProfilingRawReadResult read(const ProfilingRawReadRequest &,
                                const ProfilingCancellationCheck &) override {
        throw std::runtime_error("adapter failure");
    }
};

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

void test_derivation_contract_identity(TestState &state) {
    const auto derivation_contract = contract();
    const auto identity =
        profiling_derivation_contract_sha256(derivation_contract);
    state.require(identity.has_value(),
                  "a valid derivation contract has an identity");
    if (identity) {
        state.require(
            *identity == known_contract_sha256,
            "the derivation contract identity uses the canonical encoding");
    }

    auto reordered = derivation_contract;
    reordered.sensors.push_back(ProfilingSensorContract{
        "sensor/vram-used", "gpu/vram", ClaimFamily::ConsumableCapacity, 1, 5});
    reordered.owner_scopes.push_back(
        ProfilingOwnerScopeBinding{"owner/model-beta", digest('8')});
    auto canonical = reordered;
    std::reverse(reordered.sensors.begin(), reordered.sensors.end());
    std::reverse(reordered.owner_scopes.begin(), reordered.owner_scopes.end());
    const auto canonical_identity =
        profiling_derivation_contract_sha256(canonical);
    const auto reordered_identity =
        profiling_derivation_contract_sha256(reordered);
    state.require(
        canonical_identity.has_value() && reordered_identity.has_value() &&
            canonical_identity == reordered_identity,
        "sensor and owner ordering does not change contract identity");
    const auto canonical_owner_set =
        profiling_owner_scope_set_sha256(canonical.owner_scopes);
    const auto reordered_owner_set =
        profiling_owner_scope_set_sha256(reordered.owner_scopes);
    state.require(canonical_owner_set.has_value() &&
                      canonical_owner_set == reordered_owner_set,
                  "owner-scope identity is canonical and order independent");

    auto invalid = derivation_contract;
    invalid.sensors.front().safety_ceiling =
        invalid.sensors.front().uncertainty_bound;
    state.require(!profiling_derivation_contract_sha256(invalid).has_value(),
                  "an invalid derivation contract has no identity");

    invalid = derivation_contract;
    invalid.owner_scopes.push_back(invalid.owner_scopes.front());
    invalid.owner_scopes.back().owner_scope_id = "owner/model-beta";
    state.require(!profiling_owner_scope_set_sha256(invalid.owner_scopes)
                       .has_value(),
                  "one containment identity cannot be counted twice");

    invalid = derivation_contract;
    invalid.interval.max_interval_frames =
        max_profiling_interval_frames + 1;
    state.require(!profiling_derivation_contract_sha256(invalid).has_value(),
                  "the interval frame budget has a finite contract bound");
}

void test_stale_contract_identity_fails_before_source_access(TestState &state) {
    struct Mutation {
        const char *message;
        std::function<void(ProfilingDerivationContract &)> apply;
    };
    const std::vector<Mutation> mutations{
        {"provider identity is bound",
         [](auto &value) { value.provider_id = "provider/scripted-raw-v2"; }},
        {"provider revision is bound",
         [](auto &value) { value.provider_revision_sha256 = digest('f'); }},
        {"sensor identity is bound",
         [](auto &value) {
             value.sensors.front().sensor_id = "sensor/gtt-used-v2";
         }},
        {"constraint mapping is bound",
         [](auto &value) {
             value.sensors.front().constraint_id = "gpu/gtt-v2";
         }},
        {"uncertainty policy is bound",
         [](auto &value) { value.sensors.front().uncertainty_bound = 513; }},
        {"safety policy is bound",
         [](auto &value) { value.sensors.front().safety_ceiling = 4865; }},
        {"owner scopes are bound",
         [](auto &value) {
             value.owner_scopes.front().owner_scope_id = "owner/model-beta";
         }},
        {"owner containment identity is bound",
         [](auto &value) {
             value.owner_scopes.front().containment_identity_sha256 =
                 digest('7');
         }},
        {"freshness policy is bound",
         [](auto &value) { value.freshness_window = 61s; }},
        {"source skew policy is bound",
         [](auto &value) { value.max_source_skew = 21ms; }},
        {"event semantics are bound",
         [](auto &value) {
             value.interval.event_semantics_revision_sha256 = digest('6');
         }},
        {"observation cadence is bound",
         [](auto &value) { value.interval.max_observation_gap = 51ms; }},
        {"baseline stability is bound",
         [](auto &value) { value.interval.baseline_stability_window = 101ms; }},
        {"release stability is bound",
         [](auto &value) { value.interval.release_stability_window = 201ms; }},
        {"interval frame retention is bound",
         [](auto &value) { value.interval.max_interval_frames = 129; }},
    };

    const auto stale_context = context();
    for (const auto &mutation : mutations) {
        auto changed_contract = contract();
        mutation.apply(changed_contract);
        const auto changed_identity =
            profiling_derivation_contract_sha256(changed_contract);
        state.require(changed_identity.has_value() &&
                          *changed_identity !=
                              stale_context.observation_contract_sha256,
                      "a stale-identity fixture remains a valid changed contract");
        ScriptedProfilingObservationSource source({successful_read()});
        ProfilingObservationCollector collector(std::move(changed_contract),
                                                source, clock());

        const auto result =
            collector.collect(stale_context, [] { return false; });

        state.require(result.status ==
                              ProfilingCollectionStatus::InvalidContract &&
                          !result.observation.has_value(),
                      mutation.message);
        state.require(source.requests.empty(),
                      "a stale contract identity cannot enter the source");
    }
}

void test_collector_derives_observation_from_raw_samples(TestState &state) {
    const auto derivation_contract = contract();
    auto transaction_context = context(derivation_contract);
    transaction_context.observation_contract_sha256 = known_contract_sha256;
    ScriptedProfilingObservationSource source({successful_read()});
    ProfilingObservationCollector collector(derivation_contract, source,
                                            clock());

    const auto result =
        collector.collect(transaction_context, [] { return false; });

    state.require(result.accepted(), "a closed raw batch is accepted");
    state.require(result.observation.has_value(),
                  "an accepted batch yields a derived observation");
    if (!result.observation) return;

    const auto &observation = *result.observation;
    state.require(observation.provider_id == "provider/scripted-raw",
                  "the Server assigns provider identity from its contract");
    state.require(
        observation.observation_contract_sha256 ==
            transaction_context.observation_contract_sha256,
        "the derived observation carries the matched contract identity");
    state.require(observation.source_generation == 7,
                  "the Server derives the coherent native generation");
    state.require(observation.observed_at == "1970-01-01T00:16:40Z",
                  "the Server assigns observation time");
    state.require(observation.fresh_until == "1970-01-01T00:17:40Z",
                  "the Server derives freshness");
    state.require(observation.source_skew_milliseconds == 10,
                  "the Server derives source skew");
    state.require(observation.health == ProfilingObservationHealth::Valid,
                  "the Server derives valid health only after closure");
    state.require(observation.owner_coverage ==
                      ProfilingOwnerCoverage::Complete,
                  "the Server derives complete owner coverage");
    require_capacity(state, observation.observed_claims, 4096,
                     "the Server derives observed claims");
    require_capacity(state, observation.attributed_claims, 4096,
                     "the Server derives attributed claims");
    require_capacity(state, observation.uncertainty_claims, 512,
                     "the Server derives uncertainty from policy");
    require_capacity(state, observation.safety_margin_claims, 256,
                     "the Server derives safety margin from policy");

    state.require(source.requests.size() == 1,
                  "the collector performs one deterministic read");
    if (!source.requests.empty()) {
        state.require(source.requests.front().sensor_ids ==
                          std::vector<std::string>{"sensor/gtt-used"},
                      "the Server selects source sensors");
        state.require(source.requests.front().owner_scope_ids ==
                          std::vector<std::string>{"owner/model-alpha"},
                      "the Server supplies owner scopes");
        const auto expected_owner_scope_set =
            profiling_owner_scope_set_sha256(derivation_contract.owner_scopes);
        state.require(expected_owner_scope_set.has_value() &&
                          source.requests.front().owner_scope_set_sha256 ==
                              *expected_owner_scope_set,
                      "the Server binds the requested containment scope set");
    }
}

void test_freshness_is_anchored_before_read(TestState &state) {
    auto utc = std::make_shared<std::chrono::system_clock::time_point>(
        std::chrono::system_clock::time_point{} + 1000s);
    auto collection_clock = clock();
    collection_clock.utc_now = [utc] { return *utc; };
    ScriptedProfilingObservationSource source({successful_read()}, [utc] {
        *utc = std::chrono::system_clock::time_point{} + 2000s;
    });
    ProfilingObservationCollector collector(contract(), source,
                                            std::move(collection_clock));

    const auto result = collector.collect(context(), [] { return false; });

    state.require(result.accepted() && result.observation.has_value(),
                  "a coherent observation remains accepted");
    if (!result.observation) return;
    state.require(result.observation->observed_at == "1970-01-01T00:16:40Z",
                  "observation time is captured before source access");
    state.require(result.observation->fresh_until == "1970-01-01T00:17:40Z",
                  "freshness starts at the oldest possible acquisition time");
}

void test_collector_fails_closed(TestState &state) {
    {
        auto invalid_cadence_contract = contract();
        invalid_cadence_contract.max_source_skew =
            invalid_cadence_contract.interval.max_observation_gap;
        auto transaction_context = context();
        const auto invalid_identity =
            profiling_derivation_contract_sha256(invalid_cadence_contract);
        state.require(!invalid_identity.has_value(),
                      "observation cadence must strictly exceed source skew");
        if (invalid_identity) {
            transaction_context.observation_contract_sha256 =
                *invalid_identity;
        }
        ScriptedProfilingObservationSource source({successful_read()});
        ProfilingObservationCollector collector(
            std::move(invalid_cadence_contract), source, clock());

        const auto result =
            collector.collect(transaction_context, [] { return false; });

        state.require(result.status ==
                              ProfilingCollectionStatus::InvalidContract &&
                          !result.observation.has_value(),
                      "equal source skew and observation cadence fail closed");
        state.require(source.requests.empty(),
                      "an impossible observation cadence cannot enter the source");
    }

    {
        auto invalid_cadence_contract = contract();
        invalid_cadence_contract.max_source_skew =
            invalid_cadence_contract.interval.max_observation_gap + 1ms;
        auto transaction_context = context();
        const auto invalid_identity =
            profiling_derivation_contract_sha256(invalid_cadence_contract);
        state.require(!invalid_identity.has_value(),
                      "observation cadence must exceed greater source skew");
        if (invalid_identity) {
            transaction_context.observation_contract_sha256 =
                *invalid_identity;
        }
        ScriptedProfilingObservationSource source({successful_read()});
        ProfilingObservationCollector collector(
            std::move(invalid_cadence_contract), source, clock());

        const auto result =
            collector.collect(transaction_context, [] { return false; });

        state.require(result.status ==
                              ProfilingCollectionStatus::InvalidContract &&
                          !result.observation.has_value(),
                      "source skew above observation cadence fails closed");
        state.require(source.requests.empty(),
                      "a negative observation budget cannot enter the source");
    }

    {
        auto invalid_contract = contract();
        invalid_contract.sensors.front().safety_ceiling =
            invalid_contract.sensors.front().uncertainty_bound;
        ScriptedProfilingObservationSource source({successful_read()});
        ProfilingObservationCollector collector(std::move(invalid_contract),
                                                source, clock());
        const auto result = collector.collect(context(), [] { return false; });
        state.require(result.status ==
                              ProfilingCollectionStatus::InvalidContract &&
                          !result.observation.has_value(),
                      "invalid Server policy fails before source access");
        state.require(source.requests.empty(),
                      "invalid safety policy cannot enter the source");
    }

    {
        auto discrete_contract = contract();
        discrete_contract.sensors.front().claim_family =
            ClaimFamily::CompatibilityExclusivity;
        ScriptedProfilingObservationSource source({successful_read()});
        ProfilingObservationCollector collector(std::move(discrete_contract),
                                                source, clock());
        const auto result = collector.collect(context(), [] { return false; });
        state.require(
            result.status == ProfilingCollectionStatus::InvalidContract &&
                !result.observation.has_value(),
            "discrete claim families are outside the capacity policy");
        state.require(source.requests.empty(),
                      "unsupported claim semantics cannot enter the source");
    }

    {
        auto invalid_freshness_contract = contract();
        invalid_freshness_contract.freshness_window = 1s;
        invalid_freshness_contract.max_source_skew = 1000ms;
        ScriptedProfilingObservationSource source({successful_read()});
        ProfilingObservationCollector collector(
            std::move(invalid_freshness_contract), source, clock());
        const auto result = collector.collect(context(), [] { return false; });
        state.require(result.status ==
                              ProfilingCollectionStatus::InvalidContract &&
                          !result.observation.has_value(),
                      "freshness must strictly exceed maximum source skew");
        state.require(source.requests.empty(),
                      "an incoherent freshness policy cannot enter the source");
    }

    {
        auto extreme_window_contract = contract();
        extreme_window_contract.freshness_window = std::chrono::seconds::max();
        const auto transaction_context = context(extreme_window_contract);
        ScriptedProfilingObservationSource source({successful_read()});
        ProfilingObservationCollector collector(
            std::move(extreme_window_contract), source, clock());
        const auto result =
            collector.collect(transaction_context, [] { return false; });
        state.require(result.status ==
                              ProfilingCollectionStatus::InvalidObservation &&
                          !result.observation.has_value(),
                      "an overflowing freshness window fails closed");
        state.require(
            source.requests.empty(),
            "an overflowing freshness deadline cannot enter the source");
    }

    {
        ScriptedProfilingObservationSource source({successful_read()});
        ProfilingObservationCollector collector(
            contract(), source,
            clock(10ms, std::chrono::system_clock::time_point::max()));
        const auto result = collector.collect(context(), [] { return false; });
        state.require(result.status ==
                              ProfilingCollectionStatus::InvalidObservation &&
                          !result.observation.has_value(),
                      "an extreme collection clock fails closed");
        state.require(source.requests.empty(),
                      "an invalid freshness deadline cannot enter the source");
    }

    {
        auto times = std::make_shared<
            std::deque<std::chrono::steady_clock::time_point>>();
        times->push_back(std::chrono::steady_clock::time_point::min());
        times->push_back(std::chrono::steady_clock::time_point::max());
        ProfilingCollectionClock extreme_monotonic_clock;
        extreme_monotonic_clock.monotonic_now = [times] {
            const auto result = times->front();
            times->pop_front();
            return result;
        };
        extreme_monotonic_clock.utc_now = [] {
            return std::chrono::system_clock::time_point{} + 1000s;
        };
        ScriptedProfilingObservationSource source({successful_read()});
        ProfilingObservationCollector collector(
            contract(), source, std::move(extreme_monotonic_clock));
        const auto result = collector.collect(context(), [] { return false; });
        state.require(result.status ==
                              ProfilingCollectionStatus::InvalidObservation &&
                          !result.observation.has_value(),
                      "an unrepresentable monotonic interval fails closed");
    }

    {
        ScriptedProfilingObservationSource source({successful_read()});
        ProfilingObservationCollector collector(
            contract(), source,
            clock(10ms, std::chrono::system_clock::time_point{} - 500ms));
        const auto result = collector.collect(context(), [] { return false; });
        state.require(result.status ==
                              ProfilingCollectionStatus::InvalidObservation &&
                          !result.observation.has_value(),
                      "a pre-epoch fractional collection clock fails closed");
        state.require(source.requests.empty(),
                      "an invalid pre-epoch clock cannot enter the source");
    }

    {
        ProfilingObservationCollector collector(contract(), clock());
        const auto result = collector.collect(context(), [] { return false; });
        state.require(result.status ==
                              ProfilingCollectionStatus::EvidenceUnavailable &&
                          !result.observation.has_value(),
                      "the production collector defaults to unavailable");
    }

    {
        auto read = successful_read();
        read.owner_scope_set_sha256 = digest('1');
        ScriptedProfilingObservationSource source({std::move(read)});
        ProfilingObservationCollector collector(contract(), source, clock());
        const auto result = collector.collect(context(), [] { return false; });
        state.require(result.status ==
                              ProfilingCollectionStatus::InvalidObservation &&
                          !result.observation.has_value(),
                      "a stale owner-containment binding fails closed");
    }

    {
        auto read = successful_read();
        read.error = ProfilingSourceError::Failed;
        read.diagnostic = std::string(300, 'x');
        ScriptedProfilingObservationSource source({std::move(read)});
        ProfilingObservationCollector collector(contract(), source, clock());
        const auto result = collector.collect(context(), [] { return false; });
        state.require(result.status ==
                              ProfilingCollectionStatus::EvidenceUnavailable &&
                          !result.observation.has_value(),
                      "source errors discard partial samples");
        state.require(result.diagnostic.size() ==
                          max_local_overlay_diagnostic_bytes,
                      "source diagnostics remain bounded");
    }

    {
        ThrowingProfilingObservationSource source;
        ProfilingObservationCollector collector(contract(), source, clock());
        const auto result = collector.collect(context(), [] { return false; });
        state.require(result.status ==
                              ProfilingCollectionStatus::EvidenceUnavailable &&
                          !result.observation.has_value(),
                      "adapter exceptions fail closed at the Server boundary");
    }

    {
        auto read = successful_read();
        read.samples.pop_back();
        ScriptedProfilingObservationSource source({std::move(read)});
        ProfilingObservationCollector collector(contract(), source, clock());
        const auto result = collector.collect(context(), [] { return false; });
        state.require(result.status ==
                              ProfilingCollectionStatus::InvalidObservation &&
                          !result.observation.has_value(),
                      "missing owner samples fail closed");
    }

    {
        auto read = successful_read();
        read.samples.back().owner_scope_id = "owner/external";
        ScriptedProfilingObservationSource source({std::move(read)});
        ProfilingObservationCollector collector(contract(), source, clock());
        const auto result = collector.collect(context(), [] { return false; });
        state.require(result.status ==
                              ProfilingCollectionStatus::InvalidObservation &&
                          !result.observation.has_value(),
                      "unknown owner scopes fail closed");
    }

    {
        ScriptedProfilingObservationSource source(
            {successful_read(4096, 2048)});
        ProfilingObservationCollector collector(contract(), source, clock());
        const auto result = collector.collect(context(), [] { return false; });
        state.require(result.status ==
                              ProfilingCollectionStatus::InvalidObservation &&
                          !result.observation.has_value(),
                      "unattributed totals fail closed");
    }

    {
        auto zero_margin_contract = contract();
        zero_margin_contract.sensors.front().safety_ceiling = 4608;
        const auto transaction_context = context(zero_margin_contract);
        ScriptedProfilingObservationSource source({successful_read()});
        ProfilingObservationCollector collector(std::move(zero_margin_contract),
                                                source, clock());
        const auto result =
            collector.collect(transaction_context, [] { return false; });
        state.require(result.status ==
                              ProfilingCollectionStatus::InvalidObservation &&
                          !result.observation.has_value(),
                      "zero safety margin fails closed");
    }

    {
        auto over_ceiling_contract = contract();
        over_ceiling_contract.sensors.front().safety_ceiling = 4095;
        const auto transaction_context = context(over_ceiling_contract);
        ScriptedProfilingObservationSource source({successful_read()});
        ProfilingObservationCollector collector(
            std::move(over_ceiling_contract), source, clock());
        const auto result =
            collector.collect(transaction_context, [] { return false; });
        state.require(result.status ==
                              ProfilingCollectionStatus::InvalidObservation &&
                          !result.observation.has_value(),
                      "observations above the safety ceiling fail closed");
    }

    {
        auto overflow_contract = contract();
        overflow_contract.sensors.front().uncertainty_bound = 1;
        overflow_contract.sensors.front().safety_ceiling =
            std::numeric_limits<std::uint64_t>::max();
        const auto transaction_context = context(overflow_contract);
        ScriptedProfilingObservationSource source(
            {successful_read(std::numeric_limits<std::uint64_t>::max(),
                             std::numeric_limits<std::uint64_t>::max())});
        ProfilingObservationCollector collector(std::move(overflow_contract),
                                                source, clock());
        const auto result =
            collector.collect(transaction_context, [] { return false; });
        state.require(result.status ==
                              ProfilingCollectionStatus::InvalidObservation &&
                          !result.observation.has_value(),
                      "overflow-prone safety arithmetic fails closed");
    }

    {
        auto read = successful_read();
        read.samples.back().source_generation = 8;
        ScriptedProfilingObservationSource source({std::move(read)});
        ProfilingObservationCollector collector(contract(), source, clock());
        const auto result = collector.collect(context(), [] { return false; });
        state.require(result.status ==
                              ProfilingCollectionStatus::InvalidObservation &&
                          !result.observation.has_value(),
                      "mixed native generations fail closed");
    }

    {
        ScriptedProfilingObservationSource source({successful_read()});
        ProfilingObservationCollector collector(contract(), source,
                                                clock(21ms));
        const auto result = collector.collect(context(), [] { return false; });
        state.require(result.status ==
                              ProfilingCollectionStatus::InvalidObservation &&
                          !result.observation.has_value(),
                      "excess source skew fails closed");
    }

    {
        ScriptedProfilingObservationSource source({successful_read()});
        ProfilingObservationCollector collector(contract(), source, clock());
        const auto result = collector.collect(context(), [] { return true; });
        state.require(result.status == ProfilingCollectionStatus::Cancelled &&
                          !result.observation.has_value(),
                      "cancellation cannot produce evidence");
        state.require(source.requests.empty(),
                      "pre-read cancellation does not enter the source");
    }
}

} // namespace

int main() {
    TestState state;
    test_default_source_is_unavailable(state);
    test_default_interval_source_is_unavailable(state);
    test_derivation_contract_identity(state);
    test_stale_contract_identity_fails_before_source_access(state);
    test_collector_derives_observation_from_raw_samples(state);
    test_freshness_is_anchored_before_read(state);
    test_collector_fails_closed(state);
    return state.ok.load() ? 0 : 1;
}
