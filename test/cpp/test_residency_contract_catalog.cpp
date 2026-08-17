#define RESIDENCY_CATALOG_SEAM_NO_MAIN
#include "../residency/contract/catalog_public_seam.cpp"
#undef RESIDENCY_CATALOG_SEAM_NO_MAIN

namespace lemon::residency::catalog_internal {

CatalogSelection make_catalog_selection(
    const PromotionUnitId& id,
    CapabilityLevel capability_level,
    CapabilityLevel evidence_ceiling,
    DeliveryState delivery_state);

}

namespace {

void require_implemented_candidate_stays_inactive(
    std::string_view id,
    CapabilityLevel evidence_ceiling) {
    const auto decoded = lemon::residency::decode_promotion_unit_id(id);
    require(decoded.is_known(), "supplemental promotion-unit ID is unknown");

    const auto selection =
        lemon::residency::catalog_internal::make_catalog_selection(
            *decoded.known_value(),
            lemon::residency::CapabilityLevel::Modeled,
            evidence_ceiling,
            lemon::residency::DeliveryState::ImplementedUnverified);
    require(
        selection.status ==
            lemon::residency::CatalogSelectionStatus::Unsupported,
        "implemented-unverified candidate became effective");
    require(
        selection.candidate_promotion_unit_id.has_value() &&
            selection.candidate_promotion_unit_id->token() == id,
        "implemented-unverified candidate identity was not preserved");
    require(
        !selection.effective_promotion_unit_id.has_value(),
        "implemented-unverified candidate exposed an effective identity");
    require(
        selection.capability_level ==
            lemon::residency::CapabilityLevel::Modeled,
        "implemented-unverified candidate capability was not preserved");
    require(
        selection.evidence_ceiling == evidence_ceiling,
        "implemented-unverified candidate evidence ceiling was not preserved");
    require(
        selection.delivery_state ==
            lemon::residency::DeliveryState::ImplementedUnverified,
        "implemented-unverified candidate delivery state was not preserved");
}

void require_implemented_candidates_stay_inactive() {
    require_implemented_candidate_stays_inactive(
        "H-ROCM-ADM-GTT-HOST-v1",
        lemon::residency::CapabilityLevel::Validated);
    require_implemented_candidate_stays_inactive(
        "H-NPU-FLM-CONFLICT-XDNA2-v1",
        lemon::residency::CapabilityLevel::Modeled);
}

}

int main(int argc, char** argv) {
    const auto public_seam_result =
        lemon::residency::run_residency_catalog_public_seam(argc, argv);
    if (public_seam_result != 0) {
        return public_seam_result;
    }
    require_implemented_candidates_stay_inactive();
    return 0;
}
