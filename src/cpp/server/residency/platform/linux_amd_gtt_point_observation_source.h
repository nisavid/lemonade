#pragma once

#ifdef __linux__

#include "lemon/residency/profiling_provider.h"
#include "lemon/utils/process_containment.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace lemon::residency::internal {

inline constexpr std::string_view linux_amd_gtt_point_sensor_id =
    "linux.amdgpu.mem_info_gtt_used.bytes.v1";

struct LinuxAmdGttPointObservationSourceBinding {
    std::filesystem::path sysfs_device_directory;
    std::string drm_pdev;
    lemon::utils::ProcessContainmentIdentity process_containment_identity;
    std::chrono::milliseconds max_read_duration{0};
};

struct LinuxAmdGttPointObservationSourceTestHook;

class LinuxAmdGttPointObservationSource final
    : public ProfilingObservationSource {
public:
    // The prepared containment must outlive this observation source.
    LinuxAmdGttPointObservationSource(
        LinuxAmdGttPointObservationSourceBinding binding,
        lemon::utils::PreparedProcessContainment &process_containment);
    ~LinuxAmdGttPointObservationSource() override;

    LinuxAmdGttPointObservationSource(
        const LinuxAmdGttPointObservationSource &) = delete;
    LinuxAmdGttPointObservationSource &
    operator=(const LinuxAmdGttPointObservationSource &) = delete;
    LinuxAmdGttPointObservationSource(
        LinuxAmdGttPointObservationSource &&) = delete;
    LinuxAmdGttPointObservationSource &
    operator=(LinuxAmdGttPointObservationSource &&) = delete;

    ProfilingRawReadResult
    read(const ProfilingRawReadRequest &request,
         const ProfilingCancellationCheck &should_abort) override;

private:
    class Impl;
    enum class ReadBoundary {
        AfterOpeningSnapshot,
        AfterFirstFdinfoPass,
        AfterGlobalPoint,
        AfterSecondFdinfoPass,
        AfterClosingSnapshot,
    };
    struct TestEnvironment {
        std::filesystem::path proc_root;
        std::function<std::chrono::steady_clock::time_point()> monotonic_now;
        std::optional<long> expected_proc_filesystem_magic;
        std::optional<long> expected_sysfs_filesystem_magic;
        std::function<void(ReadBoundary)> on_read_boundary;
    };

    LinuxAmdGttPointObservationSource(
        LinuxAmdGttPointObservationSourceBinding binding,
        lemon::utils::PreparedProcessContainment &process_containment,
        TestEnvironment environment);

    std::unique_ptr<Impl> impl_;

    friend struct LinuxAmdGttPointObservationSourceTestHook;
};

} // namespace lemon::residency::internal

#endif
