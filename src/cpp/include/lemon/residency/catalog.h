#pragma once

#include "lemon/residency/generated_contract.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lemon::residency {

enum class CatalogLoadStatus {
    Accepted,
    Malformed,
    Missing,
    Duplicate,
    Ambiguous,
    PrecedenceIncomparable,
    DigestMismatch,
};

enum class CatalogSelectionStatus {
    Resolved,
    Unsupported,
    Missing,
};

struct RuntimeCatalogSelector {
    std::string source_support_baseline;
    std::string base_variant;
    std::string platform;
    std::string backend_channel;
    std::string model_type;
    OperationTemplate operation_template = OperationTemplate::Adm;
    OperationKind operation_kind = OperationKind::Admission;
    std::vector<ConstraintKind> constraints;
    std::string recovery;
    std::map<std::string, std::string> material_profiles;
};

struct CompatibilityCatalogSelector {
    std::string source_support_baseline;
    std::string platform;
    std::string coexist_by_type_variant;
    std::string exclusive_variant;
    std::string direction;
    std::string incumbent_state;
    std::vector<ConstraintKind> constraints;
};

inline constexpr bool
operation_template_accepts(OperationTemplate operation_template,
                           OperationKind operation_kind) noexcept {
    switch (operation_template) {
    case OperationTemplate::Adm:
    case OperationTemplate::Lfr:
    case OperationTemplate::Npc:
        return operation_kind == OperationKind::Admission;
    case OperationTemplate::Pre:
        return operation_kind == OperationKind::PressureReclamation;
    case OperationTemplate::Sta:
        return operation_kind == OperationKind::StartupLoad;
    case OperationTemplate::Rec:
        return operation_kind == OperationKind::ServiceTermination ||
               operation_kind == OperationKind::DeadBackendPruning ||
               operation_kind == OperationKind::SameEpochRecoveryCleanup ||
               operation_kind == OperationKind::PriorEpochOwnerCleanup ||
               operation_kind ==
                   OperationKind::ArtifactScopeRecoveryCleanup;
    case OperationTemplate::Unl:
        return operation_kind == OperationKind::ExplicitUnload ||
               operation_kind == OperationKind::ForceUnload;
    case OperationTemplate::Pin:
        return operation_kind == OperationKind::SavedPinMutation ||
               operation_kind == OperationKind::RuntimePinMutation ||
               operation_kind == OperationKind::LegacyPinBatch ||
               operation_kind ==
                   OperationKind::ResidentStateRecoveryCleanup;
    }
    return false;
}

struct CatalogSelection {
    CatalogSelectionStatus status = CatalogSelectionStatus::Missing;
    std::optional<PromotionUnitId> candidate_promotion_unit_id;
    std::optional<PromotionUnitId> effective_promotion_unit_id;
    CapabilityLevel capability_level = CapabilityLevel::Unsupported;
    CapabilityLevel evidence_ceiling = CapabilityLevel::Unsupported;
    DeliveryState delivery_state = DeliveryState::Absent;
    std::map<std::string, FallbackId> fallbacks;
    std::vector<ConstraintKind> constraints;
    std::vector<PromotionUnitId> compatibility_contract_ids;
};

struct CatalogLoadResult;

class CatalogSnapshot {
public:
    CatalogSnapshot(const CatalogSnapshot&) = default;
    CatalogSnapshot(CatalogSnapshot&&) noexcept = default;
    CatalogSnapshot& operator=(const CatalogSnapshot&) = default;
    CatalogSnapshot& operator=(CatalogSnapshot&&) noexcept = default;

    std::string_view catalog_sha256() const noexcept;
    std::string_view source_support_baseline() const noexcept;
    std::string_view selection_registry_sha256() const noexcept;

    CatalogSelection resolve(const RuntimeCatalogSelector& selector) const;
    CatalogSelection
    resolve_compatibility(const CompatibilityCatalogSelector& selector) const;

private:
    struct State;

    explicit CatalogSnapshot(std::shared_ptr<const State> state);

    std::shared_ptr<const State> state_;

    friend CatalogLoadResult load_packaged_catalog(std::string_view bytes);
};

struct CatalogValidationResult {
    CatalogLoadStatus status = CatalogLoadStatus::Malformed;
    std::string diagnostic;

    bool accepted() const noexcept {
        return status == CatalogLoadStatus::Accepted;
    }
};

struct CatalogLoadResult {
    CatalogLoadStatus status = CatalogLoadStatus::Malformed;
    std::string diagnostic;
    std::optional<CatalogSnapshot> snapshot;

    bool accepted() const noexcept {
        return status == CatalogLoadStatus::Accepted && snapshot.has_value();
    }
};

CatalogValidationResult validate_catalog_document(std::string_view bytes);
CatalogLoadResult load_packaged_catalog(std::string_view bytes);

}
