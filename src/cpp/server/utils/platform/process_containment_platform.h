#pragma once

#include "lemon/utils/process_containment.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace lemon::utils::internal {

class ProcessContainmentState {
public:
    virtual ~ProcessContainmentState() = default;

    virtual bool active() const noexcept = 0;
    virtual ProcessContainmentStartResult start(
        const std::string &executable,
        const std::vector<std::string> &args,
        const ProcessContainmentOperationControl &control,
        const std::string &working_dir,
        bool inherit_output,
        bool filter_health_logs,
        const std::vector<std::pair<std::string, std::string>> &env_vars) = 0;
    virtual ProcessContainmentSnapshotResult
    snapshot(const ProcessContainmentOperationControl &control) = 0;
    virtual ProcessContainmentOperationResult
    kill(std::chrono::milliseconds timeout) = 0;
    virtual ProcessContainmentOperationResult
    release(const ProcessContainmentOperationControl &control) = 0;
};

struct ProcessContainmentPlatformPrepareResult {
    ProcessContainmentStatus status = ProcessContainmentStatus::Failed;
    std::string diagnostic;
    std::unique_ptr<ProcessContainmentState> state;

    bool succeeded() const noexcept {
        return status == ProcessContainmentStatus::Success && state &&
               state->active();
    }
};

ProcessContainmentPlatformPrepareResult
prepare_process_containment_platform(
    const ProcessContainmentRequest &request,
    const ProcessContainmentOperationControl &control);

} // namespace lemon::utils::internal
