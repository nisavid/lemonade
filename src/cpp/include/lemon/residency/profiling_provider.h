#pragma once

#include "lemon/residency/profiling_transaction.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lemon::residency {

inline constexpr std::uint64_t max_profiling_interval_frames = 4096;

enum class ProfilingSourceError {
    None,
    Unavailable,
    Cancelled,
    Failed,
};

struct ProfilingRawSample {
    std::string sensor_id;
    // Absence identifies the all-owner sensor total. Present identities must
    // come from the Server-issued scope set in the read request.
    std::optional<std::string> owner_scope_id;
    std::uint64_t value = 0;
    std::uint64_t source_generation = 0;
};

struct ProfilingRawReadRequest {
    std::vector<std::string> sensor_ids;
    std::vector<std::string> owner_scope_ids;
    std::string owner_scope_set_sha256;
};

struct ProfilingRawReadResult {
    ProfilingSourceError error = ProfilingSourceError::Unavailable;
    std::vector<ProfilingRawSample> samples;
    std::string owner_scope_set_sha256;
    std::string diagnostic;
};

class ProfilingObservationSource {
public:
    virtual ~ProfilingObservationSource() = default;

    // Reads are synchronous: implementations must not retain either reference.
    virtual ProfilingRawReadResult
    read(const ProfilingRawReadRequest &request,
         const ProfilingCancellationCheck &should_abort) = 0;
};

class UnavailableProfilingObservationSource final
    : public ProfilingObservationSource {
public:
    ProfilingRawReadResult
    read(const ProfilingRawReadRequest &request,
         const ProfilingCancellationCheck &should_abort) override;
};

enum class ProfilingIntervalSourceError {
    None,
    Unavailable,
    Cancelled,
    HistoryLost,
    Failed,
};

struct ProfilingRawIntervalReadRequest {
    ProfilingRawReadRequest read;
    std::string event_semantics_revision_sha256;
};

struct ProfilingEventWatermark {
    std::uint64_t value = 0;

    bool operator==(const ProfilingEventWatermark &other) const noexcept {
        return value == other.value;
    }
    bool operator!=(const ProfilingEventWatermark &other) const noexcept {
        return !(*this == other);
    }
};

struct ProfilingRawIntervalFrame {
    std::string source_epoch_sha256;
    std::string owner_scope_set_sha256;
    std::string event_semantics_revision_sha256;
    ProfilingEventWatermark event_watermark;
    std::vector<ProfilingRawSample> samples;
};

struct ProfilingRawIntervalToken {
    std::uint64_t opaque_id = 0;
};

struct ProfilingRawIntervalBeginResult {
    ProfilingIntervalSourceError error =
        ProfilingIntervalSourceError::Unavailable;
    ProfilingRawIntervalToken token;
    ProfilingRawIntervalFrame checkpoint;
    std::string diagnostic;
};

struct ProfilingRawIntervalBatch {
    ProfilingIntervalSourceError error =
        ProfilingIntervalSourceError::Unavailable;
    ProfilingEventWatermark after_event_watermark;
    ProfilingEventWatermark through_event_watermark;
    std::vector<ProfilingRawIntervalFrame> event_frames;
    ProfilingRawIntervalFrame checkpoint;
    std::string diagnostic;
};

class ProfilingIntervalObservationSource {
public:
    virtual ~ProfilingIntervalObservationSource() = default;

    // Calls are synchronous. Implementations must not retain request or
    // cancellation references, and a successful begin owns one token until
    // finish drains and unregisters it. The Server serializes calls for one
    // token; implementations must synchronize any concurrently active tokens.
    // Success atomically registers the interval before taking its checkpoint.
    virtual ProfilingRawIntervalBeginResult
    begin(const ProfilingRawIntervalReadRequest &request,
          const ProfilingCancellationCheck &should_abort) = 0;
    // Event watermarks are dense: success returns exactly one retained frame
    // for each watermark after the supplied watermark through the returned
    // watermark, plus an atomic checkpoint at that watermark.
    virtual ProfilingRawIntervalBatch
    read_since(ProfilingRawIntervalToken token,
               ProfilingEventWatermark after_event_watermark,
               const ProfilingCancellationCheck &should_abort) = 0;
    // Finish must atomically drain and unregister the token without allowing
    // caller cancellation to skip source cleanup. Implementations must not
    // throw.
    virtual ProfilingRawIntervalBatch
    finish(ProfilingRawIntervalToken token,
           ProfilingEventWatermark after_event_watermark) noexcept = 0;
};

