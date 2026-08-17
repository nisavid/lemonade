#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace lemon::residency::prototype {

enum class UnknownReason {
    missing,
    malformed,
    overflow,
    stale,
    skew,
    pid_reuse,
    cgroup_mismatch,
    device_mismatch,
    topology_generation_mismatch,
    dependency_identity_mismatch,
    incoherent_total,
};

template <typename T>
class Projection {
public:
    static Projection known(T value) {
        return Projection(std::move(value));
    }

    static Projection unknown(UnknownReason reason) {
        return Projection(reason);
    }

    bool is_known() const {
        return std::holds_alternative<T>(state_);
    }

    const T& value() const {
        return std::get<T>(state_);
    }

    UnknownReason reason() const {
        return std::get<UnknownReason>(state_);
    }

private:
    explicit Projection(T value) : state_(std::move(value)) {}
    explicit Projection(UnknownReason reason) : state_(reason) {}

    std::variant<T, UnknownReason> state_;
};

template <typename T>
bool is_unknown(const Projection<T>& projection, UnknownReason reason) {
    return !projection.is_known() && projection.reason() == reason;
}

struct Bytes {
    std::uint64_t value;
};

struct SignedBytes {
    std::int64_t value;
};

struct SampleStamp {
    std::string boot_id;
    std::uint64_t monotonic_milliseconds;
};

struct SampledBytes {
    Bytes bytes;
    SampleStamp stamp;
};

struct ProcessIdentity {
    std::uint64_t pid;
    std::uint64_t starttime;
};

bool operator<(const ProcessIdentity& left, const ProcessIdentity& right) {
    return std::tie(left.pid, left.starttime) <
           std::tie(right.pid, right.starttime);
}

struct CgroupIdentity {
    std::uint64_t mount_id;
    std::uint64_t inode;
};

bool operator<(const CgroupIdentity& left, const CgroupIdentity& right) {
    return std::tie(left.mount_id, left.inode) <
           std::tie(right.mount_id, right.inode);
}

bool operator==(const CgroupIdentity& left, const CgroupIdentity& right) {
    return left.mount_id == right.mount_id && left.inode == right.inode;
}

struct DeviceIdentity {
    std::string drm_pdev;
    std::uint64_t topology_generation;
    std::string node_alias;
};

struct DependencyIdentity {
    ProcessIdentity process;
    CgroupIdentity cgroup;
    DeviceIdentity device;
    std::string boot_id;
};

struct GlobalObservation {
    Projection<Bytes> mem_info_gtt_total;
    Projection<Bytes> mem_info_gtt_used;
    SampleStamp stamp;
    DeviceIdentity device;
};

struct ClientObservation {
    Projection<Bytes> gtt_bytes;
    SampleStamp stamp;
    ProcessIdentity process;
    CgroupIdentity cgroup;
    DeviceIdentity device;
};

struct ClientObservationPair {
    ClientObservation before;
    ClientObservation after;
    bool owned;
    std::string allocation_identity;
};

struct SharedHostRegion {
    std::string identity;
    Projection<Bytes> bytes;
};

struct GlobalHostObservation {
    Projection<Bytes> memavailable;
    SampleStamp stamp;
    DeviceIdentity device;
};

struct CgroupHostObservation {
    Projection<Bytes> memory_current;
    SampleStamp stamp;
    CgroupIdentity cgroup;
    DeviceIdentity device;
};

struct CgroupHostObservationPair {
    CgroupHostObservation before;
    CgroupHostObservation after;
};

struct HostClientObservation {
    Projection<Bytes> private_bytes;
    std::vector<SharedHostRegion> shared_regions;
    SampleStamp stamp;
    ProcessIdentity process;
    CgroupIdentity cgroup;
    DeviceIdentity device;
};

struct HostGttLink {
    ProcessIdentity process;
    std::string allocation_identity;
    bool owned;
};

struct GttAllocationIdentity {
    ProcessIdentity process;
    std::string allocation_identity;
};

bool operator<(
    const GttAllocationIdentity& left, const GttAllocationIdentity& right) {
    return std::tie(left.process, left.allocation_identity) <
           std::tie(right.process, right.allocation_identity);
}

struct HostClientObservationPair {
    HostClientObservation before;
    HostClientObservation after;
    std::optional<HostGttLink> gtt_link;
    bool before_owned;
    bool after_owned;
};

struct AttributionInput {
    GlobalObservation before;
    GlobalObservation after;
    std::vector<ClientObservationPair> clients;
    GlobalHostObservation host_before;
    GlobalHostObservation host_after;
    std::vector<CgroupHostObservationPair> host_cgroups;
    std::vector<HostClientObservationPair> host_clients;
    std::string current_boot_id;
    std::uint64_t decision_monotonic_milliseconds;
    std::uint64_t maximum_age_milliseconds;
    std::uint64_t maximum_epoch_skew_milliseconds;
};

struct AttributionProjection {
    Bytes global_delta;
    SignedBytes gtt_headroom_before;
    SignedBytes gtt_headroom_after;
    Bytes observed_client_delta;
    Bytes owned_client_delta;
    Bytes unowned_demand;
    Bytes unattributed_global_delta;
    std::map<CgroupIdentity, Bytes> cgroup_rollup;
    Bytes memavailable_before;
    Bytes memavailable_after;
    SignedBytes memavailable_delta;
    std::map<CgroupIdentity, SignedBytes> cgroup_memory_deltas;
    Bytes host_client_before;
    Bytes host_client_after;
    SignedBytes host_client_delta;
    Bytes host_owned_before;
    Bytes host_owned_after;
    Bytes host_unowned_before;
    Bytes host_unowned_after;
};

struct HostOwnershipProjection {
    Bytes total;
    Bytes owned;
    Bytes unowned;
};

enum class NativeProfileMatch {
    exact,
    incomplete,
    not_hatchery,
    unavailable,
};

struct NativeProfileObservation {
    NativeProfileMatch profile;
    Projection<bool> global_gtt;
    Projection<bool> host_memory;
    Projection<bool> boot_identity;
    Projection<bool> device_identity;
    Projection<bool> process_fdinfo;
    Projection<bool> cgroup_membership;
};

using Row = std::pair<std::string, std::string>;

constexpr std::array<std::string_view, 10> kernel_evidence_contract = {
    "/sys/class/drm/device/mem_info_gtt_total",
    "/sys/class/drm/device/mem_info_gtt_used",
    "/proc/meminfo:MemAvailable",
    "/proc/sys/kernel/random/boot_id",
    "/proc/pid/fdinfo",
    "cgroup.procs",
    "mount_id",
    "inode",
    "memory.current",
    "drm_pdev",
};

constexpr std::string_view admission_fallback =
    "hatchery_rocm_admission_refuse_unknown_capacity_v1";
constexpr std::string_view pressure_fallback =
    "hatchery_rocm_pressure_disabled_invalid_evidence_v1";
constexpr std::string_view startup_fallback =
    "hatchery_rocm_startup_block_group_v1";
constexpr std::string_view recovery_fallback =
    "hatchery_rocm_recovery_block_readiness_v1";

Projection<Bytes> parse_bytes(const std::optional<std::string_view>& raw) {
    if (!raw.has_value()) {
        return Projection<Bytes>::unknown(UnknownReason::missing);
    }
    if (raw->empty()) {
        return Projection<Bytes>::unknown(UnknownReason::malformed);
    }
    std::uint64_t value = 0;
    for (const char character : *raw) {
        if (character < '0' || character > '9') {
            return Projection<Bytes>::unknown(UnknownReason::malformed);
        }
        const auto digit = static_cast<std::uint64_t>(character - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
            return Projection<Bytes>::unknown(UnknownReason::overflow);
        }
        value = value * 10U + digit;
    }
    return Projection<Bytes>::known(Bytes{value});
}

Projection<SampledBytes> require_fresh_sample(
    const SampledBytes& sample,
    const std::string& current_boot_id,
    std::uint64_t now_milliseconds,
    std::uint64_t maximum_age_milliseconds) {
    if (sample.stamp.boot_id != current_boot_id) {
        return Projection<SampledBytes>::unknown(
            UnknownReason::dependency_identity_mismatch);
    }
    if (sample.stamp.monotonic_milliseconds > now_milliseconds) {
        return Projection<SampledBytes>::unknown(UnknownReason::skew);
    }
    if (now_milliseconds - sample.stamp.monotonic_milliseconds >
        maximum_age_milliseconds) {
        return Projection<SampledBytes>::unknown(UnknownReason::stale);
    }
    return Projection<SampledBytes>::known(sample);
}

Projection<std::uint64_t> coherent_sample_window(
    const std::vector<SampleStamp>& stamps,
    std::uint64_t maximum_skew_milliseconds) {
    if (stamps.empty()) {
        return Projection<std::uint64_t>::unknown(UnknownReason::missing);
    }
    const std::string& expected_boot_id = stamps.front().boot_id;
    std::uint64_t minimum = stamps.front().monotonic_milliseconds;
    std::uint64_t maximum = minimum;
    for (const auto& stamp : stamps) {
        if (stamp.boot_id != expected_boot_id) {
            return Projection<std::uint64_t>::unknown(
                UnknownReason::dependency_identity_mismatch);
        }
        minimum = std::min(minimum, stamp.monotonic_milliseconds);
        maximum = std::max(maximum, stamp.monotonic_milliseconds);
    }
    if (maximum - minimum > maximum_skew_milliseconds) {
        return Projection<std::uint64_t>::unknown(UnknownReason::skew);
    }
    return Projection<std::uint64_t>::known(maximum - minimum);
}

