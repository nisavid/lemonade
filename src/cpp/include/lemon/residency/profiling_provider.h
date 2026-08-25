#pragma once

#include "lemon/residency/profiling_transaction.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace lemon::residency {

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
};

struct ProfilingRawReadResult {
    ProfilingSourceError error = ProfilingSourceError::Unavailable;
    std::vector<ProfilingRawSample> samples;
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

struct ProfilingSensorContract {
    std::string sensor_id;
    std::string constraint_id;
    ClaimFamily claim_family = ClaimFamily::ConsumableCapacity;
    std::uint64_t uncertainty_bound = 0;
    std::uint64_t safety_ceiling = 0;
};

struct ProfilingDerivationContract {
    std::string provider_id;
    std::string provider_revision_sha256;
    std::vector<ProfilingSensorContract> sensors;
    std::vector<std::string> owner_scope_ids;
    std::chrono::seconds freshness_window{0};
    std::chrono::milliseconds max_source_skew{0};
};

std::optional<std::string> profiling_derivation_contract_sha256(
    const ProfilingDerivationContract &contract);

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
    std::uint64_t observation_generation = 0;
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

} // namespace lemon::residency