class UnavailableProfilingIntervalObservationSource final
    : public ProfilingIntervalObservationSource {
public:
    ProfilingRawIntervalBeginResult
    begin(const ProfilingRawIntervalReadRequest &request,
          const ProfilingCancellationCheck &should_abort) override;
    ProfilingRawIntervalBatch
    read_since(ProfilingRawIntervalToken token,
               ProfilingEventWatermark after_event_watermark,
               const ProfilingCancellationCheck &should_abort) override;
    ProfilingRawIntervalBatch
    finish(ProfilingRawIntervalToken token,
           ProfilingEventWatermark after_event_watermark) noexcept override;
};

struct ProfilingSensorContract {
    std::string sensor_id;
    std::string constraint_id;
    ClaimFamily claim_family = ClaimFamily::ConsumableCapacity;
    std::uint64_t uncertainty_bound = 0;
    std::uint64_t safety_ceiling = 0;
};

struct ProfilingOwnerScopeBinding {
    std::string owner_scope_id;
    std::string containment_identity_sha256;
};

struct ProfilingIntervalContract {
    std::string event_semantics_revision_sha256;
    std::chrono::milliseconds max_observation_gap{0};
    std::chrono::milliseconds baseline_stability_window{0};
    std::chrono::milliseconds release_stability_window{0};
    std::uint64_t max_interval_frames = 0;
};

struct ProfilingDerivationContract {
    std::string provider_id;
    std::string provider_revision_sha256;
    std::vector<ProfilingSensorContract> sensors;
    std::vector<ProfilingOwnerScopeBinding> owner_scopes;
    std::chrono::seconds freshness_window{0};
    std::chrono::milliseconds max_source_skew{0};
    ProfilingIntervalContract interval;
};

std::optional<std::string> profiling_derivation_contract_sha256(
    const ProfilingDerivationContract &contract);
std::optional<std::string> profiling_owner_scope_set_sha256(
    const std::vector<ProfilingOwnerScopeBinding> &owner_scopes);

struct ProfilingCollectionClock {
    std::function<std::chrono::steady_clock::time_point()> monotonic_now;
    std::function<std::chrono::system_clock::time_point()> utc_now;
};

enum class ProfilingCollectionStatus {
    Accepted,
    Cancelled,
    EvidenceUnavailable,
    InvalidContract,
    InvalidObservation,
    DigestUnavailable,
};

struct ProfilingDerivedObservation {
    std::string provider_id;
    std::string provider_revision_sha256;
    std::string raw_provenance_sha256;
    std::string observation_contract_sha256;
    std::uint64_t source_generation = 0;
    std::string observed_at;
    std::string fresh_until;
    std::uint64_t source_skew_milliseconds = 0;
    std::uint64_t max_source_skew_milliseconds = 0;
    ProfilingObservationHealth health = ProfilingObservationHealth::Missing;
    ProfilingOwnerCoverage owner_coverage = ProfilingOwnerCoverage::Incomplete;
    std::vector<ClaimFamilyClosure> observed_claims;
    std::vector<ClaimFamilyClosure> attributed_claims;
    std::vector<ClaimFamilyClosure> uncertainty_claims;
    std::vector<ClaimFamilyClosure> safety_margin_claims;
};

struct ProfilingObservationCollectionResult {
    ProfilingCollectionStatus status =
        ProfilingCollectionStatus::EvidenceUnavailable;
    std::string diagnostic;
    std::optional<ProfilingDerivedObservation> observation;

    bool accepted() const noexcept;
};