Projection<ProcessIdentity> match_process_identity(
    const ProcessIdentity& expected, const ProcessIdentity& observed) {
    if (expected.pid != observed.pid) {
        return Projection<ProcessIdentity>::unknown(
            UnknownReason::dependency_identity_mismatch);
    }
    if (expected.starttime != observed.starttime) {
        return Projection<ProcessIdentity>::unknown(UnknownReason::pid_reuse);
    }
    return Projection<ProcessIdentity>::known(observed);
}

Projection<CgroupIdentity> match_cgroup_identity(
    const CgroupIdentity& expected, const CgroupIdentity& observed) {
    if (expected.mount_id != observed.mount_id ||
        expected.inode != observed.inode) {
        return Projection<CgroupIdentity>::unknown(UnknownReason::cgroup_mismatch);
    }
    return Projection<CgroupIdentity>::known(observed);
}

Projection<DeviceIdentity> match_device_identity(
    const DeviceIdentity& expected, const DeviceIdentity& observed) {
    if (expected.drm_pdev != observed.drm_pdev) {
        return Projection<DeviceIdentity>::unknown(UnknownReason::device_mismatch);
    }
    if (expected.topology_generation != observed.topology_generation) {
        return Projection<DeviceIdentity>::unknown(
            UnknownReason::topology_generation_mismatch);
    }
    return Projection<DeviceIdentity>::known(observed);
}

Projection<DependencyIdentity> match_dependency_identity(
    const DependencyIdentity& expected, const DependencyIdentity& observed) {
    const bool process_matches = expected.process.pid == observed.process.pid &&
                                 expected.process.starttime == observed.process.starttime;
    const bool cgroup_matches =
        expected.cgroup.mount_id == observed.cgroup.mount_id &&
        expected.cgroup.inode == observed.cgroup.inode;
    const bool device_matches =
        expected.device.drm_pdev == observed.device.drm_pdev &&
        expected.device.topology_generation ==
            observed.device.topology_generation;
    if (!process_matches || !cgroup_matches || !device_matches ||
        expected.boot_id != observed.boot_id) {
        return Projection<DependencyIdentity>::unknown(
            UnknownReason::dependency_identity_mismatch);
    }
    return Projection<DependencyIdentity>::known(observed);
}

Projection<SignedBytes> signed_difference(Bytes minuend, Bytes subtrahend) {
    const auto positive_limit =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (minuend.value >= subtrahend.value) {
        const std::uint64_t difference = minuend.value - subtrahend.value;
        if (difference > positive_limit) {
            return Projection<SignedBytes>::unknown(UnknownReason::overflow);
        }
        return Projection<SignedBytes>::known(
            SignedBytes{static_cast<std::int64_t>(difference)});
    }
    const std::uint64_t difference = subtrahend.value - minuend.value;
    const std::uint64_t negative_limit = positive_limit + 1U;
    if (difference > negative_limit) {
        return Projection<SignedBytes>::unknown(UnknownReason::overflow);
    }
    if (difference == negative_limit) {
        return Projection<SignedBytes>::known(
            SignedBytes{std::numeric_limits<std::int64_t>::min()});
    }
    return Projection<SignedBytes>::known(
        SignedBytes{-static_cast<std::int64_t>(difference)});
}

bool add_bytes(std::uint64_t& total, Bytes increment) {
    if (increment.value > std::numeric_limits<std::uint64_t>::max() - total) {
        return false;
    }
    total += increment.value;
    return true;
}

DependencyIdentity dependency_identity(const ClientObservation& observation) {
    return DependencyIdentity{
        observation.process,
        observation.cgroup,
        observation.device,
        observation.stamp.boot_id,
    };
}

DependencyIdentity dependency_identity(const HostClientObservation& observation) {
    return DependencyIdentity{
        observation.process,
        observation.cgroup,
        observation.device,
        observation.stamp.boot_id,
    };
}

struct CausalValidationContext {
    const DeviceIdentity& expected_before_device;
    const DeviceIdentity& expected_after_device;
    const std::string& current_boot_id;
    std::uint64_t decision_monotonic_milliseconds;
    std::uint64_t maximum_age_milliseconds;
};

Projection<bool> validate_causal_pair(
    const Projection<Bytes>& before_value,
    const SampleStamp& before_stamp,
    const DeviceIdentity& before_device,
    const Projection<Bytes>& after_value,
    const SampleStamp& after_stamp,
    const DeviceIdentity& after_device,
    const CausalValidationContext& context) {
    if (!before_value.is_known()) {
        return Projection<bool>::unknown(before_value.reason());
    }
    if (!after_value.is_known()) {
        return Projection<bool>::unknown(after_value.reason());
    }
    const auto pair_device =
        match_device_identity(before_device, after_device);
    const auto expected_before = match_device_identity(
        context.expected_before_device, before_device);
    const auto expected_after = match_device_identity(
        context.expected_after_device, after_device);
    if (!pair_device.is_known()) {
        return Projection<bool>::unknown(pair_device.reason());
    }
    if (!expected_before.is_known()) {
        return Projection<bool>::unknown(expected_before.reason());
    }
    if (!expected_after.is_known()) {
        return Projection<bool>::unknown(expected_after.reason());
    }
    const auto fresh_before = require_fresh_sample(
        SampledBytes{before_value.value(), before_stamp},
        context.current_boot_id,
        context.decision_monotonic_milliseconds,
        context.maximum_age_milliseconds);
    const auto fresh_after = require_fresh_sample(
        SampledBytes{after_value.value(), after_stamp},
        context.current_boot_id,
        context.decision_monotonic_milliseconds,
        context.maximum_age_milliseconds);
    if (!fresh_before.is_known()) {
        return Projection<bool>::unknown(fresh_before.reason());
    }
    if (!fresh_after.is_known()) {
        return Projection<bool>::unknown(fresh_after.reason());
    }
    return Projection<bool>::known(true);
}

Projection<Bytes> unique_host_attribution(
    const std::vector<HostClientObservation>& clients);

Projection<HostOwnershipProjection> host_ownership_projection(
    const std::vector<HostClientObservation>& clients,
    const std::vector<bool>& owned_clients);

