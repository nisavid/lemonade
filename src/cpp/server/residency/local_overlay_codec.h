#pragma once

#include "lemon/residency/local_overlay.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace lemon::residency::local_overlay_internal {

inline nlohmann::json catalog_selector_document(
    const RuntimeCatalogSelector &selector) {
    auto constraints = nlohmann::json::array();
    for (const auto constraint : selector.constraints) {
        constraints.push_back(wire_name(constraint));
    }
    return nlohmann::json{
        {"backend_channel", selector.backend_channel},
        {"base_variant", selector.base_variant},
        {"constraints", std::move(constraints)},
        {"material_profiles", selector.material_profiles},
        {"model_type", selector.model_type},
        {"operation_kind", wire_name(selector.operation_kind)},
        {"operation_template", wire_name(selector.operation_template)},
        {"platform", selector.platform},
        {"recovery", selector.recovery},
        {"source_support_baseline", selector.source_support_baseline},
    };
}

inline nlohmann::json selector_document(
    const LocalOverlaySelectorIdentity &selector) {
    return nlohmann::json{
        {"backend_build_sha256", selector.backend_build_sha256},
        {"canonical_model_id", selector.canonical_model_id},
        {"catalog", catalog_selector_document(selector.catalog_selector)},
        {"catalog_sha256", selector.catalog_sha256},
        {"configuration_sha256", selector.configuration_sha256},
        {"dependency_set_sha256", selector.dependency_set_sha256},
        {"device_identity_sha256", selector.device_identity_sha256},
        {"driver_identity_sha256", selector.driver_identity_sha256},
        {"model_artifact_sha256", selector.model_artifact_sha256},
        {"operation_contract_sha256", selector.operation_contract_sha256},
        {"topology_sha256", selector.topology_sha256},
        {"workload_sha256", selector.workload_sha256},
    };
}

} // namespace lemon::residency::local_overlay_internal