class ProfilingObservationCollector {
public:
    explicit ProfilingObservationCollector(ProfilingDerivationContract contract,
                                           ProfilingCollectionClock clock = {});
    // The injected source must outlive this collector.
    ProfilingObservationCollector(ProfilingDerivationContract contract,
                                  ProfilingObservationSource &source,
                                  ProfilingCollectionClock clock = {});

    ProfilingObservationCollector(const ProfilingObservationCollector &) =
        delete;
    ProfilingObservationCollector &
    operator=(const ProfilingObservationCollector &) = delete;
    ProfilingObservationCollector(ProfilingObservationCollector &&) = delete;
    ProfilingObservationCollector &
    operator=(ProfilingObservationCollector &&) = delete;

    ProfilingObservationCollectionResult
    collect(const ProfilingTransactionContext &context,
            const ProfilingCancellationCheck &should_abort);

private:
    ProfilingDerivationContract contract_;
    UnavailableProfilingObservationSource unavailable_source_;
    ProfilingObservationSource *source_ = nullptr;
    ProfilingCollectionClock clock_;
};

enum class ProfilingIntervalRecorderStatus {
    Ok,
    Cancelled,
    EvidenceUnavailable,
    InvalidContract,
    InvalidObservation,
    DigestUnavailable,
    InvalidState,
};

struct ProfilingRecordedCheckpoint {
    std::uint64_t capture_generation = 0;
    ProfilingEventWatermark event_watermark;
    ProfilingDerivedObservation observation;
};

struct ProfilingIntervalSegment {
    std::string source_epoch_sha256;
    std::string owner_scope_set_sha256;
    std::string event_semantics_revision_sha256;
    ProfilingEventWatermark after_event_watermark;
    ProfilingEventWatermark through_event_watermark;
    std::uint64_t first_capture_generation = 0;
    std::uint64_t last_capture_generation = 0;
    std::uint64_t frame_count = 0;
    std::string provenance_sha256;
    ProfilingRecordedCheckpoint checkpoint;
    std::vector<ClaimFamilyClosure> peak_observed_claims;
    std::vector<ClaimFamilyClosure> peak_attributed_claims;
    std::vector<ClaimFamilyClosure> external_change_claims;
    std::vector<ClaimFamilyClosure> unattributed_claims;
    std::vector<ClaimFamilyClosure> uncertainty_claims;
    std::vector<ClaimFamilyClosure> safety_margin_claims;
};

struct ProfilingIntervalRecorderResult {
    ProfilingIntervalRecorderStatus status =
        ProfilingIntervalRecorderStatus::EvidenceUnavailable;
    std::string diagnostic;
    std::optional<ProfilingIntervalSegment> segment;

    bool ok() const noexcept;
};

class ProfilingIntervalRecorder {
public:
    // One Server owner serializes the recorder lifecycle and all source calls.
    ProfilingIntervalRecorder(ProfilingDerivationContract contract,
                              ProfilingCollectionClock clock = {});
    // The injected source must outlive this recorder.
    ProfilingIntervalRecorder(ProfilingDerivationContract contract,
                              ProfilingIntervalObservationSource &source,
                              ProfilingCollectionClock clock = {});
    ~ProfilingIntervalRecorder();

    ProfilingIntervalRecorder(const ProfilingIntervalRecorder &) = delete;
    ProfilingIntervalRecorder &
    operator=(const ProfilingIntervalRecorder &) = delete;
    ProfilingIntervalRecorder(ProfilingIntervalRecorder &&) = delete;
    ProfilingIntervalRecorder &
    operator=(ProfilingIntervalRecorder &&) = delete;

    ProfilingIntervalRecorderResult
    begin(const ProfilingTransactionContext &context,
          const ProfilingCancellationCheck &should_abort);
    ProfilingIntervalRecorderResult
    poll(const ProfilingCancellationCheck &should_abort);
    ProfilingIntervalRecorderResult
    checkpoint(const ProfilingCancellationCheck &should_abort);
    ProfilingIntervalRecorderResult finish();
    bool active() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace lemon::residency