Projection<AttributionProjection> reconcile_attribution(
    const AttributionInput& input) {
    const CausalValidationContext causal_context{
        input.before.device,
        input.after.device,
        input.current_boot_id,
        input.decision_monotonic_milliseconds,
        input.maximum_age_milliseconds,
    };
    const auto global_total_pair = validate_causal_pair(
        input.before.mem_info_gtt_total,
        input.before.stamp,
        input.before.device,
        input.after.mem_info_gtt_total,
        input.after.stamp,
        input.after.device,
        causal_context);
    const auto global_used_pair = validate_causal_pair(
        input.before.mem_info_gtt_used,
        input.before.stamp,
        input.before.device,
        input.after.mem_info_gtt_used,
        input.after.stamp,
        input.after.device,
        causal_context);
    if (!global_total_pair.is_known()) {
        return Projection<AttributionProjection>::unknown(
            global_total_pair.reason());
    }
    if (!global_used_pair.is_known()) {
        return Projection<AttributionProjection>::unknown(
            global_used_pair.reason());
    }
    if (input.before.mem_info_gtt_total.value().value !=
        input.after.mem_info_gtt_total.value().value) {
        return Projection<AttributionProjection>::unknown(
            UnknownReason::incoherent_total);
    }
    const auto gtt_headroom_before = signed_difference(
        input.before.mem_info_gtt_total.value(),
        input.before.mem_info_gtt_used.value());
    const auto gtt_headroom_after = signed_difference(
        input.after.mem_info_gtt_total.value(),
        input.after.mem_info_gtt_used.value());
    if (!gtt_headroom_before.is_known()) {
        return Projection<AttributionProjection>::unknown(
            gtt_headroom_before.reason());
    }
    if (!gtt_headroom_after.is_known()) {
        return Projection<AttributionProjection>::unknown(
            gtt_headroom_after.reason());
    }

    std::vector<SampleStamp> before_stamps = {input.before.stamp};
    std::vector<SampleStamp> after_stamps = {input.after.stamp};
    std::set<ProcessIdentity> gtt_processes;
    for (const auto& client : input.clients) {
        const auto causal_pair = validate_causal_pair(
            client.before.gtt_bytes,
            client.before.stamp,
            client.before.device,
            client.after.gtt_bytes,
            client.after.stamp,
            client.after.device,
            causal_context);
        if (!causal_pair.is_known()) {
            return Projection<AttributionProjection>::unknown(
                causal_pair.reason());
        }
        const auto dependency = match_dependency_identity(
            dependency_identity(client.before),
            dependency_identity(client.after));
        if (!dependency.is_known()) {
            return Projection<AttributionProjection>::unknown(dependency.reason());
        }
        if (!gtt_processes.insert(client.before.process).second) {
            return Projection<AttributionProjection>::unknown(
                UnknownReason::dependency_identity_mismatch);
        }
        before_stamps.push_back(client.before.stamp);
        after_stamps.push_back(client.after.stamp);
    }

    const auto host_global_pair = validate_causal_pair(
        input.host_before.memavailable,
        input.host_before.stamp,
        input.host_before.device,
        input.host_after.memavailable,
        input.host_after.stamp,
        input.host_after.device,
        causal_context);
    if (!host_global_pair.is_known()) {
        return Projection<AttributionProjection>::unknown(
            host_global_pair.reason());
    }
    before_stamps.push_back(input.host_before.stamp);
    after_stamps.push_back(input.host_after.stamp);

    std::set<CgroupIdentity> host_cgroup_keys;
    std::map<CgroupIdentity, std::pair<Bytes, Bytes>> cgroup_memory_values;
    std::map<CgroupIdentity, SignedBytes> cgroup_memory_deltas;
    for (const auto& host_cgroup : input.host_cgroups) {
        const auto causal_pair = validate_causal_pair(
            host_cgroup.before.memory_current,
            host_cgroup.before.stamp,
            host_cgroup.before.device,
            host_cgroup.after.memory_current,
            host_cgroup.after.stamp,
            host_cgroup.after.device,
            causal_context);
        if (!causal_pair.is_known()) {
            return Projection<AttributionProjection>::unknown(
                causal_pair.reason());
        }
        const auto cgroup = match_cgroup_identity(
            host_cgroup.before.cgroup, host_cgroup.after.cgroup);
        if (!cgroup.is_known()) {
            return Projection<AttributionProjection>::unknown(cgroup.reason());
        }
        const CgroupIdentity key = host_cgroup.before.cgroup;
        if (!host_cgroup_keys.insert(key).second) {
            return Projection<AttributionProjection>::unknown(
                UnknownReason::dependency_identity_mismatch);
        }
        const auto memory_delta = signed_difference(
            host_cgroup.after.memory_current.value(),
            host_cgroup.before.memory_current.value());
        if (!memory_delta.is_known()) {
            return Projection<AttributionProjection>::unknown(
                memory_delta.reason());
        }
        cgroup_memory_values.emplace(
            key,
            std::make_pair(
                host_cgroup.before.memory_current.value(),
                host_cgroup.after.memory_current.value()));
        cgroup_memory_deltas.emplace(key, memory_delta.value());
        before_stamps.push_back(host_cgroup.before.stamp);
        after_stamps.push_back(host_cgroup.after.stamp);
    }
    std::set<ProcessIdentity> host_processes;
    std::set<GttAllocationIdentity> linked_gtt_allocations;
    std::vector<HostClientObservation> host_clients_before;
    std::vector<HostClientObservation> host_clients_after;
    std::vector<bool> host_client_ownership;
    for (const auto& host_client : input.host_clients) {
        const auto causal_pair = validate_causal_pair(
            host_client.before.private_bytes,
            host_client.before.stamp,
            host_client.before.device,
            host_client.after.private_bytes,
            host_client.after.stamp,
            host_client.after.device,
            causal_context);
        if (!causal_pair.is_known()) {
            return Projection<AttributionProjection>::unknown(
                causal_pair.reason());
        }
        const auto dependency = match_dependency_identity(
            dependency_identity(host_client.before),
            dependency_identity(host_client.after));
        if (!dependency.is_known()) {
            return Projection<AttributionProjection>::unknown(dependency.reason());
        }
        if (!host_processes.insert(host_client.before.process).second ||
            host_client.before_owned != host_client.after_owned) {
            return Projection<AttributionProjection>::unknown(
                UnknownReason::dependency_identity_mismatch);
        }
        const bool host_client_is_owned = host_client.before_owned;
        if (host_client.gtt_link.has_value()) {
            const HostGttLink& link = *host_client.gtt_link;
            if (link.process.pid != host_client.before.process.pid ||
                link.process.starttime != host_client.before.process.starttime) {
                return Projection<AttributionProjection>::unknown(
                    UnknownReason::dependency_identity_mismatch);
            }
            bool link_matches = false;
            for (const auto& gtt_client : input.clients) {
                if (gtt_client.before.process.pid != link.process.pid ||
                    gtt_client.before.process.starttime !=
                        link.process.starttime) {
                    continue;
                }
                const auto before_dependency = match_dependency_identity(
                    dependency_identity(gtt_client.before),
                    dependency_identity(host_client.before));
                const auto after_dependency = match_dependency_identity(
                    dependency_identity(gtt_client.after),
                    dependency_identity(host_client.after));
                if (!before_dependency.is_known() ||
                    !after_dependency.is_known() ||
                    gtt_client.allocation_identity !=
                        link.allocation_identity ||
                    gtt_client.owned != link.owned ||
                    link.owned != host_client_is_owned) {
                    return Projection<AttributionProjection>::unknown(
                        UnknownReason::dependency_identity_mismatch);
                }
                link_matches = true;
                if (!linked_gtt_allocations
                         .insert(GttAllocationIdentity{
                             link.process, link.allocation_identity})
                         .second) {
                    return Projection<AttributionProjection>::unknown(
                        UnknownReason::dependency_identity_mismatch);
                }
                break;
            }
            if (!link_matches) {
                return Projection<AttributionProjection>::unknown(
                    UnknownReason::dependency_identity_mismatch);
            }
        }
        if (host_cgroup_keys.find(host_client.before.cgroup) ==
            host_cgroup_keys.end()) {
            return Projection<AttributionProjection>::unknown(
                UnknownReason::dependency_identity_mismatch);
        }
        host_clients_before.push_back(host_client.before);
        host_clients_after.push_back(host_client.after);
        host_client_ownership.push_back(host_client_is_owned);
        before_stamps.push_back(host_client.before.stamp);
        after_stamps.push_back(host_client.after.stamp);
    }
    for (const auto& gtt_client : input.clients) {
        if (gtt_client.owned &&
            linked_gtt_allocations.find(GttAllocationIdentity{
                gtt_client.before.process, gtt_client.allocation_identity}) ==
                linked_gtt_allocations.end()) {
            return Projection<AttributionProjection>::unknown(
                UnknownReason::dependency_identity_mismatch);
        }
    }

    const auto before_window = coherent_sample_window(
        before_stamps, input.maximum_epoch_skew_milliseconds);
    const auto after_window = coherent_sample_window(
        after_stamps, input.maximum_epoch_skew_milliseconds);
    if (!before_window.is_known()) {
        return Projection<AttributionProjection>::unknown(before_window.reason());
    }
    if (!after_window.is_known()) {
        return Projection<AttributionProjection>::unknown(after_window.reason());
    }
    const auto latest_before = std::max_element(
        before_stamps.begin(), before_stamps.end(),
        [](const SampleStamp& left, const SampleStamp& right) {
            return left.monotonic_milliseconds < right.monotonic_milliseconds;
        });
    const auto earliest_after = std::min_element(
        after_stamps.begin(), after_stamps.end(),
        [](const SampleStamp& left, const SampleStamp& right) {
            return left.monotonic_milliseconds < right.monotonic_milliseconds;
        });
    if (latest_before->monotonic_milliseconds >
        earliest_after->monotonic_milliseconds) {
        return Projection<AttributionProjection>::unknown(UnknownReason::skew);
    }
    if (input.after.mem_info_gtt_used.value().value <
        input.before.mem_info_gtt_used.value().value) {
        return Projection<AttributionProjection>::unknown(
            UnknownReason::incoherent_total);
    }

    const std::uint64_t global_delta =
        input.after.mem_info_gtt_used.value().value -
        input.before.mem_info_gtt_used.value().value;
    std::uint64_t observed_client_delta = 0;
    std::uint64_t owned_client_delta = 0;
    std::map<CgroupIdentity, Bytes> cgroup_rollup;

    for (const auto& client : input.clients) {
        if (client.after.gtt_bytes.value().value <
            client.before.gtt_bytes.value().value) {
            return Projection<AttributionProjection>::unknown(
                UnknownReason::incoherent_total);
        }
        const Bytes client_delta{
            client.after.gtt_bytes.value().value -
            client.before.gtt_bytes.value().value};
        if (!add_bytes(observed_client_delta, client_delta)) {
            return Projection<AttributionProjection>::unknown(
                UnknownReason::overflow);
        }
        if (client.owned && !add_bytes(owned_client_delta, client_delta)) {
            return Projection<AttributionProjection>::unknown(
                UnknownReason::overflow);
        }
        const CgroupIdentity key = client.after.cgroup;
        auto position = cgroup_rollup.find(key);
        if (position == cgroup_rollup.end()) {
            cgroup_rollup.emplace(key, client_delta);
        } else if (!add_bytes(position->second.value, client_delta)) {
            return Projection<AttributionProjection>::unknown(
                UnknownReason::overflow);
        }
    }

    if (observed_client_delta > global_delta || owned_client_delta > global_delta) {
        return Projection<AttributionProjection>::unknown(
            UnknownReason::incoherent_total);
    }

    const auto memavailable_delta = signed_difference(
        input.host_after.memavailable.value(),
        input.host_before.memavailable.value());
    const auto host_client_before =
        unique_host_attribution(host_clients_before);
    const auto host_client_after = unique_host_attribution(host_clients_after);
    const auto host_ownership_before = host_ownership_projection(
        host_clients_before, host_client_ownership);
    const auto host_ownership_after = host_ownership_projection(
        host_clients_after, host_client_ownership);
    if (!memavailable_delta.is_known()) {
        return Projection<AttributionProjection>::unknown(
            memavailable_delta.reason());
    }
    if (!host_client_before.is_known()) {
        return Projection<AttributionProjection>::unknown(
            host_client_before.reason());
    }
    if (!host_client_after.is_known()) {
        return Projection<AttributionProjection>::unknown(
            host_client_after.reason());
    }
    if (!host_ownership_before.is_known()) {
        return Projection<AttributionProjection>::unknown(
            host_ownership_before.reason());
    }
    if (!host_ownership_after.is_known()) {
        return Projection<AttributionProjection>::unknown(
            host_ownership_after.reason());
    }
    if (host_ownership_before.value().total.value !=
            host_client_before.value().value ||
        host_ownership_after.value().total.value !=
            host_client_after.value().value) {
        return Projection<AttributionProjection>::unknown(
            UnknownReason::incoherent_total);
    }
    const auto host_client_delta = signed_difference(
        host_client_after.value(), host_client_before.value());
    if (!host_client_delta.is_known()) {
        return Projection<AttributionProjection>::unknown(
            host_client_delta.reason());
    }
    for (const auto& cgroup_memory : cgroup_memory_values) {
        std::vector<HostClientObservation> cgroup_clients_before;
        std::vector<HostClientObservation> cgroup_clients_after;
        for (std::size_t index = 0; index < host_clients_before.size(); ++index) {
            if (host_clients_before[index].cgroup == cgroup_memory.first) {
                cgroup_clients_before.push_back(host_clients_before[index]);
                cgroup_clients_after.push_back(host_clients_after[index]);
            }
        }
        const auto cgroup_client_before =
            unique_host_attribution(cgroup_clients_before);
        const auto cgroup_client_after =
            unique_host_attribution(cgroup_clients_after);
        if (!cgroup_client_before.is_known()) {
            return Projection<AttributionProjection>::unknown(
                cgroup_client_before.reason());
        }
        if (!cgroup_client_after.is_known()) {
            return Projection<AttributionProjection>::unknown(
                cgroup_client_after.reason());
        }
        if (cgroup_client_before.value().value >
                cgroup_memory.second.first.value ||
            cgroup_client_after.value().value >
                cgroup_memory.second.second.value) {
            return Projection<AttributionProjection>::unknown(
                UnknownReason::incoherent_total);
        }
    }
    return Projection<AttributionProjection>::known(AttributionProjection{
        Bytes{global_delta},
        gtt_headroom_before.value(),
        gtt_headroom_after.value(),
        Bytes{observed_client_delta},
        Bytes{owned_client_delta},
        Bytes{global_delta - owned_client_delta},
        Bytes{global_delta - observed_client_delta},
        std::move(cgroup_rollup),
        input.host_before.memavailable.value(),
        input.host_after.memavailable.value(),
        memavailable_delta.value(),
        std::move(cgroup_memory_deltas),
        host_client_before.value(),
        host_client_after.value(),
        host_client_delta.value(),
        host_ownership_before.value().owned,
        host_ownership_after.value().owned,
        host_ownership_before.value().unowned,
        host_ownership_after.value().unowned,
    });
}

