#pragma once

#include "lemon/residency/profiling_noise.h"

#include <nlohmann/json.hpp>

namespace lemon::residency::profiling_internal {

inline nlohmann::json bindings_document(
    const ProfilingNoiseBindings &bindings) {
    return nlohmann::json{
        {"background_inventory_sha256",
         bindings.background_inventory_sha256},
        {"boot_id_sha256", bindings.boot_id_sha256},
        {"campaign_contract_sha256", bindings.campaign_contract_sha256},
        {"counter_continuity_epoch_sha256",
         bindings.counter_continuity_epoch_sha256},
        {"counter_source_id", bindings.counter_source_id},
        {"counter_source_revision_sha256",
         bindings.counter_source_revision_sha256},
        {"deployment_epoch_sha256", bindings.deployment_epoch_sha256},
        {"deployment_id", bindings.deployment_id},
        {"device_identity_sha256", bindings.device_identity_sha256},
        {"driver_identity_sha256", bindings.driver_identity_sha256},
        {"kernel_identity_sha256", bindings.kernel_identity_sha256},
        {"procedure_revision_sha256", bindings.procedure_revision_sha256},
        {"topology_sha256", bindings.topology_sha256},
    };
}

} // namespace lemon::residency::profiling_internal
