#include "process_containment_platform.h"

namespace lemon::utils::internal {

ProcessContainmentPlatformPrepareResult
prepare_process_containment_platform(
    const ProcessContainmentRequest &,
    const ProcessContainmentOperationControl &) {
    ProcessContainmentPlatformPrepareResult result;
    result.status = ProcessContainmentStatus::Unsupported;
    result.diagnostic = "process containment is unsupported on this platform";
    return result;
}

} // namespace lemon::utils::internal