Projection<Bytes> unique_host_attribution(
    const std::vector<HostClientObservation>& clients) {
    std::uint64_t total = 0;
    std::map<std::string, Bytes> shared_regions;
    for (const auto& client : clients) {
        if (!client.private_bytes.is_known()) {
            return Projection<Bytes>::unknown(client.private_bytes.reason());
        }
        if (!add_bytes(total, client.private_bytes.value())) {
            return Projection<Bytes>::unknown(UnknownReason::overflow);
        }
        for (const auto& region : client.shared_regions) {
            if (!region.bytes.is_known()) {
                return Projection<Bytes>::unknown(region.bytes.reason());
            }
            const auto position = shared_regions.find(region.identity);
            if (position == shared_regions.end()) {
                shared_regions.emplace(region.identity, region.bytes.value());
            } else if (position->second.value != region.bytes.value().value) {
                return Projection<Bytes>::unknown(UnknownReason::incoherent_total);
            }
        }
    }
    for (const auto& region : shared_regions) {
        if (!add_bytes(total, region.second)) {
            return Projection<Bytes>::unknown(UnknownReason::overflow);
        }
    }
    return Projection<Bytes>::known(Bytes{total});
}

Projection<HostOwnershipProjection> host_ownership_projection(
    const std::vector<HostClientObservation>& clients,
    const std::vector<bool>& owned_clients) {
    if (clients.size() != owned_clients.size()) {
        return Projection<HostOwnershipProjection>::unknown(
            UnknownReason::dependency_identity_mismatch);
    }
    std::uint64_t owned = 0;
    std::uint64_t unowned = 0;
    std::map<std::string, std::pair<Bytes, bool>> shared_regions;
    for (std::size_t index = 0; index < clients.size(); ++index) {
        const auto& client = clients[index];
        if (!client.private_bytes.is_known()) {
            return Projection<HostOwnershipProjection>::unknown(
                client.private_bytes.reason());
        }
        std::uint64_t& private_total = owned_clients[index] ? owned : unowned;
        if (!add_bytes(private_total, client.private_bytes.value())) {
            return Projection<HostOwnershipProjection>::unknown(
                UnknownReason::overflow);
        }
        for (const auto& region : client.shared_regions) {
            if (!region.bytes.is_known()) {
                return Projection<HostOwnershipProjection>::unknown(
                    region.bytes.reason());
            }
            const auto position = shared_regions.find(region.identity);
            if (position == shared_regions.end()) {
                shared_regions.emplace(
                    region.identity,
                    std::make_pair(region.bytes.value(), owned_clients[index]));
            } else {
                if (position->second.first.value != region.bytes.value().value) {
                    return Projection<HostOwnershipProjection>::unknown(
                        UnknownReason::incoherent_total);
                }
                position->second.second =
                    position->second.second && owned_clients[index];
            }
        }
    }
    for (const auto& region : shared_regions) {
        std::uint64_t& shared_total = region.second.second ? owned : unowned;
        if (!add_bytes(shared_total, region.second.first)) {
            return Projection<HostOwnershipProjection>::unknown(
                UnknownReason::overflow);
        }
    }
    std::uint64_t total = owned;
    if (!add_bytes(total, Bytes{unowned})) {
        return Projection<HostOwnershipProjection>::unknown(
            UnknownReason::overflow);
    }
    return Projection<HostOwnershipProjection>::known(
        HostOwnershipProjection{Bytes{total}, Bytes{owned}, Bytes{unowned}});
}

Projection<std::string> read_bounded_text(
    const std::string& path, std::size_t maximum_bytes = 16'384) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return Projection<std::string>::unknown(UnknownReason::missing);
    }
    std::string text;
    char character = '\0';
    while (stream.get(character)) {
        if (text.size() == maximum_bytes) {
            return Projection<std::string>::unknown(UnknownReason::malformed);
        }
        text.push_back(character);
    }
    if (!stream.eof()) {
        return Projection<std::string>::unknown(UnknownReason::malformed);
    }
    while (!text.empty() &&
           (text.back() == '\n' || text.back() == '\r' ||
            text.back() == ' ' || text.back() == '\t')) {
        text.pop_back();
    }
    if (text.empty()) {
        return Projection<std::string>::unknown(UnknownReason::malformed);
    }
    return Projection<std::string>::known(std::move(text));
}

std::string ascii_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](char character) {
        if (character >= 'A' && character <= 'Z') {
            return static_cast<char>(character - 'A' + 'a');
        }
        return character;
    });
    return value;
}

bool is_drm_card_name(std::string_view name) {
    return name.size() > 4 && name.substr(0, 4) == "card" &&
           std::all_of(name.begin() + 4, name.end(), [](char character) {
               return character >= '0' && character <= '9';
           });
}

#ifdef __linux__
Projection<bool> hatchery_device_matches() {
    std::error_code error;
    std::filesystem::directory_iterator position("/sys/class/drm", error);
    const std::filesystem::directory_iterator end;
    if (error) {
        return Projection<bool>::unknown(UnknownReason::missing);
    }
    std::vector<std::string> cards;
    while (position != end) {
        const std::string name = position->path().filename().string();
        if (is_drm_card_name(name)) {
            cards.push_back(name);
        }
        position.increment(error);
        if (error) {
            return Projection<bool>::unknown(UnknownReason::missing);
        }
    }
    std::sort(cards.begin(), cards.end());
    if (cards.size() != 1 || cards.front() != "card1") {
        return Projection<bool>::known(false);
    }

    constexpr std::string_view device_root = "/sys/class/drm/card1/device/";
    constexpr std::string_view device_uevent_path =
        "/sys/class/drm/card1/device/uevent";
    const auto vendor = read_bounded_text(
        std::string(device_root) + "vendor", 64);
    const auto device = read_bounded_text(
        std::string(device_root) + "device", 64);
    const auto uevent =
        read_bounded_text(std::string(device_uevent_path), 4'096);
    if (!vendor.is_known()) {
        return Projection<bool>::unknown(vendor.reason());
    }
    if (!device.is_known()) {
        return Projection<bool>::unknown(device.reason());
    }
    if (!uevent.is_known()) {
        return Projection<bool>::unknown(uevent.reason());
    }

    std::map<std::string, std::string> fields;
    std::size_t start = 0;
    while (start < uevent.value().size()) {
        const std::size_t end_of_line = uevent.value().find('\n', start);
        const std::string line = uevent.value().substr(
            start,
            (end_of_line == std::string::npos ? uevent.value().size()
                                               : end_of_line) -
                start);
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos || separator == 0 ||
            separator != line.rfind('=') ||
            !fields.emplace(
                       line.substr(0, separator), line.substr(separator + 1))
                 .second) {
            return Projection<bool>::unknown(UnknownReason::malformed);
        }
        if (end_of_line == std::string::npos) {
            break;
        }
        start = end_of_line + 1;
    }
    const auto field_equals = [&fields](
                                  const std::string& key,
                                  std::string_view expected,
                                  bool case_insensitive = false) {
        const auto found = fields.find(key);
        if (found == fields.end()) {
            return false;
        }
        return case_insensitive
                   ? ascii_lower(found->second) == ascii_lower(std::string(expected))
                   : found->second == expected;
    };
    return Projection<bool>::known(
        ascii_lower(vendor.value()) == "0x1002" &&
        ascii_lower(device.value()) == "0x1586" &&
        field_equals("DRIVER", "amdgpu") &&
        field_equals("PCI_ID", "1002:1586", true) &&
        field_equals("PCI_SLOT_NAME", "0000:c6:00.0"));
}
#endif

NativeProfileMatch classify_hatchery_identity(
    const Projection<std::string>& hostname,
    const Projection<bool>& device_matches) {
    if (!hostname.is_known() && !device_matches.is_known()) {
        return NativeProfileMatch::unavailable;
    }
    if ((hostname.is_known() && hostname.value() != "hatchery") ||
        (device_matches.is_known() && !device_matches.value())) {
        return NativeProfileMatch::not_hatchery;
    }
    if (hostname.is_known() && hostname.value() == "hatchery" &&
        device_matches.is_known() && device_matches.value()) {
        return NativeProfileMatch::exact;
    }
    return NativeProfileMatch::incomplete;
}

Projection<bool> parse_memavailable(const Projection<std::string>& meminfo) {
    if (!meminfo.is_known()) {
        return Projection<bool>::unknown(meminfo.reason());
    }
    std::size_t start = 0;
    while (start < meminfo.value().size()) {
        const std::size_t end = meminfo.value().find('\n', start);
        const std::string line = meminfo.value().substr(
            start,
            (end == std::string::npos ? meminfo.value().size() : end) - start);
        if (line.substr(0, std::string_view{"MemAvailable:"}.size()) ==
            "MemAvailable:") {
            std::istringstream fields(line);
            std::string label;
            std::string raw_value;
            std::string unit;
            std::string extra;
            if (!(fields >> label >> raw_value >> unit) || fields >> extra ||
                label != "MemAvailable:" || unit != "kB") {
                return Projection<bool>::unknown(
                    UnknownReason::malformed);
            }
            const auto bytes = parse_bytes(std::string_view{raw_value});
            if (!bytes.is_known()) {
                return Projection<bool>::unknown(bytes.reason());
            }
            return Projection<bool>::known(true);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return Projection<bool>::unknown(UnknownReason::missing);
}

Projection<std::string> parse_current_cgroup_path(
    const Projection<std::string>& cgroup_membership) {
    if (!cgroup_membership.is_known()) {
        return Projection<std::string>::unknown(cgroup_membership.reason());
    }
    std::optional<std::string> relative;
    std::size_t start = 0;
    while (start < cgroup_membership.value().size()) {
        const std::size_t end = cgroup_membership.value().find('\n', start);
        const std::string line = cgroup_membership.value().substr(
            start,
            (end == std::string::npos ? cgroup_membership.value().size() : end) -
                start);
        if (line.substr(0, 3) == "0::") {
            if (relative.has_value()) {
                return Projection<std::string>::unknown(
                    UnknownReason::malformed);
            }
            relative = line.substr(3);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    if (!relative.has_value() || relative->empty() || relative->front() != '/') {
        return Projection<std::string>::unknown(UnknownReason::malformed);
    }
    std::size_t component_start = 1;
    while (component_start <= relative->size()) {
        const std::size_t separator = relative->find('/', component_start);
        const std::string_view component(
            relative->data() + component_start,
            (separator == std::string::npos ? relative->size() : separator) -
                component_start);
        if (component == "..") {
            return Projection<std::string>::unknown(
                UnknownReason::malformed);
        }
        if (separator == std::string::npos) {
            break;
        }
        component_start = separator + 1;
    }
    return Projection<std::string>::known(std::move(*relative));
}

#ifdef __linux__
Projection<bool> current_cgroup_memory_is_observed() {
    const auto relative = parse_current_cgroup_path(
        read_bounded_text("/proc/self/cgroup", 16'384));
    if (!relative.is_known()) {
        return Projection<bool>::unknown(relative.reason());
    }
    const auto memory_current = read_bounded_text(
        "/sys/fs/cgroup" + relative.value() + "/memory.current", 64);
    if (!memory_current.is_known()) {
        return Projection<bool>::unknown(memory_current.reason());
    }
    const auto bytes = parse_bytes(std::string_view{memory_current.value()});
    if (!bytes.is_known()) {
        return Projection<bool>::unknown(bytes.reason());
    }
    return Projection<bool>::known(true);
}

Projection<bool> host_memory_is_observed() {
    const auto memavailable = parse_memavailable(
        read_bounded_text("/proc/meminfo", 16'384));
    const auto memory_current = current_cgroup_memory_is_observed();
    if (!memavailable.is_known()) {
        return Projection<bool>::unknown(memavailable.reason());
    }
    if (!memory_current.is_known()) {
        return Projection<bool>::unknown(memory_current.reason());
    }
    return Projection<bool>::known(true);
}
#endif

Projection<bool> parse_gtt_pair(
    const Projection<std::string>& total,
    const Projection<std::string>& used) {
    if (!total.is_known()) {
        return Projection<bool>::unknown(total.reason());
    }
    if (!used.is_known()) {
        return Projection<bool>::unknown(used.reason());
    }
    const auto parsed_total = parse_bytes(total.value());
    const auto parsed_used = parse_bytes(used.value());
    if (!parsed_total.is_known()) {
        return Projection<bool>::unknown(parsed_total.reason());
    }
    if (!parsed_used.is_known()) {
        return Projection<bool>::unknown(parsed_used.reason());
    }
    if (parsed_total.value().value == 0 ||
        parsed_used.value().value > parsed_total.value().value) {
        return Projection<bool>::unknown(UnknownReason::incoherent_total);
    }
    return Projection<bool>::known(true);
}

Projection<bool> parse_boot_identity(
    const Projection<std::string>& boot_id) {
    if (!boot_id.is_known()) {
        return Projection<bool>::unknown(boot_id.reason());
    }
    const std::string& value = boot_id.value();
    if (value.size() != 36) {
        return Projection<bool>::unknown(UnknownReason::malformed);
    }
    for (const char character : value) {
        const bool hexadecimal =
            (character >= '0' && character <= '9') ||
            (character >= 'a' && character <= 'f') ||
            (character >= 'A' && character <= 'F') || character == '-';
        if (!hexadecimal) {
            return Projection<bool>::unknown(UnknownReason::malformed);
        }
    }
    return Projection<bool>::known(true);
}

NativeProfileObservation probe_native_hatchery_profile(
    const std::string& platform) {
    const auto unknown = []() {
        return Projection<bool>::unknown(UnknownReason::missing);
    };
#ifndef __linux__
    (void)platform;
    return NativeProfileObservation{
        NativeProfileMatch::unavailable,
        unknown(),
        unknown(),
        unknown(),
        unknown(),
        unknown(),
        unknown(),
    };
#else
    if (platform != "linux") {
        return NativeProfileObservation{
            NativeProfileMatch::unavailable,
            unknown(),
            unknown(),
            unknown(),
            unknown(),
            unknown(),
            unknown(),
        };
    }
    const auto machine =
        read_bounded_text("/proc/sys/kernel/hostname", 256);
    const auto device_matches = hatchery_device_matches();
    const NativeProfileMatch profile =
        classify_hatchery_identity(machine, device_matches);
    if (profile != NativeProfileMatch::exact) {
        return NativeProfileObservation{
            profile,
            unknown(),
            unknown(),
            unknown(),
            unknown(),
            unknown(),
            unknown(),
        };
    }

    constexpr std::string_view device_root = "/sys/class/drm/card1/device/";
    const auto global_gtt = parse_gtt_pair(
        read_bounded_text(
            std::string(device_root) + "mem_info_gtt_total", 64),
        read_bounded_text(
            std::string(device_root) + "mem_info_gtt_used", 64));
    const auto host_memory = host_memory_is_observed();
    const auto boot_identity = parse_boot_identity(
        read_bounded_text("/proc/sys/kernel/random/boot_id", 64));
    return NativeProfileObservation{
        profile,
        global_gtt,
        host_memory,
        boot_identity,
        device_matches,
        unknown(),
        unknown(),
    };
#endif
}

Projection<bool> native_causal_proof(
    const NativeProfileObservation& observation) {
    if (observation.profile != NativeProfileMatch::exact ||
        !observation.global_gtt.is_known() || !observation.global_gtt.value() ||
        !observation.host_memory.is_known() ||
        !observation.host_memory.value() ||
        !observation.boot_identity.is_known() ||
        !observation.boot_identity.value() ||
        !observation.process_fdinfo.is_known() ||
        !observation.process_fdinfo.value() ||
        !observation.cgroup_membership.is_known() ||
        !observation.cgroup_membership.value() ||
        !observation.device_identity.is_known() ||
        !observation.device_identity.value()) {
        return Projection<bool>::unknown(UnknownReason::missing);
    }
    return Projection<bool>::known(true);
}

bool evidence_contract_is_closed() {
    return kernel_evidence_contract.front() ==
               "/sys/class/drm/device/mem_info_gtt_total" &&
           kernel_evidence_contract.back() == "drm_pdev" &&
           std::find(
               kernel_evidence_contract.begin(), kernel_evidence_contract.end(),
               "/proc/pid/fdinfo") != kernel_evidence_contract.end() &&
           std::find(
               kernel_evidence_contract.begin(), kernel_evidence_contract.end(),
               "cgroup.procs") != kernel_evidence_contract.end() &&
           std::find(
               kernel_evidence_contract.begin(), kernel_evidence_contract.end(),
               "memory.current") != kernel_evidence_contract.end();
}

AttributionInput coherent_attribution_input() {
    const DeviceIdentity device{"0000:03:00.0", 9, "card0"};
    const SampleStamp before_stamp{"boot-a", 100};
    const SampleStamp after_stamp{"boot-a", 200};
    return AttributionInput{
        GlobalObservation{
            Projection<Bytes>::known(Bytes{1'000}),
            Projection<Bytes>::known(Bytes{100}),
            before_stamp,
            device,
        },
        GlobalObservation{
            Projection<Bytes>::known(Bytes{1'000}),
            Projection<Bytes>::known(Bytes{160}),
            after_stamp,
            device,
        },
        {
            ClientObservationPair{
                ClientObservation{
                    Projection<Bytes>::known(Bytes{20}),
                    before_stamp,
                    ProcessIdentity{101, 1'001},
                    CgroupIdentity{7, 11},
                    device,
                },
                ClientObservation{
                    Projection<Bytes>::known(Bytes{40}),
                    after_stamp,
                    ProcessIdentity{101, 1'001},
                    CgroupIdentity{7, 11},
                    device,
                },
                true,
                "gtt-a",
            },
            ClientObservationPair{
                ClientObservation{
                    Projection<Bytes>::known(Bytes{7}),
                    before_stamp,
                    ProcessIdentity{202, 2'002},
                    CgroupIdentity{7, 11},
                    device,
                },
                ClientObservation{
                    Projection<Bytes>::known(Bytes{22}),
                    after_stamp,
                    ProcessIdentity{202, 2'002},
                    CgroupIdentity{7, 11},
                    device,
                },
                false,
                "gtt-b",
            },
        },
        GlobalHostObservation{
            Projection<Bytes>::known(Bytes{4'096}),
            SampleStamp{"boot-a", 100},
            DeviceIdentity{"0000:03:00.0", 9, "host-before"},
        },
        GlobalHostObservation{
            Projection<Bytes>::known(Bytes{4'000}),
            SampleStamp{"boot-a", 200},
            DeviceIdentity{"0000:03:00.0", 9, "host-after"},
        },
        {
            CgroupHostObservationPair{
                CgroupHostObservation{
                    Projection<Bytes>::known(Bytes{300}),
                    SampleStamp{"boot-a", 101},
                    CgroupIdentity{7, 11},
                    DeviceIdentity{"0000:03:00.0", 9, "cgroup-before"},
                },
                CgroupHostObservation{
                    Projection<Bytes>::known(Bytes{350}),
                    SampleStamp{"boot-a", 201},
                    CgroupIdentity{7, 11},
                    DeviceIdentity{"0000:03:00.0", 9, "cgroup-after"},
                },
            },
            CgroupHostObservationPair{
                CgroupHostObservation{
                    Projection<Bytes>::known(Bytes{200}),
                    SampleStamp{"boot-a", 102},
                    CgroupIdentity{8, 12},
                    DeviceIdentity{"0000:03:00.0", 9, "external-cgroup-before"},
                },
                CgroupHostObservation{
                    Projection<Bytes>::known(Bytes{230}),
                    SampleStamp{"boot-a", 202},
                    CgroupIdentity{8, 12},
                    DeviceIdentity{"0000:03:00.0", 9, "external-cgroup-after"},
                },
            },
        },
        {
            HostClientObservationPair{
                HostClientObservation{
                    Projection<Bytes>::known(Bytes{100}),
                    {SharedHostRegion{
                        "shared-a", Projection<Bytes>::known(Bytes{50})}},
                    SampleStamp{"boot-a", 102},
                    ProcessIdentity{101, 1'001},
                    CgroupIdentity{7, 11},
                    DeviceIdentity{"0000:03:00.0", 9, "host-client-before-a"},
                },
                HostClientObservation{
                    Projection<Bytes>::known(Bytes{120}),
                    {SharedHostRegion{
                        "shared-a", Projection<Bytes>::known(Bytes{50})}},
                    SampleStamp{"boot-a", 202},
                    ProcessIdentity{101, 1'001},
                    CgroupIdentity{7, 11},
                    DeviceIdentity{"0000:03:00.0", 9, "host-client-after-a"},
                },
                std::optional<HostGttLink>{HostGttLink{
                    ProcessIdentity{101, 1'001}, "gtt-a", true}},
                true,
                true,
            },
            HostClientObservationPair{
                HostClientObservation{
                    Projection<Bytes>::known(Bytes{80}),
                    {SharedHostRegion{
                        "shared-a", Projection<Bytes>::known(Bytes{50})}},
                    SampleStamp{"boot-a", 103},
                    ProcessIdentity{202, 2'002},
                    CgroupIdentity{7, 11},
                    DeviceIdentity{"0000:03:00.0", 9, "host-client-before-b"},
                },
                HostClientObservation{
                    Projection<Bytes>::known(Bytes{95}),
                    {SharedHostRegion{
                        "shared-a", Projection<Bytes>::known(Bytes{50})}},
                    SampleStamp{"boot-a", 203},
                    ProcessIdentity{202, 2'002},
                    CgroupIdentity{7, 11},
                    DeviceIdentity{"0000:03:00.0", 9, "host-client-after-b"},
                },
                std::optional<HostGttLink>{HostGttLink{
                    ProcessIdentity{202, 2'002}, "gtt-b", false}},
                false,
                false,
            },
            HostClientObservationPair{
                HostClientObservation{
                    Projection<Bytes>::known(Bytes{40}),
                    {SharedHostRegion{
                        "external-shared",
                        Projection<Bytes>::known(Bytes{10})}},
                    SampleStamp{"boot-a", 104},
                    ProcessIdentity{303, 3'003},
                    CgroupIdentity{8, 12},
                    DeviceIdentity{"0000:03:00.0", 9, "external-before"},
                },
                HostClientObservation{
                    Projection<Bytes>::known(Bytes{55}),
                    {SharedHostRegion{
                        "external-shared",
                        Projection<Bytes>::known(Bytes{10})}},
                    SampleStamp{"boot-a", 204},
                    ProcessIdentity{303, 3'003},
                    CgroupIdentity{8, 12},
                    DeviceIdentity{"0000:03:00.0", 9, "external-after"},
                },
                std::nullopt,
                true,
                true,
            },
        },
        "boot-a",
        250,
        200,
        5,
    };
}

bool fallback_catalog_is_closed() {
    return admission_fallback ==
               "hatchery_rocm_admission_refuse_unknown_capacity_v1" &&
           pressure_fallback ==
               "hatchery_rocm_pressure_disabled_invalid_evidence_v1" &&
           startup_fallback == "hatchery_rocm_startup_block_group_v1" &&
           recovery_fallback == "hatchery_rocm_recovery_block_readiness_v1";
}

std::string current_platform() {
#ifdef _WIN32
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unsupported";
#endif
}

std::string native_profile_name(NativeProfileMatch profile) {
    switch (profile) {
    case NativeProfileMatch::exact:
        return "exact";
    case NativeProfileMatch::incomplete:
        return "incomplete";
    case NativeProfileMatch::not_hatchery:
        return "not_hatchery";
    case NativeProfileMatch::unavailable:
        return "unavailable";
    }
    return "unavailable";
}

std::string native_observation_state(const Projection<bool>& observation) {
    return observation.is_known() && observation.value() ? "observed" : "unknown";
}

std::string native_disposition(
    NativeProfileMatch profile, const Projection<bool>& causal_proof) {
    if (profile != NativeProfileMatch::exact) {
        return "deferred";
    }
    return causal_proof.is_known() && causal_proof.value() ? "passed" : "fallback";
}

void emit_rows(const std::vector<Row>& rows) {
    for (const auto& row : rows) {
        std::cout << row.first << '=' << row.second << '\n';
    }
}

std::string passed_or_failed(bool passed) {
    return passed ? "passed" : "failed";
}

std::string unknown_or_failed(bool unknown) {
    return unknown ? "unknown" : "failed";
}

int run() {
    const auto known_zero = parse_bytes(std::string_view{"0"});
    const auto missing = parse_bytes(std::nullopt);
    const auto malformed = parse_bytes(std::string_view{"12x"});
    const auto overflow = parse_bytes(std::string_view{"18446744073709551616"});
    const auto stale = require_fresh_sample(
        SampledBytes{Bytes{5}, SampleStamp{"boot-a", 10}}, "boot-a", 30, 5);
    const auto skew = coherent_sample_window(
        {SampleStamp{"boot-a", 100}, SampleStamp{"boot-a", 120}}, 5);

    const ProcessIdentity process{41, 900};
    const auto pid_reuse =
        match_process_identity(process, ProcessIdentity{41, 901});
    const CgroupIdentity cgroup{4, 8};
    const auto cgroup_mismatch =
        match_cgroup_identity(cgroup, CgroupIdentity{4, 9});
    const DeviceIdentity device{"0000:03:00.0", 7, "card0"};
    const auto device_mismatch = match_device_identity(
        device, DeviceIdentity{"0000:04:00.0", 7, "card0"});
    const auto topology_alias = match_device_identity(
        device, DeviceIdentity{"0000:03:00.0", 7, "renderD128"});
    const auto topology_generation_mismatch = match_device_identity(
        device, DeviceIdentity{"0000:03:00.0", 8, "card0"});
    const DependencyIdentity dependency{
        process, cgroup, device, "boot-a"};
    const auto dependency_mismatch = match_dependency_identity(
        dependency,
        DependencyIdentity{
            process,
            CgroupIdentity{4, 9},
            device,
            "boot-a",
        });

    const auto headroom = signed_difference(Bytes{10}, Bytes{12});
    const AttributionInput attribution_input = coherent_attribution_input();
    const auto attribution = reconcile_attribution(attribution_input);
    AttributionInput incoherent_input = attribution_input;
    incoherent_input.after.mem_info_gtt_used =
        Projection<Bytes>::known(Bytes{110});
    const auto incoherent_attribution = reconcile_attribution(incoherent_input);
    AttributionInput wrong_device_input = attribution_input;
    wrong_device_input.clients.front().before.device.drm_pdev = "0000:04:00.0";
    wrong_device_input.clients.front().after.device.drm_pdev = "0000:04:00.0";
    const auto wrong_device_attribution =
        reconcile_attribution(wrong_device_input);
    AttributionInput boot_change_input = attribution_input;
    boot_change_input.after.stamp.boot_id = "boot-b";
    for (auto& client : boot_change_input.clients) {
        client.after.stamp.boot_id = "boot-b";
    }
    const auto boot_change_attribution =
        reconcile_attribution(boot_change_input);
    AttributionInput reversed_time_input = attribution_input;
    reversed_time_input.before.stamp.monotonic_milliseconds = 210;
    for (auto& client : reversed_time_input.clients) {
        client.before.stamp.monotonic_milliseconds = 210;
    }
    const auto reversed_time_attribution =
        reconcile_attribution(reversed_time_input);
    AttributionInput stale_causal_input = attribution_input;
    stale_causal_input.decision_monotonic_milliseconds = 1'000;
    stale_causal_input.maximum_age_milliseconds = 100;
    const auto stale_causal_attribution =
        reconcile_attribution(stale_causal_input);
    AttributionInput missing_host_input = attribution_input;
    missing_host_input.host_before.memavailable =
        Projection<Bytes>::unknown(UnknownReason::missing);
    const auto missing_host_attribution =
        reconcile_attribution(missing_host_input);
    AttributionInput missing_shared_host_input = attribution_input;
    missing_shared_host_input.host_clients.back()
        .before.shared_regions.front().bytes =
        Projection<Bytes>::unknown(UnknownReason::missing);
    const auto missing_shared_host_attribution =
        reconcile_attribution(missing_shared_host_input);
    AttributionInput stale_host_input = attribution_input;
    stale_host_input.host_before.stamp.monotonic_milliseconds = 0;
    const auto stale_host_attribution = reconcile_attribution(stale_host_input);
    AttributionInput host_cgroup_mismatch_input = attribution_input;
    host_cgroup_mismatch_input.host_cgroups.front().after.cgroup.inode = 12;
    const auto host_cgroup_mismatch_attribution =
        reconcile_attribution(host_cgroup_mismatch_input);
    AttributionInput host_device_mismatch_input = attribution_input;
    host_device_mismatch_input.host_before.device.drm_pdev = "0000:04:00.0";
    host_device_mismatch_input.host_after.device.drm_pdev = "0000:04:00.0";
    const auto host_device_mismatch_attribution =
        reconcile_attribution(host_device_mismatch_input);
    AttributionInput host_dependency_mismatch_input = attribution_input;
    ++host_dependency_mismatch_input.host_clients.front()
          .after.process.starttime;
    const auto host_dependency_mismatch_attribution =
        reconcile_attribution(host_dependency_mismatch_input);
    AttributionInput incoherent_host_input = attribution_input;
    incoherent_host_input.host_clients.front().after.private_bytes =
        Projection<Bytes>::known(Bytes{300});
    const auto incoherent_host_attribution =
        reconcile_attribution(incoherent_host_input);
    AttributionInput overflow_host_input = attribution_input;
    overflow_host_input.host_clients.front().before.private_bytes =
        Projection<Bytes>::known(
            Bytes{std::numeric_limits<std::uint64_t>::max()});
    const auto overflow_host_attribution =
        reconcile_attribution(overflow_host_input);
    AttributionInput missing_global_gtt_input = attribution_input;
    missing_global_gtt_input.before.mem_info_gtt_total =
        Projection<Bytes>::unknown(UnknownReason::missing);
    const auto missing_global_gtt_attribution =
        reconcile_attribution(missing_global_gtt_input);
    AttributionInput malformed_global_gtt_input = attribution_input;
    malformed_global_gtt_input.after.mem_info_gtt_used =
        Projection<Bytes>::unknown(UnknownReason::malformed);
    const auto malformed_global_gtt_attribution =
        reconcile_attribution(malformed_global_gtt_input);
    AttributionInput missing_client_gtt_input = attribution_input;
    missing_client_gtt_input.clients.front().before.gtt_bytes =
        Projection<Bytes>::unknown(UnknownReason::missing);
    const auto missing_client_gtt_attribution =
        reconcile_attribution(missing_client_gtt_input);
    AttributionInput overflow_client_gtt_input = attribution_input;
    overflow_client_gtt_input.clients.back().after.gtt_bytes =
        Projection<Bytes>::unknown(UnknownReason::overflow);
    const auto overflow_client_gtt_attribution =
        reconcile_attribution(overflow_client_gtt_input);
    AttributionInput total_drift_input = attribution_input;
    total_drift_input.after.mem_info_gtt_total =
        Projection<Bytes>::known(Bytes{1'001});
    const auto total_drift_attribution =
        reconcile_attribution(total_drift_input);
    AttributionInput wrong_explicit_link_input = attribution_input;
    wrong_explicit_link_input.host_clients.front()
        .gtt_link->allocation_identity = "wrong-allocation";
    const auto wrong_explicit_link_attribution =
        reconcile_attribution(wrong_explicit_link_input);
    AttributionInput missing_owned_host_projection_input = attribution_input;
    missing_owned_host_projection_input.host_clients.erase(
        missing_owned_host_projection_input.host_clients.begin());
    const auto missing_owned_host_projection_attribution =
        reconcile_attribution(missing_owned_host_projection_input);
    AttributionInput mismatched_independent_ownership_input = attribution_input;
    mismatched_independent_ownership_input.host_clients.back().after_owned = false;
    const auto mismatched_independent_ownership_attribution =
        reconcile_attribution(mismatched_independent_ownership_input);
    const std::string platform = current_platform();
    const NativeProfileObservation native_observation =
        probe_native_hatchery_profile(platform);
    const auto native = native_causal_proof(native_observation);
    const auto known_false = Projection<bool>::known(false);
    const NativeProfileObservation false_native_observation{
        NativeProfileMatch::exact,
        Projection<bool>::known(true),
        Projection<bool>::known(true),
        Projection<bool>::known(true),
        Projection<bool>::known(true),
        known_false,
        Projection<bool>::known(true),
    };
    const auto false_native = native_causal_proof(false_native_observation);

    const bool known_zero_passed =
        known_zero.is_known() && known_zero.value().value == 0;
    const bool missing_unknown = is_unknown(missing, UnknownReason::missing);
    const bool malformed_unknown =
        is_unknown(malformed, UnknownReason::malformed);
    const bool overflow_unknown = is_unknown(overflow, UnknownReason::overflow);
    const bool stale_unknown = is_unknown(stale, UnknownReason::stale);
    const bool skew_unknown = is_unknown(skew, UnknownReason::skew);
    const bool pid_reuse_unknown =
        is_unknown(pid_reuse, UnknownReason::pid_reuse);
    const bool cgroup_mismatch_unknown =
        is_unknown(cgroup_mismatch, UnknownReason::cgroup_mismatch);
    const bool device_mismatch_unknown =
        is_unknown(device_mismatch, UnknownReason::device_mismatch);
    const bool topology_alias_passed =
        topology_alias.is_known() && topology_alias.value().node_alias == "renderD128";
    const bool topology_generation_unknown = is_unknown(
        topology_generation_mismatch,
        UnknownReason::topology_generation_mismatch);
    const bool dependency_mismatch_unknown = is_unknown(
        dependency_mismatch, UnknownReason::dependency_identity_mismatch);
    const bool headroom_passed =
        headroom.is_known() && headroom.value().value == -2 &&
        attribution.is_known() &&
        attribution.value().gtt_headroom_before.value == 900 &&
        attribution.value().gtt_headroom_after.value == 840;
    const bool client_attribution_passed =
        attribution.is_known() &&
        attribution.value().global_delta.value == 60 &&
        attribution.value().observed_client_delta.value == 35 &&
        attribution.value().unattributed_global_delta.value == 25;
    const bool cgroup_rollup_passed =
        attribution.is_known() &&
        attribution.value().cgroup_rollup.size() == 1 &&
        attribution.value().cgroup_rollup.at(CgroupIdentity{7, 11}).value == 35;
    const bool memavailable_independent =
        attribution.is_known() &&
        attribution.value().global_delta.value == 60 &&
        attribution.value().memavailable_before.value == 4'096 &&
        attribution.value().memavailable_after.value == 4'000 &&
        attribution.value().memavailable_delta.value == -96 &&
        attribution.value().cgroup_memory_deltas.size() == 2 &&
        attribution.value()
                .cgroup_memory_deltas.at(CgroupIdentity{7, 11})
                .value == 50 &&
        attribution.value()
                .cgroup_memory_deltas.at(CgroupIdentity{8, 12})
                .value == 30 &&
        attribution.value().host_client_delta.value == 50;
    const bool shared_not_doubled =
        attribution.is_known() &&
        attribution.value().host_client_before.value == 280 &&
        attribution.value().host_client_after.value == 330;
    const bool unowned_not_adopted =
        attribution.is_known() &&
        attribution.value().owned_client_delta.value == 20 &&
        attribution.value().unowned_demand.value == 40 &&
        attribution.value().host_owned_before.value == 150 &&
        attribution.value().host_owned_after.value == 185 &&
        attribution.value().host_unowned_before.value == 130 &&
        attribution.value().host_unowned_after.value == 145;
    const bool incoherent_total_unknown = is_unknown(
        incoherent_attribution, UnknownReason::incoherent_total);
    const bool wrong_device_unknown = is_unknown(
        wrong_device_attribution, UnknownReason::device_mismatch);
    const bool boot_change_unknown = is_unknown(
        boot_change_attribution, UnknownReason::dependency_identity_mismatch);
    const bool reversed_time_unknown =
        is_unknown(reversed_time_attribution, UnknownReason::skew);
    const bool stale_causal_unknown =
        is_unknown(stale_causal_attribution, UnknownReason::stale);
    const bool missing_host_unknown =
        is_unknown(missing_host_attribution, UnknownReason::missing) &&
        is_unknown(missing_shared_host_attribution, UnknownReason::missing);
    const bool stale_host_unknown =
        is_unknown(stale_host_attribution, UnknownReason::stale);
    const bool host_cgroup_mismatch_unknown = is_unknown(
        host_cgroup_mismatch_attribution, UnknownReason::cgroup_mismatch);
    const bool host_device_mismatch_unknown = is_unknown(
        host_device_mismatch_attribution, UnknownReason::device_mismatch);
    const bool host_dependency_mismatch_unknown = is_unknown(
        host_dependency_mismatch_attribution,
        UnknownReason::dependency_identity_mismatch);
    const bool incoherent_host_unknown = is_unknown(
        incoherent_host_attribution, UnknownReason::incoherent_total);
    const bool overflow_host_unknown =
        is_unknown(overflow_host_attribution, UnknownReason::overflow);
    const bool global_gtt_unknown =
        is_unknown(missing_global_gtt_attribution, UnknownReason::missing) &&
        is_unknown(
            malformed_global_gtt_attribution, UnknownReason::malformed);
    const bool client_gtt_unknown =
        is_unknown(missing_client_gtt_attribution, UnknownReason::missing) &&
        is_unknown(overflow_client_gtt_attribution, UnknownReason::overflow);
    const bool total_drift_unknown =
        is_unknown(total_drift_attribution, UnknownReason::incoherent_total);
    const bool host_only_actor_passed =
        attribution.is_known() &&
        !attribution_input.host_clients.back().gtt_link.has_value() &&
        std::none_of(
            attribution_input.clients.begin(),
            attribution_input.clients.end(),
            [&attribution_input](const ClientObservationPair& client) {
                return client.before.process.pid ==
                           attribution_input.host_clients.back()
                               .before.process.pid &&
                       client.before.process.starttime ==
                           attribution_input.host_clients.back()
                               .before.process.starttime;
            }) &&
        attribution_input.host_clients.back().before_owned &&
        attribution_input.host_clients.back().after_owned &&
        attribution.value().cgroup_rollup.find(CgroupIdentity{8, 12}) ==
            attribution.value().cgroup_rollup.end() &&
        attribution.value()
                .cgroup_memory_deltas.at(CgroupIdentity{8, 12})
                .value == 30 &&
        attribution.value().host_owned_after.value -
                attribution.value().host_owned_before.value ==
            35;
    const bool wrong_explicit_link_unknown = is_unknown(
        wrong_explicit_link_attribution,
        UnknownReason::dependency_identity_mismatch);
    const bool missing_owned_host_projection_unknown = is_unknown(
        missing_owned_host_projection_attribution,
        UnknownReason::dependency_identity_mismatch);
    const bool mismatched_independent_ownership_unknown = is_unknown(
        mismatched_independent_ownership_attribution,
        UnknownReason::dependency_identity_mismatch);
    const bool native_evidence_incomplete =
        is_unknown(native, UnknownReason::missing);
    const bool native_false_fails_closed =
        native_observation_state(known_false) == "unknown" &&
        is_unknown(false_native, UnknownReason::missing) &&
        native_disposition(NativeProfileMatch::exact, known_false) == "fallback";

    const std::array<bool, 39> synthetic_checks = {
        known_zero_passed,
        missing_unknown,
        malformed_unknown,
        overflow_unknown,
        stale_unknown,
        skew_unknown,
        pid_reuse_unknown,
        cgroup_mismatch_unknown,
        device_mismatch_unknown,
        topology_alias_passed,
        topology_generation_unknown,
        dependency_mismatch_unknown,
        headroom_passed,
        client_attribution_passed,
        cgroup_rollup_passed,
        memavailable_independent,
        shared_not_doubled,
        unowned_not_adopted,
        incoherent_total_unknown,
        wrong_device_unknown,
        boot_change_unknown,
        reversed_time_unknown,
        stale_causal_unknown,
        missing_host_unknown,
        stale_host_unknown,
        host_cgroup_mismatch_unknown,
        host_device_mismatch_unknown,
        host_dependency_mismatch_unknown,
        incoherent_host_unknown,
        overflow_host_unknown,
        global_gtt_unknown,
        client_gtt_unknown,
        total_drift_unknown,
        host_only_actor_passed,
        wrong_explicit_link_unknown,
        missing_owned_host_projection_unknown,
        mismatched_independent_ownership_unknown,
        native_false_fails_closed,
        evidence_contract_is_closed(),
    };
    const bool synthetic_causal_attribution = std::all_of(
        synthetic_checks.begin(), synthetic_checks.end(), [](bool value) {
            return value;
        });
    const bool fallbacks_valid = fallback_catalog_is_closed();
    const bool platform_supported = platform != "unsupported";

    emit_rows({
        {"projection.known_zero", passed_or_failed(known_zero_passed)},
        {"projection.missing", unknown_or_failed(missing_unknown)},
        {"projection.malformed", unknown_or_failed(malformed_unknown)},
        {"projection.overflow", unknown_or_failed(overflow_unknown)},
        {"projection.stale", unknown_or_failed(stale_unknown)},
        {"projection.skew", unknown_or_failed(skew_unknown)},
        {"projection.pid_reuse", unknown_or_failed(pid_reuse_unknown)},
        {"projection.cgroup_mismatch", unknown_or_failed(cgroup_mismatch_unknown)},
        {"projection.device_mismatch", unknown_or_failed(device_mismatch_unknown)},
        {"projection.topology_alias", passed_or_failed(topology_alias_passed)},
        {"projection.topology_generation_mismatch",
         unknown_or_failed(topology_generation_unknown)},
        {"projection.dependency_identity_mismatch",
         unknown_or_failed(dependency_mismatch_unknown)},
        {"gtt.headroom_signed", passed_or_failed(headroom_passed)},
        {"gtt.client_attribution_only",
         passed_or_failed(client_attribution_passed)},
        {"gtt.cgroup_rollup", passed_or_failed(cgroup_rollup_passed)},
        {"host.memavailable_independent",
         passed_or_failed(memavailable_independent)},
        {"host.shared_bytes_not_doubled", passed_or_failed(shared_not_doubled)},
        {"ownership.unowned_not_adopted", passed_or_failed(unowned_not_adopted)},
        {"attribution.incoherent_total",
         unknown_or_failed(incoherent_total_unknown)},
        {"synthetic.causal_attribution",
         passed_or_failed(synthetic_causal_attribution)},
        {"hatchery.native_profile",
         native_profile_name(native_observation.profile)},
        {"native.global_gtt",
         native_observation_state(native_observation.global_gtt)},
        {"native.host_memory",
         native_observation_state(native_observation.host_memory)},
        {"native.boot_identity",
         native_observation_state(native_observation.boot_identity)},
        {"native.device_identity",
         native_observation_state(native_observation.device_identity)},
        {"native.process_fdinfo",
         native_observation_state(native_observation.process_fdinfo)},
        {"native.cgroup_membership",
         native_observation_state(native_observation.cgroup_membership)},
        {"hatchery.native_causal_attribution",
         native_disposition(native_observation.profile, native)},
        {"fallback.admission",
         fallbacks_valid ? std::string(admission_fallback) : "invalid"},
        {"fallback.pressure",
         fallbacks_valid ? std::string(pressure_fallback) : "invalid"},
        {"fallback.startup",
         fallbacks_valid ? std::string(startup_fallback) : "invalid"},
        {"fallback.recovery",
         fallbacks_valid ? std::string(recovery_fallback) : "invalid"},
        {"platform.current", platform},
        {"runtime_authority", "none"},
    });

    return synthetic_causal_attribution && native_evidence_incomplete &&
                   fallbacks_valid && platform_supported
               ? 0
               : 1;
}

}

int main() {
    return lemon::residency::prototype::run();
}
